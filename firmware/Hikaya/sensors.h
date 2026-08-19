// Rorelsesensorn, klockan och minneskortet - allt som ar maskinvara utom
// skarmen och GPS:en.
//
// Avlasningen sker i en egen trad sa att den haller jamn takt aven nar skarmen
// ritas om. Traden ar ocksa den enda som *skriver* till minneskortet: resan,
// dagboken och tillstandsfilen skrivs alla harifran, och skarmen bestaller i
// stallet for att skriva sjalv. Da kan tva skrivningar aldrig krocka, och all
// filskrivning har ett enda hem.
//
// Lasningar far daremot ske fran skarmtraden - kundlistan gor det - eftersom
// filsystemslagret serialiserar atkomsten. Det som inte far ske darifran ar att
// frigora nagot avlasningstraden samtidigt soker i; se cams::reload().

#pragma once

#include <Arduino.h>

// Ett avlast varde fran rorelsesensorn.
struct Sample {
  float ax, ay, az;  // acceleration per axel, i g
  float atot;        // total acceleration, i g
  float gx, gy, gz;  // vridhastighet per axel, i grader/sekund
  float temp;        // sensorns temperatur, i grader Celsius
};

namespace sensors {

// Startar i2c, rorelsesensorn, klockan, GPS:en, minneskortet och
// avlasningstraden. Returnerar false bara om rorelsesensorn inte svarar.
// Allt annat gar att atgarda i efterhand och stoppar inte uppstarten.
bool begin();

bool imuOk();
bool sdMounted();

// Forsoker montera minneskortet igen, t.ex. efter att anvandaren stoppat i ett.
bool remount();

Sample latest();

// Ledigt och totalt utrymme pa kortet.
uint64_t freeBytes();
uint64_t cardBytes();

// ------------------------------------------------------------------ tiden --
// Klockan halls i UTC, alltid. Det ar den tid som ska sta i en gpx-fil, och
// det ar den enda tid som ar entydig aret om. Lokal tid ar en sak man raknar
// fram nar den ska visas for en manniska, inte nagot man lagrar.

// Sekunder sedan 1970 i UTC. Noll betyder att klockan aldrig blivit stalld och
// alltsa inte vet nagot - da ska ingen tid skrivas alls, hellre det an ett
// artal som ser riktigt ut och inte ar det.
uint32_t unixUtc();

// Sant nar klockan gatt efter satellittid minst en gang sedan starten.
bool clockSynced();

// "2026-08-17T14:03:05Z" - formen gpx vill ha.
void isoUtc(uint32_t t, char *out, size_t len);

// "2026-08-17 16:03" - lokal tid enligt offseten i config.h, for manniskor.
void localStamp(uint32_t t, char *out, size_t len);

// Bara klockslaget, "16:03".
void localClock(uint32_t t, char *out, size_t len);

}  // namespace sensors
