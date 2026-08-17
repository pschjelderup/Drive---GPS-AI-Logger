// Kundlistan.
//
// Den bor pa minneskortet som KUNDER.CSV och synkas ner fran webben, dar det ar
// betydligt trevligare att skriva in namn an pa en pekskarm i en bil. Filen gar
// ocksa att skriva for hand med vilken texteditor som helst - en enhet som
// kraver ett moln for att kunna namnge en kund ar en enhet som slutar fungera
// nar molnet gor det.
//
// Format: ett namn per rad. Star det ett id forst, avskilt med semikolon, tas
// namnet efter semikolonet. Bada dessa fungerar alltsa:
//
//   Volvo Torslanda
//   4711;Volvo Torslanda

#pragma once

#include <Arduino.h>

namespace customers {

void begin();
void reload();

uint8_t count();

// Namnet pa plats i, eller tom strang om i ligger utanfor listan.
const char *name(uint8_t i);

}  // namespace customers
