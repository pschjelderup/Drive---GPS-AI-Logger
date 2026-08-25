#include "axp.h"

#if defined(BOARD_LCD35)

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

namespace axp {

bool begin() {
  static XPowersPMU pmu;
  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println("pmic: AXP2101 svarar inte pa 0x34");
    return false;
  }
  // Samma skenor och nivaer som Waveshares factory-firmware. ALDO- och
  // DLDO-utgangarna ar av efter kallstart, och nagon av dem bar panelen.
  pmu.setALDO1Voltage(3300);
  pmu.setALDO2Voltage(3300);
  pmu.setALDO3Voltage(3300);
  pmu.setALDO4Voltage(3300);
  pmu.setBLDO1Voltage(1500);
  pmu.setBLDO2Voltage(2800);
  pmu.setDLDO1Voltage(3300);
  pmu.setDLDO2Voltage(3300);
  pmu.enableALDO1();
  pmu.enableALDO2();
  pmu.enableALDO3();
  pmu.enableALDO4();
  pmu.enableBLDO1();
  pmu.enableBLDO2();
  pmu.enableDLDO1();
  pmu.enableDLDO2();
  Serial.println("pmic: skenorna pa");
  return true;
}

}  // namespace axp

#endif  // BOARD_LCD35
