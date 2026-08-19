#include "cams.h"

#include <SD_MMC.h>
#include <math.h>

#include "config.h"
#include "geo.h"
#include "gnss.h"
#include "sensors.h"
#include "sound.h"

namespace {

// ---------------------------------------------------------------- format ---
// Bada filerna ar sorterade pa latitud i vaxande ordning. Det ar det som gor
// att en position gar att slå upp med binarsokning i stallet for att jamforas
// mot varje rad: en bil pa vag 73 behover inte fraga om kameror i Kiruna.

const uint32_t kCamMagic = 0x31434C44;    // "DLC1"
const uint32_t kLimitMagic = 0x31484C44;  // "DLH1"

#pragma pack(push, 1)
struct FileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
  uint32_t count;
};

struct CamRecord {
  int32_t lat;       // grader * 1e7
  int32_t lon;
  uint16_t bearing;  // 0-359 grader, 0xFFFF = riktningen okand
  uint8_t limitKmh;  // 0 = okand
  uint8_t flags;     // bit 0 = ATK-stracka, dar snittfarten mats
};

struct LimitRecord {
  int32_t lat;
  int32_t lon;
  uint8_t limitKmh;
  uint8_t flags;
};
#pragma pack(pop)

const uint16_t kBearingUnknown = 0xFFFF;
const uint8_t kFlagAverageSpeed = 0x01;

CamRecord *g_cams = nullptr;
uint32_t g_camCount = 0;

// Hastighetspunkterna ar for manga for minnet, sa filen halls oppen och lases
// med sokningar. En oppen fil kostar ingenting nar den inte lases.
File g_limitFile;
uint32_t g_limitCount = 0;
uint32_t g_limitDataStart = 0;

uint8_t g_currentLimit = 0;
CamWarning g_warning = {};

SemaphoreHandle_t g_mutex = nullptr;

uint32_t g_lastScanMs = 0;

// Bestalld inlasning. Se reload() langst ned.
volatile bool g_wantReload = false;

// Handslaget for filbyte. Se beginUpdate() langst ned.
volatile bool g_suspend = false;
volatile bool g_suspended = false;

// Vilken kamera vi varnar for och vilken ring som redan ljudit. Utan det skulle
// varje avlasning ge ett nytt pip hela vagen fram till kameran.
int32_t g_warnedCam = -1;
uint8_t g_warnedRing = 0;  // 0 = ingen, 1 = langt, 2 = mitten, 3 = nara

// Overhastighet. Den maste hålla i sig en stund och far sedan vila, sa att en
// omkorning inte ger ett pip och en jamn overhastighet inte ger tvahundra.
uint32_t g_overSinceMs = 0;
uint32_t g_lastOverWarnMs = 0;

void lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

double toDeg(int32_t v) { return (double)v / 10000000.0; }

void freeCams() {
  if (g_cams) {
    free(g_cams);
    g_cams = nullptr;
  }
  g_camCount = 0;
}

bool readHeader(File &f, uint32_t magic, uint16_t recordSize, uint32_t &count) {
  FileHeader h = {};
  if (f.read((uint8_t *)&h, sizeof(h)) != (int)sizeof(h)) return false;
  if (h.magic != magic) return false;
  if (h.version != 1) return false;
  if (h.recordSize != recordSize) return false;
  count = h.count;
  return true;
}

void loadCams() {
  freeCams();
  if (!sensors::sdMounted()) return;

  File f = SD_MMC.open(CAMS_FILE, FILE_READ);
  if (!f) return;

  uint32_t count = 0;
  if (!readHeader(f, kCamMagic, sizeof(CamRecord), count) || count == 0 ||
      count > 200000) {
    f.close();
    return;
  }

  // Kamerorna far bo i minnet: hela Sverige ar knappt trettiotusen byte, och de
  // genomsoks en gang i sekunden hela resan.
  CamRecord *buf = (CamRecord *)malloc((size_t)count * sizeof(CamRecord));
  if (!buf) {
    f.close();
    return;
  }

  const size_t want = (size_t)count * sizeof(CamRecord);
  const size_t got = f.read((uint8_t *)buf, want);
  f.close();

  if (got != want) {
    free(buf);
    return;
  }

  g_cams = buf;
  g_camCount = count;
}

void loadLimits() {
  if (g_limitFile) g_limitFile.close();
  g_limitCount = 0;
  if (!sensors::sdMounted()) return;

  g_limitFile = SD_MMC.open(LIMITS_FILE, FILE_READ);
  if (!g_limitFile) return;

  uint32_t count = 0;
  if (!readHeader(g_limitFile, kLimitMagic, sizeof(LimitRecord), count) ||
      count == 0) {
    g_limitFile.close();
    return;
  }
  g_limitCount = count;
  g_limitDataStart = sizeof(FileHeader);
}

