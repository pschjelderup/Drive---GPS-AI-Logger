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

void begin();

// Natuppgifterna, satta fran enhetens wifi-sida. Tomt ssid stanger av synken.
void configure(const char *ssid, const char *password, const char *token);
bool configured();
String ssid();

// Be om en synk sa snart villkoren tillater, i stallet for att venta ut
// intervallet. Knappen pa wifi-sidan.
void requestSync();

CloudStatus status();

}  // namespace cloudsync
