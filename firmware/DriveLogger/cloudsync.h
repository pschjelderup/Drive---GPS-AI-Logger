// Molnsynken: enheten ansluter till ett vanligt wifi - typiskt telefonens
// hotspot - och pratar med webbappens moln av sig sjalv.
//
// Uppat: nya resor ur dagboken och gpx-filer som inte lamnat kortet.
// Nedat: kamerafilen, hastighetsfilen och kundlistan, nar nya versioner finns.
//
// Villkoren ar tva och bada kravs: natuppgifter ska vara ifyllda (pa enhetens
// wifi-sida), och ingen resa far paga. Under fard ar bade synken och
// stationslaget avstangda - de drar strom och har inget arende da.
//
// Synken kor i en egen trad. Den ror kortet, men bara sadant som ligger
// stilla nar ingen resa pagar: gpx-mappen, dagboken (lasning), datafilerna
// (via cams-handslaget) och kundlistan.

#pragma once

#include <Arduino.h>

enum CloudState : uint8_t {
  CLOUD_OFF = 0,        // inga natuppgifter ifyllda
  CLOUD_IDLE,           // vantar pa nasta forsok
  CLOUD_CONNECTING,     // forsoker na natet
  CLOUD_SYNCING,
  CLOUD_DONE,           // senaste synken gick igenom
  CLOUD_ERROR,
};

struct CloudStatus {
  CloudState state;
  char detail[64];       // manskligt besked om vad som hander eller hande
  uint32_t tripsUploaded;
  uint32_t gpxUploaded;
  uint32_t filesDownloaded;
};

namespace cloudsync {

// Flera sparade nat - foretagets, hemmets, telefonens hotspot. Synken tar
// det som finns dar bilen star: en skanning valjer det starkaste synliga,
// och syns inget provas naten i tur och ordning (dolda ssid syns inte i
// skanningar, men gar att ansluta till).
const uint8_t kNetMax = 4;

void begin();

// Natuppgifterna, satta fran enhetens wifi-sida. Tomt ssid tommer platsen;
// samma ssid med tomt losenord behaller det lagrade - sidan visar ju aldrig
// hemligheterna. Tomt token behaller ocksa det lagrade.
void configureNets(const char *ssids[kNetMax], const char *passwords[kNetMax],
                   const char *token);
bool configured();
bool hasToken();
String netSsid(uint8_t i);
bool netHasPassword(uint8_t i);

// Natet enheten senast nadde molnet via - eller forsta sparade, innan dess.
String ssid();

// Be om en synk sa snart villkoren tillater, i stallet for att venta ut
// intervallet. Knappen pa wifi-sidan.
void requestSync();

CloudStatus status();

}  // namespace cloudsync
