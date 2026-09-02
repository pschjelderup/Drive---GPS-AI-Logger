// Resan: nar den borjar, nar den slutar, vart den gick och vad den var till.
//
// En resa startar och slutar av sig sjalv. Det ar inte en bekvamlighet utan
// hela poangen: en korjournal som bara innehaller de resor nagon kom ihag att
// trycka igang ar inte en korjournal.
//
// Tva saker skrivs till kortet for varje resa:
//
//  - en gpx-fil, sparet, som gar att lasa i vilken kartapp som helst
//  - en rad i resedagboken, med start, mal, stracka, syfte och kund
//
// Och en tredje sak medan resan pagar: tillstandsfilen. Den ar det som gor att
// ett stromavbrott inte kostar en resa. Se kommentaren i trip.cpp.

#pragma once

#include <Arduino.h>

// Vad resan var till. Ordningen ar densamma som knapparna pa skarmen.
enum TripPurpose : uint8_t {
  PURPOSE_UNSET = 0,   // annu inte markt - blir DIFFUST om ingen valjer
  PURPOSE_PRIVAT = 1,
  PURPOSE_FORETAG = 2,
  PURPOSE_DIFFUST = 3,
};

// Varfor resan tog slut. Star i dagboken, eftersom det ar skillnad pa en resa
// som avslutades vid en parkeringsplats och en som avslutades av att stromen
// forsvann.
enum TripEndReason : uint8_t {
  END_NONE = 0,
  END_AUTO = 1,        // bilen stod stilla lange nog
  END_MANUAL = 2,      // knappen pa skarmen
  END_POWERLOSS = 3,   // strommen forsvann - med tandningsstyrd strom det
                       // normala avslutet. Lakt vid nasta start
  END_NO_SPACE = 4,    // kortet blev fullt
};

struct TripStatus {
  bool active;
  bool waitingForFix;   // resan ar igang men mottagaren har ingen position

  uint32_t index;       // resans nummer, samma som i filnamnet
  TripPurpose purpose;
  char customer[40];    // tomt om ingen kund valts

  uint32_t startUtc;
  double startLat, startLon;

  double lat, lon;      // senast kanda position
  double distanceM;
  uint32_t points;
  uint32_t elapsedS;
  uint32_t stoppedS;    // hur lange bilen statt stilla just nu, 0 om den rullar

  float speedKmh;
  float maxSpeedKmh;
  uint32_t speedingS;   // sekunder over skyltad hastighet
  uint32_t movingS;     // sekunder i rullning - resans rullande tid sa har langt

  char fileName[48];

  // Sant nar en resa just avslutats och annu inte fatt sitt syfte. Skarmen
  // fragar da, och svaret hinner alltid fram: rader skrivs forst nar fragan ar
  // besvarad, och en obesvarad fraga blir DIFFUST av sig sjalv.
  bool awaitingPurpose;
  uint32_t awaitingIndex;
  double awaitingKm;
};

// En resa som strommen tog, lakt vid nasta start. Ingen egen skarm - med
// tandningsstyrd strom ar det har varje resa - men uppgifterna finns kvar for
// serieporten och felsokningen.
struct RecoveredTrip {
  bool valid;
  uint32_t index;
  uint32_t endUtc;
  double lat, lon;
  double distanceM;
};

namespace trip {

// Lases in fran kortet. Har upptacks en resa som aldrig blev avslutad, och har
// skrivs den fardigt med sista kanda position som mal.
void begin();

// Anropas fran avlasningstraden. All filhantering sker harifran.
void tick();

TripStatus status();

// Manuell override. Resan behover dem inte for att fungera, men den som vill
// dela en resa i tva - kundbesok pa vagen hem - ska kunna gora det.
// Bada lamnar bara en begaran: avlasningstraden utfor den inom ett varv
// (50 ms), och skarmen ser resultatet i nasta modell. Att vanta har - som
// forr, i upp till tva sekunder - fros touchen precis nar man tryckt.
// startManual ar sant nar begaran togs emot, falskt nar en resa redan pagar.
bool startManual();
void endManual();

// Avslutar den pagaende resan och startar en ny fran samma punkt, sa att inget
// glapp uppstar mellan dem.
void splitHere();

// Syftet gar att satta nar som helst: under resan, eller pa fragan efterat.
void setPurpose(TripPurpose p);

// Kunden hamtas ur den nedsynkade listan. Att valja kund innebar att resan ar
// en foretagsresa - det behover ingen tala om separat.
void setCustomer(const char *name);

// Nasta resa startar har, oavsett var mottagaren tror att vi ar. Anvands nar en
// resa lakts efter stromavbrott: dar stromen forsvann borjar nasta resa.
void startNextFrom(double lat, double lon);

RecoveredTrip recovered();
void clearRecovered();

// Namnet pa syftet, for skarmen och for filerna.
const char *purposeName(TripPurpose p);
const char *purposeSlug(TripPurpose p);

}  // namespace trip
