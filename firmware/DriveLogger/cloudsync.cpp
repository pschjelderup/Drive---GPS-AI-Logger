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

char g_ssid[33] = "";
char g_pass[65] = "";
char g_token[64] = "";

CloudStatus g_status = {};
SemaphoreHandle_t g_mutex = nullptr;

volatile bool g_syncNow = false;
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

// Versionsblocket for en fil: {"kameror":{"version":"abc","parts":1}, ...}
bool fileVersion(const String &body, const char *name, char *ver, size_t vlen,
                 int *parts) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":{", name);
  const int at = body.indexOf(pat);
  if (at < 0) return false;
  const String slice = body.substring(at, at + 160);
  if (!jsonStr(slice, "version", ver, vlen)) return false;
  *parts = (int)jsonInt(slice, "parts", 1);
  return true;
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

  // Raderna efter senast synkade resa samlas i klumpar, sa att en lang
  // eftersslapning inte behover rymmas i minnet pa en gang.
  String batch;
  batch.reserve(8192);
  uint32_t sent = 0;
  static char line[768];

  auto flush = [&]() -> bool {
    if (!batch.length()) return true;
    WiFiClientSecure tls;
    HTTPClient http;
    if (!httpBegin(http, tls, "/trips")) return false;
    http.addHeader("Content-Type", "application/x-ndjson");
    const int code = http.POST(batch);
    http.end();
    if (code != 200) return false;
    batch = "";
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
    if (batch.length() > 24000 && !flush()) { f.close(); return false; }
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
// magin, och byter forst da. En halv fil pa riktig plats ar varre an ingen.
bool downloadFile(const char *urlName, int parts, const char *target,
                  const uint32_t magic) {
  const char *tmp = "/DRIVE/NED.TMP";
  if (SD_MMC.exists(tmp)) SD_MMC.remove(tmp);

  File out = SD_MMC.open(tmp, FILE_WRITE);
  if (!out) return false;

  uint8_t buf[4096];
  for (int p = 0; p < parts; p++) {
    if (mustAbort()) { out.close(); SD_MMC.remove(tmp); return false; }

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
    if (code != 200) { http.end(); out.close(); SD_MMC.remove(tmp); return false; }

    WiFiClient *stream = http.getStreamPtr();
    int remaining = http.getSize();
    while (remaining != 0) {
      if (mustAbort()) { http.end(); out.close(); SD_MMC.remove(tmp); return false; }
      const size_t got = stream->readBytes(
          buf, min((int)sizeof(buf), remaining > 0 ? remaining : (int)sizeof(buf)));
      if (got == 0) break;
      if (out.write(buf, got) != got) {
        http.end(); out.close(); SD_MMC.remove(tmp); return false;
      }
      if (remaining > 0) remaining -= got;
    }
    http.end();
    Serial.printf("moln: %s del %d/%d klar\n", urlName, p + 1, parts);
  }
  out.close();

  // Magin ar kvittot pa att det var ratt sorts fil och att borjan kom fram.
  File check = SD_MMC.open(tmp, FILE_READ);
  if (!check) return false;
  uint32_t gotMagic = 0;
  const bool okRead = check.read((uint8_t *)&gotMagic, 4) == 4;
  check.close();
  if (!okRead || gotMagic != magic) {
    SD_MMC.remove(tmp);
    return false;
  }

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

  WiFiClientSecure tls;
  HTTPClient http;
  if (!httpBegin(http, tls, "/config")) return false;
  const int code = http.GET();
  if (code != 200) {
    http.end();
    setState(CLOUD_ERROR, code == 401 ? "fel token" : "molnet svarar inte");
    return false;
  }
  const String cfg = http.getString();
  http.end();

  const long lastSynced = jsonInt(cfg, "last_synced_trip", 0);

  setState(CLOUD_SYNCING, "laddar upp resor");
  if (!uploadTrips(lastSynced)) return false;

  setState(CLOUD_SYNCING, "laddar upp gpx");
  if (!uploadGpx()) return false;

  g_prefs.begin("cloud", false);

  char ver[24];
  int parts = 1;
  char have[24];

  if (fileVersion(cfg, "kunder", ver, sizeof(ver), &parts)) {
    g_prefs.getString("vKund", have, sizeof(have));
    if (strcmp(ver, have) != 0) {
      setState(CLOUD_SYNCING, "hamtar kundlistan");
      if (downloadKunder()) g_prefs.putString("vKund", ver);
    }
  }

  if (fileVersion(cfg, "kameror", ver, sizeof(ver), &parts)) {
    g_prefs.getString("vKam", have, sizeof(have));
    if (strcmp(ver, have) != 0) {
      setState(CLOUD_SYNCING, "hamtar kamerafilen");
      if (downloadFile("kameror", 1, CAMS_FILE, 0x31434C44)) {
        g_prefs.putString("vKam", ver);
      }
    }
  }

  if (fileVersion(cfg, "hastighet", ver, sizeof(ver), &parts)) {
    g_prefs.getString("vHast", have, sizeof(have));
    if (strcmp(ver, have) != 0) {
      // Stora filen. Delarna hamtas i foljd till samma tillfalliga fil;
      // ett avbrott kostar omtag, aldrig en halv fil pa riktig plats.
      setState(CLOUD_SYNCING, "hamtar hastighetsfilen");
      if (downloadFile("hastighet", parts, LIMITS_FILE, 0x31484C44)) {
        g_prefs.putString("vHast", ver);
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

    if (g_ssid[0] == '\0') {
      if (g_status.state != CLOUD_OFF) setState(CLOUD_OFF, "");
      continue;
    }

    // Under fard: koppla ned stationen och vanta. Natet har inget arende da.
    if (trip::status().active) {
      if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
      continue;
    }

    const uint32_t now = millis();
    if (!g_syncNow && now < nextAttemptMs) continue;
    g_syncNow = false;

    setState(CLOUD_CONNECTING, "soker natet");
    WiFi.enableSTA(true);
    WiFi.begin(g_ssid, g_pass);

    bool up = false;
    for (int i = 0; i < 30; i++) {
      if (trip::status().active) break;
      if (WiFi.status() == WL_CONNECTED) { up = true; break; }
      delay(1000);
    }

    if (!up) {
      WiFi.disconnect(true);
      // Vanligaste orsakerna i den har ordningen: hotspoten sander bara pa
      // 5 GHz (radion har hor bara 2,4), hotspoten ar inte igang, eller
      // telefonen med hotspoten ar sjalv ansluten till enhetens wifi.
      setState(CLOUD_IDLE, "natet syntes inte - 2,4 GHz? hotspot pa?");
      nextAttemptMs = now + backoffS * 1000UL;
      backoffS = min<uint32_t>(backoffS * 2, 900);
      continue;
    }

    const bool ok = runSync();
    WiFi.disconnect(true);

    if (ok) {
      setState(CLOUD_DONE, "synkad");
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
  g_prefs.getString("ssid", g_ssid, sizeof(g_ssid));
  g_prefs.getString("pass", g_pass, sizeof(g_pass));
  g_prefs.getString("tok", g_token, sizeof(g_token));
  g_prefs.end();

  setState(g_ssid[0] ? CLOUD_IDLE : CLOUD_OFF, g_ssid[0] ? "redo" : "");

  // TLS behover rejalt med stack. Kor pa andra karnan, sa att avlasningstraden
  // pa karna 0 aldrig behover dela varv med en nedladdning.
  xTaskCreatePinnedToCore(syncTask, "cloudsync", 16384, nullptr, 2, nullptr, 1);
}

void configure(const char *ssid, const char *password, const char *token) {
  strncpy(g_ssid, ssid ? ssid : "", sizeof(g_ssid) - 1);
  g_ssid[sizeof(g_ssid) - 1] = '\0';
  // Sidan visar aldrig lagrade hemligheter, sa faltet star tomt aven nar
  // ett losenord finns. Ett tomt falt betyder darfor "behall det som ar" -
  // annars raderar varje omsparning uppgifterna utan att nagon marker det.
  // Att tomma allt gors med tomt ssid: det stanger av synken.
  if (g_ssid[0] == '\0') {
    g_pass[0] = '\0';
    g_token[0] = '\0';
  } else {
    if (password && password[0]) {
      strncpy(g_pass, password, sizeof(g_pass) - 1);
      g_pass[sizeof(g_pass) - 1] = '\0';
    }
    if (token && token[0]) {
      strncpy(g_token, token, sizeof(g_token) - 1);
      g_token[sizeof(g_token) - 1] = '\0';
    }
  }

  g_prefs.begin("cloud", false);
  g_prefs.putString("ssid", g_ssid);
  g_prefs.putString("pass", g_pass);
  g_prefs.putString("tok", g_token);
  g_prefs.end();

  g_reconfigured = true;
  g_syncNow = g_ssid[0] != '\0';
}

bool configured() { return g_ssid[0] != '\0'; }

bool hasPassword() { return g_pass[0] != '\0'; }

bool hasToken() { return g_token[0] != '\0'; }

String ssid() { return String(g_ssid); }

void requestSync() { g_syncNow = true; }

CloudStatus status() {
  lock();
  CloudStatus s = g_status;
  unlock();
  return s;
}

}  // namespace cloudsync
