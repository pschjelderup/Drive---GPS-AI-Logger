#include "cloudsync.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include "storage.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "cams.h"
#include "config.h"
#include "customers.h"
#include "logg.h"
#include "obd.h"
#include "sensors.h"
#include "trip.h"
#include "websync.h"

namespace {

// Webbappens moln. Adressen ar fast: det ar var egen edge-funktion, och
// enhetens legitimation ar token i huvudet - inte adressen.
const char *kBase =
    "https://jdjkeloiwjkcycelmexq.supabase.co/functions/v1/drive-sync";
const char *kHost = "jdjkeloiwjkcycelmexq.supabase.co";

// TLS utan certifikatkontroll, med oppna ogon: kedjan bakom molnet byter
// rotcertifikat pa sina egna villkor, och en enhet i en bil kan inte fa nya
// rotlistor pushade till sig. Det enda hemliga i trafiken ar enhetstoken, och
// den byts med tva klick i webbappen om den skulle lacka. Att vagra synka den
// dagen ett certifikat roterats vore det samre felet.
//
// Trafiken gar dessutom over anvandarens egen hotspot, inte offentliga nat.

Preferences g_prefs;

char g_ssids[cloudsync::kNetMax][33] = {};
char g_passes[cloudsync::kNetMax][65] = {};
char g_token[64] = "";

// Natet som senast bar hela vagen till molnet - det ar det som visas.
char g_active[33] = "";
// Tur-och-ordning-raknaren for blindforsoken, nar ingen skanning traffar.
uint8_t g_rr = 0;

bool anyNet() {
  for (uint8_t i = 0; i < cloudsync::kNetMax; i++) {
    if (g_ssids[i][0]) return true;
  }
  return false;
}

CloudStatus g_status = {};
SemaphoreHandle_t g_mutex = nullptr;

volatile bool g_syncNow = false;
volatile bool g_autoSync = true;
volatile bool g_reconfigured = false;

void lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

void setState(CloudState s, const char *detail) {
  lock();
  g_status.state = s;
  strncpy(g_status.detail, detail ? detail : "", sizeof(g_status.detail) - 1);
  g_status.detail[sizeof(g_status.detail) - 1] = '\0';
  unlock();
  Serial.printf("moln: %s\n", detail ? detail : "");
  // Felen gar rakt in i enhetsloggen - det ar precis de raderna man vill
  // kunna lasa i webbappen i efterhand.
  if (s == CLOUD_ERROR && detail && detail[0]) {
    logg::event("synkfel: %s", detail);
  }
}

// Resan har alltid foretrade. Kontrolleras mellan varje steg och varje block,
// sa att en resa som borjar mitt i en nedladdning inte behover vanta pa den.
bool mustAbort() { return trip::status().active; }

// ------------------------------------------------------------ http-hjalp ---

// Var egen server, vara egna svarsformer. En full json-tolk vore mer kod an
// hela synken; det har ar strstr pa nycklar vi sjalva valt namnen pa.
bool jsonStr(const String &body, const char *key, char *out, size_t len) {
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const int at = body.indexOf(pat);
  if (at < 0) return false;
  const int start = at + strlen(pat);
  const int end = body.indexOf('"', start);
  if (end < 0) return false;
  const int n = min((int)len - 1, end - start);
  memcpy(out, body.c_str() + start, n);
  out[n] = '\0';
  return true;
}

long jsonInt(const String &body, const char *key, long fallback) {
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const int at = body.indexOf(pat);
  if (at < 0) return fallback;
  return atol(body.c_str() + at + strlen(pat));
}

// Versionsblocket for en fil: {"kameror":{"version":"abc","size":N,"parts":1}}
bool fileVersion(const String &body, const char *name, char *ver, size_t vlen,
                 int *parts, long *size) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":{", name);
  const int at = body.indexOf(pat);
  if (at < 0) return false;
  const String slice = body.substring(at, at + 160);
  if (!jsonStr(slice, "version", ver, vlen)) return false;
  *parts = (int)jsonInt(slice, "parts", 1);
  *size = jsonInt(slice, "size", 0);
  return true;
}

// En version som misslyckats manga ganger sedan uppstart lamnas ifred tills
// molnet har en ny. Gransen ar generos: med ateruppupptagningen gor varje
// forsok framsteg - de hela delarna ligger kvar - sa fler forsok betyder
// narmare mal, inte samma gigabyte om igen.
struct FileAttempt {
  char ver[24];
  uint8_t fails;
};
FileAttempt g_kamAttempt = {};

bool worthTrying(FileAttempt &a, const char *ver) {
  if (strcmp(a.ver, ver) != 0) {
    strncpy(a.ver, ver, sizeof(a.ver) - 1);
    a.ver[sizeof(a.ver) - 1] = '\0';
    a.fails = 0;
  }
  return a.fails < 12;
}

// EN tls-session per synkrunda, inte en per anrop. Varje anrop byggde forr
// sin egen WiFiClientSecure: handskakning, fyrtio kilobyte allokerade och
// frigjorda, tolv ganger per runda. Det var de omtagen som fragmenterade
// heapen tills inte ens /config gick fram (kod -1). Nu oppnas forbindelsen
// en gang, halls vid liv mellan anropen (servern talar keep-alive), och
// slapps forst nar rundan ar slut - da far accesspunkten sitt minne tillbaka.
WiFiClientSecure g_tls;
HTTPClient g_http;
bool g_httpInit = false;

bool httpBegin(const String &path) {
  if (!g_httpInit) {
    g_tls.setInsecure();  // se blocket overst
    g_http.setReuse(true);
    g_http.setTimeout(30000);
    g_httpInit = true;
  }
  if (!g_http.begin(g_tls, String(kBase) + path)) return false;
  g_http.addHeader("x-drive-token", g_token);
  return true;
}

// Slapper forbindelsen helt. Efter ett fel mitt i en strom ar den anda inte
// att lita pa - nasta anrop borjar da om med ny handskakning - och i slutet
// av rundan ska tls-minnet lamnas tillbaka.
void httpDrop() {
  g_http.end();
  g_tls.stop();
}

// ------------------------------------------------------------ synkstegen ---

