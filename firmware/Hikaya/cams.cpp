#include "cams.h"

#include "storage.h"
#include <esp_timer.h>
#include <math.h>

#include "config.h"
#include "geo.h"
#include "gnss.h"
#include "logg.h"
#include "sensors.h"
#include "sound.h"

namespace {

// ---------------------------------------------------------------- format ---
// Bada filerna ar sorterade pa latitud i vaxande ordning. Det ar det som gor
// att en position gar att slå upp med binarsokning i stallet for att jamforas
// mot varje rad: en bil pa vag 73 behover inte fraga om kameror i Kiruna.

const uint32_t kCamMagic = 0x31434C44;    // "DLC1"
const uint32_t kLimitMagic = 0x31484C44;  // "DLH1"
// Webbappens byggare skrev lange "DHL1" - tva bokstaver i fel ordning - och
// de filerna ligger pa kort darute. Innehallet ar identiskt, sa de godtas:
// finns filen sa anvands den, oavsett vilken av de tva den bar.
const uint32_t kLimitMagicOld = 0x314C4844;  // "DHL1"

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

// Hastighetspunkterna ar for manga for minnet i sin helhet. Filen oppnas
// bara nar nagot ska lasas ur den - av fonsterladdaren i bakgrunden, eller
// i nodfall av avlasningstraden - och lamnas stangd daremellan: kortets
// filsystem har fem platser for oppna filer, och resan behover sina.
uint32_t g_limitCount = 0;
uint32_t g_limitDataStart = 0;
uint32_t g_limitSize = 0;

// ---- indexet -------------------------------------------------------------
// En sokning i filen ar inte gratis: filsystemet har ingen snabbsokning, sa
// varje hopp bakat i en fil pa 139 MB betyder att klusterkedjan vandras fran
// filens borjan - tiotals sektorlasningar per hopp. Binarsokningen gjorde
// tjugofem sadana hopp per uppslag, en gang i sekunden, i avlasningstraden
// som har hogst prioritet och skriver resan. Kortet var upptaget mer an
// halva tiden, och allt annat som ville at det - loggen, synken, skarmens
// kundlista - fick vanta. Det ar det som kandes som en hangd enhet.
//
// Indexet ar forsta latituden i varje block om LIMIT_INDEX_STRIDE poster.
// Det ligger i psram (ett par hundra kilobyte), sa halva sokningen sker i
// minnet och filen behover bara EN sokning per uppslag, till ratt block.
// Bygget laser filen en gang fran borjan till slut - sekventiellt, det ar
// billigt - i en egen trad med lagsta prioritet, och sparas pa kortet sa
// att nasta start slipper.
const uint32_t kIdxMagic = 0x31584C44;  // "DLX1"

#pragma pack(push, 1)
struct IdxHeader {
  uint32_t magic;
  uint32_t fileSize;  // hastighetsfilens langd - andras den ar indexet fel
  uint32_t count;
  uint32_t stride;
  uint32_t blocks;
};
#pragma pack(pop)

int32_t *g_idx = nullptr;  // psram
uint32_t g_idxBlocks = 0;
volatile bool g_idxReady = false;
volatile bool g_idxBuilding = false;
// Indexet las och byts av tre trader (avlasningen, byggaren, fonster-
// laddaren); laset halls bara under sjalva sokningen och bytet.
SemaphoreHandle_t g_idxLock = nullptr;

// ---- fonstret ------------------------------------------------------------
// Den aktuella delen av filen, i psram: alla poster med latitud i
// [g_winLo, g_winHi). Uppslagen gar helt i minnet. Nar bilen narmar sig en
// kant begar avlasningstraden ett nytt fonster runt sig, och laddaren -
// egen trad, lagsta prioritet - laser det sekventiellt ur filen (indexet
// sager var) till en ny buffert och byter under laset. Det gamla fonstret
// lever tills bytet ar gjort, sa uppslagen har alltid nagot att svara ur.
LimitRecord *g_win = nullptr;  // psram
uint32_t g_winCount = 0;
int32_t g_winLo = 0, g_winHi = 0;
bool g_winEdgeLo = false, g_winEdgeHi = false;  // fonstret nar filens kant
volatile bool g_winReady = false;
volatile bool g_winLoading = false;
volatile bool g_winWanted = false;
volatile int32_t g_winReqCenter = 0;
uint32_t g_winRetryMs = 0;
uint32_t g_winLoadMaxMs = 0;
SemaphoreHandle_t g_winLock = nullptr;

// Blocket som lases vid ett uppslag. Internminne, DMA-dugligt, tilldelat en
// gang - inte pa avlasningstradens stack, dar det inte far plats bredvid
// resans egna buffertar.
const size_t kBlockBytes = LIMIT_INDEX_STRIDE * sizeof(LimitRecord);
uint8_t *g_blockBuf = nullptr;

uint32_t g_lookupMaxUs = 0;

uint32_t idxBlocksFor(uint32_t count) {
  return (count + LIMIT_INDEX_STRIDE - 1) / LIMIT_INDEX_STRIDE;
}

void freeIndex() {
  g_idxReady = false;
  if (g_idxLock) xSemaphoreTake(g_idxLock, portMAX_DELAY);
  if (g_idx) {
    free(g_idx);
    g_idx = nullptr;
  }
  g_idxBlocks = 0;
  if (g_idxLock) xSemaphoreGive(g_idxLock);
}

void freeWindow() {
  if (g_winLock) xSemaphoreTake(g_winLock, portMAX_DELAY);
  g_winReady = false;
  LimitRecord *old = g_win;
  g_win = nullptr;
  g_winCount = 0;
  if (g_winLock) xSemaphoreGive(g_winLock);
  if (old) free(old);
}

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

bool readHeader(File &f, uint32_t magic, uint16_t recordSize, uint32_t &count,
                uint32_t altMagic = 0) {
  FileHeader h = {};
  if (f.read((uint8_t *)&h, sizeof(h)) != (int)sizeof(h)) return false;
  if (h.magic != magic && (altMagic == 0 || h.magic != altMagic)) return false;
  if (h.version != 1) return false;
  if (h.recordSize != recordSize) return false;
  count = h.count;
  return true;
}

void loadCams() {
  freeCams();
  if (!sensors::sdMounted()) return;

  File f = SDCARD.open(CAMS_FILE, FILE_READ);
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

// Indexet fran kortet, om det hor till exakt den har filen.
bool loadIndexFile(uint32_t fileSize, uint32_t count) {
  File f = SDCARD.open(LIMITS_INDEX_FILE, FILE_READ);
  if (!f) return false;
  IdxHeader h = {};
  const bool headOk = f.read((uint8_t *)&h, sizeof(h)) == (int)sizeof(h) &&
                      h.magic == kIdxMagic && h.fileSize == fileSize &&
                      h.count == count && h.stride == LIMIT_INDEX_STRIDE &&
                      h.blocks == idxBlocksFor(count) && h.blocks > 0;
  if (!headOk) {
    f.close();
    return false;
  }
  int32_t *idx =
      (int32_t *)heap_caps_malloc((size_t)h.blocks * 4, MALLOC_CAP_SPIRAM);
  if (!idx) {
    f.close();
    return false;
  }
  // Via en liten intern buffert: kortlasningar rakt in i psram ar inte
  // sjalvklart DMA-dugliga, och det har ar nagra hundra kilobyte en gang.
  bool ok = true;
  uint32_t done = 0;
  while (done < h.blocks && ok) {
    const uint32_t n = min<uint32_t>(h.blocks - done, kBlockBytes / 4);
    ok = f.read(g_blockBuf, n * 4) == (int)(n * 4);
    if (ok) memcpy(idx + done, g_blockBuf, n * 4);
    done += n;
  }
  f.close();
  if (!ok) {
    free(idx);
    return false;
  }
  g_idx = idx;
  g_idxBlocks = h.blocks;
  g_idxReady = true;
  return true;
}

// Bygget, i egen trad. Laser hela filen fran borjan i klumpar om tva block,
// plockar forsta latituden ur varje block, sparar resultatet och lamnar
// over. Under tiden svarar uppslagen "okand grans" - hellre det i en minut
// an en seg enhet i evighet.
void indexTask(void *) {
  const uint32_t t0 = millis();
  bool ok = false;
  int32_t *idx = nullptr;
  uint32_t blocks = 0;

  // Egen lasbuffert: g_blockBuf tillhor avlasningstraden, och den kan sta
  // mitt i ett uppslag medan det har pagar.
  const size_t chunk = kBlockBytes * 2;
  uint8_t *buf =
      (uint8_t *)heap_caps_malloc(chunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

  File f = SDCARD.open(LIMITS_FILE, FILE_READ);
  uint32_t count = 0;
  if (f && buf &&
      readHeader(f, kLimitMagic, sizeof(LimitRecord), count, kLimitMagicOld) &&
      count > 0) {
    blocks = idxBlocksFor(count);
    idx = (int32_t *)heap_caps_malloc((size_t)blocks * 4, MALLOC_CAP_SPIRAM);
    if (idx && f.seek(sizeof(FileHeader))) {
      ok = true;
      uint32_t rec = 0;
      uint32_t chunks = 0;
      while (rec < count) {
        // Ett filbyte pa gang: slapp allt, bytet laser om efterat.
        if (g_suspend) { ok = false; break; }
        const size_t want =
            min(chunk, (size_t)(count - rec) * sizeof(LimitRecord));
        if (f.read(buf, want) != (int)want) { ok = false; break; }
        for (size_t off = 0; off < want; off += sizeof(LimitRecord), rec++) {
          if (rec % LIMIT_INDEX_STRIDE == 0) {
            int32_t lat;
            memcpy(&lat, buf + off, 4);
            idx[rec / LIMIT_INDEX_STRIDE] = lat;
          }
        }
        // Lagsta prioritet racker inte pa en karna dar wifi och synk bor:
        // en paus da och da slapper fram dem pa riktigt.
        if ((++chunks & 3) == 0) delay(1); else taskYIELD();
      }
    }
  }
  const uint32_t fileSize = f ? (uint32_t)f.size() : 0;
  if (f) f.close();

  if (ok) {
    // Spara, genom den interna bufferten (se loadIndexFile). Misslyckas
    // skrivningen anvands indexet anda - det byggs bara om nasta start.
    IdxHeader h = {kIdxMagic, fileSize, count, LIMIT_INDEX_STRIDE, blocks};
    if (SDCARD.exists(LIMITS_INDEX_FILE)) SDCARD.remove(LIMITS_INDEX_FILE);
    File o = SDCARD.open(LIMITS_INDEX_FILE, FILE_WRITE);
    bool saved = o && o.write((const uint8_t *)&h, sizeof(h)) == sizeof(h);
    uint32_t done = 0;
    while (saved && done < blocks) {
      const uint32_t n = min<uint32_t>(blocks - done, chunk / 4);
      memcpy(buf, idx + done, n * 4);
      saved = o.write(buf, n * 4) == n * 4;
      done += n;
    }
    if (o) o.close();
    if (!saved) SDCARD.remove(LIMITS_INDEX_FILE);

    // Overlamningen: pekaren och langden forst, flaggan sist, med en
    // barriar emellan - avlasningstraden laser flaggan forst.
    freeIndex();
    if (g_idxLock) xSemaphoreTake(g_idxLock, portMAX_DELAY);
    g_idx = idx;
    g_idxBlocks = blocks;
    if (g_idxLock) xSemaphoreGive(g_idxLock);
    __sync_synchronize();
    g_idxReady = true;
    logg::event("hastighetsindex: %lu block byggt pa %lu s%s",
                (unsigned long)blocks, (unsigned long)((millis() - t0) / 1000),
                saved ? ", sparat pa kortet" : " - gick INTE att spara");
  } else {
    if (idx) free(idx);
    logg::event("hastighetsindex: bygget avbrots");
  }
  if (buf) free(buf);
  g_idxBuilding = false;
  vTaskDelete(nullptr);
}

// Indexet for den oppna filen: fran kortet om det finns och stammer, annars
// byggs det i bakgrunden.
void loadIndex() {
  freeIndex();
  if (g_limitCount == 0 || !g_blockBuf) return;
  if (loadIndexFile(g_limitSize, g_limitCount)) {
    logg::event("hastighetsindex: laddat fran kortet (%lu block)",
                (unsigned long)g_idxBlocks);
    return;
  }
  if (g_idxBuilding) return;
  g_idxBuilding = true;
  if (xTaskCreatePinnedToCore(indexTask, "gransidx", 6144, nullptr, 1, nullptr,
                              0) != pdPASS) {
    g_idxBuilding = false;
    logg::event("hastighetsindex: fick ingen trad att bygga i");
  } else {
    logg::event("hastighetsindex: saknas - byggs i bakgrunden");
  }
}

void loadLimits() {
  g_limitCount = 0;
  freeWindow();
  if (!sensors::sdMounted()) return;

  File f = SDCARD.open(LIMITS_FILE, FILE_READ);
  if (!f) return;

  uint32_t count = 0;
  if (!readHeader(f, kLimitMagic, sizeof(LimitRecord), count, kLimitMagicOld) ||
      count == 0) {
    f.close();
    return;
  }
  g_limitSize = (uint32_t)f.size();
  f.close();
  g_limitCount = count;
  g_limitDataStart = sizeof(FileHeader);
  loadIndex();
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

// Forsta blocket vars forsta latitud ar minst sa hog - i indexet, i minnet.
uint32_t firstBlockAtLeast(int32_t lat) {
  uint32_t lo = 0, hi = g_idxBlocks;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (g_idx[mid] < lat) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

// Fonsterladdaren. Raknar ut vilka block som tacker +/- HALF runt mitten,
// laser dem i foljd ur filen till en ny psram-buffert och byter in den.
// Sekventiell lasning ar det kortet ar bra pa: nagra megabyte tar en
// sekund, och traden har lagsta prioritet sa resan och synken gar fore.
bool loadWindow(int32_t center) {
  const uint32_t t0 = millis();
  const int32_t half = (int32_t)(LIMIT_WINDOW_HALF_DEG * 1e7);
  const int32_t lo = center - half;
  const int32_t hi = center + half;

  uint32_t startBlock = 0, endBlock = 0, blocks = 0;
  int32_t winLo = 0, winHi = 0;
  if (xSemaphoreTake(g_idxLock, portMAX_DELAY) != pdTRUE) return false;
  if (g_idx && g_idxBlocks) {
    blocks = g_idxBlocks;
    startBlock = firstBlockAtLeast(lo);
    if (startBlock > 0) startBlock--;
    endBlock = firstBlockAtLeast(hi);
    if (endBlock > blocks) endBlock = blocks;
    // Taket: i tata omraden krymps fonstret runt bilen tills det far plats.
    const uint32_t maxBlocks = LIMIT_WINDOW_MAX_BYTES / kBlockBytes;
    if (endBlock - startBlock > maxBlocks) {
      const uint32_t c = firstBlockAtLeast(center);
      startBlock = c > maxBlocks / 2 ? c - maxBlocks / 2 : 0;
      endBlock = min<uint32_t>(startBlock + maxBlocks, blocks);
    }
    // Tackningen ar exakt: alla poster med latitud i [idx[start], idx[end])
    // ligger i blocken [start, end) eftersom filen ar sorterad.
    winLo = g_idx[startBlock];
    winHi = endBlock < blocks ? g_idx[endBlock] : INT32_MAX;
  }
  xSemaphoreGive(g_idxLock);
  if (!blocks || endBlock <= startBlock) return false;

  const uint32_t firstRec = startBlock * LIMIT_INDEX_STRIDE;
  const uint32_t lastRec = min<uint32_t>(endBlock * LIMIT_INDEX_STRIDE, g_limitCount);
  if (lastRec <= firstRec) return false;
  const uint32_t n = lastRec - firstRec;

  LimitRecord *buf = (LimitRecord *)heap_caps_malloc(
      (size_t)n * sizeof(LimitRecord), MALLOC_CAP_SPIRAM);
  const size_t chunk = kBlockBytes * 2;
  uint8_t *tmp = (uint8_t *)heap_caps_malloc(chunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  bool ok = buf && tmp;

  File f;
  if (ok) {
    f = SDCARD.open(LIMITS_FILE, FILE_READ);
    ok = f && f.seek(g_limitDataStart + (uint32_t)firstRec * sizeof(LimitRecord));
  }
  size_t done = 0;
  const size_t total = (size_t)n * sizeof(LimitRecord);
  uint32_t chunks = 0;
  while (ok && done < total) {
    if (g_suspend) { ok = false; break; }
    const size_t want = min(chunk, total - done);
    if (f.read(tmp, want) != (int)want) { ok = false; break; }
    memcpy((uint8_t *)buf + done, tmp, want);
    done += want;
    if ((++chunks & 3) == 0) delay(1); else taskYIELD();
  }
  if (f) f.close();
  if (tmp) free(tmp);

  if (!ok) {
    if (buf) free(buf);
    return false;
  }

  xSemaphoreTake(g_winLock, portMAX_DELAY);
  LimitRecord *old = g_win;
  g_win = buf;
  g_winCount = n;
  g_winLo = winLo;
  g_winHi = winHi;
  g_winEdgeLo = startBlock == 0;
  g_winEdgeHi = endBlock >= blocks;
  g_winReady = true;
  xSemaphoreGive(g_winLock);
  if (old) free(old);

  const uint32_t ms = millis() - t0;
  if (ms > g_winLoadMaxMs) g_winLoadMaxMs = ms;
  Serial.printf("granser: fonster %lu poster (%lu kB) pa %lu ms\n",
                (unsigned long)n, (unsigned long)(total / 1024),
                (unsigned long)ms);
  return true;
}

void windowTask(void *) {
  for (;;) {
    delay(250);
    if (!g_winWanted || g_suspend || !g_idxReady || g_limitCount == 0) continue;
    if (g_winRetryMs && (int32_t)(millis() - g_winRetryMs) < 0) continue;
    g_winWanted = false;
    g_winLoading = true;
    const bool ok = loadWindow(g_winReqCenter);
    g_winLoading = false;
    if (!ok) {
      // Minnet eller kortet sa nej: vanta tio sekunder innan nasta forsok,
      // sa att avlasningstraden inte bestaller om i varje varv.
      g_winRetryMs = millis() + 10000;
      logg::event("hastighetsfonster: kunde inte lasas in");
    } else {
      g_winRetryMs = 0;
    }
  }
}

// Uppslag i fonstret. Sant i covered nar fonstret tacker hela sokbandet -
// annars ar svaret inte att lita pa och filen far fraga i stallet.
uint8_t lookupWindow(double lat, double lon, int32_t lo, int32_t hi, bool &covered) {
  covered = false;
  if (!g_winReady || !g_winLock) return 0;
  if (xSemaphoreTake(g_winLock, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
  uint8_t best = 0;
  if (g_winReady && g_win && lo >= g_winLo && hi < g_winHi) {
    covered = true;
    // Forsta posten med latitud >= lo, binart i minnet.
    uint32_t a = 0, b = g_winCount;
    while (a < b) {
      const uint32_t mid = a + (b - a) / 2;
      if (g_win[mid].lat < lo) a = mid + 1; else b = mid;
    }
    double bestM = (double)LIMIT_MATCH_RADIUS_M;
    for (uint32_t i = a; i < g_winCount; i++) {
      if (g_win[i].lat > hi) break;
      const double d = geo::distanceM(lat, lon, toDeg(g_win[i].lat), toDeg(g_win[i].lon));
      if (d < bestM) {
        bestM = d;
        best = g_win[i].limitKmh;
      }
    }
  }
  xSemaphoreGive(g_winLock);
  return best;
}

// Ber om ett nytt fonster nar bilen narmar sig kanten pa det som finns -
// eller nar det inte finns nagot. Laddaren tar det inom en kvarts sekund.
void wantWindowAround(int32_t target, int32_t delta) {
  if (g_winLoading || !g_idxReady) return;
  const int32_t margin = (int32_t)(LIMIT_WINDOW_MARGIN_DEG * 1e7);
  bool need = !g_winReady;
  if (!need) {
    if (!g_winEdgeLo && target - delta < g_winLo + margin) need = true;
    if (!g_winEdgeHi && target + delta > g_winHi - margin) need = true;
  }
  if (need) {
    g_winReqCenter = target;
    g_winWanted = true;
  }
}

// Nodfallet: fonstret tacker inte - det laddas just nu, eller fick inte
// plats. Da lases blocket direkt ur filen med indexets hjalp: EN sokning
// i filen, nagra tiotal millisekunder. Filen oppnas for tillfallet och
// stangs igen; det har hander bara i sekunderna kring ett fonsterbyte.
uint8_t lookupFile(double lat, double lon, int32_t lo, int32_t hi) {
  if (!g_blockBuf || xSemaphoreTake(g_idxLock, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
  uint32_t block = 0;
  bool haveIdx = g_idx && g_idxBlocks;
  if (haveIdx) {
    block = firstBlockAtLeast(lo);
    if (block > 0) block--;
  }
  xSemaphoreGive(g_idxLock);
  if (!haveIdx) return 0;

  uint32_t rec = block * LIMIT_INDEX_STRIDE;
  if (rec >= g_limitCount) return 0;

  double bestM = (double)LIMIT_MATCH_RADIUS_M;
  uint8_t best = 0;

  File f = SDCARD.open(LIMITS_FILE, FILE_READ);
  if (f && f.seek(g_limitDataStart + rec * sizeof(LimitRecord))) {
    // Taket ar en sakerhetslina: ett smalt band tvars over landet ar nagra
    // block, aldrig sextiofyra - men en fil som inte ar sorterad ska inte
    // kunna lasa avlasningstraden i en lasning av hela kortet.
    uint8_t blocksRead = 0;
    while (rec < g_limitCount && blocksRead < 64) {
      const uint32_t n = min<uint32_t>(g_limitCount - rec, LIMIT_INDEX_STRIDE);
      const size_t want = (size_t)n * sizeof(LimitRecord);
      if (f.read(g_blockBuf, want) != (int)want) break;
      const LimitRecord *r = (const LimitRecord *)g_blockBuf;
      bool past = false;
      for (uint32_t k = 0; k < n; k++) {
        if (r[k].lat > hi) { past = true; break; }
        if (r[k].lat < lo) continue;
        const double d = geo::distanceM(lat, lon, toDeg(r[k].lat), toDeg(r[k].lon));
        if (d < bestM) {
          bestM = d;
          best = r[k].limitKmh;
        }
      }
      rec += n;
      blocksRead++;
      if (past) break;
    }
  }
  if (f) f.close();
  return best;
}

// Skyltad hastighet dar bilen ar. Punkterna ligger tatt langs vagarna, sa den
// narmaste inom nagra tiotal meter ar den som galler. Hittas ingen sadan kor vi
// pa en vag som inte finns i filen, och da svarar vi noll - inte en gissning.
//
// Forst fonstret i minnet; tacker det inte, filen. Och i bada fallen: se
// till att nasta fonster ar pa vag innan det behovs.
uint8_t lookupLimit(double lat, double lon) {
  if (g_limitCount == 0 || !g_idxReady) return 0;
  __sync_synchronize();
  const int64_t t0 = esp_timer_get_time();

  // Sextio meter i latitud. Longituden kan vara vidare pa svenska breddgrader,
  // men avstandet raknas riktigt for varje kandidat, sa fonstret behover bara
  // vara garanterat tillrackligt stort.
  const int32_t delta = (int32_t)(((double)LIMIT_MATCH_RADIUS_M / 111320.0) * 1e7);
  const int32_t target = (int32_t)llround(lat * 1e7);
  const int32_t lo = target - delta;
  const int32_t hi = target + delta;

  wantWindowAround(target, delta);

  bool covered = false;
  uint8_t best = lookupWindow(lat, lon, lo, hi, covered);
  if (!covered) best = lookupFile(lat, lon, lo, hi);

  const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
  if (us > g_lookupMaxUs) g_lookupMaxUs = us;
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
  if (g_idxLock == nullptr) g_idxLock = xSemaphoreCreateMutex();
  if (g_winLock == nullptr) g_winLock = xSemaphoreCreateMutex();
  if (!g_blockBuf) {
    g_blockBuf = (uint8_t *)heap_caps_malloc(kBlockBytes,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  }
  // Har finns ingen avlasningstrad an, sa inlasningen far ske direkt.
  loadCams();
  loadLimits();
  // Fonsterladdaren: lagsta prioritet, karna 0, vantar pa bestallningar.
  xTaskCreatePinnedToCore(windowTask, "gransfonster", 6144, nullptr, 1, nullptr, 0);
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
      g_limitCount = 0;
      g_idxReady = false;  // indexet behalls, inlasningen avgor om det duger
      freeWindow();        // fonstret hor till den gamla filen
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
bool indexReady() { return g_idxReady; }
bool indexBuilding() { return g_idxBuilding; }

uint32_t lookupMaxMs() {
  const uint32_t us = g_lookupMaxUs;
  g_lookupMaxUs = 0;
  return us / 1000;
}

uint32_t windowLoadMaxMs() {
  const uint32_t ms = g_winLoadMaxMs;
  g_winLoadMaxMs = 0;
  return ms;
}

bool windowReady() { return g_winReady; }

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
