// Wifi-synken: enhetens eget natverk och webbsida.
//
// Nar ingen resa pagar reser enheten ett eget wifi-nat. Telefonen ansluter,
// och tack vare fangstportalen oppnas sidan av sig sjalv - samma mekanism som
// far ett hotellwifi att poppa upp. Dar listas resorna, gpx-filerna gar att
// hamta, och kamerafilen och kundlistan gar att ladda upp. Ingen kabel, ingen
// kortutmatning, ingen app.
//
// Under fard ar natet slackt. Det har inget arende da, det drar strom, och en
// webbsida ar inget man ska titta pa nar man kor.
//
// Servern kors fran skarmtraden, sa en overforing kan frysa bilden nagon
// sekund. Det ar ett medvetet byte: samma trad betyder att sidan aldrig
// krockar med skarmens egna lasningar, och nar sidan anvands star bilen still.

#pragma once

#include <Arduino.h>

namespace websync {

void begin();

// Anropas fran huvudloopen. Startar och stoppar natet efter resans tillstand
// och driver webbservern.
void tick();

// Sant nar natet ar uppe. Skarmen visar da namn och adress, sa att ingen
// behover minnas dem.
bool isUp();
const char *ssid();
String ipString();

// Antal anslutna enheter. Skarmen sager "telefon ansluten" i stallet for en
// adress nar nagon val hittat fram.
uint8_t clientCount();

}  // namespace websync