bool uploadTrips(long lastSynced) {
  File f = SDCARD.open(TRIPS_JSONL, FILE_READ);
  if (!f) return true;  // ingen dagbok ar inte ett fel

  // Raderna efter senast synkade resa samlas i sma klumpar. Sma av ett skal:
  // klumpen ligger i internminnet samtidigt som tls-anslutningen byggs, och
  // tls ar den store - en stor klump har kvavt handskakningen sa att
  // uppladdningen aldrig ens nadde servern.
  String batch;
  batch.reserve(2048);
  uint32_t sent = 0;
  static char line[768];

  auto flush = [&]() -> bool {
    if (!batch.length()) return true;
    HTTPClient &http = g_http;
    if (!httpBegin("/trips")) {
      setState(CLOUD_ERROR, "uppladdning: tls fick inte plats");
      return false;
    }
    http.addHeader("Content-Type", "application/x-ndjson");
    const int code = http.POST(batch);
    http.end();
    if (code != 200) {
      httpDrop();
      // Koden i klartext pa skarmen - ett tyst "laddar upp resor" som
      // aldrig blir nagot mer ar ofelsokbart fran forarsatet.
      char msg[48];
      snprintf(msg, sizeof(msg), "uppladdning misslyckades (kod %d)", code);
      setState(CLOUD_ERROR, msg);
      return false;
    }
    // Ny strang i stallet for tomd: en tomd behaller sin buffert, och
    // poangen har ar att lamna tillbaka minnet mellan klumparna.
    batch = String();
    batch.reserve(2048);
    return true;
  };

  while (f.available()) {
    if (mustAbort()) { f.close(); return false; }
    const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    if (!strchr(line, '}')) continue;  // halvskriven sista rad

    const char *at = strstr(line, "\"resa\":");
    if (!at) continue;
    if (atol(at + 7) <= lastSynced) continue;

    batch += line;
    batch += '\n';
    sent++;
    if (batch.length() > 4000 && !flush()) { f.close(); return false; }
  }
  f.close();
  if (!flush()) return false;

  lock();
  g_status.tripsUploaded += sent;
  unlock();
  if (sent) Serial.printf("moln: %lu resor uppladdade\n", (unsigned long)sent);
  return true;
}

// Falskt bara nar rundan inte kan fortsatta alls (resa borjade, eller tls
// far inte plats i minnet). En enskild fil som molnet vagrar hoppas over och
// raknas - den far ett nytt forsok nasta runda, och de andra filerna ska
// inte sta och vanta pa den.
bool uploadGpx(uint8_t &failed) {
  File dir = SDCARD.open(GPX_DIR);
  if (!dir) return true;

  // Namnen samlas forst, sa att flytten till UPPLADDAT inte sker mitt i en
  // pagaende kataloglasning.
  String names[24];
  uint8_t count = 0;
  File entry;
  while ((entry = dir.openNextFile()) && count < 24) {
    if (!entry.isDirectory()) names[count++] = String(entry.name());
    entry.close();
  }
  dir.close();

  for (uint8_t i = 0; i < count; i++) {
    if (mustAbort()) return false;
    const String &name = names[i];
    if (!name.endsWith(".GPX")) continue;

    String path = String(GPX_DIR) + "/" + name;
    File f = SDCARD.open(path, FILE_READ);
    if (!f) continue;

    HTTPClient &http = g_http;
    if (!httpBegin(String("/gpx?name=") + name)) {
      f.close();
      return false;
    }
    http.addHeader("Content-Type", "application/gpx+xml");
    // Strommad uppladdning: filen gar genom en buffert, aldrig in i minnet hel.
    const int code = http.sendRequest("POST", &f, f.size());
    http.end();
    f.close();

    if (code != 200) {
      failed++;
      httpDrop();
      logg::event("gpx %s vagrades (kod %d) - fritt %lu, storsta block %lu",
                  name.c_str(), code,
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)heap_caps_get_largest_free_block(
                      MALLOC_CAP_INTERNAL));
      continue;
    }

    // Uppladdad ar inte raderad: filen flyttas till UPPLADDAT och ligger kvar
    // pa kortet. Kortet ar en kopia av sanningen aven efter synken.
    String to = String(GPX_SYNCED_DIR) + "/" + name;
    if (SDCARD.exists(to)) SDCARD.remove(to);
    SDCARD.rename(path, to);

    lock();
    g_status.gpxUploaded++;
    unlock();
    Serial.printf("moln: %s uppladdad\n", name.c_str());
  }
  return true;
}

// Hamtar en fil - i en eller flera delar - till en tillfallig fil, kontrollerar
// storlek och magi, och byter forst da. En halv fil pa riktig plats ar varre
// an ingen.
//
// Delarna ar exakt CLOUD_PART_BYTES (utom den sista), och varje del strommas
// forst till en egen liten tempfil som laggs pa huvudfilen forst nar den ar
// hel. Darfor ar huvudfilens langd alltid en delgrans - och en bruten
// nedladdning kan aterupptas dar den var, over bade omstarter och resor.
// Hastighetsfilen ar 144 MB for att Sveriges vagnat ar stort; utan
// ateruppupptagning hann den aldrig fram over en hotspot.
const char *kTmpFile = "/DRIVE/NED.TMP";

// Kortets utrymme. Ett fullt minneskort ar ett tyst fel rakt igenom: SD-
// skrivningen returnerar noll byte utan att saga ifran, den halva delen
// kastas, och nasta runda borjar om pa exakt samma stalle. Darfor mats
// platsen innan varje stor nedladdning - och foljer med statusraden.
bool sdSpace(uint64_t &total, uint64_t &ledigt) {
  total = SDCARD.totalBytes();
  if (!total) return false;
  const uint64_t anvant = SDCARD.usedBytes();
  ledigt = anvant > total ? 0 : total - anvant;
  return true;
}

// Sant nar en skrivning misslyckades for att kortet inte hade plats kvar.
bool g_diskFull = false;

