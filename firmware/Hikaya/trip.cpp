#include "trip.h"

#include <Preferences.h>
#include "storage.h"
#include <math.h>
#include <unistd.h>

#include "cams.h"
#include "config.h"
#include "eco.h"
#include "geo.h"
#include "gnss.h"
#include "sensors.h"
#include "stats.h"

namespace {

// ------------------------------------------------------- tillstandsfilen ----
//
// Det har ar det som gor att ett stromavbrott inte kostar en resa.
//
// Gpx-filen klarar sig sjalv: avslutningen skrivs efter varje punkt och skrivs
// over av nasta, sa filen pa kortet ar alltid komplett. Men en komplett gpx-fil
// vet ingenting om att den tillhorde en resa som skulle ha ett mal, ett syfte
// och en rad i dagboken. Det ar det tillstandsfilen bar.
//
// Den skrivs om vid varje sparpunkt och innehaller allt som behovs for att
// skriva resan fardigt utan att lasa gpx-filen: var bilen senast var, hur langt
// den kommit, vad resan var till, och - viktigast - om resan nagonsin
// avslutades.
//
// Tva slots, vaxelvis, med lopnummer och kontrollsumma. Varje slot ar 512 byte
// sa att de hamnar i olika sektorer pa kortet: en avbruten skrivning kan da
// aldrig skada bada. Vid start lases bada och den med hogsta lopnummer och
// riktig kontrollsumma galler. Sager den att en resa var oppen, sa forsvann
// stromen mitt i den, och da ar sista kanda position resans mal.

const uint32_t kStateMagic = 0x31565244;  // "DRV1"
const uint16_t kStateVersion = 1;
const size_t kSlotSize = 512;

#pragma pack(push, 1)
struct StateRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t seq;

  uint8_t open;           // 1 = resan pagar och har aldrig avslutats
  uint8_t recordWritten;  // 1 = raden star redan i dagboken
  uint8_t purpose;
  uint8_t endReason;

  uint32_t index;
  uint32_t startUtc;
  double startLat, startLon;
  uint8_t haveStart;
  uint8_t ecoValid;  // 1 = ecopoangen bygger pa riktig matning
  uint8_t pad[2];

  uint32_t lastUtc;
  double lastLat, lastLon;
  double distanceM;
  uint32_t points;
  uint32_t movingS;
  uint32_t speedingS;
  float maxSpeedKmh;
  float ecoScore;
  uint32_t hardEvents;
  char customer[40];

  uint32_t crc;  // over alla byte fore detta falt
};
#pragma pack(pop)

uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}

void stamp(StateRecord &r) {
  r.magic = kStateMagic;
  r.version = kStateVersion;
  r.size = (uint16_t)sizeof(StateRecord);
  r.crc = crc32((const uint8_t *)&r, sizeof(StateRecord) - sizeof(uint32_t));
}

bool valid(const StateRecord &r) {
  if (r.magic != kStateMagic) return false;
  if (r.version != kStateVersion) return false;
  if (r.size != sizeof(StateRecord)) return false;
  const uint32_t want =
      crc32((const uint8_t *)&r, sizeof(StateRecord) - sizeof(uint32_t));
  return want == r.crc;
}

// ---------------------------------------------------------------- gpx ------
// Avslutningen pa gpx-filen. Den skrivs efter varje punkt och skrivs sedan over
// av nasta punkt, sa att filen pa kortet alltid ar komplett och gar att oppna
// aven om stromen tar slut mitt i en resa.
const char *kGpxFooter = "</trkseg></trk></gpx>\n";

// Knepet vilar pa att en sparpunkt alltid ar langre an avslutningen. Den
// kortaste punkt vi skriver ar drygt sextio tecken, avslutningen tjugotva, sa
// nasta punkt tacker alltid over den helt och lamnar ingen svans efter sig.

File g_gpx;
uint32_t g_trkEnd = 0;

SemaphoreHandle_t g_mutex = nullptr;
Preferences g_prefs;

StateRecord g_state = {};
uint8_t g_slot = 0;
uint32_t g_seq = 0;

bool g_active = false;
TripStatus g_status = {};

RecoveredTrip g_recovered = {};

// Kommandon fran skarmtraden. De utfors i avlasningstraden, sa att bara en trad
// nagonsin ror minneskortet.
volatile bool g_cmdStart = false;
volatile bool g_cmdEnd = false;
volatile bool g_cmdSplit = false;
volatile uint8_t g_cmdPurpose = PURPOSE_UNSET;
volatile bool g_cmdCustomer = false;
char g_pendingCustomer[40] = "";

// Nasta resa startar har. Satts nar en resa lakts efter stromavbrott: dar
// stromen forsvann borjar nasta resa.
bool g_havePendingStart = false;
double g_pendingLat = 0, g_pendingLon = 0;

