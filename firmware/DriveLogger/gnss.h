// Valfri GPS-mottagare (u-blox, t.ex. SparkFun NEO-M9N) pa i2c-bussen.
//
// Modulen letas upp automatiskt vid start. Sitter ingen GPS inkopplad
// fungerar allt precis som vanligt - loggen far bara farre kolumner.

#pragma once

#include <Arduino.h>

struct GnssFix {
  bool valid;        // sant nar mottagaren har en anvandbar position
  uint8_t fixType;   // 0 = ingen, 2 = tvadimensionell, 3 = tredimensionell
  uint8_t sats;      // antal satelliter som anvands
  double lat;        // grader
  double lon;        // grader
  float altM;        // hojd over havet, meter
  float speedKmh;    // hastighet over marken
  float courseDeg;   // kurs over grund, 0-360 grader
};

// Rasiffror for felsokning over serieporten. Utan dem gar det inte att skilja
// "modulen svarar inte" fran "modulen svarar men ser inga satelliter", och det
// ar tva helt olika fel.
struct GnssDebug {
  bool present;
  uint32_t polls;      // antal forsok att lasa av
  uint32_t packets;    // antal mottagna positionspaket
  uint8_t fixType;
  uint8_t sats;
};

namespace gnss {

GnssDebug debug();

// Letar efter en mottagare pa i2c. Returnerar false om ingen finns, vilket
// inte ar ett fel utan bara betyder att kortet kor utan GPS.
bool begin();

bool present();

// Anropas ofta. Gor av med i2c-trafik hogst en gang per sekund, sa att
// loggtakten inte paverkas.
void poll();

GnssFix fix();

// Sann nar mottagaren har satellittid, vilket ar exakt till sekunden.
bool timeValid();
void utc(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour,
         uint8_t &minute, uint8_t &second);

}  // namespace gnss
