// Minneskortet, bakom ett gemensamt namn. AMOLED 2.41-kortet har sin
// kortplats pa SDMMC-bussen; LCD 3.5-kortet har sin pa vanlig SPI. Bada
// exponerar samma FS-granssnitt, sa all filkod skriver SDCARD och slipper
// veta vilken buss som bar den. Monteringen - det enda som faktiskt skiljer -
// bor i sensors.cpp.
#pragma once

#include "config.h"

#if defined(BOARD_LCD35)
#include <SD.h>
#define SDCARD SD
#else
#include <SD_MMC.h>
#define SDCARD SD_MMC
#endif
