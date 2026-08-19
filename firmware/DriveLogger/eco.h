// Ecodrive: hur mjukt du kor, medan du kor.
//
// Allt vilar pa en enda insikt: sparsam korning och mjuk korning ar samma sak.
// Bransle gar at nar farten andras - vid gaspadrag, inbromsning och kurvtagning
// - och ju haftigare andringen ar, desto mer gar at. Det gar alltsa att mata
// sparsamhet med enbart accelerometern, utan att veta nagot om motorn.
//
// Kortet far sitta hur som helst i bilen. Tyngdkraften visar vilket hall som ar
// ned, och den riktningen raknas bort, sa att det som blir kvar ar den
// vagrata accelerationen - det som faktiskt kanns i sätet.

#pragma once

#include <Arduino.h>

#include "sensors.h"

struct EcoStatus {
  float score;      // 0-100, lever medan du kor

  // Resans poang: tidsviktat medel av score sedan nollstallningen. Den leva
  // poangen ovan ater sig tillbaka till hundra pa tva minuters lugn korning,
  // sa vid resans slut star den nastan alltid pa hundra oavsett hur resan
  // var - ett medel over hela resan ar det som faktiskt sager nagot.
  float tripScore;

  // Sant nar matningen racker for att redovisas: lodlinjen funnen och minst
  // ECO_MEASURED_MIN_S sekunder uppmatta. En resa utan matning ska sta som
  // omatt i dagboken, inte som noll - noll ar ett omdome, omatt ar ett
  // konstaterande.
  bool measured;
  uint32_t measuredS;
  float lonG;       // vagrat, langs kortets ena axel
  float latG;       // vagrat, langs den andra
  float magG;       // total vagrat acceleration
  float peakG;      // hogsta sedan nollstallningen
  bool levelled;    // sant nar tyngdkraften hittats och vardena ar palitliga

  // Monteringslaget. Lutning klarar sig utan tara - den hittas anda - men ett
  // sparat lage gor att lodlinjen ar ratt direkt vid start i stallet for efter
  // en halv minut.
  bool levelStored;

  // Vilket hall som ar framat. Gar bara att lara sig under korning: nar bilen
  // star still finns inget framat att observera. Utan den ar bubblans storlek
  // ratt men riktningen godtycklig.
  bool forwardKnown;
  float forwardQuality;  // 0-1
  bool forwardNeedsGnss;

  // Handelser sedan nollstallningen. Uppdelningen kraver GPS; utan den
  // rapporteras allt som hardTotal.
  bool gpsClassify;
  uint32_t hardAccel;
  uint32_t hardBrake;
  uint32_t hardTurn;
  uint32_t hardTotal;

  uint32_t elapsedS;

  // Sant nar kortet ligger stilla. Avgors av gyrot, inte av nagot som raknats
  // fram ur lodlinjen.
  bool atRest;

  // Granserna som gallde nar vardena raknades fram. Skarmen ritar ringar och
  // farger efter dessa, sa att en andring i gransmenyn syns direkt i bilden.
  float softG;
  float hardG;
  float bubbleG;
};

namespace eco {

void begin();

// Anropas fran avlasningstraden, en gang per varv.
void tick(const Sample &s);

// Granserna, ställbara i farten fran gransmenyn. Avslutningsgransen harleds ur
// den harda: en egen ratt for den vore en knapp till att forsta utan att ge
// nagot, eftersom det enda den gor ar att hindra ett enda ryck fran att raknas
// som flera nar vardet studsar kring gransen.
void setLimits(float softG, float hardG, float bubbleG, float penaltyPerGs,
               uint16_t windowS);

// Nollstaller poang, toppvarde och raknare. Tyngdkraftsriktningen behalls,
// eftersom den inte har med korningen att gora.
void reset();

// Sparar monteringslaget och borjar om inlarningen av framatriktningen.
// Sjalva arbetet utfors av avlasningstraden; anropet vantar pa svar i upp till
// nagra sekunder och far darfor inte goras fran den traden.
// Returnerar false om enheten inte star stilla - da ar det inte tyngdkraften
// man skulle spara utan en rorelse.
bool tare();

EcoStatus status();

}  // namespace eco
