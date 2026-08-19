#include "gnss.h"

#include <SparkFun_u-blox_GNSS_v3.h>
#include <Wire.h>

#include "config.h"

namespace {

SFE_UBLOX_GNSS gps;

bool g_present = false;
bool g_timeValid = false;
GnssFix g_fix = {};
uint32_t g_lastPollMs = 0;
uint32_t g_polls = 0;
uint32_t g_packets = 0;

uint16_t g_year = 0;
uint8_t g_month = 0, g_day = 0, g_hour = 0, g_minute = 0, g_second = 0;

uint32_t g_epochUtc = 0;
uint32_t g_epochAtMs = 0;

// Sekunder sedan 1970 ur ett datum i UTC - samma dagar-sedan-eran-formel som i
// sensors.cpp. Aret borjar i mars sa att skottdagen hamnar sist och slutar
// vara ett sarfall.
int64_t epochFromUtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                     uint8_t minute, uint8_t second) {
  int32_t y = (int32_t)year;
  if (month <= 2) y -= 1;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy =
      (uint32_t)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5) + day - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
  return days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + second;
}

}  // namespace

namespace gnss {

bool begin() {
  // Knacka forst pa adressen. Utan den kontrollen skulle bibliotekets egen
  // uppstart sitta och vanta pa svar fran nagot som inte finns, varje gang
  // nagon kor utan GPS.
  Wire.beginTransmission(GNSS_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    g_present = false;
    return false;
  }

  if (!gps.begin(Wire, GNSS_I2C_ADDR)) {
    g_present = false;
    return false;
  }

  // Bara u-blox eget binarformat pa i2c - kortare meddelanden an nmea och
  // inget som behover tolkas som text.
  gps.setI2COutput(COM_TYPE_UBX);

  // Mottagaren skickar sjalv nya positioner. Da blir avlasningen nedan en
  // ren minneslasning i stallet for en fraga som maste inavaktas.
  gps.setAutoPVT(true);

  g_present = true;
  return true;
}

bool present() { return g_present; }

void poll() {
  if (!g_present) return;

  const uint32_t now = millis();
  if (now - g_lastPollMs < 1000) return;
  g_lastPollMs = now;

  // Falskt betyder bara att inget nytt kommit sedan sist. Da behaller vi de
  // varden vi redan har.
  g_polls++;
  if (!gps.getPVT()) return;
  g_packets++;

  g_fix.fixType = gps.getFixType();
  g_fix.sats = gps.getSIV();
  g_fix.lat = (double)gps.getLatitude() / 10000000.0;
  g_fix.lon = (double)gps.getLongitude() / 10000000.0;
  g_fix.altM = (float)gps.getAltitudeMSL() / 1000.0f;
  g_fix.speedKmh = (float)gps.getGroundSpeed() * 0.0036f;  // mm/s till km/h
  g_fix.courseDeg = (float)gps.getHeading() / 100000.0f;   // grader * 1e5

  // gnssFixOK ar mottagarens eget omdome: fixet ligger inom dess
  // noggrannhets- och DOP-masker. Utan den flaggan slapps de forsta, vilda
  // losningarna under uppstarten igenom som "positioner" - det var en sadan
  // som satte en maxfart pa 362 388 km/h i en riktig resa.
  g_fix.valid = g_fix.fixType >= 2 && gps.getGnssFixOk() && !gps.getInvalidLlh();

  g_fix.speedAccKmh = (float)gps.getSpeedAccEst() * 0.0036f;  // mm/s till km/h
  g_fix.speedTrusted = g_fix.valid && g_fix.fixType >= 3 &&
                       g_fix.speedAccKmh > 0.0f &&
                       g_fix.speedAccKmh <= SPEED_TRUST_ACC_KMH &&
                       g_fix.speedKmh >= 0.0f &&
                       g_fix.speedKmh <= SPEED_TRUST_MAX_KMH;

  g_timeValid = gps.getTimeValid() && gps.getDateValid();
  if (g_timeValid) {
    g_year = gps.getYear();
    g_month = gps.getMonth();
    g_day = gps.getDay();
    g_hour = gps.getHour();
    g_minute = gps.getMinute();
    g_second = gps.getSecond();

    // Ankaret flyttas bara nar ett nytt tidspaket faktiskt kommit - det ar
    // det som gor att klockan tickar av kristallen daremellan i stallet for
    // att upprepa en gammal avlasning.
    if (g_year >= 2024) {
      g_epochUtc = (uint32_t)epochFromUtc(g_year, g_month, g_day, g_hour,
                                          g_minute, g_second);
      g_epochAtMs = now;
    }
  }
}

GnssFix fix() { return g_fix; }

GnssDebug debug() {
  GnssDebug d;
  d.present = g_present;
  d.polls = g_polls;
  d.packets = g_packets;
  d.fixType = g_fix.fixType;
  d.sats = g_fix.sats;
  return d;
}

bool timeValid() { return g_timeValid; }

uint32_t epochUtc() { return g_epochUtc; }

uint32_t epochAgeMs() {
  if (g_epochUtc == 0) return 0;
  return millis() - g_epochAtMs;
}

void utc(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour,
         uint8_t &minute, uint8_t &second) {
  year = g_year;
  month = g_month;
  day = g_day;
  hour = g_hour;
  minute = g_minute;
  second = g_second;
}

}  // namespace gnss
