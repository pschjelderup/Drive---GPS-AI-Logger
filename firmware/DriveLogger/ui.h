// Allt som ritas pa skarmen.
//
// Skarmen sitter i en bil och ska ga att uppfatta i ogonvran. Darav
// upplagget: farten sa stor att den lases utan att fokusera, farg som betyder
// samma sak overallt, och varningar som lagger sig over bilden i stallet for
// att tranga undan den.

#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "cams.h"
#include "eco.h"
#include "trip.h"

// Anvandarens installningar. Sparas i kortets flashminne och overlever
// stromavbrott och omflashning.
struct AppSettings {
  uint8_t screenIdx;
  uint8_t soundOn;

  // Ecodrive-granserna. Egen meny, atkomlig fran ecodrive-skarmen, sa att de
  // gar att prova ut medan bilen rullar.
  uint8_t ecoSoftIdx;
  uint8_t ecoHardIdx;
  uint8_t ecoBubbleIdx;
  uint8_t ecoPenaltyIdx;
  uint8_t ecoWindowIdx;
};

enum Screen {
  SCREEN_MAIN,
  SCREEN_PURPOSE,    // fragan efter en avslutad resa
  SCREEN_CUSTOMER,   // kundlistan, synkad fran webben
  SCREEN_ECO,
  SCREEN_ECO_LIMITS,
  SCREEN_MENU,
  SCREEN_RECOVERED,  // beskedet om en resa som stromavbrottet tog
};

// En ruta pa skarmen. Anvands bade for att rita knappar och for att avgora var
// anvandaren tryckte.
struct Rect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

namespace ui {

// ---- huvudskarmen
extern const Rect kBtnPrivat;
extern const Rect kBtnForetag;
extern const Rect kBtnDiffust;
extern const Rect kBtnTripAction;
extern const Rect kBtnEco;
extern const Rect kBtnMiddle;  // DELA under resa, KUNDER annars
extern const Rect kBtnMenu;
extern const Rect kBtnSoundToggle;  // ljudikonen i statusraden

// ---- fragan efter resan
extern const Rect kBtnAskPrivat;
extern const Rect kBtnAskForetag;
extern const Rect kBtnAskDiffust;

// ---- kundlistan
Rect customerRow(uint8_t row);
extern const Rect kBtnCustomerNone;
extern const Rect kBtnCustomerPrev;
extern const Rect kBtnCustomerNext;

// ---- ecodrive
extern const Rect kBtnEcoReset;
extern const Rect kBtnEcoLimits;
extern const Rect kBtnEcoBack;
Rect ecoMinus(uint8_t row);
Rect ecoPlus(uint8_t row);

// ---- menyn
Rect menuMinus(uint8_t row);
Rect menuPlus(uint8_t row);
extern const Rect kBtnTare;
extern const Rect kBtnBack;

void begin(Arduino_Canvas *canvas);

void drawMain(const TripStatus &t, const CamWarning &w, uint8_t limitKmh,
              float speedKmh, const AppSettings &cfg);
void drawPurposeAsk(const TripStatus &t, uint32_t secondsLeft);
void drawCustomers(const char *const *names, uint8_t count, uint8_t page,
                   uint8_t pages);
void drawEco(const EcoStatus &e);
void drawEcoLimits(const AppSettings &cfg, const EcoStatus &e);
void drawMenu(const AppSettings &cfg, const char *version);
void drawRecovered(const RecoveredTrip &r);

// Meddelande over hela skarmen, for fel som anvandaren maste atgarda.
void drawMessage(const char *title, const char *line1, const char *line2);

}  // namespace ui
