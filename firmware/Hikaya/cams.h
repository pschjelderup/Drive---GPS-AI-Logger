// Fartkameror och skyltade hastigheter.
//
// Bada listorna kommer fran Trafikverkets oppna data och laggs pa minneskortet
// av synken. Kamerorna ar fa nog att bo i minnet; hastighetspunkterna ar for
// manga och lases direkt fran kortet med binarsokning. Saknas nagon av filerna
// fungerar allt annat som vanligt - skarmen sager rakt ut att den inte kan
// varna, i stallet for att tiga och lata som om vagen var fri.
//
// Filformaten ar beskrivna i tools/hamta-trafikverket.py, som ar det som skapar
// dem.

#pragma once

#include <Arduino.h>

struct CamWarning {
  bool active;
  uint32_t distanceM;
  uint8_t limitKmh;     // kamerans egen skyltade hastighet, 0 om okand
  bool averageSpeed;    // sant for ATK-stracka, dar snittfarten mats
};

namespace cams {

// Lases in fran kortet. Kraver att kortet ar monterat.
void begin();

// Forsoker lasa in listorna igen, t.ex. efter en synk eller efter att ett kort
// stoppats i.
void reload();

// Anropas ofta. Gor av med arbete hogst en gang i sekunden, vilket ar samma
// takt som mottagaren levererar positioner i.
void tick();

bool loaded();
uint32_t count();
bool limitsLoaded();

// Skyltad hastighet dar bilen ar, i km/h. Noll betyder att vi inte vet - och da
// visar skarmen inte heller nagon over- eller underhastighet. En gissad
// hastighetsgrans ar varre an ingen.
uint8_t currentLimitKmh();

CamWarning warning();

// Handslag for filbyte. Uppladdningen sker i skarmtraden, men filerna ags av
// avlasningstraden - hastighetsfilen halls till och med oppen dar. beginUpdate
// ber traden slappa filerna och vantar pa kvitto; endUpdate later den lasa om.
// Utan handslaget skulle sokningen kunna sta mitt i en fil som byts ut.
void beginUpdate();
void endUpdate();

}  // namespace cams