// Loparen for resedetektorn.
uint32_t g_movingMs = 0;   // hur lange den rullat, innan resan startat
double g_detLat = 0, g_detLon = 0;  // dar farten forst sags - for reservvagen
uint32_t g_stoppedMs = 0;  // hur lange den statt stilla, under resan
uint32_t g_lastTickMs = 0;
uint32_t g_lastPointMs = 0;
uint32_t g_movingTotalMs = 0;
uint32_t g_speedingTotalMs = 0;

// Sista punkten dar bilen faktiskt rorde sig. Det ar den som blir resans mal -
// inte dar den stod nar de fyra minuterna gick ut. Annars hade varje resa fatt
// parkeringstiden pahangd och malet hamnat i gps-bruset kring parkeringen.
bool g_haveMoving = false;
double g_movingLat = 0, g_movingLon = 0;
uint32_t g_movingUtc = 0;

void lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

void ensureDirs() {
  if (!SDCARD.exists(DRIVE_DIR)) SDCARD.mkdir(DRIVE_DIR);
  if (!SDCARD.exists(GPX_DIR)) SDCARD.mkdir(GPX_DIR);
  if (!SDCARD.exists(GPX_SYNCED_DIR)) SDCARD.mkdir(GPX_SYNCED_DIR);
}

void gpxPath(uint32_t index, char *out, size_t len) {
  snprintf(out, len, GPX_DIR "/R%04lu.GPX", (unsigned long)index);
}

// ------------------------------------------------------- tillstand pa kort -

// De levande vardena in i tillstandet, sa att det som skrivs till kortet ar
// resan sa har langt - inte nollor som fylls i forst vid avslutet.
//
// Det har raden ar hela skillnaden mellan en dagbok och en gissning i en bil
// med tandningsstyrd strom: dar ar stromavbrottet det normala avslutet, och
// da skrivs raden fran senast sparade tillstand. Innan detta fanns fick varje
// sadan resa rullande tid noll och ecopoang noll - verkliga resor pa 5,7 och
// 101,7 km stod med noll rullande minuter i dagboken.
void refreshLiveStats() {
  if (!g_active) return;
  g_state.movingS = g_movingTotalMs / 1000;
  g_state.speedingS = g_speedingTotalMs / 1000;

  const EcoStatus e = eco::status();
  g_state.ecoScore = e.tripScore;
  g_state.ecoValid = e.measured ? 1 : 0;
  g_state.hardEvents = e.hardAccel + e.hardBrake + e.hardTurn + e.hardTotal;
}

void writeState() {
  if (!sensors::sdMounted()) return;
  ensureDirs();

  g_state.seq = ++g_seq;
  stamp(g_state);

  File f = SDCARD.open(STATE_FILE, "r+");
  if (!f) {
    // Filen finns inte an. Skapa den i full storlek, sa att de tva slotsen
    // ligger i olika sektorer fran forsta stund.
    f = SDCARD.open(STATE_FILE, FILE_WRITE);
    if (!f) return;
    uint8_t zero[kSlotSize] = {0};
    f.write(zero, kSlotSize);
    f.write(zero, kSlotSize);
    f.flush();
  }

  g_slot ^= 1;
  f.seek((uint32_t)g_slot * kSlotSize);
  f.write((const uint8_t *)&g_state, sizeof(g_state));
  f.flush();
  f.close();
}

// Lases vid start. Den slot som har hogsta lopnummer och riktig kontrollsumma
// ar den sanna.
bool readState(StateRecord &out, uint8_t &slotOut) {
  if (!sensors::sdMounted()) return false;
  File f = SDCARD.open(STATE_FILE, FILE_READ);
  if (!f) return false;

  bool found = false;
  for (uint8_t slot = 0; slot < 2; slot++) {
    StateRecord r = {};
    if (!f.seek((uint32_t)slot * kSlotSize)) continue;
    if (f.read((uint8_t *)&r, sizeof(r)) != (int)sizeof(r)) continue;
    if (!valid(r)) continue;
    if (!found || r.seq > out.seq) {
      out = r;
      slotOut = slot;
      found = true;
    }
  }
  f.close();
  return found;
}

// ------------------------------------------------------------- dagboken ----

// Svenska decimaltecken i csv-filen. Excel pa en svensk dator laser "12.3" som
// text men "12,3" som ett tal, och en kolumn man inte kan summera ar en kolumn
// man inte har.
void fmtSv(double v, uint8_t decimals, char *out, size_t len) {
  snprintf(out, len, "%.*f", (int)decimals, v);
  for (size_t i = 0; i < len && out[i]; i++) {
    if (out[i] == '.') out[i] = ',';
  }
}

