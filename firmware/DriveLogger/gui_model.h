// Modellen mellan varlden och skarmen.
//
// Skarmkoden (gui_screens) ar ren LVGL och vet ingenting om GPS-mottagare,
// SD-kort eller trippdetektorer - den far allt den ritar ur den har
// strukturen, och allt anvandaren gor lamnar den ifran sig som atgarder.
// Det ar den skiljelinjen som gor att exakt samma skarmar kan renderas pa
// en vanlig dator till PNG och granskas innan nagot flashas.

#pragma once

#include <stdint.h>

// Samma ordning som knapparna. Speglar TripPurpose i trip.h.
enum GuiPurpose : uint8_t {
  GUI_PURPOSE_UNSET = 0,
  GUI_PURPOSE_PRIVAT = 1,
  GUI_PURPOSE_FORETAG = 2,
  GUI_PURPOSE_DIFFUST = 3,
};

struct GuiModel {
  // ---- klockan och statusraden
  char clock[8];        // "07:32", tom strang = klockan inte stalld
  bool gpsPresent;
  bool gpsFix;
  uint8_t sats;
  bool sdOk;
  bool cloudConfigured;
  bool cloudBusy;       // synk pagar just nu
  bool apClient;        // telefon ansluten till enhetens wifi

  // ---- korningen
  float speedKmh;
  uint8_t limitKmh;     // 0 = okand
  bool speedTrusted;
  bool camsLoaded;
  bool limitsLoaded;

  bool camActive;
  uint32_t camDistanceM;
  uint8_t camLimitKmh;

  bool tripActive;
  bool waitingForFix;
  uint32_t tripIndex;
  double tripKm;
  uint32_t tripElapsedS;
  uint32_t tripMovingS;
  uint32_t stoppedS;    // hur lange bilen statt stilla, 0 om den rullar
  uint32_t stopAfterS;  // nar resan avslutas automatiskt
  float maxSpeedKmh;
  GuiPurpose purpose;
  char customer[40];

  // ---- fragan efter resan
  bool askPurpose;
  uint32_t askIndex;
  double askKm;
  uint32_t askSecondsLeft;

  // ---- ecodrive
  float ecoScore;       // levande 0-100
  float ecoTripScore;   // resans medel
  bool ecoMeasured;
  float ecoMagG;
  float ecoLonG;
  float ecoLatG;
  float ecoPeakG;
  bool ecoLevelled;
  bool ecoForwardKnown;
  float ecoForwardQuality;
  float ecoSoftG;
  float ecoHardG;
  float ecoBubbleG;
  uint32_t ecoHardAccel, ecoHardBrake, ecoHardTurn, ecoHardTotal;

  // ---- statistiken
  double statTotalKm;
  uint32_t statTrips;
  uint32_t statMovingS;
  uint32_t statPoints;
  float statMaxKmh;
  uint32_t statSpeedingS;
  double statPrivatKm, statForetagKm, statDiffustKm;
  uint32_t statFreeMb, statCardMb;
  double statKmLeft;

  // ---- molnet och natet
  char apSsid[36];
  char apPassword[36];
  char cloudSsid[36];
  char cloudDetail[64];
  uint32_t cloudTrips, cloudGpx, cloudFiles;
  uint32_t camCount;

  // ---- installningarna
  bool soundOn;
  uint8_t screenIdx;
  uint8_t screenCount;
  uint16_t screenTimeoutS;  // varde for aktuellt index, 0 = aldrig
  bool autoSyncOn;
  char version[24];

  // ---- kundlistan (fylls pa nar valjaren oppnas)
  uint8_t customerCount;
  const char *const *customerNames;
};

// Allt anvandaren kan gora. Skarmkoden ropar, gluet gor.
struct GuiActions {
  void (*setPurpose)(GuiPurpose p);
  void (*startTrip)();
  void (*endTrip)();
  void (*splitTrip)();
  void (*pickCustomer)(const char *name);  // nullptr = ingen kund
  void (*openCustomers)();                 // be gluet fylla kundlistan
  void (*toggleSound)(bool on);
  void (*setScreenIdx)(uint8_t idx);
  void (*tare)(void (*done)(bool ok));
  void (*ecoReset)();
  void (*requestCloudSync)();
  void (*toggleAutoSync)(bool on);
};
