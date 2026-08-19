// Rakning pa jordytan. Bade resedetektorn och kameravarningen behover samma
// tre svar, sa de bor pa ett stalle.

#pragma once

#include <Arduino.h>

namespace geo {

// Avstand mellan tva positioner, i meter.
double distanceM(double lat1, double lon1, double lat2, double lon2);

// Kompassriktningen fran den forsta positionen till den andra, 0-360 grader
// dar 0 ar rakt norr.
float bearingDeg(double lat1, double lon1, double lat2, double lon2);

// Skillnaden mellan tva kompassriktningar, alltid 0-180 grader. Det ar den man
// vill ha nar fragan ar "pekar de tva ungefar samma hall", eftersom 350 och 10
// grader ligger tjugo grader isar och inte trehundrafyrtio.
float headingDiffDeg(float a, float b);

}  // namespace geo
