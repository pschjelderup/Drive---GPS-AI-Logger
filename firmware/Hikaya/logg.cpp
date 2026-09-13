#include "logg.h"

#include <Arduino.h>
#include <stdarg.h>

#include "config.h"
#include "sensors.h"
#include "storage.h"

namespace {

// Raderna gar via en ko till en egen skrivtrad. Den som loggar - skarmen,
// avlasningstraden, synken - ror alltsa aldrig kortet sjalv och kan aldrig
// fastna bakom nagon annans filskrivning. Forut skrevs raden direkt, under
// ett las: skarmtraden kunde da sta och vanta pa kortet mitt i ett varv,
// och det syntes som ett hack precis nar loggen hade nagot att saga.
const size_t kLineMax = 200;
const UBaseType_t kDepth = 32;

QueueHandle_t g_queue = nullptr;
StaticQueue_t g_queueCtl;
bool g_up = false;
volatile unsigned long g_lost = 0;

void writeLine(const char *line) {
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
    if (f.print(line) == 0) g_lost++;
    f.close();
  } else {
    g_lost++;
  }
}

void writerTask(void *) {
  char line[kLineMax];
  for (;;) {
    if (xQueueReceive(g_queue, line, portMAX_DELAY) == pdTRUE) {
      writeLine(line);
    }
  }
}

}  // namespace

namespace logg {

void begin() {
  if (g_up) return;
  // Kon ligger i psram - den ar bara byte som kopieras. Utan psram far
  // internminnet ta det; det ar sex kilobyte.
  uint8_t *store = (uint8_t *)heap_caps_malloc(kDepth * kLineMax, MALLOC_CAP_SPIRAM);
  if (!store) store = (uint8_t *)malloc(kDepth * kLineMax);
  if (!store) return;
  g_queue = xQueueCreateStatic(kDepth, kLineMax, store, &g_queueCtl);
  if (!g_queue) return;
  // Lagsta prioritet pa karna 0: raderna har tid pa sig, och de ska aldrig
  // ta kortet fran resan.
  if (xTaskCreatePinnedToCore(writerTask, "logg", 4096, nullptr, 1, nullptr, 0) !=
      pdPASS) {
    return;
  }
  g_up = true;
}

void event(const char *fmt, ...) {
  char text[kLineMax - 32];
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
  char line[kLineMax];
  snprintf(line, sizeof(line), "%s %s\n", stamp, text);
  // Full ko betyder att kortet inte hinner med - raden offras hellre an
  // att den som loggar far vanta. Att det hande syns i siffran.
  if (xQueueSend(g_queue, line, 0) != pdTRUE) g_lost++;
}

unsigned long lostLines() { return g_lost; }

}  // namespace logg
