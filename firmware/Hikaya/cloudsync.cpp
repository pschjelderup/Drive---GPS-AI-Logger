#include "cloudsync.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "cams.h"
#include "config.h"
#include "customers.h"
#include "sensors.h"
#include "trip.h"

namespace {

// Webbappens moln. Adressen ar fast: det ar var egen edge-funktion, och
// enhetens legitimation ar token i huvudet - inte adressen.
const char *kBase =
    "https://jdjkeloiwjkcycelmexq.supabase.co/functions/v1/drive-sync";

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
FileAttempt g_hastAttempt = {};

bool worthTrying(FileAttempt &a, const char *ver) {
  if (strcmp(a.ver, ver) != 0) {
    strncpy(a.ver, ver, sizeof(a.ver) - 1);
    a.ver[sizeof(a.ver) - 1] = '\0';
    a.fails = 0;
  }
  return a.fails < 12;
}

bool httpBegin(HTTPClient &http, WiFiClientSecure &tls, const String &path) {
  tls.setInsecure();  // se blocket overst
  if (!http.begin(tls, String(kBase) + path)) return false;
  http.addHeader("x-drive-token", g_token);
  http.setTimeout(30000);
  return true;
}

// ------------------------------------------------------------ synkstegen ---

bool uploadTrips(long lastSynced) {
  File f = SD_MMC.open(TRIPS_JSONL, FILE_READ);
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
    int code;
    {
      WiFiClientSecure tls;
      HTTPClient http;
      if (!httpBegin(http, tls, "/trips")) {
        setState(CLOUD_ERROR, "uppladdning: tls fick inte plats");
        return false;
      }
      http.addHeader("Content-Type", "application/x-ndjson");
      code = http.POST(batch);
      http.end();
    }
    if (code != 200) {
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

bool uploadGpx() {
  File dir = SD_MMC.open(GPX_DIR);
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
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) continue;

    WiFiClientSecure tls;
    HTTPClient http;
    if (!httpBegin(http, tls, String("/gpx?name=") + name)) {
      f.close();
      return false;
    }
    http.addHeader("Content-Type", "application/gpx+xml");
    // Strommad uppladdning: filen gar genom en buffert, aldrig in i minnet hel.
    const int code = http.sendRequest("POST", &f, f.size());
    http.end();
    f.close();

    if (code != 200) return false;

    // Uppladdad ar inte raderad: filen flyttas till UPPLADDAT och ligger kvar
    // pa kortet. Kortet ar en kopia av sanningen aven efter synken.
    String to = String(GPX_SYNCED_DIR) + "/" + name;
    if (SD_MMC.exists(to)) SD_MMC.remove(to);
    SD_MMC.rename(path, to);

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
bool downloadFile(const char *urlName, int parts, const char *target,
                  const uint32_t magic, long expectBytes, const char *ver) {
  const char *tmp = "/DRIVE/NED.TMP";
  const char *del = "/DRIVE/DEL.TMP";

  // Ateruppta dar det brots - om tmp-filen tillhor samma fil och version
  // och slutar pa en delgrans. Allt annat borjar om fran noll.
  int startPart = 0;
  {
    char tf[16] = "", tv[24] = "";
    g_prefs.getString("tmpFil", tf, sizeof(tf));
    g_prefs.getString("tmpVer", tv, sizeof(tv));
    if (SD_MMC.exists(tmp) && strcmp(tf, urlName) == 0 &&
        strcmp(tv, ver) == 0 && expectBytes > 0) {
      File h = SD_MMC.open(tmp, FILE_READ);
      if (h) {
        const uint64_t sz = h.size();
        h.close();
        if (sz > 0 && sz % CLOUD_PART_BYTES == 0 &&
            (int)(sz / CLOUD_PART_BYTES) < parts) {
          startPart = (int)(sz / CLOUD_PART_BYTES);
        }
      }
    }
    if (startPart == 0 && SD_MMC.exists(tmp)) SD_MMC.remove(tmp);
    g_prefs.putString("tmpFil", urlName);
    g_prefs.putString("tmpVer", ver);
  }
  if (startPart > 0) {
    Serial.printf("moln: %s aterupptas fran del %d/%d\n", urlName,
                  startPart + 1, parts);
  }

  uint8_t buf[4096];
  for (int p = startPart; p < parts; p++) {
    // Resan har foretrade - men tmp-filen lamnas kvar, sa att delarna som
    // redan ar hemma inte behover hamtas igen.
    if (mustAbort()) return false;

    const long partExpect = (p < parts - 1 || expectBytes <= 0)
        ? (long)CLOUD_PART_BYTES
        : expectBytes - (long)(parts - 1) * (long)CLOUD_PART_BYTES;

    if (SD_MMC.exists(del)) SD_MMC.remove(del);
    File out = SD_MMC.open(del, FILE_WRITE);
    if (!out) return false;

    WiFiClientSecure tls;
    HTTPClient http;
    char path[80];
    if (parts > 1) {
      snprintf(path, sizeof(path), "/file/%s?part=%d", urlName, p);
    } else {
      snprintf(path, sizeof(path), "/file/%s", urlName);
    }
    if (!httpBegin(http, tls, path)) { out.close(); return false; }

    const int code = http.GET();
    if (code != 200) { http.end(); out.close(); return false; }

    WiFiClient *stream = http.getStreamPtr();
    int remaining = http.getSize();
    while (remaining != 0) {
      if (mustAbort()) { http.end(); out.close(); return false; }
      const size_t got = stream->readBytes(
          buf, min((int)sizeof(buf), remaining > 0 ? remaining : (int)sizeof(buf)));
      if (got == 0) break;
      if (out.write(buf, got) != got) {
        http.end(); out.close(); return false;
      }
      if (remaining > 0) remaining -= got;
    }
    http.end();
    out.close();

    // Delen maste vara exakt sa stor som kontraktet sager (sista delen ar
    // resten). En trunkerad del kastas och hamtas om nasta varv - de hela
    // delarna fore den ar redan i sakerhet i huvudfilen.
    File dh = SD_MMC.open(del, FILE_READ);
    const long dsz = dh ? (long)dh.size() : -1;
    if (dh && (expectBytes <= 0 || dsz == partExpect)) {
      // Hel: lagg pa huvudfilen.
      File main = SD_MMC.open(tmp, p == 0 ? FILE_WRITE : FILE_APPEND);
      bool ok = main;
      while (ok && dh.available()) {
        const size_t gotc = dh.read(buf, sizeof(buf));
        if (gotc == 0) break;
        ok = main.write(buf, gotc) == gotc;
      }
      if (main) main.close();
      dh.close();
      SD_MMC.remove(del);
      if (!ok) { SD_MMC.remove(tmp); return false; }
      Serial.printf("moln: %s del %d/%d klar\n", urlName, p + 1, parts);
    } else {
      if (dh) dh.close();
      SD_MMC.remove(del);
      Serial.printf("moln: %s del %d/%d brots (%ld av %ld byte)\n", urlName,
                    p + 1, parts, dsz, partExpect);
      return false;
    }
  }

  // Storleken ar det yttre kvittot: molnet sa i /config exakt hur stor filen
  // ar, och en annan siffra ar en annan fil.
  File check = SD_MMC.open(tmp, FILE_READ);
  if (!check) return false;
  const long gotBytes = (long)check.size();
  uint32_t gotMagic = 0;
  const bool okRead = check.read((uint8_t *)&gotMagic, 4) == 4;
  check.close();
  if (!okRead || gotMagic != magic ||
      (expectBytes > 0 && gotBytes != expectBytes)) {
    Serial.printf("moln: %s forkastad (%ld av %ld byte)\n", urlName, gotBytes,
                  expectBytes);
    SD_MMC.remove(tmp);
    g_prefs.putString("tmpFil", "");
    return false;
  }
  g_prefs.putString("tmpFil", "");

  // Kamerafilerna kan vara oppna i avlasningstraden - handslaget later den
  // slappa dem, och lasa om efterat.
  cams::beginUpdate();
  if (SD_MMC.exists(target)) SD_MMC.remove(target);
  const bool ok = SD_MMC.rename(tmp, target);
  cams::endUpdate();

  if (ok) {
    lock();
    g_status.filesDownloaded++;
    unlock();
  }
  return ok;
}

bool downloadKunder() {
  WiFiClientSecure tls;
  HTTPClient http;
  if (!httpBegin(http, tls, "/file/kunder")) return false;
  const int code = http.GET();
  if (code != 200) { http.end(); return false; }
  const String csv = http.getString();
  http.end();
  if (!csv.length()) return false;

  File f = SD_MMC.open(CUSTOMERS_FILE, FILE_WRITE);
  if (!f) return false;
  f.print(csv);
  f.close();
  customers::reload();
  return true;
}

// Hela synkvarvet. Sant nar allt gick igenom.
bool runSync() {
  setState(CLOUD_SYNCING, "hamtar molnlaget");
  Serial.printf("moln: fritt internminne %lu byte\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  String cfg;
  {
    WiFiClientSecure tls;
    HTTPClient http;
    if (!httpBegin(http, tls, "/config")) return false;
    const int code = http.GET();
    if (code != 200) {
      http.end();
      char msg[48];
      snprintf(msg, sizeof(msg),
               code == 401 ? "fel token" : "molnet svarar inte (kod %d)", code);
      setState(CLOUD_ERROR, msg);
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

  setState(CLOUD_SYNCING, "laddar upp resor");
  if (!uploadTrips(lastSynced)) { g_prefs.end(); return false; }

  setState(CLOUD_SYNCING, "laddar upp gpx");
  if (!uploadGpx()) { g_prefs.end(); return false; }

  if (fileVersion(cfg, "kameror", ver, sizeof(ver), &parts, &size)) {
    g_prefs.getString("vKam", have, sizeof(have));
    if (strcmp(ver, have) != 0 && worthTrying(g_kamAttempt, ver)) {
      setState(CLOUD_SYNCING, "hamtar kamerafilen");
      if (downloadFile("kameror", 1, CAMS_FILE, 0x31434C44, size, ver)) {
        g_prefs.putString("vKam", ver);
      } else {
        g_kamAttempt.fails++;
      }
    }
  }

  if (fileVersion(cfg, "hastighet", ver, sizeof(ver), &parts, &size)) {
    g_prefs.getString("vHast", have, sizeof(have));
    if (strcmp(ver, have) != 0 && worthTrying(g_hastAttempt, ver)) {
      // Stora filen. Delarna hamtas i foljd till samma tillfalliga fil;
      // ett avbrott kostar omtag, aldrig en halv fil pa riktig plats.
      setState(CLOUD_SYNCING, "hamtar hastighetsfilen");
      if (downloadFile("hastighet", parts, LIMITS_FILE, 0x31484C44, size, ver)) {
        g_prefs.putString("vHast", ver);
      } else {
        g_hastAttempt.fails++;
      }
    }
  }

  g_prefs.end();
  return true;
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

    // Vilket av de sparade naten finns har? En skanning ser dem som sander,
    // och det starkaste vinner - bilen kan sta pa jobbet, hemma eller vid
    // en paslagen hotspot, och ratt nat ar det som faktiskt hors.
    setState(CLOUD_CONNECTING, "soker naten");
    WiFi.enableSTA(true);
    int pick = -1;
    {
      const int16_t found = WiFi.scanNetworks();
      int bestRssi = -1000;
      for (int16_t i = 0; i < found; i++) {
        const String seen = WiFi.SSID(i);
        for (uint8_t s = 0; s < cloudsync::kNetMax; s++) {
          if (g_ssids[s][0] && seen == g_ssids[s] && WiFi.RSSI(i) > bestRssi) {
            bestRssi = WiFi.RSSI(i);
            pick = s;
          }
        }
      }
      WiFi.scanDelete();
    }

    // Syntes inget: prova nasta sparade nat i tur och ordning anda. Dolda
    // ssid syns aldrig i en skanning, och en hotspot kan annonsera glest -
    // men bada svarar pa ett riktigt anslutningsforsok.
    uint32_t waitS = 30;
    if (pick < 0) {
      for (uint8_t s = 0; s < cloudsync::kNetMax; s++) {
        const uint8_t cand = (g_rr + s) % cloudsync::kNetMax;
        if (g_ssids[cand][0]) { pick = cand; break; }
      }
      g_rr = (uint8_t)(pick + 1) % cloudsync::kNetMax;
      waitS = 12;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "ansluter till %s", g_ssids[pick]);
    setState(CLOUD_CONNECTING, msg);
    WiFi.begin(g_ssids[pick], g_passes[pick]);

    bool up = false;
    for (uint32_t i = 0; i < waitS; i++) {
      if (trip::status().active) break;
      if (WiFi.status() == WL_CONNECTED) { up = true; break; }
      delay(1000);
    }

    if (!up) {
      WiFi.disconnect(true);
      // Vanligaste orsakerna i den har ordningen: natet sander bara pa
      // 5 GHz (radion har hor bara 2,4), hotspoten ar inte igang, eller
      // telefonen med hotspoten ar sjalv ansluten till enhetens wifi.
      snprintf(msg, sizeof(msg), "%s nas inte - 2,4 GHz? hotspot pa?",
               g_ssids[pick]);
      setState(CLOUD_IDLE, msg);
      nextAttemptMs = now + backoffS * 1000UL;
      backoffS = min<uint32_t>(backoffS * 2, 900);
      continue;
    }

    strncpy(g_active, g_ssids[pick], sizeof(g_active) - 1);
    g_active[sizeof(g_active) - 1] = '\0';

    const bool ok = runSync();
    WiFi.disconnect(true);

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

void requestSync() { g_syncNow = true; }

void setAutoSync(bool on) { g_autoSync = on; }

CloudStatus status() {
  lock();
  CloudStatus s = g_status;
  unlock();
  return s;
}

}  // namespace cloudsync