void appendTripRow(const StateRecord &r) {
  ensureDirs();

  char startLocal[24], endLocal[24];
  sensors::localStamp(r.startUtc, startLocal, sizeof(startLocal));
  sensors::localStamp(r.lastUtc, endLocal, sizeof(endLocal));

  const uint32_t minutes =
      (r.lastUtc > r.startUtc) ? (r.lastUtc - r.startUtc) / 60 : 0;

  char km[16], maxKmh[16], eco[16];
  fmtSv(r.distanceM / 1000.0, 2, km, sizeof(km));
  fmtSv(r.maxSpeedKmh, 0, maxKmh, sizeof(maxKmh));

  // En resa utan matning star som omatt, inte som noll. En nolla ar ett
  // omdome om korningen; en tom cell sager det som ar sant - att ingen
  // matning finns.
  if (r.ecoValid) {
    fmtSv(r.ecoScore, 0, eco, sizeof(eco));
  } else {
    eco[0] = '\0';
  }

  const char *slut = "";
  switch (r.endReason) {
    case END_AUTO: slut = "automatiskt"; break;
    case END_MANUAL: slut = "knapp"; break;
    case END_POWERLOSS: slut = "strom av"; break;
    case END_NO_SPACE: slut = "kortet fullt"; break;
    default: slut = "okant"; break;
  }

  char gpx[48];
  gpxPath(r.index, gpx, sizeof(gpx));

  // ---- csv, for manniskor och Excel. Semikolon som avdelare, eftersom
  // decimaltecknet ar ett komma.
  const bool fresh = !SDCARD.exists(TRIPS_CSV);
  File c = SDCARD.open(TRIPS_CSV, FILE_APPEND);
  if (c) {
    if (fresh) {
      c.print(
          "resa;start;mal;minuter;km;syfte;kund;start_lat;start_lon;mal_lat;"
          "mal_lon;maxfart_kmh;fortkorning_min;ecopoang;harda_moment;avslut;"
          "gpx;karta\n");
    }
    c.printf(
        "%lu;%s;%s;%lu;%s;%s;%s;%.6f;%.6f;%.6f;%.6f;%s;%lu;%s;%lu;%s;%s;"
        "https://www.google.com/maps/dir/?api=1&origin=%.6f,%.6f&destination=%."
        "6f,%.6f\n",
        (unsigned long)r.index, startLocal, endLocal, (unsigned long)minutes, km,
        trip::purposeName((TripPurpose)r.purpose), r.customer, r.startLat,
        r.startLon, r.lastLat, r.lastLon, maxKmh,
        (unsigned long)(r.speedingS / 60), eco, (unsigned long)r.hardEvents,
        slut, gpx, r.startLat, r.startLon, r.lastLat, r.lastLon);
    c.flush();
    c.close();
  }

  // ---- jsonl, for synken. En rad per resa, radslutet skrivs sist, sa att en
  // halvskriven rad efter ett stromavbrott gar att kanna igen och slanga.
  char startIso[28], endIso[28];
  sensors::isoUtc(r.startUtc, startIso, sizeof(startIso));
  sensors::isoUtc(r.lastUtc, endIso, sizeof(endIso));

  // Omatt ecopoang skrivs som null, sa att molnet och webbappen kan skilja
  // "korde perfekt" fran "ingen matning" - tva pastaenden som inte har samma
  // siffra.
  char ecoJson[16];
  if (r.ecoValid) {
    snprintf(ecoJson, sizeof(ecoJson), "%.0f", r.ecoScore);
  } else {
    snprintf(ecoJson, sizeof(ecoJson), "null");
  }

  File j = SDCARD.open(TRIPS_JSONL, FILE_APPEND);
  if (j) {
    j.printf(
        "{\"resa\":%lu,\"start\":\"%s\",\"mal\":\"%s\",\"start_lat\":%.7f,"
        "\"start_lon\":%.7f,\"mal_lat\":%.7f,\"mal_lon\":%.7f,\"meter\":%.1f,"
        "\"punkter\":%lu,\"syfte\":\"%s\",\"kund\":\"%s\",\"maxfart_kmh\":%.1f,"
        "\"fortkorning_s\":%lu,\"rullande_s\":%lu,\"ecopoang\":%s,"
        "\"harda_moment\":%lu,\"avslut\":\"%s\",\"gpx\":\"R%04lu.GPX\"}\n",
        (unsigned long)r.index, startIso, endIso, r.startLat, r.startLon,
        r.lastLat, r.lastLon, r.distanceM, (unsigned long)r.points,
        trip::purposeSlug((TripPurpose)r.purpose), r.customer, r.maxSpeedKmh,
        (unsigned long)r.speedingS, (unsigned long)r.movingS, ecoJson,
        (unsigned long)r.hardEvents, slut, (unsigned long)r.index);
    j.flush();
    j.close();
  }

  // Statistiksidan haller sina summor i minnet och far resan har, sa att den
  // slipper lasa om dagboken.
  stats::noteTrip(r.distanceM / 1000.0, r.movingS, r.points, r.purpose,
                  r.speedingS, r.maxSpeedKmh);
}

// ------------------------------------------------------------------ gpx ----

// Skriver avslutningen och backar filpekaren dit den stod, sa att nasta punkt
// hamnar pa ratt plats.
void sealGpx() {
  if (!g_gpx) return;
  g_trkEnd = g_gpx.position();
  g_gpx.write((const uint8_t *)kGpxFooter, strlen(kGpxFooter));
  g_gpx.flush();
  g_gpx.seek(g_trkEnd);
}

