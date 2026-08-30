#include "obd.h"

#include <BLEDevice.h>
#include <Preferences.h>

#include "config.h"
#include "logg.h"
#include "trip.h"

namespace {

// ------------------------------------------------------------- adaptrarna --
// ELM327-kloner pa BLE talar samma sprak over olika tjanster. I stallet for
// att lista alla varianters uuid-par letar vi upp tjansten och tar den
// egenskap i den som kan skicka aviseringar och den som gar att skriva till.
// Det tacker bade FFF0/FFF1/FFF2, FFE0/FFE1 och 18F0/2AF0/2AF1 - och nasta
// klon ocksa.
const char *kServiceUuids[] = {
    "0000fff0-0000-1000-8000-00805f9b34fb",
    "0000ffe0-0000-1000-8000-00805f9b34fb",
    "000018f0-0000-1000-8000-00805f9b34fb",
    "0000ffe5-0000-1000-8000-00805f9b34fb",
};
const uint8_t kServiceCount = 4;

// Namnen adaptrarna brukar annonsera. Bara ett stod nar tjansten inte syns i
// annonsen - manga adaptrar annonserar namnet men inte sina tjanster.
bool nameLooksLikeObd(const String &n) {
  if (!n.length()) return false;
  String u = n;
  u.toUpperCase();
  return u.indexOf("OBD") >= 0 || u.indexOf("ELM") >= 0 ||
         u.indexOf("VGATE") >= 0 || u.indexOf("ICAR") >= 0 ||
         u.indexOf("VEEPEAK") >= 0 || u.indexOf("VLINK") >= 0 ||
         u.indexOf("V-LINK") >= 0 || u.indexOf("LELINK") >= 0 ||
         u.indexOf("KONNWEI") >= 0 || u.indexOf("CARISTA") >= 0;
}

// ------------------------------------------------------------------ pid:ar -
// Numret ar det som star efter lage 01 i fragan. Kommentaren ar vad bilen
// svarar med.
const uint8_t PID_SUPPORTED_01 = 0x00;
const uint8_t PID_SUPPORTED_21 = 0x20;
const uint8_t PID_SUPPORTED_41 = 0x40;
const uint8_t PID_LOAD = 0x04;      // A*100/255 procent
const uint8_t PID_COOLANT = 0x05;   // A-40 grader
const uint8_t PID_RPM = 0x0C;       // (A*256+B)/4
const uint8_t PID_SPEED = 0x0D;     // A km/h
const uint8_t PID_INTAKE = 0x0F;    // A-40 grader
const uint8_t PID_MAF = 0x10;       // (A*256+B)/100 gram luft per sekund
const uint8_t PID_THROTTLE = 0x11;  // A*100/255 procent
const uint8_t PID_RUNTIME = 0x1F;   // A*256+B sekunder sedan motorstart
const uint8_t PID_FUEL = 0x2F;      // A*100/255 procent i tanken
const uint8_t PID_VOLT = 0x42;      // (A*256+B)/1000 volt
const uint8_t PID_FUELTYPE = 0x51;  // 1 = bensin, 4 = diesel
const uint8_t PID_HYBRID = 0x5B;    // A*100/255 procent kvar i hybridbatteriet
const uint8_t PID_OIL = 0x5C;       // A-40 grader
const uint8_t PID_FUELRATE = 0x5E;  // (A*256+B)/20 liter per timme
const uint8_t PID_AMBIENT = 0x46;   // A-40 grader

// -------------------------------------------------------------- tillstand --

Preferences g_prefs;
SemaphoreHandle_t g_mutex = nullptr;

volatile bool g_enabled = false;
volatile bool g_forget = false;
volatile bool g_bleUp = false;

ObdData g_data = {};
ObdTripSummary g_sum = {};
// Medelvarvtalets rakning. Egna variabler och inte static i funktionen, sa
// att en ny resa faktiskt borjar om fran noll.
uint64_t g_rpmSum = 0;
uint32_t g_rpmN = 0;

// Radiodelen. Bara traden ror dem, utom rx-bufferten som aviseringarna fyller.
BLEClient *g_client = nullptr;
BLERemoteCharacteristic *g_write = nullptr;
BLERemoteCharacteristic *g_notify = nullptr;

char g_rx[512];
volatile uint16_t g_rxLen = 0;
volatile bool g_prompt = false;  // '>' sedd - adaptern vantar pa nasta fraga

// Stodda pid:ar ur 0100/0120/0140, som en bitmask per block.
uint32_t g_sup01 = 0, g_sup21 = 0, g_sup41 = 0;
bool g_diesel = false;

void lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

void setState(ObdState s) {
  lock();
  g_data.state = s;
  unlock();
}

// ------------------------------------------------------------ radiotrafik --

void notifyCb(BLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  for (size_t i = 0; i < len; i++) {
    const char c = (char)data[i];
    if (c == '>') {
      g_prompt = true;
      continue;
    }
    if (g_rxLen < sizeof(g_rx) - 1) g_rx[g_rxLen++] = c;
  }
  g_rx[g_rxLen] = '\0';
}

void rxClear() {
  g_rxLen = 0;
  g_rx[0] = '\0';
  g_prompt = false;
}

// Skickar en rad och vantar in adapterns prompt. Falskt vid tystnad - da ar
// adaptern borta, och traden tar om fran scanningen.
bool ask(const char *cmd, uint32_t timeoutMs) {
  if (!g_write || !g_client || !g_client->isConnected()) return false;
  rxClear();
  char line[24];
  const int n = snprintf(line, sizeof(line), "%s\r", cmd);
  if (!g_write->writeValue((uint8_t *)line, (size_t)n,
                           !g_write->canWriteNoResponse())) {
    return false;
  }
  const uint32_t t0 = millis();
  while (!g_prompt) {
    if (millis() - t0 > timeoutMs) return false;
    if (!g_client->isConnected()) return false;
    delay(5);
  }
  return true;
}

// Svaret utan blanksteg och radbrott, i versaler - da gar det att leta i det
// med enkel textsokning oavsett hur adaptern radbryter.
void tidy(char *out, size_t outLen) {
  size_t o = 0;
  for (uint16_t i = 0; i < g_rxLen && o < outLen - 1; i++) {
    const char c = g_rx[i];
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
    out[o++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  out[o] = '\0';
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Plockar ut databyten ur ett svar pa lage 01. Svaret ar "41" + pid + data,
// eventuellt efter "SEARCHING..." eller en radrubrik fran en annan
// styrenhet. Returnerar antal byte, eller -1 nar bilen inte svarade.
int readPid(uint8_t pid, uint8_t *out, uint8_t maxBytes) {
  char cmd[8];
  snprintf(cmd, sizeof(cmd), "01%02X", (unsigned)pid);
  if (!ask(cmd, 1200)) return -1;

  char s[512];
  tidy(s, sizeof(s));
  if (strstr(s, "NODATA") || strstr(s, "UNABLE") || strstr(s, "STOPPED") ||
      strstr(s, "ERROR") || strstr(s, "?")) {
    return -1;
  }

  char want[6];
  snprintf(want, sizeof(want), "41%02X", (unsigned)pid);
  const char *at = strstr(s, want);
  if (!at) return -1;
  at += 4;

  uint8_t n = 0;
  while (n < maxBytes) {
    const int hi = hexVal(at[0]);
    const int lo = hi < 0 ? -1 : hexVal(at[1]);
    if (hi < 0 || lo < 0) break;
    out[n++] = (uint8_t)((hi << 4) | lo);
    at += 2;
  }
  return n;
}

// Ar pid:en med i bilens egen lista over vad den svarar pa? Bitarna ligger
// med hogsta pid:en forst i varje block om 32.
bool supported(uint8_t pid) {
  if (pid == 0x00) return true;
  if (pid >= 0x01 && pid <= 0x20) return g_sup01 & (1u << (32 - pid));
  if (pid >= 0x21 && pid <= 0x40) return g_sup21 & (1u << (32 - (pid - 0x20)));
  if (pid >= 0x41 && pid <= 0x60) return g_sup41 & (1u << (32 - (pid - 0x40)));
  return false;
}

uint32_t readSupportMask(uint8_t basePid) {
  uint8_t b[8];
  const int n = readPid(basePid, b, 4);
  if (n < 4) return 0;
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | b[3];
}

// ------------------------------------------------------------ uppkoppling --

// Kopplar ned men behaller klientobjektet - biblioteket ager det, och en
// ny uppkoppling gors pa samma objekt. Vid avstangning av tillvalet slapps
// allt i stallet med BLEDevice::deinit.
void dropLink() {
  if (g_client) {
    if (g_client->isConnected()) g_client->disconnect();
    delay(100);
  }
  g_write = nullptr;
  g_notify = nullptr;
  g_sup01 = g_sup21 = g_sup41 = 0;
  lock();
  g_data.has = 0;
  g_data.samples = 0;
  unlock();
}

// Letar upp adaptern. Sparad adress provas forst - da slipper vi skanna varje
// gang bilen startas.
bool findAndConnect() {
  setState(OBD_SEARCHING);

  char saved[20] = "";
  g_prefs.begin("obd", true);
  g_prefs.getString("addr", saved, sizeof(saved));
  g_prefs.end();

  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);

  BLEScanResults *res = scan->start(6, false);
  const int count = res ? res->getCount() : 0;

  int best = -1;
  int bestRssi = -999;
  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice d = res->getDevice(i);
    bool match = false;

    if (saved[0] && d.getAddress().toString() == String(saved)) {
      best = i;
      break;  // den vi kande igen vinner alltid
    }
    for (uint8_t s = 0; s < kServiceCount && !match; s++) {
      if (d.isAdvertisingService(BLEUUID(kServiceUuids[s]))) match = true;
    }
    if (!match && d.haveName() && nameLooksLikeObd(d.getName())) match = true;
    if (match && d.getRSSI() > bestRssi) {
      bestRssi = d.getRSSI();
      best = i;
    }
  }

  if (best < 0) {
    scan->clearResults();
    return false;
  }

  BLEAdvertisedDevice dev = res->getDevice(best);
  const String addr = dev.getAddress().toString();
  const String name = dev.haveName() ? dev.getName() : String("OBD-adapter");
  scan->clearResults();

  setState(OBD_CONNECTING);
  logg::event("obd: kopplar upp mot %s (%s)", name.c_str(), addr.c_str());

  if (!g_client) g_client = BLEDevice::createClient();
  if (!g_client->connect(&dev)) {
    dropLink();
    return false;
  }
  g_client->setMTU(185);  // farre delar per svar an standardens 23 byte

  // Tjansten kan vara vilken som helst av de kanda - och egenskaperna i den
  // valjs pa vad de KAN, inte pa sina nummer.
  BLERemoteService *svc = nullptr;
  for (uint8_t s = 0; s < kServiceCount && !svc; s++) {
    svc = g_client->getService(BLEUUID(kServiceUuids[s]));
  }
  if (!svc) {
    // Okand klon: ta forsta tjansten som har bade avisering och skrivning.
    auto *all = g_client->getServices();
    if (all) {
      for (auto &it : *all) {
        auto *chars = it.second->getCharacteristics();
        bool n = false, w = false;
        if (chars) {
          for (auto &c : *chars) {
            if (c.second->canNotify()) n = true;
            if (c.second->canWrite() || c.second->canWriteNoResponse()) w = true;
          }
        }
        if (n && w) { svc = it.second; break; }
      }
    }
  }
  if (!svc) {
    logg::event("obd: adaptern har ingen tjanst vi kanner igen");
    dropLink();
    return false;
  }

  auto *chars = svc->getCharacteristics();
  if (chars) {
    for (auto &c : *chars) {
      if (!g_notify && c.second->canNotify()) g_notify = c.second;
      if (!g_write && (c.second->canWrite() || c.second->canWriteNoResponse())) {
        g_write = c.second;
      }
    }
  }
  if (!g_write || !g_notify) {
    logg::event("obd: hittade ingen kanal att prata over");
    dropLink();
    return false;
  }

  g_notify->registerForNotify(notifyCb);

  lock();
  strncpy(g_data.adapter, name.c_str(), sizeof(g_data.adapter) - 1);
  g_data.adapter[sizeof(g_data.adapter) - 1] = '\0';
  unlock();

  g_prefs.begin("obd", false);
  g_prefs.putString("addr", addr);
  g_prefs.end();
  return true;
}

// ELM327:an stalls in en gang per uppkoppling: inget eko, inga radrubriker,
// automatiskt protokollval. Sedan fragar vi bilen vad den kan svara pa.
bool handshake() {
  setState(OBD_HANDSHAKE);

  ask("ATZ", 3000);   // nollstallning - svaret ar adapterns egen version
  delay(300);
  if (!ask("ATE0", 1500)) return false;  // eko av, annars kommer fragan tillbaka
  ask("ATL0", 800);   // inga radbrott
  ask("ATS0", 800);   // inga blanksteg
  ask("ATH0", 800);   // inga rubriker
  ask("ATSP0", 800);  // valj protokoll sjalv

  // Forsta riktiga fragan far ta tid: adaptern provar sig fram bland
  // protokollen, och det ar det som tar sekunderna vid forsta start.
  g_sup01 = 0;
  for (uint8_t i = 0; i < 3 && g_sup01 == 0; i++) {
    g_sup01 = readSupportMask(PID_SUPPORTED_01);
  }
  if (g_sup01 == 0) {
    // Adaptern lever men bilen svarar inte - tandningen ar av, eller
    // adaptern sitter lost.
    setState(OBD_NO_CAR);
    return false;
  }

  g_sup21 = supported(PID_SUPPORTED_21) ? readSupportMask(PID_SUPPORTED_21) : 0;
  g_sup41 = supported(PID_SUPPORTED_41) ? readSupportMask(PID_SUPPORTED_41) : 0;

  if (ask("ATDPN", 800)) {
    char s[64];
    tidy(s, sizeof(s));
    lock();
    strncpy(g_data.protocol, s[0] ? s : "?", sizeof(g_data.protocol) - 1);
    g_data.protocol[sizeof(g_data.protocol) - 1] = '\0';
    unlock();
  }

  // Bransletypen avgor hur luftmangden raknas om till liter. Saknas den
  // gissar vi bensin - felet blir tio procent pa forbrukningen, inte pa
  // nagot annat.
  g_diesel = false;
  if (supported(PID_FUELTYPE)) {
    uint8_t b[4];
    if (readPid(PID_FUELTYPE, b, 1) >= 1) g_diesel = (b[0] == 4);
  }

  logg::event("obd: bilen svarar (%s%s)", g_diesel ? "diesel" : "bensin",
              supported(PID_HYBRID) ? ", hybridbatteri" : "");
  setState(OBD_LIVE);
  return true;
}

// ---------------------------------------------------------- avlasningarna --

// Resans summering matas har, en gang per varv, med tiden sedan forra varvet.
void accumulate(uint32_t dtMs) {
  if (dtMs == 0 || dtMs > 10000) return;  // forsta varvet eller ett langt hopp

  lock();
  const ObdData d = g_data;
  unlock();

  const bool running = (d.has & OBD_HAS_RPM) && d.rpm > 300;

  lock();
  g_sum.any = true;
  g_sum.samples++;
  if (d.has & OBD_HAS_RPM) {
    if (d.rpm > g_sum.maxRpm) g_sum.maxRpm = d.rpm;
    // Medelvarvtalet raknas bara nar motorn gar - annars skulle en hybrid
    // som rullar pa el dra ner det till halva sanningen.
    if (running) {
      g_rpmSum += d.rpm;
      g_rpmN++;
      g_sum.avgRpm = (uint16_t)(g_rpmSum / g_rpmN);
    }
  }
  if ((d.has & OBD_HAS_COOLANT) && d.coolantC > g_sum.maxCoolantC) {
    g_sum.maxCoolantC = d.coolantC;
  }
  if ((d.has & OBD_HAS_LOAD) && d.loadPct > g_sum.maxLoadPct) {
    g_sum.maxLoadPct = d.loadPct;
  }
  if (d.has & OBD_HAS_FUEL) {
    if (g_sum.fuelStartPct == 0xFF || g_sum.fuelStartPct == 0) {
      g_sum.fuelStartPct = d.fuelPct;
    }
    g_sum.fuelEndPct = d.fuelPct;
  }
  if (d.has & OBD_HAS_FLOW) {
    g_sum.fuelLiters += d.flowLh * (float)dtMs / 3600000.0f;
  }
  if (running) {
    g_sum.engineOnS += dtMs / 1000;
    if ((d.has & OBD_HAS_SPEED) && d.speedKmh == 0) g_sum.idleS += dtMs / 1000;
  }
  unlock();
}

// Ett varv avlasningar. De snabba varden lases varje varv, de langsamma var
// tionde - kylvattnet andrar sig inte fyra ganger i sekunden, och varje
// fraga kostar tid pa bussen.
bool pollRound(uint8_t &slow) {
  uint8_t b[8];
  int n;
  bool any = false;

  struct Fast {
    uint8_t pid;
    uint32_t flag;
  };
  static const Fast fast[] = {
      {PID_RPM, OBD_HAS_RPM},
      {PID_SPEED, OBD_HAS_SPEED},
      {PID_LOAD, OBD_HAS_LOAD},
      {PID_THROTTLE, OBD_HAS_THROTTLE},
  };

  for (const Fast &f : fast) {
    if (!supported(f.pid)) continue;
    n = readPid(f.pid, b, 4);
    if (n < 1) continue;
    any = true;
    lock();
    switch (f.pid) {
      case PID_RPM:
        if (n >= 2) g_data.rpm = (uint16_t)(((b[0] << 8) | b[1]) / 4);
        break;
      case PID_SPEED: g_data.speedKmh = b[0]; break;
      case PID_LOAD: g_data.loadPct = (uint8_t)(b[0] * 100 / 255); break;
      case PID_THROTTLE: g_data.throttlePct = (uint8_t)(b[0] * 100 / 255); break;
    }
    g_data.has |= f.flag;
    g_data.samples++;
    g_data.lastReplyMs = millis();
    unlock();
  }

  // Flodet: bilens eget matt om det finns, annars raknat ur luftmangden.
  if (supported(PID_FUELRATE)) {
    n = readPid(PID_FUELRATE, b, 4);
    if (n >= 2) {
      any = true;
      lock();
      g_data.flowLh = (float)((b[0] << 8) | b[1]) / 20.0f;
      g_data.has |= OBD_HAS_FLOW;
      unlock();
    }
  } else if (supported(PID_MAF)) {
    n = readPid(PID_MAF, b, 4);
    if (n >= 2) {
      any = true;
      const float mafGs = (float)((b[0] << 8) | b[1]) / 100.0f;
      // Luft per liter bransle gange branslets densitet: bensin 14,7 * 745,
      // diesel 14,5 * 832 gram per liter.
      const float perLiter = g_diesel ? 12064.0f : 10951.5f;
      lock();
      g_data.flowLh = mafGs * 3600.0f / perLiter;
      g_data.has |= OBD_HAS_FLOW;
      unlock();
    }
  }

  struct Slow {
    uint8_t pid;
    uint32_t flag;
  };
  static const Slow slowList[] = {
      {PID_COOLANT, OBD_HAS_COOLANT}, {PID_FUEL, OBD_HAS_FUEL},
      {PID_INTAKE, OBD_HAS_INTAKE},   {PID_AMBIENT, OBD_HAS_AMBIENT},
      {PID_VOLT, OBD_HAS_VOLT},       {PID_OIL, OBD_HAS_OIL},
      {PID_HYBRID, OBD_HAS_HYBRID},   {PID_RUNTIME, OBD_HAS_RUNTIME},
  };
  const uint8_t slowCount = sizeof(slowList) / sizeof(slowList[0]);

  // En langsam per varv, i tur och ordning.
  for (uint8_t tries = 0; tries < slowCount; tries++) {
    const Slow &s = slowList[slow % slowCount];
    slow++;
    if (!supported(s.pid)) continue;
    n = readPid(s.pid, b, 4);
    if (n < 1) break;
    any = true;
    lock();
    switch (s.pid) {
      case PID_COOLANT: g_data.coolantC = (int16_t)b[0] - 40; break;
      case PID_FUEL: g_data.fuelPct = (uint8_t)(b[0] * 100 / 255); break;
      case PID_INTAKE: g_data.intakeC = (int16_t)b[0] - 40; break;
      case PID_AMBIENT: g_data.ambientC = (int16_t)b[0] - 40; break;
      case PID_OIL: g_data.oilC = (int16_t)b[0] - 40; break;
      case PID_HYBRID: g_data.hybridPct = (uint8_t)(b[0] * 100 / 255); break;
      case PID_VOLT:
        if (n >= 2) g_data.voltage = (float)((b[0] << 8) | b[1]) / 1000.0f;
        break;
      case PID_RUNTIME:
        if (n >= 2) g_data.runtimeS = (uint32_t)((b[0] << 8) | b[1]);
        break;
    }
    g_data.has |= s.flag;
    unlock();
    break;
  }

  return any;
}

// ------------------------------------------------------------------ traden -

void obdTask(void *) {
  uint8_t slow = 0;
  uint32_t lastAccMs = 0;
  uint32_t nextTryMs = 0;
  uint8_t fails = 0;

  for (;;) {
    if (!g_enabled) {
      if (g_bleUp) {
        dropLink();
        BLEDevice::deinit(true);
        g_client = nullptr;  // deinit slapper objektet at oss
        g_bleUp = false;
        setState(OBD_OFF);
        logg::event("obd: avslaget, bluetooth nedstangt");
      }
      lastAccMs = 0;
      delay(1000);
      continue;
    }

    if (g_forget) {
      g_forget = false;
      g_prefs.begin("obd", false);
      g_prefs.remove("addr");
      g_prefs.end();
      dropLink();
      logg::event("obd: adaptern glomd, letar igen");
    }

    if (!g_bleUp) {
      BLEDevice::init("Hikaya");
      g_bleUp = true;
      logg::event("obd: bluetooth igang, letar adapter");
    }

    if (!g_client || !g_client->isConnected()) {
      if (millis() < nextTryMs) { delay(500); continue; }
      dropLink();
      if (!findAndConnect() || !handshake()) {
        dropLink();
        // Trappa upp vantan: en bil utan adapter ska inte skanna varje
        // sekund i timmar, men en bil som just fatt tandning pa ska hitta
        // den inom nagon minut.
        fails = fails < 8 ? fails + 1 : 8;
        nextTryMs = millis() + (uint32_t)fails * 15000UL;
        continue;
      }
      fails = 0;
      lastAccMs = millis();
    }

    if (!pollRound(slow)) {
      lock();
      const uint32_t last = g_data.lastReplyMs;
      unlock();
      if (millis() - last > 15000UL) {
        logg::event("obd: bilen tystnade - kopplar ned");
        dropLink();
        setState(OBD_NO_CAR);
        nextTryMs = millis() + 15000UL;
        continue;
      }
    }

    const uint32_t now = millis();
    accumulate(lastAccMs ? now - lastAccMs : 0);
    lastAccMs = now;

    // Under en molnsynk far bussen vila: tls och bluetooth om samma
    // internminne ar en darig kombination, och nagon resa pagar anda inte.
    delay(trip::status().active ? 60 : 500);
  }
}

}  // namespace

namespace obd {

void begin(bool enabled) {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  g_data.state = enabled ? OBD_SEARCHING : OBD_OFF;
  g_sum.fuelStartPct = 0xFF;
  g_enabled = enabled;

  // Traden startas alltid, men utan tillvalet gor den ingenting alls -
  // ingen bluetooth-stack, inget minne, ingen strom.
  xTaskCreatePinnedToCore(obdTask, "obd", 8192, nullptr, 1, nullptr, 0);
}

void setEnabled(bool on) {
  if (g_enabled == on) return;
  g_enabled = on;
  if (on) setState(OBD_SEARCHING);
  logg::event("obd: tillvalet %s", on ? "pa" : "av");
}

bool enabled() { return g_enabled; }

void forget() { g_forget = true; }

ObdData data() {
  lock();
  ObdData d = g_data;
  unlock();
  return d;
}

void noteTripStart() {
  lock();
  memset(&g_sum, 0, sizeof(g_sum));
  g_sum.fuelStartPct = 0xFF;
  g_sum.maxCoolantC = -999;
  g_rpmSum = 0;
  g_rpmN = 0;
  unlock();
}

ObdTripSummary summary() {
  lock();
  ObdTripSummary s = g_sum;
  unlock();
  return s;
}

}  // namespace obd
