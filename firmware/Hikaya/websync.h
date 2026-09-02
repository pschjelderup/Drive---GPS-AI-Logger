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
// Servern kor i en egen trad pa karna 0, inte i skarmens loop. Den lag i
// huvudloopen forut, och da fros touchen sa lange telefonen hamtade en fil -
// nar sidan anvands star visserligen bilen still, men skarmen ska anda svara.
// Kortet skyddas av filsystemets eget las, sa tradarna kan lasa var for sig.

#pragma once

#include <Arduino.h>

namespace websync {

// Startar tradens: den reser och lagger ner natet efter resans tillstand och
// driver webbservern. Huvudloopen behover inte gora nagot.
void begin();

// Lagger ner accesspunkten tillfalligt. Molnsynken gor det medan den kor:
// ap och station samtidigt, plus webbserver, dns och tls, ar mer an
// internminnet racker till pa det mindre kortet - och det ar tls som
// forlorar. Nagon som star pa konfigsidan mitt i en synk far den tillbaka
// nar synken ar klar.
void suspend(bool on);

// Sant nar natet ar uppe. Skarmen visar da namn och adress, sa att ingen
// behover minnas dem.
bool isUp();
const char *ssid();
String ipString();

// Antal anslutna enheter. Skarmen sager "telefon ansluten" i stallet for en
// adress nar nagon val hittat fram.
uint8_t clientCount();

}  // namespace websync