// Chunkad http-strom: svar utan Content-Length ramas in bit for bit som
// "storlek-i-hex\r\n data \r\n", avslutat med en nollstor bit. Ramarna ar
// inte innehall - det var de som gjorde varje nedladdad del nagra hundra
// byte "for stor" sa att storlekskontrollen forkastade den, om och om igen.
// Har skalas ramen av och bara nyttolasten skrivs till filen.
bool readChunked(WiFiClient *stream, File &out, uint8_t *buf, size_t bufLen) {
  char line[20];
  for (;;) {
    if (mustAbort()) return false;
    const size_t n = stream->readBytesUntil('\n', line, sizeof(line) - 1);
    if (n == 0) return false;
    line[n] = '\0';
    const long sz = strtol(line, nullptr, 16);
    if (sz < 0) return false;
    if (sz == 0) {
      stream->readBytesUntil('\n', line, sizeof(line) - 1);  // avslutande tomrad
      return true;
    }
    long left = sz;
    uint32_t lastProgressMs = millis();
    while (left > 0) {
      if (mustAbort()) return false;
      const size_t got =
          stream->readBytes(buf, min((long)bufLen, left));
      if (got == 0) {
        // Samma talamod som den olanka lasningen: hotspots hackar, och
        // forst en dod lank eller 20 s utan framsteg ar slutet.
        const bool dead = !stream->connected() && stream->available() == 0;
        if (dead || millis() - lastProgressMs > 20000UL) return false;
        delay(50);
        continue;
      }
      lastProgressMs = millis();
      if (out.write(buf, got) != got) {
        g_diskFull = true;
        return false;
      }
      left -= got;
    }
    stream->readBytesUntil('\n', line, sizeof(line) - 1);  // bitens \r\n
  }
}

// En manuellt ditlagd fil - nedladdad fran webbappen till en dator och lagd
// pa kortet for hand - kanns igen pa att den redan har exakt den storlek och
// den magi som molnet utlovar. Da ar nedladdningen redan gjord, bara inte av
// enheten sjalv. (Tva olika versioner med exakt samma bytelangd skulle luras
// har, men filerna byggs om fran hela NVDB och landar aldrig pa pricken lika.)
// En fil som finns men inte matchar loggas med bada siffrorna - det ar exakt
// det man behover se nar ett manuellt kort "inte tas emot".
bool adoptLocal(const char *target, const char *namn, const uint32_t magic,
                long expectBytes) {
  if (expectBytes <= 0) return false;
  File f = SDCARD.open(target, FILE_READ);
  if (!f) return false;
  const long sz = (long)f.size();
  uint32_t m = 0;
  const bool okRead = f.read((uint8_t *)&m, 4) == 4;
  f.close();
  const bool ok = okRead && m == magic && sz == expectBytes;
  if (!ok) {
    logg::event("%s pa kortet (%ld byte, %s) matchar inte molnets (%ld byte)",
                namn, sz, okRead && m == magic ? "ratt signatur" : "fel signatur",
                expectBytes);
  }
  return ok;
}

