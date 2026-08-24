// TCA9554-io-expandern pa LCD 3.5-kortet. Den ager tre signaler vi behover:
// skarmens reset, kortplatsens CS (halls fast lag - kortet ar ensamt pa sin
// spi-buss) och pekskarmens avbrott (som vi anda pollar). Kameran stangs av
// och forstarkaren halls tyst, sa de inte drar strom i onodan.
//
// Pa AMOLED 2.41-kortet finns samma krets men behover inte roras - dar ar
// filen tom.
#pragma once

#include "config.h"

namespace expander {

// Satter riktningar och grundlagen, och slapper skarmens reset.
// Kraver att Wire redan ar igang.
bool begin();

// Pulsar skarmens reset: lag, paus, hog, paus. Anropas fore panel->begin().
void lcdReset();

}  // namespace expander
