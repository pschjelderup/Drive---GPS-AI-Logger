// Gluet mellan LVGL-skarmarna och hardvaran: display, pekskarm, modellfyllning
// och skarmslackning. Skarmarna sjalva bor i gui_screens och vet ingenting om
// nagot av det har.

#pragma once

#include <Arduino_GFX_Library.h>
#include <TouchDrv.hpp>

// Anvandarens installningar. Sparas i flashminnet och overlever stromavbrott
// och omflashning.
struct AppSettings {
  uint8_t screenIdx;
  uint8_t soundOn;

  // Ecodrive-granserna. Stalls inte pa enheten langre - vardena bor kvar och
  // tillampas, men skruvandet flyttade till webben nar skarmen gjordes om.
  uint8_t ecoSoftIdx;
  uint8_t ecoHardIdx;
  uint8_t ecoBubbleIdx;
  uint8_t ecoPenaltyIdx;
  uint8_t ecoWindowIdx;

  // Autosynk av/pa - avslagen synkar molnet bara pa knappen.
  uint8_t autoSync;
};

namespace gui {

// Startar LVGL mot panelen och pekskarmen och bygger alla skarmar.
// Installningarna ags av huvudskissen; gui ropar pa spara/tillampa nar
// anvandaren andrar nagot.
void begin(Arduino_RM690B0 *panel, TouchDrvFT6X36 *touch, bool touchOk,
           AppSettings *cfg, void (*saveSettings)(), void (*applySettings)());

// Anropas fran huvudloopen sa ofta det gar. Skoter LVGL:s tidshantering,
// modellfyllningen, fragan efter avslutad resa och skarmslackningen.
void tick();

// Skarmen av eller pa, for knappen pa kortet.
void setDisplayOn(bool on);
bool displayOn();

}  // namespace gui
