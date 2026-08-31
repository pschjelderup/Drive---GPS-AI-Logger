// Enhetens egen handelselogg. Raderna hamnar pa minneskortet och foljer med
// upp till molnet vid nasta synk, sa att webbappen kan visa vad enheten
// faktiskt varit med om - synkar, fel, omstarter - utan att nagon behover
// sitta med en usb-kabel i bilen.
#pragma once

namespace logg {

// Kraver att minneskortet ar monterat. Fore begin skrivs raderna bara till
// serieporten.
void begin();

// printf-stil. Varje rad far en tidsstampel: klocktid nar klockan ar staller,
// annars sekunder sedan start.
void event(const char *fmt, ...);

// Rader som inte gick att skriva till kortet. Ett fullt kort tar inte emot
// ett tecken, och da tystnar loggen precis nar den behovs som mest - siffran
// ar det enda spar som finns kvar, och foljer darfor med synkens statusrad.
unsigned long lostLines();

}  // namespace logg