bool downloadFile(const char *urlName, int parts, const char *target,
                  const uint32_t magic, long expectBytes, const char *ver,
                  const char *human) {
  const char *tmp = kTmpFile;
  const char *del = "/DRIVE/DEL.TMP";

  // Ateruppta dar det brots - om tmp-filen tillhor samma fil och version
  // och slutar pa en delgrans. Allt annat borjar om fran noll.
  int startPart = 0;
  {
    char tf[16] = "", tv[24] = "";
    g_prefs.getString("tmpFil", tf, sizeof(tf));
    g_prefs.getString("tmpVer", tv, sizeof(tv));
    if (SDCARD.exists(tmp) && strcmp(tf, urlName) == 0 &&
        strcmp(tv, ver) == 0 && expectBytes > 0) {
      File h = SDCARD.open(tmp, FILE_READ);
      if (h) {
        const uint64_t sz = h.size();
        h.close();
        if (sz > 0 && sz % CLOUD_PART_BYTES == 0 &&
            (int)(sz / CLOUD_PART_BYTES) < parts) {
          startPart = (int)(sz / CLOUD_PART_BYTES);
        } else if (sz == expectBytes) {
          // Hela filen ar redan hemma - bara kontrollen och filbytet
          // aterstar. Sa har ser det ut nar sjalva inbytet misslyckades
          // forra varvet; da ska inte 130 MB hamtas om for det.
          startPart = parts;
        }
      }
    }
    if (startPart == 0 && SDCARD.exists(tmp)) SDCARD.remove(tmp);
    g_prefs.putString("tmpFil", urlName);
    g_prefs.putString("tmpVer", ver);
  }
  if (startPart > 0) {
    Serial.printf("moln: %s aterupptas fran del %d/%d\n", urlName,
                  startPart + 1, parts);
  }

  // Ryms resten pa kortet? Huvudfilen vaxer till hela storleken, en del i
  // taget mellanlandar i DEL.TMP, och en manuellt ditlagd fil av fel version
  // ligger kvar bredvid tills bytet sker. Utan den har kontrollen syns ett
  // fullt kort bara som att samma del hamtas om, runda efter runda, i all
  // evighet - vilket ar precis vad synkloggen visat.
  if (expectBytes > 0 && startPart < parts) {
    const long hemma = (long)startPart * (long)CLOUD_PART_BYTES;
    const uint64_t behovs =
        (uint64_t)(expectBytes - hemma) + (uint64_t)CLOUD_PART_BYTES;
    uint64_t total = 0, ledigt = 0;
    if (sdSpace(total, ledigt) && ledigt < behovs) {
      g_diskFull = true;
      logg::event(
          "%s: kortet ar fullt - %lu MB ledigt, behover %lu MB (%lu MB totalt)",
          urlName, (unsigned long)(ledigt >> 20),
          (unsigned long)(behovs >> 20), (unsigned long)(total >> 20));
      setState(CLOUD_SYNCING, "kortet ar fullt");
      return false;
    }
  }

  uint8_t buf[4096];
  for (int p = startPart; p < parts; p++) {
    // Resan har foretrade - men tmp-filen lamnas kvar, sa att delarna som
    // redan ar hemma inte behover hamtas igen.
    if (mustAbort()) return false;

    // Del for del pa skarmen. En halvtimmes nedladdning med samma text hela
    // vagen ser ut som en hangning fran forarsatet.
    if (parts > 1) {
      char prog[48];
      snprintf(prog, sizeof(prog), "hamtar %s (del %d/%d)", human, p + 1,
               parts);
      setState(CLOUD_SYNCING, prog);
    }

    const long partExpect = (p < parts - 1 || expectBytes <= 0)
        ? (long)CLOUD_PART_BYTES
        : expectBytes - (long)(parts - 1) * (long)CLOUD_PART_BYTES;

    if (SDCARD.exists(del)) SDCARD.remove(del);
    File out = SDCARD.open(del, FILE_WRITE);
    if (!out) return false;

    HTTPClient &http = g_http;
    char path[80];
    if (parts > 1) {
      snprintf(path, sizeof(path), "/file/%s?part=%d", urlName, p);
    } else {
      snprintf(path, sizeof(path), "/file/%s", urlName);
    }
    if (!httpBegin(path)) {
      out.close();
      logg::event("%s del %d/%d: tls fick inte plats", urlName, p + 1, parts);
      return false;
    }

    const int code = http.GET();
    if (code != 200) {
      httpDrop();
      out.close();
      // Koden ensam har visat sig otillracklig: "svar -1" betyder att
      // tls-uppkopplingen inte gick att fa till, och den vanligaste orsaken
      // ar att minnet inte racker till ett handslag till efter en runda
      // fylld av uppladdningar. Da ar det just de tva siffrorna man behover.
      logg::event("%s del %d/%d: svar %d - fritt %lu, storsta block %lu",
                  urlName, p + 1, parts, code,
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)heap_caps_get_largest_free_block(
                      MALLOC_CAP_INTERNAL));
      return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    int remaining = http.getSize();
    // Bara ett helt last svar lamnar forbindelsen i ett skick dar nasta
    // anrop kan aterbruka den. Allt annat slapps.
    bool whole = false;
    if (remaining == -1) {
      // Ingen Content-Length: svaret ar chunkat och ramarna maste skalas av.
      whole = readChunked(stream, out, buf, sizeof(buf));
    } else {
      // En lasning som ger noll byte ar inte slutet: hotspots och mobilnat
      // stannar upp i sekunder mitt i en overforing, och att doma ut delen
      // vid forsta hacket var darfor detsamma som att aldrig fa hem den.
      // Sa lange forbindelsen lever tals 20 sekunder utan framsteg.
      uint32_t lastProgressMs = millis();
      while (remaining != 0) {
        if (mustAbort()) { httpDrop(); out.close(); return false; }
        const size_t got = stream->readBytes(
            buf,
            min((int)sizeof(buf), remaining > 0 ? remaining : (int)sizeof(buf)));
        if (got == 0) {
          const bool dead = !stream->connected() && stream->available() == 0;
          if (dead || millis() - lastProgressMs > 20000UL) break;
          delay(50);  // vanta ut hacket utan att snurra varm
          continue;
        }
        lastProgressMs = millis();
        if (out.write(buf, got) != got) {
          // Kortet tog inte emot. Det ar ett tyst fel i SD-lagret, och utan
          // den har raden ser det ut som en klippt nedladdning.
          g_diskFull = true;
          httpDrop(); out.close(); return false;
        }
        if (remaining > 0) remaining -= got;
      }
      whole = remaining == 0;
    }
    if (whole) http.end(); else httpDrop();
    out.close();

    // Delen maste vara exakt sa stor som kontraktet sager (sista delen ar
    // resten). En trunkerad del kastas och hamtas om nasta varv - de hela
    // delarna fore den ar redan i sakerhet i huvudfilen.
    File dh = SDCARD.open(del, FILE_READ);
    const long dsz = dh ? (long)dh.size() : -1;
    if (dh && (expectBytes <= 0 || dsz == partExpect)) {
      // Hel: lagg pa huvudfilen.
      File main = SDCARD.open(tmp, p == 0 ? FILE_WRITE : FILE_APPEND);
      bool ok = main;
      while (ok && dh.available()) {
        const size_t gotc = dh.read(buf, sizeof(buf));
        // En nollasning fran kortet mitt i en kopiering ar ett fel, inte
        // ett slut: att tyst bryta har gjorde huvudfilen for kort, och
        // felet syntes forst i slutkontrollen - av hela filen.
        if (gotc == 0) { ok = false; break; }
        ok = main.write(buf, gotc) == gotc;
        if (!ok) g_diskFull = true;
      }
      if (main) main.close();
      dh.close();
      SDCARD.remove(del);
      if (!ok) {
        // Halva delen hann pa huvudfilen och det gar inte att klippa av en
        // fil pa kortet, sa allt maste om. Orsaken ar vard en rad: utan den
        // ser tappade 100 MB ut som ett natverksfel.
        logg::event("%s del %d/%d: kortet vagrade skriva%s - allt hamtas om",
                    urlName, p + 1, parts, g_diskFull ? " (fullt?)" : "");
        SDCARD.remove(tmp);
        return false;
      }
      Serial.printf("moln: %s del %d/%d klar\n", urlName, p + 1, parts);
    } else {
      if (dh) dh.close();
      SDCARD.remove(del);
      // Siffran ar sjalva diagnosen, sa den gar till enhetsloggen: noll byte
      // ar en dod forbindelse, nastan hela delen ar en klippt strom - tva
      // helt olika fel som ser likadana ut pa skarmen.
      logg::event("%s del %d/%d brots (%ld av %ld byte)", urlName, p + 1,
                  parts, dsz, partExpect);
      return false;
    }
  }

  // Storleken ar det yttre kvittot: molnet sa i /config exakt hur stor filen
  // ar, och en annan siffra ar en annan fil.
  File check = SDCARD.open(tmp, FILE_READ);
  if (!check) return false;
  const long gotBytes = (long)check.size();
  uint32_t gotMagic = 0;
  const bool okRead = check.read((uint8_t *)&gotMagic, 4) == 4;
  check.close();
  if (!okRead || gotMagic != magic ||
      (expectBytes > 0 && gotBytes != expectBytes)) {
    // Orsaken i klartext till enhetsloggen - "forkastad" utan siffror var
    // ofelsokbart fran webappen.
    logg::event("%s forkastad (%ld av %ld byte, %s)", urlName, gotBytes,
                expectBytes,
                okRead && gotMagic == magic ? "ratt signatur" : "FEL signatur");
    SDCARD.remove(tmp);
    g_prefs.putString("tmpFil", "");
    return false;
  }

  // Kamerafilerna kan vara oppna i avlasningstraden - handslaget later den
  // slappa dem, och lasa om efterat.
  cams::beginUpdate();
  bool removed = true;
  if (SDCARD.exists(target)) removed = SDCARD.remove(target);
  const bool ok = removed && SDCARD.rename(tmp, target);
  cams::endUpdate();

  if (!ok) {
    // Den hela, kontrollerade filen lamnas kvar som tmp och prefs pekar
    // fortfarande pa den - nasta forsok hoppar direkt till inbytet i
    // stallet for att hamta om alltihop.
    logg::event("%s: kunde inte byta in filen pa plats (%s misslyckades)",
                urlName, removed ? "namnbytet" : "borttagningen");
    return false;
  }
  g_prefs.putString("tmpFil", "");

  lock();
  g_status.filesDownloaded++;
  unlock();
  return true;
}

