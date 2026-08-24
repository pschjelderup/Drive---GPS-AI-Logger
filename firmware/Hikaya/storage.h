// Minneskortet, bakom ett gemensamt namn. Bada korten har kortplatsen pa
// SDMMC-bussen i 1-bitslage - bara pinnarna skiljer, och 3.5-kortet later
// dessutom D3/CS ga via io-expandern (hog = SD-lage). All filkod skriver
// SDCARD; monteringen bor i sensors.cpp.
#pragma once

#include "config.h"

#include <SD_MMC.h>
#define SDCARD SD_MMC
