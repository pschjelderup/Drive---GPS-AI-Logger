// Ljudet.
//
// Kortet har ingen hogtalare och ingen ljudkrets, sa varningen kommer ur en
// piezo pa UART-portens TXD-stift. Se kommentaren vid PIN_BUZZER i config.h.
//
// Allt ligger bakom det har granssnittet med flit. En piezo kan bara pipa, men
// en I2S-forstarkare med inspelade ord skulle sagas in pa samma stalle utan att
// nagon varningslogik behovde skrivas om: den sager *vad* som hant, inte hur det
// ska lata.

#pragma once

#include <Arduino.h>

enum SoundCue : uint8_t {
  CUE_NONE = 0,
  CUE_CAM_FAR,      // fartkamera langt fram - hinner slappa gasen
  CUE_CAM_MID,      // narmar sig
  CUE_CAM_NEAR,     // nu
  CUE_OVER_LIMIT,   // over skyltad hastighet
  CUE_TRIP_START,
  CUE_TRIP_END,
  CUE_TAP,          // kvitto pa ett knapptryck
  CUE_ERROR,
};

namespace sound {

void begin();

// Ljudet gar att stanga av. En reselogg som tjuter nar man kor med sovande barn
// i baksatet blir en reselogg man drar ur.
void setEnabled(bool on);
bool enabled();

// Spelar upp ett besked. Ett pagaende besked med hogre angelagenhet avbryts
// aldrig av ett lagre: den nara kameravarningen far inte tystas av ett
// knappkvitto.
void play(SoundCue cue);

// Anropas ofta fran huvudloopen. Det ar den som stegar fram monstret, sa att
// inget anrop nagonsin behover vanta.
void tick();

void silence();

}  // namespace sound