bool downloadKunder() {
  HTTPClient &http = g_http;
  if (!httpBegin("/file/kunder")) return false;
  const int code = http.GET();
  if (code != 200) { httpDrop(); return false; }
  const String csv = http.getString();
  http.end();
  if (!csv.length()) return false;

  File f = SDCARD.open(CUSTOMERS_FILE, FILE_WRITE);
  if (!f) return false;
  f.print(csv);
  f.close();
  customers::reload();
  return true;
}

// Enhetsloggen upp till molnet. Nya rader sedan forra synken skickas i sma
// klumpar; hur langt vi kommit star i prefs, sa inget skickas tva ganger.
// Loggen ar ett hjalpmedel och far aldrig falla rundan - ett fel har lamnar
// raderna kvar till nasta forsok, tyst.
// En rad rakt upp i molnet, utan omvagen over kortet. Den behovs for att
// det varsta felet - ett fullt kort - ocksa ar det som tystar loggfilen: da
// star webbappen tom just den gang man behover se varfor.
bool postLog(const char *data, size_t n) {
  HTTPClient &http = g_http;
  if (!httpBegin("/log")) {
    Serial.println("moln: loggen - tls fick inte plats");
    return false;
  }
  http.addHeader("Content-Type", "text/plain");
  const int code = http.POST((uint8_t *)data, n);
  http.end();
  if (code != 200) {
    httpDrop();
    Serial.printf("moln: loggen vagrades (kod %d)\n", code);
    return false;
  }
  return true;
}

// Enhetens halsa i en enda rad: internminne, kortets utrymme och hur manga
// loggrader som gatt forlorade. Skrivs aldrig till kortet - den ar till for
// tillfallena da kortet inte tar emot nagot.
void postStatus() {
  char stamp[24];
  const uint32_t t = sensors::unixUtc();
  if (t) {
    sensors::isoUtc(t, stamp, sizeof(stamp));
  } else {
    snprintf(stamp, sizeof(stamp), "+%lus", (unsigned long)(millis() / 1000));
  }

  uint64_t total = 0, ledigt = 0;
  const bool sdOk = sdSpace(total, ledigt);

  char rad[200];
  const int n = snprintf(
      rad, sizeof(rad),
      "%s status: fritt %lu, storsta block %lu, kort %lu/%lu MB ledigt, "
      "tappade loggrader %lu\n",
      stamp,
      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
      (unsigned long)(sdOk ? (ledigt >> 20) : 0),
      (unsigned long)(sdOk ? (total >> 20) : 0), logg::lostLines());
  Serial.printf("moln: %s", rad);
  if (n > 0) postLog(rad, (size_t)n);
}

void uploadLog() {
  // Statusraden forst av allt: den ar genererad i minnet och kommer fram
  // aven nar bade kortet och resten av rundan sviker.
  postStatus();

  File f = SDCARD.open(LOG_FILE, FILE_READ);
  if (!f) return;
  uint32_t sent = g_prefs.getUInt("logSent", 0);
  const uint32_t size = (uint32_t)f.size();
  if (sent > size) sent = 0;  // filen har roterats - borja om fran borjan

  uint8_t rounds = 0;
  static char chunk[4001];
  while (sent < size && rounds++ < 8) {
    if (mustAbort()) break;
    f.seek(sent);
    const size_t got = f.read((uint8_t *)chunk, sizeof(chunk) - 1);
    if (!got) break;
    // Klipp vid sista radslutet sa att en rad aldrig delas i tva poster.
    size_t n = got;
    if (sent + got < size) {
      while (n && chunk[n - 1] != '\n') n--;
      if (!n) n = got;
    }

    if (!postLog(chunk, n)) {
      Serial.printf("moln: loggen stannade med %lu byte kvar\n",
                    (unsigned long)(size - sent));
      break;
    }
    sent += n;
    g_prefs.putUInt("logSent", sent);
  }
  f.close();
}

