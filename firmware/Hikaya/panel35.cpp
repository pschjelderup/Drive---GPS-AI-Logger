#include "panel35.h"

#if defined(BOARD_LCD35)

#include <Arduino.h>

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

esp_lcd_panel_io_handle_t g_io = nullptr;
SemaphoreHandle_t g_done = nullptr;

// tx_color ar asynkront (DMA); semaforen ger besked nar bufferten ar
// fri att ateranvandas. En overforing i taget racker gott har.
bool IRAM_ATTR colorDone(esp_lcd_panel_io_handle_t,
                         esp_lcd_panel_io_event_data_t *, void *) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(g_done, &hp);
  return hp == pdTRUE;
}

void cmd(uint8_t c, const uint8_t *d, size_t n, uint32_t ms) {
  esp_lcd_panel_io_tx_param(g_io, c, d, n);
  if (ms) delay(ms);
}

void window(int x, int y, int w, int h) {
  const int x2 = x + w - 1, y2 = y + h - 1;
  const uint8_t ca[] = {(uint8_t)(x >> 8), (uint8_t)x,
                        (uint8_t)(x2 >> 8), (uint8_t)x2};
  const uint8_t ra[] = {(uint8_t)(y >> 8), (uint8_t)y,
                        (uint8_t)(y2 >> 8), (uint8_t)y2};
  esp_lcd_panel_io_tx_param(g_io, 0x2A, ca, 4);
  esp_lcd_panel_io_tx_param(g_io, 0x2B, ra, 4);
}

void send(const void *px, size_t bytes) {
  esp_lcd_panel_io_tx_color(g_io, 0x2C, px, bytes);
  xSemaphoreTake(g_done, pdMS_TO_TICKS(1000));
}

}  // namespace

namespace panel35 {

bool begin() {
  spi_bus_config_t bus = {};
  bus.sclk_io_num = PIN_LCD_SCK;
  bus.mosi_io_num = PIN_LCD_MOSI;
  bus.miso_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  // LVGL:s ritbuffert ar 60 rader; fill() gar i 40-raderssteg.
  bus.max_transfer_sz = SCREEN_W * 24 * 2 + 16;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    return false;
  }

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = -1;  // CS ar jordstrappad pa kortet
  io.dc_gpio_num = PIN_LCD_DC;
  io.spi_mode = 0;
  io.pclk_hz = 80 * 1000 * 1000;  // samma som factory
  io.trans_queue_depth = 4;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  io.on_color_trans_done = colorDone;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io,
                               &g_io) != ESP_OK) {
    return false;
  }
  g_done = xSemaphoreCreateBinary();

  // Waveshares egen initsekvens ur factory-firmwarens ST7796-komponent,
  // foljd av BGR-fargordning, invertering (ips-panel) och display pa -
  // exakt som factory-porten gor.
  cmd(0x01, nullptr, 0, 120);  // mjukreset
  cmd(0x11, nullptr, 0, 120);  // vakna ur somn
  { const uint8_t d[] = {0x05}; cmd(0x3A, d, 1, 0); }   // 16 bitar/pixel
  { const uint8_t d[] = {0xC3}; cmd(0xF0, d, 1, 0); }   // las upp
  { const uint8_t d[] = {0x96}; cmd(0xF0, d, 1, 0); }
  { const uint8_t d[] = {0x01}; cmd(0xB4, d, 1, 0); }
  { const uint8_t d[] = {0xC6}; cmd(0xB7, d, 1, 0); }
  { const uint8_t d[] = {0x80, 0x45}; cmd(0xC0, d, 2, 0); }
  { const uint8_t d[] = {0x13}; cmd(0xC1, d, 1, 0); }
  { const uint8_t d[] = {0xA7}; cmd(0xC2, d, 1, 0); }
  { const uint8_t d[] = {0x0A}; cmd(0xC5, d, 1, 0); }
  { const uint8_t d[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33};
    cmd(0xE8, d, 8, 0); }
  { const uint8_t d[] = {0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30, 0x33,
                         0x47, 0x17, 0x13, 0x13, 0x2B, 0x31};
    cmd(0xE0, d, 14, 0); }
  { const uint8_t d[] = {0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2F, 0x33,
                         0x47, 0x38, 0x15, 0x16, 0x2C, 0x32};
    cmd(0xE1, d, 14, 0); }
  { const uint8_t d[] = {0x3C}; cmd(0xF0, d, 1, 0); }   // las igen
  { const uint8_t d[] = {0x69}; cmd(0xF0, d, 1, 120); }
  // MX + BGR: utan MX-biten ar bilden spegelvand langs X-axeln pa den
  // har panelen - texten lases baklanges. 0x48 = spegla X, BGR-ordning.
  { const uint8_t d[] = {0x48}; cmd(0x36, d, 1, 0); }
  cmd(0x21, nullptr, 0, 0);   // invertera - ips-glas
  cmd(0x29, nullptr, 0, 20);  // display pa
  return true;
}

void blit(int x, int y, int w, int h, uint16_t *px) {
  if (!g_io) return;
  // ST7796 vill ha MSB forst; LVGL levererar little endian. Swappen gors
  // pa plats - LVGL ar klar med innehallet nar flushen far bufferten.
  const int n = w * h;
  for (int i = 0; i < n; i++) px[i] = __builtin_bswap16(px[i]);
  window(x, y, w, h);
  send(px, (size_t)n * 2);
}

void fill(uint16_t color565) {
  if (!g_io) return;
  static uint16_t rad[SCREEN_W * 40];
  const uint16_t be = (uint16_t)((color565 << 8) | (color565 >> 8));
  for (int i = 0; i < SCREEN_W * 40; i++) rad[i] = be;
  for (int y = 0; y < SCREEN_H; y += 40) {
    window(0, y, SCREEN_W, 40);
    send(rad, sizeof(rad));
  }
}

void displayOn(bool on) {
  if (!g_io) return;
  cmd(on ? 0x29 : 0x28, nullptr, 0, 20);
}

}  // namespace panel35

#endif  // BOARD_LCD35
