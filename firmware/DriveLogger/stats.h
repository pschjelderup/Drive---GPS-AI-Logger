// Statistiken: allt bilen gjort, raknat ur resedagboken.
//
// Siffrorna byggs fran RESOR.JSONL vid start och uppdateras nar en resa
// skrivs. Dagboken ar alltsa sanningen har ocksa - statistiken ar en
// sammanfattning av den, inte en egen bokforing som kan glida isar fran den.
//
// Lagringsprognosen bygger pa uppmatta tal, inte antaganden: sa manga byte har
// gpx-mappen faktiskt kostat, sa manga kilometer har den tackt. Forst nar det
// inte finns nagot att mata pa anvands ett antagande, och da sags det.

#pragma once

#include <Arduino.h>

struct StatsSummary {
  // Ur dagboken.
  uint32_t trips;
  double totalKm;
  uint32_t movingS;       // rullande tid, inte tid med tandning pa
  uint32_t points;
  uint32_t speedingS;
  float maxSpeedKmh;      // hogsta nagonsin

  // Per syfte, i kilometer.
  double privatKm;
  double foretagKm;
  double diffustKm;

  // Kortet.
  uint64_t freeBytes;
  uint64_t cardBytes;

  // Uppmatt kostnad och prognos. bytesPerKm ar noll tills det finns minst en
  // kilometer att mata pa - da visas prognosen som okand i stallet for pahittad.
  uint32_t bytesPerKm;
  double kmLeft;          // sa manga km till ryms pa kortet
  uint64_t pointsLeft;

  bool measured;          // sant nar prognosen vilar pa uppmatta tal
};

namespace stats {

// Laser dagboken och mater gpx-mappen. Anropas efter att kortet monterats.
void begin();

// En resa har just skrivits till dagboken. Billigare an att lasa om filen.
void noteTrip(double km, uint32_t movingS, uint32_t points, uint8_t purpose,
              uint32_t speedingS, float maxSpeedKmh);

StatsSummary summary();

}  // namespace stats