// Hela synkvarvet. Sant nar allt gick igenom.
bool runSync() {
  setState(CLOUD_SYNCING, "hamtar molnlaget");
  Serial.printf("moln: fritt internminne %lu byte, storsta block %lu\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_largest_free_block(
                    MALLOC_CAP_INTERNAL));

  String cfg;
  {
    HTTPClient &http = g_http;
    if (!httpBegin("/config")) return false;
    const int code = http.GET();
    if (code != 200) {
      httpDrop();
      char msg[48];
      snprintf(msg, sizeof(msg),
               code == 401 ? "fel token" : "molnet svarar inte (kod %d)", code);
      setState(CLOUD_ERROR, msg);
      logg::event("moln svarade inte (kod %d) - fritt %lu, storsta block %lu",
                  code,
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)heap_caps_get_largest_free_block(
                      MALLOC_CAP_INTERNAL));
      return false;
    }
    cfg = http.getString();
    http.end();
  }

  const long lastSynced = jsonInt(cfg, "last_synced_trip", 0);

  g_prefs.begin("cloud", false);

  // Buffertarna maste borja tomma: getString ror dem inte alls nar nyckeln
  // saknas, och en jamforelse mot stackskrap ar ingen jamforelse.
  char ver[24] = "";
  int parts = 1;
  long size = 0;
  char have[24] = "";

  // Enhetsloggen gar upp allra forst, direkt efter config. Den lag tidigare
  // efter uppladdningarna, och nar en runda dog pa vagen dog felsokningen
  // med den: raderna som beskrev felet fastnade bakom felet. Nagra kilobyte
  // text ar dessutom det billigaste anropet i hela rundan.
  g_diskFull = false;
  uploadLog();

  // Manuellt ditlagda filer antas forst av allt: de har stegen ar sma och
  // hinner fram aven pa en lank som dor efter nagra sekunder, och ett
  // fardigt kort ska inte sta som "vill ladda ner" for att uppladdningarna
  // rakade stryka med forst.
  if (fileVersion(cfg, "kameror", ver, sizeof(ver), &parts, &size)) {
    g_prefs.getString("vKam", have, sizeof(have));
    if (strcmp(ver, have) != 0 &&
        adoptLocal(CAMS_FILE, "kamerafilen", 0x31434C44, size)) {
      g_prefs.putString("vKam", ver);
      logg::event("kamerafilen fanns redan pa kortet - version %s antagen",
                  ver);
      cams::beginUpdate();
      cams::endUpdate();
    }
  }
  if (fileVersion(cfg, "hastighet", ver, sizeof(ver), &parts, &size)) {
    g_prefs.getString("vHast", have, sizeof(have));
    if (strcmp(ver, have) != 0 &&
        adoptLocal(LIMITS_FILE, "hastighetsfilen", 0x31484C44, size)) {
      g_prefs.putString("vHast", ver);
      logg::event("hastighetsfilen fanns redan pa kortet - version %s antagen",
                  ver);
      cams::beginUpdate();
      cams::endUpdate();
    }
  }

  // Kundlistan forst: hundra byte som gui:t behover ska aldrig fa vanta pa
  // en jattefil eller stoppas av en uppladdning som strular.
  if (fileVersion(cfg, "kunder", ver, sizeof(ver), &parts, &size)) {
    g_prefs.getString("vKund", have, sizeof(have));
    if (strcmp(ver, have) != 0) {
      setState(CLOUD_SYNCING, "hamtar kundlistan");
      if (downloadKunder()) {
        g_prefs.putString("vKund", ver);
        Serial.printf("moln: kundlistan uppdaterad, %u kunder\n",
                      (unsigned)customers::count());
      }
    }
  }

  // Fram till hit ar felen harda - utan config vet vi ingenting. Harifran
  // ar de mjuka: varje steg gor sitt basta och rundan fortsatter, sa att en
  // strulande fil aldrig staller de andra. Bara en resa som borjar avbryter.
  bool allOk = true;

  setState(CLOUD_SYNCING, "laddar upp resor");
  const bool tripsOk = uploadTrips(lastSynced);
  if (!tripsOk) {
    if (mustAbort()) { g_prefs.end(); return false; }
    allOk = false;
  }

  if (tripsOk) {
    setState(CLOUD_SYNCING, "laddar upp gpx");
    uint8_t gpxFailed = 0;
    if (!uploadGpx(gpxFailed)) {
      if (mustAbort()) { g_prefs.end(); return false; }
      allOk = false;
    }
    if (gpxFailed) allOk = false;
  }

  if (fileVersion(cfg, "kameror", ver, sizeof(ver), &parts, &size)) {
    g_prefs.getString("vKam", have, sizeof(have));
    // Adoptionen ar redan provad i borjan av rundan - hit nar bara
    // versioner som faktiskt maste hamtas.
    if (strcmp(ver, have) != 0) {
      if (worthTrying(g_kamAttempt, ver)) {
        setState(CLOUD_SYNCING, "hamtar kamerafilen");
        if (downloadFile("kameror", 1, CAMS_FILE, 0x31434C44, size, ver,
                         "kamerafilen")) {
          g_prefs.putString("vKam", ver);
          logg::event("kamerafilen uppdaterad till version %s", ver);
        } else {
          g_kamAttempt.fails++;
          allOk = false;
        }
      }
    }
  }

  // Hastighetsfilen synkas aldrig over wifi. Den ar 130 MB i 34 delar, och
  // det var omtagen pa den - en halvtimme av misslyckade tls-uppkopplingar
  // - som drog ner resten av synken med sig: varje runda efter ett filforsok
  // blev samre, tills inte ens /config gick fram (kod -1). Filen byts sa
  // sallan att den far komma in den sakra vagen: webbappens knapp "Ladda ner
  // HASTIGHET.BIN", kortet i datorn, klart. Adoptionen overst i rundan
  // kanner igen den. Har sags bara, en gang per version, om kortet ar
  // inaktuellt - sa att webbappens enhetslogg visar det utan att tjata.
  if (fileVersion(cfg, "hastighet", ver, sizeof(ver), &parts, &size)) {
    g_prefs.getString("vHast", have, sizeof(have));
    if (strcmp(ver, have) != 0) {
      static char nagged[24] = "";
      if (strcmp(nagged, ver) != 0) {
        strncpy(nagged, ver, sizeof(nagged) - 1);
        nagged[sizeof(nagged) - 1] = '\0';
        logg::event("hastighetsfilen pa kortet ar inte molnets version %s - "
                    "ladda ner HASTIGHET.BIN i webbappen och lagg pa kortet",
                    ver);
      }
    }
  }

  g_prefs.end();
  return allOk;
}

// ------------------------------------------------------------- traden ------