bool openGpx(uint32_t index, TripPurpose purpose, const char *customer) {
  ensureDirs();

  char path[48];
  gpxPath(index, path, sizeof(path));
  g_gpx = SDCARD.open(path, FILE_WRITE);
  if (!g_gpx) return false;

  char when[28];
  sensors::isoUtc(sensors::unixUtc(), when, sizeof(when));

  // Syftet star bade i namnet och som eget falt. Namnet ar det man ser i en
  // kartapp; faltet ar det som gar att lasa maskinellt utan att gissa.
  g_gpx.printf(
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<gpx version=\"1.1\" creator=\"Hikaya\" "
      "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
      "<metadata><name>Resa %lu</name>%s%s%s</metadata>\n"
      "<trk><name>Resa %lu - %s</name><type>%s</type>%s%s%s<trkseg>\n",
      (unsigned long)index, when[0] ? "<time>" : "", when, when[0] ? "</time>" : "",
      (unsigned long)index, trip::purposeName(purpose),
      trip::purposeSlug(purpose), (customer && customer[0]) ? "<desc>" : "",
      (customer && customer[0]) ? customer : "",
      (customer && customer[0]) ? "</desc>" : "");

  strncpy(g_status.fileName, path, sizeof(g_status.fileName) - 1);
  g_status.fileName[sizeof(g_status.fileName) - 1] = '\0';

  sealGpx();
  return true;
}

void writePoint(const GnssFix &f) {
  if (!g_gpx) return;

  char when[28];
  sensors::isoUtc(sensors::unixUtc(), when, sizeof(when));

  char pt[192];
  int n = snprintf(pt, sizeof(pt), "<trkpt lat=\"%.7f\" lon=\"%.7f\">", f.lat,
                   f.lon);
  if (f.fixType >= 3) {
    n += snprintf(pt + n, sizeof(pt) - n, "<ele>%.1f</ele>", f.altM);
  }
  if (when[0]) {
    n += snprintf(pt + n, sizeof(pt) - n, "<time>%s</time>", when);
  }
  n += snprintf(pt + n, sizeof(pt) - n, "<sat>%u</sat></trkpt>\n",
                (unsigned)f.sats);
  g_gpx.write((const uint8_t *)pt, n);

  // Avslutningen skrivs om efter varje punkt, inte var trettionde sekund.
  // Kostnaden ar en tomning per punkt; vinsten ar att ett stromavbrott hogst
  // kostar den senaste punkten i stallet for en halv minut av resan.
  sealGpx();
}

// ------------------------------------------------------- resans livscykel --

uint32_t nextIndex() {
  g_prefs.begin("driveidx", false);
  uint32_t index = g_prefs.getUInt("tripIdx", 0) + 1;

  // Raknaren bor i flashminnet och overlever bade stromavbrott och
  // omflashning. Men byter man minneskort kan numret redan vara upptaget, sa
  // filen far avgora i sista ledet.
  char path[48];
  while (index < 100000) {
    gpxPath(index, path, sizeof(path));
    if (!SDCARD.exists(path)) break;
    index++;
  }
  g_prefs.putUInt("tripIdx", index);
  g_prefs.end();
  return index;
}

void publishStatus() {
  lock();
  g_status.active = g_active;
  g_status.index = g_state.index;
  g_status.purpose = (TripPurpose)g_state.purpose;
  strncpy(g_status.customer, g_state.customer, sizeof(g_status.customer) - 1);
  g_status.customer[sizeof(g_status.customer) - 1] = '\0';
  g_status.startUtc = g_state.startUtc;
  g_status.startLat = g_state.startLat;
  g_status.startLon = g_state.startLon;
  g_status.lat = g_state.lastLat;
  g_status.lon = g_state.lastLon;
  g_status.distanceM = g_state.distanceM;
  g_status.points = g_state.points;
  g_status.maxSpeedKmh = g_state.maxSpeedKmh;
  g_status.speedingS = g_speedingTotalMs / 1000;
  g_status.movingS = g_movingTotalMs / 1000;
  g_status.stoppedS = g_stoppedMs / 1000;
  unlock();
}