// Forsta kameran med latitud minst sa hog. Binarsokning i en sorterad lista.
uint32_t firstCamAtLeast(int32_t lat) {
  uint32_t lo = 0, hi = g_camCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (g_cams[mid].lat < lat) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

// Samma sak i filen med hastighetspunkter. Har kostar varje jamforelse en
// sokning och en lasning, sa antalet steg ar det som avgor tiden - och de ar
// logaritmiskt fa.
uint32_t firstLimitAtLeast(int32_t lat) {
  uint32_t lo = 0, hi = g_limitCount;
  LimitRecord r;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (!g_limitFile.seek(g_limitDataStart + mid * sizeof(LimitRecord))) break;
    if (g_limitFile.read((uint8_t *)&r, sizeof(r)) != (int)sizeof(r)) break;
    if (r.lat < lat) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

// Skyltad hastighet dar bilen ar. Punkterna ligger tatt langs vagarna, sa den
// narmaste inom nagra tiotal meter ar den som galler. Hittas ingen sadan kor vi
// pa en vag som inte finns i filen, och da svarar vi noll - inte en gissning.
uint8_t lookupLimit(double lat, double lon) {
  if (!g_limitFile || g_limitCount == 0) return 0;

  // Sextio meter i latitud. Longituden kan vara vidare pa svenska breddgrader,
  // men avstandet raknas riktigt for varje kandidat, sa fonstret behover bara
  // vara garanterat tillrackligt stort.
  const int32_t delta = (int32_t)(((double)LIMIT_MATCH_RADIUS_M / 111320.0) * 1e7);
  const int32_t target = (int32_t)llround(lat * 1e7);

  uint32_t i = firstLimitAtLeast(target - delta);
  if (i >= g_limitCount) return 0;

  if (!g_limitFile.seek(g_limitDataStart + i * sizeof(LimitRecord))) return 0;

  // Laser i klump. Fonstret ar ett smalt band tvars over landet, sa det ar
  // nagra hundra punkter - en enda lasning, inte en per punkt.
  const uint32_t kBatch = 256;
  LimitRecord batch[kBatch];

  double bestM = (double)LIMIT_MATCH_RADIUS_M;
  uint8_t best = 0;

  while (i < g_limitCount) {
    const uint32_t n = (g_limitCount - i < kBatch) ? (g_limitCount - i) : kBatch;
    const size_t want = (size_t)n * sizeof(LimitRecord);
    if (g_limitFile.read((uint8_t *)batch, want) != (int)want) break;

    for (uint32_t k = 0; k < n; k++) {
      if (batch[k].lat > target + delta) return best;
      const double d =
          geo::distanceM(lat, lon, toDeg(batch[k].lat), toDeg(batch[k].lon));
      if (d < bestM) {
        bestM = d;
        best = batch[k].limitKmh;
      }
    }
    i += n;
  }
  return best;
}

// Narmaste kamera framfor bilen. Tva villkor, och bada behovs:
//
//  - kameran ska ligga i fardriktningen, annars varnar vi for den vi just
//    passerade
//  - kameran ska mata i vart hall, annars varnar vi for motsatt korbana
//
// Trafikverket anger vilken riktning kameran overvakar, sa det andra villkoret
// gar att stalla pa riktiga uppgifter i stallet for att gissas.
int32_t nearestAhead(const GnssFix &f, uint32_t &distOut) {
  if (g_camCount == 0) return -1;

  const int32_t delta = (int32_t)(CAM_SEARCH_WINDOW_DEG * 1e7);
  const int32_t target = (int32_t)llround(f.lat * 1e7);

  int32_t best = -1;
  double bestM = (double)CAM_WARN_FAR_M;

  for (uint32_t i = firstCamAtLeast(target - delta); i < g_camCount; i++) {
    if (g_cams[i].lat > target + delta) break;

    const double clat = toDeg(g_cams[i].lat);
    const double clon = toDeg(g_cams[i].lon);
    const double d = geo::distanceM(f.lat, f.lon, clat, clon);
    if (d >= bestM) continue;

    const float toCam = geo::bearingDeg(f.lat, f.lon, clat, clon);
    if (geo::headingDiffDeg(toCam, f.courseDeg) > CAM_AHEAD_TOLERANCE_DEG) {
      continue;
    }

    if (g_cams[i].bearing != kBearingUnknown) {
      if (geo::headingDiffDeg((float)g_cams[i].bearing, f.courseDeg) >
          CAM_BEARING_TOLERANCE_DEG) {
        continue;
      }
    }

    bestM = d;
    best = (int32_t)i;
  }

  if (best >= 0) distOut = (uint32_t)llround(bestM);
  return best;
}

void scan() {
  const GnssFix f = gnss::fix();

  CamWarning w = {};
  uint8_t limit = 0;

  if (f.valid) {
    limit = lookupLimit(f.lat, f.lon);

    // Kan vi inte hitta vagen i hastighetsfilen far kameran svara i stallet.
    // Uppgiften kommer inte fran Trafikverkets kameradata - den bar ingen
    // hastighet alls - utan bakas in i kameraposten nar filen skapas, ur samma
    // vagdata. Den ar alltsa lika bra som vagdatan var, men bunden till en punkt
    // i stallet for till strackan. Se tools/hamta-trafikverket.py --granser.
    uint32_t dist = 0;
    const int32_t cam = nearestAhead(f, dist);

    // Fardriktningen ar inte att lita pa nar bilen nastan star stilla, och en
    // varning nar man rullar fram i en ko ar bara i vagen.
    const bool moving = f.speedKmh >= 20.0f;

    if (cam >= 0 && moving && dist <= CAM_WARN_FAR_M) {
      w.active = true;
      w.distanceM = dist;
      w.limitKmh = g_cams[cam].limitKmh;
      w.averageSpeed = (g_cams[cam].flags & kFlagAverageSpeed) != 0;

      if (limit == 0 && g_cams[cam].limitKmh > 0 &&
          dist <= CAM_LIMIT_RADIUS_M) {
        limit = g_cams[cam].limitKmh;
      }

      // Ny kamera: borja om med ringarna.
      if (cam != g_warnedCam) {
        g_warnedCam = cam;
        g_warnedRing = 0;
      }

      uint8_t ring = 1;
      if (dist <= CAM_WARN_NEAR_M) {
        ring = 3;
      } else if (dist <= CAM_WARN_MID_M) {
        ring = 2;
      }

      // Ljudet kommer nar man kommer in i en ring, en gang per ring. Att aka
      // bakat genom en ring - koer, avfarter - ger inget nytt pip.
      if (ring > g_warnedRing) {
        g_warnedRing = ring;
        sound::play(ring == 3 ? CUE_CAM_NEAR
                              : (ring == 2 ? CUE_CAM_MID : CUE_CAM_FAR));
      }
    } else if (g_warnedCam >= 0) {
      // Kameran ar passerad eller borta ur bilden.
      const double d = geo::distanceM(f.lat, f.lon, toDeg(g_cams[g_warnedCam].lat),
                                      toDeg(g_cams[g_warnedCam].lon));
      if (d > CAM_WARN_FAR_M + CAM_PASSED_M) {
        g_warnedCam = -1;
        g_warnedRing = 0;
      }
    }

    // ---- overhastighet, med egen rost. Man ska hora skillnad pa "du kor for
    // fort" och "det star en kamera dar framme" utan att titta pa skarmen.
    if (limit > 0 && f.speedKmh > (float)limit + LIMIT_TOLERANCE_KMH) {
      if (g_overSinceMs == 0) g_overSinceMs = millis();
      const bool heldLongEnough = millis() - g_overSinceMs > 4000;
      const bool restedLongEnough =
          g_lastOverWarnMs == 0 || millis() - g_lastOverWarnMs > 30000;
      if (heldLongEnough && restedLongEnough) {
        g_lastOverWarnMs = millis();
        sound::play(CUE_OVER_LIMIT);
      }
    } else {
      g_overSinceMs = 0;
    }
  }

  lock();
  g_warning = w;
  g_currentLimit = limit;
  unlock();
}

}  // namespace

namespace cams {

void begin() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
  // Har finns ingen avlasningstrad an, sa inlasningen far ske direkt.
  loadCams();
  loadLimits();
}

void reload() {
  // Inlasningen frigor och tilldelar om kameralistan. Gjordes det harifran -
  // skarmtraden, dar knappen sitter - skulle avlasningstraden kunna sta mitt i en
  // sokning i den lista som just frigjordes. Darfor bestalls inlasningen i
  // stallet, och utfors av samma trad som soker i den.
  g_wantReload = true;
}

void tick() {
  // Under ett filbyte slapper traden sina filer och ror ingenting forran
  // bytet ar klart. Kvittot ar g_suspended - det ar det uppladdningen vantar
  // pa innan den vagar rora filerna.
  if (g_suspend) {
    if (!g_suspended) {
      if (g_limitFile) g_limitFile.close();
      g_limitCount = 0;
      freeCams();
      g_suspended = true;
    }
    return;
  }
  g_suspended = false;

  if (g_wantReload) {
    g_wantReload = false;
    loadCams();
    loadLimits();
  }

  const uint32_t now = millis();
  if (now - g_lastScanMs < CAM_SCAN_INTERVAL_MS) return;
  g_lastScanMs = now;
  scan();
}

bool loaded() { return g_camCount > 0; }
uint32_t count() { return g_camCount; }
bool limitsLoaded() { return g_limitCount > 0; }

uint8_t currentLimitKmh() {
  lock();
  const uint8_t v = g_currentLimit;
  unlock();
  return v;
}

CamWarning warning() {
  lock();
  CamWarning w = g_warning;
  unlock();
  return w;
}

void beginUpdate() {
  g_suspend = true;
  // Avlasningstraden gar ett varv pa nagra tiotal millisekunder, sa vantan ar
  // kort. Tidsgransen finns for att en hangd trad inte ska ta webbservern med
  // sig - da byts filen anda, och det varsta som kan handa ar att en sokning
  // misslyckas en gang.
  for (int i = 0; i < 200 && !g_suspended; i++) delay(10);
}

void endUpdate() {
  g_wantReload = true;
  g_suspend = false;
}

}  // namespace cams
