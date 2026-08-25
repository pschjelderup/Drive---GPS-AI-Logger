// ST7796-panelen pa LCD 3.5-kortet, driven via esp_lcd - samma stack,
// klocka och initsekvens som Waveshares factory-firmware. Bade
// Arduino_GFX:s ESP32SPI- och HWSPI-klasser lamnar den har panel-
// revisionen svart; esp_lcd ar bevisat pa hardvaran.
#pragma once

#include "config.h"

#if defined(BOARD_LCD35)
#include <stdint.h>

namespace panel35 {
// Startar SPI-bussen, skickar panelens initsekvens och slar pa displayen.
bool begin();
// Ritar ett fonster med RGB565-pixlar i LVGL:s ordning (little endian).
// Bufferten byteswappas pa plats och ar ateranvandbar nar anropet
// atervander.
void blit(int x, int y, int w, int h, uint16_t *px);
// Fyller hela skarmen med en farg (RGB565). Bring-up och slacklage.
void fill(uint16_t color565);
// Displayens pa/av-kommando (0x29/0x28). Bakgrundsljuset styrs separat.
void displayOn(bool on);
}  // namespace panel35
#endif