void startTrip(double lat, double lon, bool haveFix) {
  // En avslutad resa som annu inte fatt sin rad far den nu, innan tillstandet
  // skrivs over.
  //
  // Utan det steget forsvinner raden i tva verkliga fall: att man kor vidare
  // inom minuten utan att ha svarat pa fragan om syftet, och att man trycker
  // DELA HAR. I bada skulle den nya resan skriva over tillstandet, och da finns
  // det ingenting kvar som vet att en rad fattades. Att en resa kan forsvinna
  // for att man korde vidare for fort ar precis det tillstandsfilen ar till for
  // att hindra.
  if (g_state.index != 0 && !g_state.open && !g_state.recordWritten) {
    StateRecord prev = g_state;
    if (prev.purpose == PURPOSE_UNSET) prev.purpose = PURPOSE_DIFFUST;
    appendTripRow(prev);

    lock();
    g_status.awaitingPurpose = false;
    unlock();
  }

  memset(&g_state, 0, sizeof(g_state));
  g_state.index = nextIndex();
  g_state.open = 1;
  g_state.recordWritten = 0;
  g_state.purpose = PURPOSE_UNSET;
  g_state.endReason = END_NONE;
  g_state.startUtc = sensors::unixUtc();
  g_state.lastUtc = g_state.startUtc;

  if (haveFix) {
    g_state.haveStart = 1;
    g_state.startLat = lat;
    g_state.startLon = lon;
    g_state.lastLat = lat;
    g_state.lastLon = lon;
  }

  if (!openGpx(g_state.index, PURPOSE_UNSET, "")) {
    // Utan gpx-fil finns ingen resa att logga. Hellre saga det an att lata
    // skarmen visa en resa som inte hamnar nagonstans.
    g_state.open = 0;
    return;
  }

  g_movingMs = 0;
  g_stoppedMs = 0;
  g_lastPointMs = 0;
  g_movingTotalMs = 0;
  g_speedingTotalMs = 0;
  g_haveMoving = haveFix;
  g_movingLat = lat;
  g_movingLon = lon;
  g_movingUtc = g_state.startUtc;

  // Ecodrive borjar om vid varje resa, sa att poangen sager nagot om den har
  // korningen och inte om alla korningar sedan kortet flashades.
  eco::reset();

  g_active = true;
  writeState();
  publishStatus();
}

// Skriver resan fardigt. Malet ar sista punkten dar bilen rorde sig - inte dar
// den stod nar tiden gick ut.
void closeTrip(TripEndReason reason) {
  if (g_gpx) {
    // Avslutningen skrivs en sista gang, och den har gangen backar vi inte.
    g_gpx.write((const uint8_t *)kGpxFooter, strlen(kGpxFooter));
    g_gpx.flush();
    g_gpx.close();
  }

  if (g_haveMoving) {
    g_state.lastLat = g_movingLat;
    g_state.lastLon = g_movingLon;
    if (g_movingUtc) g_state.lastUtc = g_movingUtc;
  }

  refreshLiveStats();
  g_state.endReason = reason;
  g_state.open = 0;
  g_active = false;

  // Raden skrivs forst nar syftet ar satt. Ar det inte satt an vantar vi pa
  // fragan pa skarmen - och skulle stromen ga innan nagon svarat star det i
  // tillstandsfilen att raden fattas, sa nasta start skriver den.
  if (g_state.purpose == PURPOSE_UNSET) {
    g_state.recordWritten = 0;
    writeState();

    lock();
    g_status.awaitingPurpose = true;
    g_status.awaitingIndex = g_state.index;
    g_status.awaitingKm = g_state.distanceM / 1000.0;
    unlock();
  } else {
    appendTripRow(g_state);
    g_state.recordWritten = 1;
    writeState();
  }

  publishStatus();
}

// ------------------------------------------------------------ laga gpx ----
//
// Avslutningen skrivs efter varje punkt, sa filen pa kortet ar normalt komplett.
// Men punkten skrivs genom filsystemets buffert, och gar den bufferten over en
// sektorsgrans mitt i en punkt hamnar en halv punkt pa kortet dar avslutningen
// stod. Da ar filen inte langre giltig xml, och en kartapp vagrar oppna den -
// vilket ar precis det den har konstruktionen skulle undvika.
//
// Fonstret ar smalt, men det finns, sa vi tar inte i det med tro. Eftersom en
// avbruten resa upptacks vid start anda gar filen att laga dar: klipp efter den
// sista hela punkten och skriv avslutningen igen. Da ar filen alltid lasbar,
// inte nastan alltid.
void repairGpx(uint32_t index) {
  if (!sensors::sdMounted()) return;

  char path[48];
  gpxPath(index, path, sizeof(path));

  File f = SDCARD.open(path, FILE_READ);
  if (!f) return;
  const size_t size = f.size();
  if (size == 0) {
    f.close();
    return;
  }

  // En punkt ar under tvahundra byte och avslutningen tjugotva, sa den sista
  // hela punkten ligger garanterat i de sista femhundra.
  const size_t kTail = 512;
  const size_t from = (size > kTail) ? size - kTail : 0;
  uint8_t buf[kTail + 1];
  f.seek(from);
  const size_t got = f.read(buf, size - from);
  f.close();
  if (got == 0) return;
  buf[got] = '\0';

  // Sista stallet dar en punkt - eller, om inga punkter hanns med, sjalva
  // sparets borjan - tar slut.
  const char *marks[2] = {"</trkpt>", "<trkseg>"};
  size_t cut = 0;
  for (uint8_t m = 0; m < 2 && cut == 0; m++) {
    const size_t len = strlen(marks[m]);
    const char *hit = nullptr;
    for (const char *p = (const char *)buf; (p = strstr(p, marks[m])) != nullptr;
         p++) {
      hit = p;
    }
    if (hit) cut = from + (size_t)(hit - (const char *)buf) + len;
  }
  if (cut == 0) return;  // filen sager inget vi kan lita pa - lat den vara

  // Radslutet efter markeringen hor till raden. Utan det steget skulle klippet
  // hamna precis fore radbrytningen, och da vore filen aldrig "redan hel" - varje
  // lagning hade skrivit om svansen i onodan och tagit radbrytningen med sig.
  while (cut - from < got && (buf[cut - from] == '\r' || buf[cut - from] == '\n')) {
    cut++;
  }

  const size_t footerLen = strlen(kGpxFooter);

  // Ar allt efter klippet redan exakt avslutningen ar filen hel. Det ar det
  // vanliga fallet, och da ska ingenting skrivas.
  if (size == cut + footerLen &&
      memcmp(buf + (cut - from), kGpxFooter, footerLen) == 0) {
    return;
  }

  // SD_MMC monteras pa /sdcard, sa det ar den vagen filsystemet kanner igen.
  char full[64];
  snprintf(full, sizeof(full), "/sdcard%s", path);
  if (truncate(full, (off_t)cut) != 0) return;

  File w = SDCARD.open(path, FILE_APPEND);
  if (!w) return;
  w.write((const uint8_t *)kGpxFooter, footerLen);
  w.flush();
  w.close();
}

