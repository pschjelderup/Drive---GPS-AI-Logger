#include "geo.h"

#include <math.h>

namespace {
const double kEarthR = 6371008.8;  // jordens medelradie, meter
const double kRad = M_PI / 180.0;
}  // namespace

namespace geo {

double distanceM(double lat1, double lon1, double lat2, double lon2) {
  const double dLat = (lat2 - lat1) * kRad;
  const double dLon = (lon2 - lon1) * kRad;
  const double a = sin(dLat / 2) * sin(dLat / 2) +
                   cos(lat1 * kRad) * cos(lat2 * kRad) * sin(dLon / 2) *
                       sin(dLon / 2);
  return 2.0 * kEarthR * asin(fmin(1.0, sqrt(a)));
}

float bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = lat1 * kRad;
  const double p2 = lat2 * kRad;
  const double dl = (lon2 - lon1) * kRad;
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double deg = atan2(y, x) / kRad;
  if (deg < 0) deg += 360.0;
  return (float)deg;
}

float headingDiffDeg(float a, float b) {
  float d = fabsf(a - b);
  while (d > 360.0f) d -= 360.0f;
  if (d > 180.0f) d = 360.0f - d;
  return d;
}

}  // namespace geo
