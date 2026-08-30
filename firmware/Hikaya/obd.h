// Bilens egen bild av sig sjalv, ur OBD2-uttaget.
//
// Tillvalet: en ELM327-adapter i uttaget later loggen berata inte bara var
// bilen var utan vad den gjorde - varvtal, kylvattentemperatur, motorlast,
// bransle. Utan adapter fungerar allt precis som forut; modulen ror inte ens
// radion forran nagon slar pa den.
//
// VIKTIGT OM ADAPTERN: kortet har ESP32-S3, och den kretsen har INGEN
// bluetooth classic - bara BLE. De billigaste "ELM327 mini"-adaptrarna talar
// classic (spp) och gar darfor inte att para har. Adaptern maste vara en
// BLE-modell (bt 4.0/5.0), till exempel Vgate iCar Pro BLE. Wifi-adaptrar
// gar inte heller: radion behovs till molnsynken.
#pragma once

#include <Arduino.h>

// Vilka varden bilen faktiskt lamnar ut varierar med marke, arsmodell och
// motor. Bitmasken sager vad som ar sant just nu; skarmen visar streck for
// resten i stallet for att gissa nollor.
enum ObdHas : uint32_t {
  OBD_HAS_RPM = 1u << 0,
  OBD_HAS_SPEED = 1u << 1,
  OBD_HAS_COOLANT = 1u << 2,
  OBD_HAS_LOAD = 1u << 3,
  OBD_HAS_THROTTLE = 1u << 4,
  OBD_HAS_FUEL = 1u << 5,       // tanknivan i procent
  OBD_HAS_INTAKE = 1u << 6,
  OBD_HAS_AMBIENT = 1u << 7,
  OBD_HAS_VOLT = 1u << 8,       // styrenhetens matningsspanning
  OBD_HAS_OIL = 1u << 9,
  OBD_HAS_FLOW = 1u << 10,      // momentan forbrukning (l/h)
  OBD_HAS_HYBRID = 1u << 11,    // hybridbatteriets laddning i procent
  OBD_HAS_RUNTIME = 1u << 12,   // sekunder sedan motorstart
};

enum ObdState : uint8_t {
  OBD_OFF = 0,        // avslaget i installningarna
  OBD_SEARCHING,      // letar efter adaptern
  OBD_CONNECTING,     // hittad, kopplar upp
  OBD_HANDSHAKE,      // adaptern svarar, protokollet stalls in
  OBD_LIVE,           // bilen svarar, varden tickar in
  OBD_NO_CAR,         // adaptern lever men bilen svarar inte (tandning av)
};

struct ObdData {
  ObdState state;
  uint32_t has;         // vilka falt nedan som ar sanna

  uint16_t rpm;
  uint8_t speedKmh;
  int16_t coolantC;
  int16_t intakeC;
  int16_t ambientC;
  int16_t oilC;
  uint8_t loadPct;
  uint8_t throttlePct;
  uint8_t fuelPct;
  uint8_t hybridPct;
  float flowLh;         // liter per timme just nu
  float voltage;
  uint32_t runtimeS;

  uint32_t samples;     // antal lyckade avlasningar sedan uppkopplingen
  uint32_t lastReplyMs; // nar bilen senast svarade
  char adapter[24];     // adapterns namn, for skarmen
  char protocol[24];    // t.ex. "ISO 15765-4 (CAN 11/500)"
};

// Resans summering. Nollstalls nar en resa borjar och lases av nar den
// avslutas - det ar den har som foljer med upp i molnet.
struct ObdTripSummary {
  bool any;             // falskt = ingen obd-data alls under resan
  uint16_t maxRpm;
  uint16_t avgRpm;
  int16_t maxCoolantC;
  uint8_t maxLoadPct;
  uint8_t fuelStartPct, fuelEndPct;
  float fuelLiters;     // integrerad forbrukning, 0 om flodet ar okant
  uint32_t idleS;       // sekunder med motorn igang och bilen stillastaende
  uint32_t engineOnS;   // sekunder med motorn igang (hybrid: forbranningsdelen)
  uint32_t samples;
};

namespace obd {

// Laser installningen och startar traden. Ar tillvalet avslaget ror den inte
// radion alls - ingen bluetooth-stack, inget minne, ingen strom.
void begin(bool enabled);

// Slar pa eller av tillvaget i drift. Avslaget kopplar ned och slapper
// bluetooth-stacken.
void setEnabled(bool on);
bool enabled();

// Glom den inlarda adaptern och leta igen fran borjan.
void forget();

ObdData data();

// Resans summering. noteTripStart nollstaller, summary() laser av.
void noteTripStart();
ObdTripSummary summary();

}  // namespace obd
