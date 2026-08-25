#include "expander.h"

#if defined(BOARD_LCD35)

#include <Wire.h>

namespace {

// TCA9554: register 0x01 ar utgangslagen, 0x03 ar riktningarna (1 = ingang).
const uint8_t kRegOutput = 0x01;
const uint8_t kRegConfig = 0x03;

// BARA skarmens reset (bit 1) gors till utgang - precis som Waveshares
// demos och de tester som faktiskt tande panelen. Kamerans PWDN, kortets
// CS och forstarkaren lamnas som ingangar med kortets egna motstand;
// att driva dem har visat sig onodigt och var en av avvikelserna mot
// den fungerande demokoden.
const uint8_t kConfig = 0b11111101;

// Grundlaget: skarmen ur reset (RST hog).
const uint8_t kIdle = 0b00000010;

uint8_t g_output = kIdle;

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(EXPANDER_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

}  // namespace

namespace expander {

bool begin() {
  g_output = kIdle;
  const bool ok = writeReg(kRegConfig, kConfig) &&
                  writeReg(kRegOutput, g_output);
  if (!ok) Serial.println("expander: TCA9554 svarar inte pa 0x20");
  return ok;
}

void lcdReset() {
  // Samma monster och tider som Waveshares demokod: hog, lag, hog med
  // 10/10/200 ms. ST7796:an ar kinkig med for kort uppvakningspaus.
  writeReg(kRegOutput, g_output | (1 << EXIO_LCD_RST));
  delay(10);
  writeReg(kRegOutput, g_output & ~(1 << EXIO_LCD_RST));
  delay(10);
  writeReg(kRegOutput, g_output | (1 << EXIO_LCD_RST));
  delay(200);
}

}  // namespace expander

#endif  // BOARD_LCD35
