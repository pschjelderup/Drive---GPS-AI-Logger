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
  g_fix.valid = g_fix.fixType >= 2;

  g_timeValid = gps.getTimeValid() && gps.getDateValid();
  if (g_timeValid) {
    g_year = gps.getYear();
    g_month = gps.getMonth();
    g_day = gps.getDay();
    g_hour = gps.getHour();
    g_minute = gps.getMinute();
    g_second = gps.getSecond();
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
