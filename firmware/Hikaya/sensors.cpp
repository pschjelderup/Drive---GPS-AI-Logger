#include "sensors.h"

#include "storage.h"
#include <SensorPCF85063.hpp>
#include <SensorQMI8658.hpp>
#include <Wire.h>
#include <math.h>
#include <time.h>

#include "cams.h"
#include "config.h"
#include "eco.h"
#include "gnss.h"
#include "trip.h"

namespace {

SensorQMI8658 imu;
SensorPCF85063 rtc;

bool g_imuOk = false;
bool g_rtcOk = false;
bool g_sdOk = false;

// Skyddar det som delas mellan avlasningstraden och skarmen.
SemaphoreHandle_t g_mutex = nullptr;

Sample g_latest = {};
uint64_t g_freeBytes = 0;
uint64_t g_cardBytes = 0;

uint32_t g_lastClockSyncMs = 0;
bool g_clockSynced = false;

void lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

// Sekunder sedan 1970 ur ett datum i UTC. Formeln raknar dagar sedan eran
// direkt ur kalenderns egna regler, sa den behover ingen tidszon och ingen
// tabell - och ger samma svar oavsett hur kortet ar installt.
int64_t unixFromUtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                    uint8_t minute, uint8_t second) {
  // Aret borjar i mars, sa att skottdagen hamnar sist och slutar vara ett
  // sarfall i rakningen.
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

SensorQMI8658::AccelRange accelRangeEnum(uint8_t g) {
  switch (g) {
    case 2: return SensorQMI8658::ACC_RANGE_2G;
    case 4: return SensorQMI8658::ACC_RANGE_4G;
    case 16: return SensorQMI8658::ACC_RANGE_16G;
    default: return SensorQMI8658::ACC_RANGE_8G;
  }
}

SensorQMI8658::GyroRange gyroRangeEnum(uint16_t dps) {
  switch (dps) {
    case 64: return SensorQMI8658::GYR_RANGE_64DPS;
    case 128: return SensorQMI8658::GYR_RANGE_128DPS;
    case 256: return SensorQMI8658::GYR_RANGE_256DPS;
    case 1024: return SensorQMI8658::GYR_RANGE_1024DPS;
    default: return SensorQMI8658::GYR_RANGE_512DPS;
  }
}

// Sensorn far ga snabbare an vi lasar av den. Det ger ett slags medelvarde i
// sensorns eget filter i stallet for att vi rakar pricka en enstaka
// mattidpunkt.
void applySensorConfig() {
  imu.configAccelerometer(accelRangeEnum(IMU_ACCEL_RANGE_G),
                          SensorQMI8658::ACC_ODR_250Hz,
                          SensorQMI8658::LPF_MODE_0);
  imu.configGyroscope(gyroRangeEnum(IMU_GYRO_RANGE_DPS),
                      SensorQMI8658::GYR_ODR_224_2Hz,
                      SensorQMI8658::LPF_MODE_0);
  imu.enableAccelerometer();
  imu.enableGyroscope();
}

void refreshFreeSpace() {
  if (!g_sdOk) {
    lock();
    g_freeBytes = 0;
    g_cardBytes = 0;
    unlock();
    return;
  }
  const uint64_t total = SDCARD.totalBytes();
  const uint64_t used = SDCARD.usedBytes();
  lock();
  g_cardBytes = total;
  g_freeBytes = (total > used) ? (total - used) : 0;
  unlock();
}

// Satellittid ar exakt till sekunden. Klockan stalls sa fort ett fix finns och
// sedan en gang i timmen for att motverka drift.
//
// Till skillnad fran Gmate lagras UTC rakt av. Offseten i config.h anvands
// bara nar tiden ska visas, sa att gpx-filerna ar riktiga aret om utan att
// nagon behover komma ihag att byta om till sommartid.
void syncClockFromGnss() {
  if (!g_rtcOk || !gnss::timeValid()) return;

  const uint32_t now = millis();
  if (g_clockSynced && (now - g_lastClockSyncMs) < 3600000UL) return;

  uint16_t year;
  uint8_t month, day, hour, minute, second;
  gnss::utc(year, month, day, hour, minute, second);
  if (year < 2024) return;

  rtc.setDateTime(RTC_DateTime(year, month, day, hour, minute, second));

  // Kretsens oscillator har visat sig kunna sta stilla: en hel resa fick
  // samma tidsstampel pa varenda punkt, och tiden rorde sig bara nar den har
  // synken skrev om den. Darfor kontrolleras och startas oscillatorn vid
  // varje skrivning. Kostnaden ar en registerlasning i timmen.
  if (!rtc.isRunning()) rtc.start();

  g_lastClockSyncMs = now;
  g_clockSynced = true;
}

void samplerTask(void *) {
  TickType_t last = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000 / IMU_SAMPLE_HZ);
  uint32_t ticks = 0;

  for (;;) {
    gnss::poll();
    syncClockFromGnss();

    Sample s = {};
    if (g_imuOk) {
      imu.getAccelerometer(s.ax, s.ay, s.az);
      imu.getGyroscope(s.gx, s.gy, s.gz);
      s.temp = imu.getTemperature_C();
      s.atot = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    }

    lock();
    g_latest = s;
    unlock();

    // Ecodrive raknar hela tiden, aven nar skarmen visar nagot annat eller ar
    // slackt. Annars skulle poangen borja om varje gang man tittade pa den.
    eco::tick(s);

    // Resan agar minneskortet och skrivs harifran, fran en enda trad.
    trip::tick();

    // Kameravarningen letar i sin lista en gang i sekunden - samma takt som
    // mottagaren levererar positioner i.
    cams::tick();

    // Ledigt utrymme ar dyrt att rakna ut, sa det gors sallan.
    if (++ticks >= (uint32_t)IMU_SAMPLE_HZ * 30) {
      ticks = 0;
      refreshFreeSpace();
    }

    vTaskDelayUntil(&last, period ? period : 1);
  }
}

}  // namespace

