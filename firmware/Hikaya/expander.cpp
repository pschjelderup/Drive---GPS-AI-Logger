#include "expander.h"

#if defined(BOARD_LCD35)

#include <Wire.h>

namespace {

// TCA9554: register 0x01 ar utgangslagen, 0x03 ar riktningarna (1 = ingang).
const uint8_t kRegOutput = 0x01;
const uint8_t kRegConfig = 0x03;

// Utgangar: bit 0 (kamerans PWDN), 1 (skarmens RST), 3 (kortets CS),
// 7 (forstarkarens CTRL). Resten lamnas som ingangar.
const uint8_t kConfig = 0b01110100;

// Grundlagen: kameran avstangd (PWDN hog), skarmen ur reset (RST hog),
// kortet valt (CS lag), forstarkaren tyst (CTRL lag).
const uint8_t kIdle = 0b00000011;

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
  const bool ok = writeReg(kRegOutput, g_output) &&
                  writeReg(kRegConfig, kConfig);
  if (!ok) Serial.println("expander: TCA9554 svarar inte pa 0x20");
  return ok;
}

void lcdReset() {
  writeReg(kRegOutput, g_output & ~(1 << EXIO_LCD_RST));
  delay(20);
  writeReg(kRegOutput, g_output | (1 << EXIO_LCD_RST));
  delay(120);
}

}  // namespace expander

#endif  // BOARD_LCD35