void syncTask(void *) {
  uint32_t nextAttemptMs = 0;
  uint32_t backoffS = 120;

  for (;;) {
    delay(1000);

    if (g_reconfigured) {
      g_reconfigured = false;
      WiFi.disconnect(true);
      nextAttemptMs = 0;
      backoffS = 120;
    }

    if (!anyNet()) {
      if (g_status.state != CLOUD_OFF) setState(CLOUD_OFF, "");
      continue;
    }

    // Under fard: koppla ned stationen och vanta. Natet har inget arende da.
    if (trip::status().active) {
      if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
      continue;
    }

    const uint32_t now = millis();
    // Med autosynken avslagen hander ingenting forran knappen trycks.
    if (!g_syncNow && (!g_autoSync || now < nextAttemptMs)) continue;
    g_syncNow = false;

    // Alla sparade nat provas i EN runda tills nagot gar upp - inte ett
    // per runda med lang backoff emellan. Ordningen: de nat skanningen
    // faktiskt hor forst, starkast forst; darefter de ohorbara i tur och
    // ordning - dolda ssid syns aldrig i en skanning och en hotspot kan
    // annonsera glest, men bada svarar pa ett riktigt forsok.
    // Innan radion tas i ansprak: lagg ner accesspunkten och bluetooth.
    // Bada haller internminne som tls-handskakningen behover, och pa det
    // mindre kortet ar det skillnaden mellan en synk och "kod -1".
    websync::suspend(true);
    obd::suspend(true);
    // Och vanta in att de FAKTISKT lagt sig. Obd-traden kan sta mitt i en
    // sex sekunders skanning nar pausen begars; att ga vidare innan
    // bluetooth ar nere gav synken 7 kB storsta block och kod -1.
    for (uint8_t w = 0; w < 100 && (websync::isUp() || obd::bleUp()); w++) {
      delay(100);
    }

    setState(CLOUD_CONNECTING, "soker naten");
    WiFi.enableSTA(true);
    int16_t rssi[cloudsync::kNetMax];
    bool heard[cloudsync::kNetMax] = {};
    {
      const int16_t found = WiFi.scanNetworks();
      for (uint8_t s = 0; s < cloudsync::kNetMax; s++) {
        rssi[s] = -1000;
        if (!g_ssids[s][0]) continue;
        for (int16_t i = 0; i < found; i++) {
          if (WiFi.SSID(i) == g_ssids[s] && WiFi.RSSI(i) > rssi[s]) {
            rssi[s] = WiFi.RSSI(i);
            heard[s] = true;
          }
        }
      }
      WiFi.scanDelete();
    }

    uint8_t order[cloudsync::kNetMax];
    uint8_t nOrder = 0;
    {
      bool taken[cloudsync::kNetMax] = {};
      for (;;) {
        int best = -1;
        for (uint8_t s = 0; s < cloudsync::kNetMax; s++) {
          if (heard[s] && !taken[s] && (best < 0 || rssi[s] > rssi[best])) {
            best = s;
          }
        }
        if (best < 0) break;
        taken[best] = true;
        order[nOrder++] = (uint8_t)best;
      }
      for (uint8_t s = 0; s < cloudsync::kNetMax; s++) {
        const uint8_t cand = (g_rr + s) % cloudsync::kNetMax;
        if (g_ssids[cand][0] && !taken[cand]) {
          taken[cand] = true;
          order[nOrder++] = cand;
        }
      }
      g_rr = (uint8_t)((g_rr + 1) % cloudsync::kNetMax);
    }

    char msg[64];
    bool up = false;
    int pick = -1;
    for (uint8_t k = 0; k < nOrder && !up; k++) {
      const uint8_t cand = order[k];
      if (trip::status().active) break;
      snprintf(msg, sizeof(msg), "ansluter till %s (%u/%u)", g_ssids[cand],
               (unsigned)(k + 1), (unsigned)nOrder);
      setState(CLOUD_CONNECTING, msg);
      WiFi.begin(g_ssids[cand], g_passes[cand]);
      // Ett hort nat far tid pa sig; ett ohorbart provas snabbt sa att
      // rundan hinner vidare till nasta.
      const uint32_t waitS = heard[cand] ? 30 : 12;
      for (uint32_t i = 0; i < waitS; i++) {
        if (trip::status().active) break;
        if (WiFi.status() == WL_CONNECTED) { up = true; pick = cand; break; }
        delay(1000);
      }
      if (!up) WiFi.disconnect(true);
    }

    if (!up) {
      WiFi.disconnect(true);
      websync::suspend(false);
      obd::suspend(false);
      // Vanligaste orsakerna i den har ordningen: naten sander bara pa
      // 5 GHz (radion har hor bara 2,4), hotspoten ar inte igang, eller
      // telefonen med hotspoten ar sjalv ansluten till enhetens wifi.
      setState(CLOUD_IDLE, "inget av naten nas - 2,4 GHz? hotspot pa?");
      logg::event("synk: inget av %u nat gick att na", (unsigned)nOrder);
      nextAttemptMs = millis() + backoffS * 1000UL;
      backoffS = min<uint32_t>(backoffS * 2, 900);
      continue;
    }

    strncpy(g_active, g_ssids[pick], sizeof(g_active) - 1);
    g_active[sizeof(g_active) - 1] = '\0';

    // Uppkopplad ar inte detsamma som anvandbar. "Kod -1" betyder bara att
    // anslutningen inte gick att fa till - och den vanligaste orsaken pa en
    // telefonhotspot ar att namnuppslaget inte svarar. Ett uppslag har
    // skiljer det fallet fran ett riktigt natfel, och en reservserver later
    // synken ga vidare i stallet for att fastna.
    {
      IPAddress ip;
      bool dnsOk = WiFi.hostByName(kHost, ip);
      if (!dnsOk) {
        Serial.println("moln: namnuppslaget svarade inte - provar reserv-dns");
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                    IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
        delay(200);
        dnsOk = WiFi.hostByName(kHost, ip);
        logg::event("dns: hotspotens server svarade inte, reserv %s",
                    dnsOk ? "loste namnet" : "hjalpte inte heller");
      }
      Serial.printf("moln: ip %s  gw %s  dns %s  namn %s\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.gatewayIP().toString().c_str(),
                    WiFi.dnsIP().toString().c_str(),
                    dnsOk ? ip.toString().c_str() : "OLOST");
    }
    logg::event("synk borjar via %s (rssi %d)", g_ssids[pick],
                heard[pick] ? (int)rssi[pick] : 0);

    lock();
    const CloudStatus fore = g_status;
    unlock();

    const bool ok = runSync();
    // Rundan ar slut, oavsett hur: forbindelsen slapps och tls-minnet
    // lamnas tillbaka innan accesspunkten far radion igen.
    httpDrop();

    lock();
    const CloudStatus efter = g_status;
    unlock();
    logg::event("synk %s via %s: %lu resor, %lu gpx, %lu filer",
                ok ? "klar" : "delvis", g_ssids[pick],
                (unsigned long)(efter.tripsUploaded - fore.tripsUploaded),
                (unsigned long)(efter.gpxUploaded - fore.gpxUploaded),
                (unsigned long)(efter.filesDownloaded - fore.filesDownloaded));
    // Slutraden hinner ofta inte med i den har rundans uppladdning - den
    // ligger forst i kon nasta gang, och det racker.
    WiFi.disconnect(true);
    websync::suspend(false);
    obd::suspend(false);

    if (ok) {
      snprintf(msg, sizeof(msg), "synkad via %s", g_ssids[pick]);
      setState(CLOUD_DONE, msg);
      backoffS = 120;
      nextAttemptMs = millis() + 15UL * 60UL * 1000UL;
    } else if (!trip::status().active) {
      setState(CLOUD_ERROR, g_status.detail[0] ? g_status.detail : "synken brots");
      nextAttemptMs = millis() + backoffS * 1000UL;
      backoffS = min<uint32_t>(backoffS * 2, 900);
    }
    // Brots synken av en resa sags ingenting - det ar det normala, inte ett fel.
  }
}

}  // namespace