namespace sensors {

bool begin() {
  g_mutex = xSemaphoreCreateMutex();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // Sensorn sitter pa 0x6B pa det har kortet, men vissa exemplar svarar pa
  // 0x6A. Prova bada.
  g_imuOk = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
  if (!g_imuOk) {
    g_imuOk = imu.begin(Wire, QMI8658_H_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
  }
  if (g_imuOk) applySensorConfig();

  g_rtcOk = rtc.begin(Wire, PIN_I2C_SDA, PIN_I2C_SCL);
  if (g_rtcOk) {
    // Oscillatorn kan sta stilla - antingen for att STOP-biten fastnat eller
    // for att kretsen tappat spanning. En stillastaende klocka ser giltig ut
    // vid en enda avlasning, sa den maste fragas uttryckligen.
    if (!rtc.isRunning()) rtc.start();

    // Ett kort som aldrig fatt tiden stalld svarar med ett orimligt artal. Da
    // satter vi klockan till tidpunkten firmware byggdes, vilket ligger nara
    // nog for att resorna ska ga att sortera innan GPS:en hunnit fa fix.
    RTC_DateTime now = rtc.getDateTime();
    if (now.getYear() < 2024 || now.getYear() > 2099) {
      rtc.setDateTime(RTC_DateTime(__DATE__, __TIME__));
    }
  }

  gnss::begin();
  eco::begin();

  remount();

  // Resan och kamerorna maste lasa fran kortet, sa de startas efter
  // monteringen. Trip::begin ar ocksa det som upptacker en resa som aldrig
  // blev avslutad och lakar den.
  trip::begin();
  cams::begin();

  // Riklig stack: raderna formateras med flyttal, vilket kraver mer utrymme an
  // man forst tror.
  xTaskCreatePinnedToCore(samplerTask, "sampler", 12288, nullptr, 5, nullptr, 0);
  return g_imuOk;
}

bool imuOk() { return g_imuOk; }
bool sdMounted() { return g_sdOk; }

bool remount() {
  if (g_sdOk) SDCARD.end();
  g_sdOk = false;

#if defined(BOARD_LCD35)
  // Kortplatsen sitter pa SPI. Riktiga CS-signalen halls permanent lag av
  // io-expandern (satt i expander::begin) - kortet ar ensamt pa bussen, sa
  // det far vara valt jamt. Biblioteket kraver anda en CS-pinne att vifta
  // med; det far kameraklockans, som gar till en tom kontakt.
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS_DUMMY);
  if (SDCARD.begin(PIN_SD_CS_DUMMY, SPI, 25000000, "/sdcard")) {
    g_sdOk = (SDCARD.cardType() != CARD_NONE);
  }
#else
  SDCARD.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  // true = enbitslage, vilket ar sa kortplatsen ar kopplad pa det har kortet.
  if (SDCARD.begin("/sdcard", true, false)) {
    g_sdOk = (SDCARD.cardType() != CARD_NONE);
  }
#endif
  refreshFreeSpace();
  return g_sdOk;
}

Sample latest() {
  lock();
  Sample s = g_latest;
  unlock();
  return s;
}

uint64_t freeBytes() {
  lock();
  const uint64_t v = g_freeBytes;
  unlock();
  return v;
}

uint64_t cardBytes() {
  lock();
  const uint64_t v = g_cardBytes;
  unlock();
  return v;
}

uint32_t unixUtc() {
  // Forsta valet ar satellittiden, forankrad i millis(). Den tickar av
  // processorns kristall mellan tidspaketen och kan inte sta stilla - det
  // kan rtc-kretsen, vilket en verklig resa bevisade: varenda punkt fick
  // samma tidsstampel tills timsynken rackte fram en ny. Rtc:n ar kvar som
  // reserv for tiden mellan start och forsta fix.
  const uint32_t epoch = gnss::epochUtc();
  if (epoch != 0) return epoch + gnss::epochAgeMs() / 1000;

  if (!g_rtcOk) return 0;
  RTC_DateTime dt = rtc.getDateTime();
  if (dt.getYear() < 2024 || dt.getYear() > 2099) return 0;

  // Klockan star i UTC, sa tiden ska tolkas som UTC. mktime gar inte att
  // anvanda: den drar av maskinens tidszon, och den ar inte satt pa ett kort
  // som inte vet var i varlden det ligger. timegm, som hade gjort ratt, finns
  // inte i libc pa esp32. Rakningen nedan ar den vanliga
  // dagar-sedan-eran-formeln och beror inte pa nagon installning alls.
  return (uint32_t)unixFromUtc(dt.getYear(), dt.getMonth(), dt.getDay(),
                               dt.getHour(), dt.getMinute(), dt.getSecond());
}

bool clockSynced() { return g_clockSynced; }

void isoUtc(uint32_t t, char *out, size_t len) {
  if (t == 0) {
    if (len) out[0] = '\0';
    return;
  }
  const time_t stamp = (time_t)t;
  struct tm tm;
  gmtime_r(&stamp, &tm);
  snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void localStamp(uint32_t t, char *out, size_t len) {
  if (t == 0) {
    if (len) out[0] = '\0';
    return;
  }
  const time_t stamp = (time_t)t + (time_t)GNSS_UTC_OFFSET_MINUTES * 60;
  struct tm tm;
  gmtime_r(&stamp, &tm);
  snprintf(out, len, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

void localClock(uint32_t t, char *out, size_t len) {
  if (t == 0) {
    snprintf(out, len, "--:--");
    return;
  }
  const time_t stamp = (time_t)t + (time_t)GNSS_UTC_OFFSET_MINUTES * 60;
  struct tm tm;
  gmtime_r(&stamp, &tm);
  snprintf(out, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

}  // namespace sensors
