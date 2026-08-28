#include "gnss.h"

#include "config.h"
#include "logg.h"

#if !defined(GNSS_UART)
#include <SparkFun_u-blox_GNSS_v3.h>
#include <Wire.h>
#endif

namespace {

#if !defined(GNSS_UART)
SFE_UBLOX_GNSS gps;
#endif

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

#if defined(GNSS_UART)

// NEO-6M-tolken: mottagaren strommar nmea-text pa uarten, och har plockas
// RMC (position, fart, tid, datum), GGA (satelliter, hdop, hojd) och GSA
// (2d/3d) ur strommen. Checksumman kontrolleras pa varje mening. NEO-6M
// lamnar ingen egen fartosakerhet som M9N gor, sa fortroendet for farten
// bygger pa 3d-fix, satellitantal och hdop i stallet - och hdop far sta i
// osakerhetsfaltet sa att loggens fartrad behaller sin form.
uint8_t g_quality = 0;   // gga falt 6: 0 = inget fix
uint8_t g_gsaFix = 1;    // gsa falt 2: 1 = inget, 2 = 2d, 3 = 3d
float g_hdop = 99.0f;
bool g_rmcA = false;
uint8_t g_inView = 0;    // gsv falt 3: satelliter i sikte
bool g_hadFix = false;

char g_line[110];
uint8_t g_len = 0;

bool checksumOk(const char *s) {
  if (s[0] != '$') return false;
  uint8_t x = 0;
  const char *p = s + 1;
  while (*p && *p != '*') x ^= (uint8_t)*p++;
  if (*p != '*') return false;
  return strtol(p + 1, nullptr, 16) == x;
}

// Faltpekare: kommatecknen nollas och varje falts borjan noteras. Tomma
// falt blir tomma strangar - precis vad atof ger noll pa.
uint8_t splitFields(char *s, char *f[], uint8_t maxF) {
  uint8_t n = 0;
  f[n++] = s;
  for (char *p = s; *p && n < maxF; p++) {
    if (*p == ',' || *p == '*') {
      *p = '\0';
      f[n++] = p + 1;
    }
  }
  return n;
}

double nmeaDeg(const char *v, const char *hemi) {
  const double raw = atof(v);
  if (raw == 0.0) return 0.0;
  const int dd = (int)(raw / 100.0);
  const double deg = dd + (raw - dd * 100.0) / 60.0;
  return (hemi[0] == 'S' || hemi[0] == 'W') ? -deg : deg;
}

void composeFix(uint32_t now) {
  g_fix.valid = g_rmcA && g_quality > 0;
  g_fix.fixType = g_gsaFix >= 2 ? g_gsaFix : (g_rmcA ? 2 : 0);
  // Ingen fartosakerhet fran mottagaren - hdop star i faltet som en
  // fingervisning, och fortroendet avgors av kriterierna nedan.
  g_fix.speedAccKmh = g_hdop;
  g_fix.speedTrusted = g_fix.valid && g_fix.fixType >= 3 &&
                       g_fix.sats >= 5 && g_hdop > 0.0f && g_hdop <= 2.0f &&
                       g_fix.speedKmh >= 0.0f &&
                       g_fix.speedKmh <= SPEED_TRUST_MAX_KMH;
  (void)now;
}

void handleLine(char *s, uint32_t now) {
  if (!checksumOk(s)) return;
  if (!g_present) {
    g_present = true;
    // Forsta hela meningen ar kvittot pa att kablarna sitter ratt - fran
    // och med har ar allt som aterstar antenn och himmel.
    logg::event("gps: forsta meningen pa uarten - modulen hors");
  }

  char *f[20];
  const uint8_t n = splitFields(s, f, 20);
  const char *typ = f[0] + 3;  // $GPRMC / $GNRMC -> "RMC"

  if (strncmp(typ, "RMC", 3) == 0 && n >= 10) {
    g_rmcA = f[2][0] == 'A';
    if (g_rmcA) {
      g_fix.lat = nmeaDeg(f[3], f[4]);
      g_fix.lon = nmeaDeg(f[5], f[6]);
      g_fix.speedKmh = (float)(atof(f[7]) * 1.852);
      g_fix.courseDeg = (float)atof(f[8]);
      g_packets++;

      // Tid och datum: hhmmss ur falt 1, ddmmyy ur falt 9.
      const long hms = atol(f[1]);
      const long dmy = atol(f[9]);
      const uint16_t yy = 2000 + (uint16_t)(dmy % 100);
      if (dmy > 0 && yy >= 2024) {
        g_year = yy;
        g_month = (uint8_t)((dmy / 100) % 100);
        g_day = (uint8_t)(dmy / 10000);
        g_hour = (uint8_t)(hms / 10000);
        g_minute = (uint8_t)((hms / 100) % 100);
        g_second = (uint8_t)(hms % 100);
        g_timeValid = true;
        g_epochUtc = (uint32_t)epochFromUtc(g_year, g_month, g_day, g_hour,
                                            g_minute, g_second);
        g_epochAtMs = now;
      }
    }
    composeFix(now);
  } else if (strncmp(typ, "GGA", 3) == 0 && n >= 10) {
    g_quality = (uint8_t)atol(f[6]);
    g_fix.sats = (uint8_t)atol(f[7]);
    g_hdop = (float)atof(f[8]);
    g_fix.altM = (float)atof(f[9]);
    if (g_quality > 0 && !g_hadFix) {
      g_hadFix = true;
      logg::event("gps: forsta fixet efter %lu s (%u satelliter)",
                  (unsigned long)(now / 1000), (unsigned)g_fix.sats);
    }
    composeFix(now);
  } else if (strncmp(typ, "GSA", 3) == 0 && n >= 3) {
    g_gsaFix = (uint8_t)atol(f[2]);
    composeFix(now);
  } else if (strncmp(typ, "GSV", 3) == 0 && n >= 4) {
    // Satelliter i sikte. GGA:s siffra ar de som ANVANDS i losningen och
    // star pa noll anda tills fixet bildas - under sokningen ar det den har
    // som visar att nagot hander. Den lanas ut till visningen sa lange
    // inget fix finns; fortroendebedomningen ror den aldrig, eftersom den
    // kraver fix och da ager GGA siffran igen.
    g_inView = (uint8_t)atol(f[3]);
    if (g_quality == 0) g_fix.sats = g_inView;
  }
}

bool begin() {
  // Uartens port oppnas alltid; "narvaro" ar nar forsta hela meningen med
  // riktig checksumma kommit - en modul kan inte knackas pa som pa i2c.
  Serial1.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
  g_present = false;
  return true;
}

bool present() { return g_present; }

void poll() {
  const uint32_t now = millis();
  if (now - g_lastPollMs >= 1000) {
    g_lastPollMs = now;
    g_polls++;
  }

  // Tom bufferten varje varv - 9600 baud ar under en byte per millisekund,
  // sa det har ar en handfull tecken per anrop, aldrig en storstadning.
  while (Serial1.available()) {
    const char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (g_len > 6) {
        g_line[g_len] = '\0';
        handleLine(g_line, now);
      }
      g_len = 0;
    } else if (g_len < sizeof(g_line) - 1) {
      g_line[g_len++] = c;
    } else {
      g_len = 0;  // for lang rad ar skrap - borja om
    }
  }
}

#else  // i2c-vagen: u-blox M8+ (NEO-M9N pa qwiic)

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

#endif  // GNSS_UART

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
