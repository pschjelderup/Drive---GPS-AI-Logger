// Strom via AXP2101-PMIC:en pa LCD 3.5-kortet. Panelens matning ligger
// pa LDO-skenor som ar av tills nagon slar pa dem - factory-firmwaren
// gor exakt det har, och utan det ar skarmen svart.
#pragma once

#include "config.h"

#if defined(BOARD_LCD35)
namespace axp {
// Slar pa LDO-skenorna (3.3 V). Kraver att Wire ar igang. Returnerar
// false om PMIC:en inte svarar - da fortsatter vi anda, kortet kan
// vara en revision utan PMIC-beroende.
bool begin();
}  // namespace axp
#endif