// En resa som stromen tog. I en bil med tandningsstyrd strom ar det har inte
// ett haveri utan det normala sattet en resa slutar pa: man parkerar, vrider av
// tandningen, och strommen forsvinner innan nagon fraga hunnit stallas.
//
// Sista kanda position blir malet, och nasta resa borjar dar. Ar syftet inte
// satt stalls fragan vid nasta start i stallet for att gissas till diffust -
// att tvangsmarka varje resa som ingen hann tagga vore att gora det normala
// avslutet till ett samre avslut.
void healInterrupted(const StateRecord &r) {
  repairGpx(r.index);

  StateRecord fixed = r;
  fixed.endReason = END_POWERLOSS;
  fixed.open = 0;

  g_recovered.valid = true;
  g_recovered.index = fixed.index;
  g_recovered.endUtc = fixed.lastUtc;
  g_recovered.lat = fixed.lastLat;
  g_recovered.lon = fixed.lastLon;
  g_recovered.distanceM = fixed.distanceM;

  // Dar stromen forsvann borjar nasta resa. Bilen har inte flyttat sig av sig
  // sjalv medan den var stromlos.
  if (fixed.lastLat != 0.0 || fixed.lastLon != 0.0) {
    g_havePendingStart = true;
    g_pendingLat = fixed.lastLat;
    g_pendingLon = fixed.lastLon;
  }

  if (fixed.purpose == PURPOSE_UNSET) {
    // Raden vantar pa svaret. Samma maskineri som efter ett vanligt avslut tar
    // hand om resten: ett svar skriver raden, tystnad i en minut skriver den
    // som diffust, och kor man ivag direkt skrivs den nar nasta resa borjar.
    fixed.recordWritten = 0;
    g_state = fixed;
    writeState();

    lock();
    g_status.awaitingPurpose = true;
    g_status.awaitingIndex = fixed.index;
    g_status.awaitingKm = fixed.distanceM / 1000.0;
    unlock();
  } else {
    // Syftet taggades under resan - da finns inget att fraga om, och skarmen
    // behover inte saga nagonting alls vid start.
    appendTripRow(fixed);
    fixed.recordWritten = 1;
    g_state = fixed;
    writeState();
  }
}

}  // namespace

namespace trip {

const char *purposeName(TripPurpose p) {
  switch (p) {
    case PURPOSE_PRIVAT: return "Privat";
    case PURPOSE_FORETAG: return "Foretag";
    case PURPOSE_DIFFUST: return "Diffust";
    default: return "Omarkt";
  }
}

const char *purposeSlug(TripPurpose p) {
  switch (p) {
    case PURPOSE_PRIVAT: return "privat";
    case PURPOSE_FORETAG: return "foretag";
    case PURPOSE_DIFFUST: return "diffust";
    default: return "omarkt";
  }
}

void begin() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
  memset(&g_status, 0, sizeof(g_status));
  g_lastTickMs = millis();

  StateRecord r = {};
  uint8_t slot = 0;
  if (!readState(r, slot)) return;

  g_seq = r.seq;
  g_slot = slot;

  if (r.open) {
    // Resan avslutades aldrig. Stromen forsvann mitt i den - vilket i en bil
    // med tandningsstyrd strom ar det vanliga slutet pa en resa.
    healInterrupted(r);
  } else if (!r.recordWritten) {
    // Resan avslutades snyggt, men fragan om syftet hann inte besvaras innan
    // strommen forsvann. Fragan ar inte forverkad for det - den stalls nu, vid
    // start, nar foraren anda sitter framfor skarmen.
    g_state = r;
    lock();
    g_status.awaitingPurpose = true;
    g_status.awaitingIndex = r.index;
    g_status.awaitingKm = r.distanceM / 1000.0;
    unlock();
  } else {
    g_state = r;
  }
}