namespace cloudsync {

void begin() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();

  g_prefs.begin("cloud", true);
  char key[8];
  for (uint8_t i = 0; i < kNetMax; i++) {
    snprintf(key, sizeof(key), "ssid%u", i);
    g_prefs.getString(key, g_ssids[i], sizeof(g_ssids[i]));
    snprintf(key, sizeof(key), "pass%u", i);
    g_prefs.getString(key, g_passes[i], sizeof(g_passes[i]));
  }
  // Aldre firmware sparade ett enda nat under "ssid"/"pass". Det flyttar in
  // pa forsta platsen, sa att en uppgradering inte tappar uppgifterna.
  if (!anyNet()) {
    g_prefs.getString("ssid", g_ssids[0], sizeof(g_ssids[0]));
    g_prefs.getString("pass", g_passes[0], sizeof(g_passes[0]));
  }
  g_prefs.getString("tok", g_token, sizeof(g_token));
  g_prefs.end();

  setState(anyNet() ? CLOUD_IDLE : CLOUD_OFF, anyNet() ? "redo" : "");

  // TLS behover rejalt med stack. Kor pa karna 1, dar aven huvudloopen med
  // gui:t bor - men med SAMMA prioritet, inte hogre: med prioritet 2 svalte
  // en langre nedladdning skarmen helt, och det kandes som att hela enheten
  // hangde sig. Lika prioritet ger turordning, och gui:t far sina varv.
  xTaskCreatePinnedToCore(syncTask, "cloudsync", 16384, nullptr, 1, nullptr, 1);
}

void configureNets(const char *ssids[kNetMax], const char *passwords[kNetMax],
                   const char *token) {
  // Sidan visar aldrig lagrade hemligheter, sa losenordsfalten star tomma
  // aven nar losenord finns. Darfor: samma ssid med tomt losenord behaller
  // det lagrade; nytt ssid tar det som skrivits (aven tomt - oppna nat
  // finns); tomt ssid tommer platsen.
  for (uint8_t i = 0; i < kNetMax; i++) {
    const char *s = ssids[i] ? ssids[i] : "";
    const char *p = passwords[i] ? passwords[i] : "";
    const bool sameNet = strcmp(s, g_ssids[i]) == 0;
    if (s[0] == '\0') {
      g_ssids[i][0] = '\0';
      g_passes[i][0] = '\0';
    } else {
      strncpy(g_ssids[i], s, sizeof(g_ssids[i]) - 1);
      g_ssids[i][sizeof(g_ssids[i]) - 1] = '\0';
      if (p[0] || !sameNet) {
        strncpy(g_passes[i], p, sizeof(g_passes[i]) - 1);
        g_passes[i][sizeof(g_passes[i]) - 1] = '\0';
      }
    }
  }
  // Token ar enhetens, inte natets. Tomt falt behaller den lagrade.
  if (token && token[0]) {
    strncpy(g_token, token, sizeof(g_token) - 1);
    g_token[sizeof(g_token) - 1] = '\0';
  }

  g_prefs.begin("cloud", false);
  char key[8];
  for (uint8_t i = 0; i < kNetMax; i++) {
    snprintf(key, sizeof(key), "ssid%u", i);
    g_prefs.putString(key, g_ssids[i]);
    snprintf(key, sizeof(key), "pass%u", i);
    g_prefs.putString(key, g_passes[i]);
  }
  g_prefs.putString("tok", g_token);
  g_prefs.end();

  g_reconfigured = true;
  g_syncNow = anyNet();
}

bool configured() { return anyNet(); }

bool hasToken() { return g_token[0] != '\0'; }

String netSsid(uint8_t i) {
  return i < kNetMax ? String(g_ssids[i]) : String("");
}

bool netHasPassword(uint8_t i) {
  return i < kNetMax && g_passes[i][0] != '\0';
}

String ssid() {
  if (g_active[0]) return String(g_active);
  for (uint8_t i = 0; i < kNetMax; i++) {
    if (g_ssids[i][0]) return String(g_ssids[i]);
  }
  return String("");
}

void requestSync() {
  g_syncNow = true;
  // Kvittens direkt pa skarmen - synktraden vaknar inom en sekund, men
  // utan den har raden ser trycket ut att inte ha tagit.
  if (!trip::status().active && anyNet()) {
    setState(CLOUD_CONNECTING, "synk pa gang");
  }
}

void setAutoSync(bool on) { g_autoSync = on; }

CloudStatus status() {
  lock();
  CloudStatus s = g_status;
  unlock();
  return s;
}

}  // namespace cloudsync
