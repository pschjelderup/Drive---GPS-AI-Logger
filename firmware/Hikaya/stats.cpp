#include "stats.h"

#include "storage.h"

#include "config.h"
#include "sensors.h"
#include "trip.h"

namespace {

StatsSummary g_s = {};
SemaphoreHandle_t g_mutex = nullptr;

// Gpx-mappens uppmatta storlek. Raknas vid start och skrivs upp nar en resa
// stangs, sa att prognosen foljer verkligheten utan att mappen behover ga
// igenom pa nytt.
uint64_t g_gpxBytes = 0;

void lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

// Plockar ett tal ur en jsonl-rad. Raderna skrivs av trip.cpp med kanda
// nyckelnamn, sa en strangsokning racker och kostar inget minne - det har ar
// var egen fil, inte frammande json.
double numAfter(const char *line, const char *key) {
  const char *p = strstr(line, key);
  if (!p) return 0;
  p += strlen(key);
  return atof(p);
}

void recompute() {
  const uint64_t freeB = sensors::freeBytes();
  const uint64_t cardB = sensors::cardBytes();

  lock();
  g_s.freeBytes = freeB;
  g_s.cardBytes = cardB;

  if (g_s.totalKm >= 1.0 && g_gpxBytes > 0) {
    // Uppmatt: sa har manga byte har en kilometer faktiskt kostat hittills.
    // Dagboken och tillstandsfilen ingar inte - de ar sma nog att forsvinna i
    // avrundningen.
    g_s.bytesPerKm = (uint32_t)(g_gpxBytes / g_s.totalKm);
    g_s.measured = true;
  } else {
    // Inget att mata pa an. Hundra punkter per km (var tionde meter) om ca 120
    // byte ar formatets ovre kant - hellre en forsiktig prognos an en glad.
    g_s.bytesPerKm = 12000;
    g_s.measured = false;
  }

  g_s.kmLeft = (double)freeB / (double)g_s.bytesPerKm;

  const double pointsPerKm =
      (g_s.totalKm >= 1.0 && g_s.points > 0)
          ? (double)g_s.points / g_s.totalKm
          : 1000.0 / TRACK_MIN_MOVE_M;
  g_s.pointsLeft = (uint64_t)(g_s.kmLeft * pointsPerKm);
  unlock();
}

}  // namespace

namespace stats {

void begin() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();

  lock();
  memset(&g_s, 0, sizeof(g_s));
  unlock();
  g_gpxBytes = 0;

  if (!sensors::sdMounted()) {
    recompute();
    return;
  }

  // ---- dagboken, rad for rad
  File f = SDCARD.open(TRIPS_JSONL, FILE_READ);
  if (f) {
    // En rad ar nagra hundra byte; bufferten tal det dubbla.
    static char line[768];
    while (f.available()) {
      const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
      line[n] = '\0';
      // En halvskriven rad efter ett stromavbrott saknar radslut och ar
      // darmed sista raden; den kanns igen pa att slutklammern fattas.
      if (!strchr(line, '}')) continue;

      const double km = numAfter(line, "\"meter\":") / 1000.0;
      const uint32_t moving = (uint32_t)numAfter(line, "\"rullande_s\":");
      const uint32_t pts = (uint32_t)numAfter(line, "\"punkter\":");
      const uint32_t speeding = (uint32_t)numAfter(line, "\"fortkorning_s\":");
      const float maxKmh = (float)numAfter(line, "\"maxfart_kmh\":");

      uint8_t purpose = PURPOSE_DIFFUST;
      if (strstr(line, "\"syfte\":\"privat\"")) purpose = PURPOSE_PRIVAT;
      else if (strstr(line, "\"syfte\":\"foretag\"")) purpose = PURPOSE_FORETAG;

      lock();
      g_s.trips++;
      g_s.totalKm += km;
      g_s.movingS += moving;
      g_s.points += pts;
      g_s.speedingS += speeding;
      if (maxKmh > g_s.maxSpeedKmh) g_s.maxSpeedKmh = maxKmh;
      if (purpose == PURPOSE_PRIVAT) g_s.privatKm += km;
      else if (purpose == PURPOSE_FORETAG) g_s.foretagKm += km;
      else g_s.diffustKm += km;
      unlock();
    }
    f.close();
  }

  // ---- gpx-mappens verkliga kostnad. Bada mapparna: en synkad resa har
  // fortfarande kostat sin plats pa kortet, den har bara bytt hylla.
  const char *dirs[2] = {GPX_DIR, GPX_SYNCED_DIR};
  for (uint8_t i = 0; i < 2; i++) {
    File dir = SDCARD.open(dirs[i]);
    if (!dir) continue;
    File entry;
    while ((entry = dir.openNextFile())) {
      if (!entry.isDirectory()) g_gpxBytes += entry.size();
      entry.close();
    }
    dir.close();
  }

  recompute();
}

void noteTrip(double km, uint32_t movingS, uint32_t points, uint8_t purpose,
              uint32_t speedingS, float maxSpeedKmh) {
  lock();
  g_s.trips++;
  g_s.totalKm += km;
  g_s.movingS += movingS;
  g_s.points += points;
  g_s.speedingS += speedingS;
  if (maxSpeedKmh > g_s.maxSpeedKmh) g_s.maxSpeedKmh = maxSpeedKmh;
  if (purpose == PURPOSE_PRIVAT) g_s.privatKm += km;
  else if (purpose == PURPOSE_FORETAG) g_s.foretagKm += km;
  else g_s.diffustKm += km;
  unlock();

  // Punkterna ar ungefar lika stora, sa resans kostnad gar att lagga till utan
  // att lasa filen: sa manga punkter ganger den uppmatta genomsnittspunkten.
  if (points > 0) g_gpxBytes += (uint64_t)points * 117;

  recompute();
}

StatsSummary summary() {
  // Ledigt utrymme ror sig medan en resa skrivs, sa det hamtas farskt.
  lock();
  StatsSummary s = g_s;
  unlock();
  s.freeBytes = sensors::freeBytes();
  return s;
}

}  // namespace stats