void tick() {
  const uint32_t now = millis();
  const uint32_t dt = now - g_lastTickMs;
  g_lastTickMs = now;
  if (dt > 5000) return;  // klockan hoppade, hoppa over varvet

  const GnssFix f = gnss::fix();

  // ---- kommandon fran skarmen, utforda har sa att bara en trad ror kortet
  if (g_cmdPurpose != PURPOSE_UNSET) {
    const TripPurpose p = (TripPurpose)g_cmdPurpose;
    g_cmdPurpose = PURPOSE_UNSET;
    g_state.purpose = p;

    if (!g_active && !g_state.recordWritten) {
      // Svaret pa fragan efter resan. Nu gar raden att skriva.
      appendTripRow(g_state);
      g_state.recordWritten = 1;
      lock();
      g_status.awaitingPurpose = false;
      unlock();
    }
    writeState();
    publishStatus();
  }

  if (g_cmdCustomer) {
    g_cmdCustomer = false;
    strncpy(g_state.customer, g_pendingCustomer, sizeof(g_state.customer) - 1);
    g_state.customer[sizeof(g_state.customer) - 1] = '\0';
    // Att valja kund ar att saga att resan ar en foretagsresa.
    if (g_state.customer[0]) g_state.purpose = PURPOSE_FORETAG;

    if (!g_active && !g_state.recordWritten) {
      appendTripRow(g_state);
      g_state.recordWritten = 1;
      lock();
      g_status.awaitingPurpose = false;
      unlock();
    }
    writeState();
    publishStatus();
  }

  if (g_cmdEnd) {
    g_cmdEnd = false;
    if (g_active) closeTrip(END_MANUAL);
  }

  if (g_cmdSplit) {
    g_cmdSplit = false;
    if (g_active) {
      const double lat = g_state.lastLat;
      const double lon = g_state.lastLon;
      const bool had = g_state.haveStart != 0;
      closeTrip(END_MANUAL);
      startTrip(lat, lon, had);
    }
  }

  if (g_cmdStart) {
    g_cmdStart = false;
    if (!g_active) {
      if (f.valid) {
        startTrip(f.lat, f.lon, true);
      } else if (g_havePendingStart) {
        startTrip(g_pendingLat, g_pendingLon, true);
      } else {
        startTrip(0, 0, false);
      }
    }
  }

  lock();
  g_status.speedKmh = f.valid ? f.speedKmh : 0.0f;
  g_status.waitingForFix = g_active && !f.valid;
  if (g_active && g_state.startUtc) {
    const uint32_t nowUtc = sensors::unixUtc();
    g_status.elapsedS = (nowUtc > g_state.startUtc) ? nowUtc - g_state.startUtc : 0;
  }
  unlock();

  // ---- resedetektorn
  // Betrodd fart ar forstahandsvagen: da racker nagra sekunder over
  // startfarten. Men betrodd fart kraver att mottagaren rapporterar en
  // fartosakerhet och 3d-fix - gor den inte det ska journalen anda inte
  // krava en knapptryckning. Reservvagen staller darfor hogre krav: godkand
  // fart dubbelt sa lange, och en verklig forflyttning fran platsen dar
  // farten forst sags. En parkerad bil med brusig mottagning star kvar pa
  // sin plats; en bil som kort ivag gor det inte. Det ar forflyttningen,
  // inte fartsiffran, som inte gar att fejka fran en garageuppfart.
  if (!g_active) {
    const bool fartOk = f.valid && f.speedKmh >= TRIP_START_KMH &&
                        f.speedKmh <= SPEED_TRUST_MAX_KMH;
    if (fartOk) {
      if (g_movingMs == 0) {
        g_detLat = f.lat;
        g_detLon = f.lon;
      }
      g_movingMs += dt;
      const double awayM = geo::distanceM(g_detLat, g_detLon, f.lat, f.lon);
      const bool go = f.speedTrusted
          ? g_movingMs >= (uint32_t)TRIP_START_S * 1000
          : (g_movingMs >= (uint32_t)TRIP_START_S * 2000 && awayM >= 40.0);
      if (go) {
        // Har borjar resan. Ligger en start kvar fran ett stromavbrott anvands
        // den, sa att resan borjar dar bilen faktiskt stod och inte dar den
        // rakade vara nar mottagaren vaknade.
        if (g_havePendingStart) {
          g_havePendingStart = false;
          startTrip(g_pendingLat, g_pendingLon, true);
        } else {
          startTrip(f.lat, f.lon, true);
        }
      }
    } else if (g_movingMs > 0) {
      g_movingMs = (g_movingMs > dt) ? g_movingMs - dt : 0;
    }
    return;
  }

  // ---- pagaende resa
  if (!f.valid) return;

  // Startpunkten kan ha fattats nar resan startade manuellt utan fix. Forsta
  // riktiga positionen far bli start.
  if (!g_state.haveStart) {
    g_state.haveStart = 1;
    g_state.startLat = f.lat;
    g_state.startLon = f.lon;
    g_state.lastLat = f.lat;
    g_state.lastLon = f.lon;
    if (!g_state.startUtc) {
      g_state.startUtc = sensors::unixUtc();
      g_state.lastUtc = g_state.startUtc;
    }
  }

  // All fartstatistik kraver betrodd fart. En enda opalitlig punkt racker
  // annars for att satta en maxfart ingen bil kan kora - dagboken har haft
  // 362 388 km/h fran precis det. Positionen kan fortfarande duga till
  // sparet; det ar bara siffrorna om fart som star over.
  if (f.speedTrusted) {
    if (f.speedKmh > g_state.maxSpeedKmh) g_state.maxSpeedKmh = f.speedKmh;

    // Overhastighet raknas mot den skyltade hastigheten dar vi ar, nar den ar
    // kand. Ar den okand raknas ingen overhastighet - hellre en lucka i
    // statiken an en siffra som bygger pa en gissning.
    const uint8_t limit = cams::currentLimitKmh();
    if (limit > 0 && f.speedKmh > (float)limit + LIMIT_TOLERANCE_KMH) {
      g_speedingTotalMs += dt;
    }
  }

  // Utan betrodd fart star bade rullande tid och stillestandstiden stilla:
  // hellre en dagbok som saknar nagra sekunder an en som hittar pa dem.
  const bool moving = f.speedTrusted && f.speedKmh >= TRIP_STOP_KMH;
  if (moving) {
    g_stoppedMs = 0;
    g_movingTotalMs += dt;
  } else if (f.speedTrusted) {
    g_stoppedMs += dt;
    if (g_stoppedMs >= (uint32_t)TRIP_STOP_S * 1000) {
      closeTrip(END_AUTO);
      return;
    }
  }

  // ---- sparpunkter
  const bool tooSoon =
      g_lastPointMs != 0 && (now - g_lastPointMs) < (uint32_t)TRACK_MIN_INTERVAL_S * 1000;
  if (tooSoon) {
    publishStatus();
    return;
  }

  double stepM = 0;
  if (g_state.points > 0) {
    stepM = geo::distanceM(g_state.lastLat, g_state.lastLon, f.lat, f.lon);
    const bool movedEnough = stepM >= TRACK_MIN_MOVE_M;
    const bool waitedLongEnough =
        (now - g_lastPointMs) >= (uint32_t)TRACK_MAX_INTERVAL_S * 1000;
    if (!movedEnough && !waitedLongEnough) {
      publishStatus();
      return;
    }
    if (movedEnough) g_state.distanceM += stepM;
  }

  writePoint(f);

  g_state.lastLat = f.lat;
  g_state.lastLon = f.lon;
  g_state.lastUtc = sensors::unixUtc();
  g_state.points++;
  g_lastPointMs = now;

  if (moving) {
    g_haveMoving = true;
    g_movingLat = f.lat;
    g_movingLon = f.lon;
    g_movingUtc = g_state.lastUtc;
  }

  // Kortet far inte skrivas fullt. Slut pa plats avslutar resan snyggt i
  // stallet for att lamna en halv fil efter sig.
  if (sensors::freeBytes() > 0 && sensors::freeBytes() < 2UL * 1024UL * 1024UL) {
    closeTrip(END_NO_SPACE);
    return;
  }

  refreshLiveStats();
  writeState();
  publishStatus();
}

TripStatus status() {
  lock();
  TripStatus s = g_status;
  unlock();
  return s;
}

bool startManual() {
  if (g_active) return false;
  g_cmdStart = true;
  for (int i = 0; i < 200 && !g_active; i++) delay(10);
  return g_active;
}

void endManual() {
  if (!g_active) return;
  g_cmdEnd = true;
  for (int i = 0; i < 200 && g_active; i++) delay(10);
}

void splitHere() {
  if (!g_active) return;
  g_cmdSplit = true;
}

void setPurpose(TripPurpose p) {
  if (p == PURPOSE_UNSET) return;
  g_cmdPurpose = (uint8_t)p;
}

void setCustomer(const char *name) {
  strncpy(g_pendingCustomer, name ? name : "", sizeof(g_pendingCustomer) - 1);
  g_pendingCustomer[sizeof(g_pendingCustomer) - 1] = '\0';
  g_cmdCustomer = true;
}

void startNextFrom(double lat, double lon) {
  g_havePendingStart = true;
  g_pendingLat = lat;
  g_pendingLon = lon;
}

RecoveredTrip recovered() { return g_recovered; }

void clearRecovered() { g_recovered.valid = false; }

}  // namespace trip
