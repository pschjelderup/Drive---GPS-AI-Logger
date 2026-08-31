#include "logg.h"

#include <Arduino.h>
#include <stdarg.h>

#include "config.h"
#include "sensors.h"
#include "storage.h"

namespace {

// Bade huvudloopen och synktraden loggar, sa skrivningen ar en atgard i
// taget. Muteten skyddar filen, inte serieporten - den far tala i mun.
SemaphoreHandle_t g_mutex = nullptr;
bool g_up = false;
unsigned long g_lost = 0;

}  // namespace

namespace logg {

void begin() {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  g_up = true;
}

void event(const char *fmt, ...) {
  char text[180];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);

  // Klocktid nar den finns - loggen laddas ofta upp langt efter handelsen,
  // och serverns mottagningstid sager da ingenting. Fore forsta gps-fixet
  // far uppetiden duga.
  char stamp[24];
  const uint32_t t = sensors::unixUtc();
  if (t) {
    sensors::isoUtc(t, stamp, sizeof(stamp));
  } else {
    snprintf(stamp, sizeof(stamp), "+%lus", (unsigned long)(millis() / 1000));
  }

  Serial.printf("logg: %s %s\n", stamp, text);

  if (!g_up) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  File f = SDCARD.open(LOG_FILE, FILE_APPEND);
  if (f && f.size() > LOG_MAX_BYTES) {
    // Taket ar generost mot vardagsvolymen - hit nar bara en enhet som inte
    // synkat pa mycket lange. Da ar de aldsta raderna ocksa de minst varda.
    f.close();
    SDCARD.remove(LOG_FILE);
    f = SDCARD.open(LOG_FILE, FILE_WRITE);
  }
  if (f) {
    // Ett fullt kort tar emot noll tecken utan att saga ifran. Raden ar da
    // borta for alltid - men att den fanns ar i sig diagnosen, sa den raknas.
    if (f.printf("%s %s\n", stamp, text) == 0) g_lost++;
    f.close();
  } else {
    g_lost++;
  }
  xSemaphoreGive(g_mutex);
}

unsigned long lostLines() { return g_lost; }

}  // namespace logg
