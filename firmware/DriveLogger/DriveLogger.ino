// DriveLogger - reselogg for Waveshare ESP32-S3-Touch-AMOLED-2.41
//
// Loggar varje resa i bilen till en gpx-fil och en rad i en resedagbok, och
// markerar den som privat, foretag eller diffus. Resan startar och slutar av sig
// sjalv, eftersom en korjournal som bara innehaller de resor nagon kom ihag att
// trycka igang inte ar en korjournal.
//
// Skarmen visar under fard hastigheten, den skyltade hastigheten, hur mycket
// over eller under man ligger, och varnar for fartkameror ur Trafikverkets
// oppna data. Ecodrive-skarmen fran Gmate finns kvar som en egen vy.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <TouchDrv.hpp>
#include <Wire.h>

#include "cams.h"
#include "config.h"
#include "customers.h"
#include "eco.h"
#include "gnss.h"
#include "sensors.h"
#include "sound.h"
#include "trip.h"
#include "ui.h"

// ------------------------------------------------------------- skarmen ----
Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_D0,
                                             PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);

Arduino_RM690B0 *panel =
    new Arduino_RM690B0(bus, PIN_LCD_RST, 0 /* rotation */, SCREEN_W, SCREEN_H,
                        LCD_COL_OFFSET, 0, LCD_COL_OFFSET, 0);

// Allt ritas forst i en bildbuffert i psram och skickas sedan till skarmen i ett
// svep. Det ger en bild utan flimmer - och det ar ocksa det som gor de
// genomskinliga lagren mojliga, eftersom en buffert gar att lasa tillbaka.
Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel, 0, 0, 0);

TouchDrvFT6X36 touch;
bool touchOk = false;

Preferences prefs;
AppSettings cfg = {DEFAULT_SCREEN_TIMEOUT_INDEX, DEFAULT_SOUND_ON,
                   DEFAULT_ECO_SOFT_INDEX,       DEFAULT_ECO_HARD_INDEX,
                   DEFAULT_ECO_BUBBLE_INDEX,     DEFAULT_ECO_PENALTY_INDEX,
                   DEFAULT_ECO_WINDOW_INDEX};

Screen screen = SCREEN_MAIN;
Screen customerReturn = SCREEN_MAIN;
bool screenOn = true;
uint32_t lastActivityMs = 0;
uint32_t lastDrawMs = 0;
bool wasTouched = false;
bool lastButtonState = HIGH;

// Fragan om syftet efter en avslutad resa. Svarar man inte blir resan diffus,
// vilket ar arligt - det var den.
const uint32_t kPurposeAskMs = 60000;
uint32_t purposeAskStartMs = 0;

uint8_t customerPage = 0;

// Versionsstrangen kommer fran bygget. Utan den gar det inte att se om
// flashningen tog, och det ar den forsta frågan nar nagot beter sig gammalt.
#ifndef FW_VERSION
#define FW_VERSION "lokal"
#endif

// --------------------------------------------------------- installningar --

void loadSettings() {
  prefs.begin("drive", true);
  cfg.screenIdx = prefs.getUChar("screen", DEFAULT_SCREEN_TIMEOUT_INDEX);
  cfg.soundOn = prefs.getUChar("sound", DEFAULT_SOUND_ON);
  cfg.ecoSoftIdx = prefs.getUChar("ecoSoft", DEFAULT_ECO_SOFT_INDEX);
  cfg.ecoHardIdx = prefs.getUChar("ecoHard", DEFAULT_ECO_HARD_INDEX);
  cfg.ecoBubbleIdx = prefs.getUChar("ecoBub", DEFAULT_ECO_BUBBLE_INDEX);
  cfg.ecoPenaltyIdx = prefs.getUChar("ecoPen", DEFAULT_ECO_PENALTY_INDEX);
  cfg.ecoWindowIdx = prefs.getUChar("ecoWin", DEFAULT_ECO_WINDOW_INDEX);
  prefs.end();

  // Ett trasigt eller gammalt sparat varde far inte gora enheten obrukbar.
  if (cfg.screenIdx >= kScreenTimeoutCount) {
    cfg.screenIdx = DEFAULT_SCREEN_TIMEOUT_INDEX;
  }
  if (cfg.soundOn > 1) cfg.soundOn = DEFAULT_SOUND_ON;
  if (cfg.ecoSoftIdx >= kEcoSoftCount) cfg.ecoSoftIdx = DEFAULT_ECO_SOFT_INDEX;
  if (cfg.ecoHardIdx >= kEcoHardCount) cfg.ecoHardIdx = DEFAULT_ECO_HARD_INDEX;
  if (cfg.ecoBubbleIdx >= kEcoBubbleCount) {
    cfg.ecoBubbleIdx = DEFAULT_ECO_BUBBLE_INDEX;
  }
  if (cfg.ecoPenaltyIdx >= kEcoPenaltyCount) {
    cfg.ecoPenaltyIdx = DEFAULT_ECO_PENALTY_INDEX;
  }
  if (cfg.ecoWindowIdx >= kEcoWindowCount) {
    cfg.ecoWindowIdx = DEFAULT_ECO_WINDOW_INDEX;
  }
}

void saveSettings() {
  prefs.begin("drive", false);
  prefs.putUChar("screen", cfg.screenIdx);
  prefs.putUChar("sound", cfg.soundOn);
  prefs.putUChar("ecoSoft", cfg.ecoSoftIdx);
  prefs.putUChar("ecoHard", cfg.ecoHardIdx);
  prefs.putUChar("ecoBub", cfg.ecoBubbleIdx);
  prefs.putUChar("ecoPen", cfg.ecoPenaltyIdx);
  prefs.putUChar("ecoWin", cfg.ecoWindowIdx);
  prefs.end();
}

void applySettings() {
  eco::setLimits(kEcoSoft[cfg.ecoSoftIdx], kEcoHard[cfg.ecoHardIdx],
                 kEcoBubble[cfg.ecoBubbleIdx], kEcoPenalty[cfg.ecoPenaltyIdx],
                 kEcoWindowS[cfg.ecoWindowIdx]);
  sound::setEnabled(cfg.soundOn != 0);
}

// ------------------------------------------------------------ felsokning --
// Ett kort som sitter i en bil har ingen annan insyn an serieporten.

void scanI2C() {
  Serial.println("i2c-enheter:");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    found++;
    const char *known = "";
    switch (addr) {
      case 0x20: known = "  (io-expander)"; break;
      case 0x38: known = "  (pekskarm)"; break;
      case 0x42: known = "  (GPS)"; break;
      case 0x51: known = "  (klocka)"; break;
      case 0x6A:
      case 0x6B: known = "  (rorelsesensor)"; break;
    }
    Serial.printf("  0x%02X%s\n", addr, known);
  }
  if (found == 0) Serial.println("  inga - bussen ar tyst");
}

void printStatusLine() {
  const GnssDebug d = gnss::debug();
  const TripStatus t = trip::status();

  if (!d.present) {
    Serial.println("GPS: ingen modul pa 0x42");
  } else {
    // Paket utan satelliter ar antennfallet: modulen mar bra, den ser bara
    // ingenting. Inga paket alls ar ett busproblem.
    Serial.printf("GPS: avlasningar %lu  paket %lu  fixtyp %u  satelliter %u\n",
                  (unsigned long)d.polls, (unsigned long)d.packets,
                  (unsigned)d.fixType, (unsigned)d.sats);
  }

  Serial.printf(
      "resa: %s  nr %lu  %.2f km  %lu punkter  grans %u  kameror %lu\n",
      t.active ? "pagar" : "ingen", (unsigned long)t.index, t.distanceM / 1000.0,
      (unsigned long)t.points, (unsigned)cams::currentLimitKmh(),
      (unsigned long)cams::count());
}

// -------------------------------------------------------- skarm av och pa --

void setScreen(bool on) {
  if (on == screenOn) return;
  screenOn = on;
  if (on) {
    panel->displayOn();
    panel->setBrightness(200);
    lastDrawMs = 0;  // tvinga omritning direkt
  } else {
    panel->setBrightness(0);
    panel->displayOff();
  }
  lastActivityMs = millis();
}

// ------------------------------------------------------------- pekskarm ---

// Oversatter fran pekskarmens koordinater till skarmens, ifall panelen ar
// monterad speglad eller vriden.
void mapTouch(int16_t &x, int16_t &y) {
#if TOUCH_SWAP_XY
  const int16_t t = x;
  x = y;
  y = t;
#endif
#if TOUCH_FLIP_X
  x = SCREEN_W - 1 - x;
#endif
#if TOUCH_FLIP_Y
  y = SCREEN_H - 1 - y;
#endif
}

void openCustomers(Screen returnTo) {
  customers::reload();
  customerPage = 0;
  customerReturn = returnTo;
  screen = SCREEN_CUSTOMER;
}

void toggleSound() {
  cfg.soundOn = cfg.soundOn ? 0 : 1;
  applySettings();
  saveSettings();
  // Kvittot spelas bara nar ljudet slas pa. Ett pip som bekraftar att ljudet ar
  // avstangt vore ett pip for mycket.
  if (cfg.soundOn) sound::play(CUE_TAP);
}

void onPressMain(int16_t x, int16_t y) {
  if (ui::kBtnSoundToggle.contains(x, y)) {
    toggleSound();
    return;
  }

  if (ui::kBtnPrivat.contains(x, y)) {
    trip::setPurpose(PURPOSE_PRIVAT);
    sound::play(CUE_TAP);
    return;
  }

  if (ui::kBtnForetag.contains(x, y)) {
    // Att markera en resa som foretagsresa och att saga vilken kund det galler
    // ar samma handling i praktiken, sa listan oppnas direkt. Vill man ingen
    // kund finns knappen "INGEN KUND" dar.
    trip::setPurpose(PURPOSE_FORETAG);
    sound::play(CUE_TAP);
    openCustomers(SCREEN_MAIN);
    return;
  }

  if (ui::kBtnDiffust.contains(x, y)) {
    trip::setPurpose(PURPOSE_DIFFUST);
    sound::play(CUE_TAP);
    return;
  }

  if (ui::kBtnTripAction.contains(x, y)) {
    const TripStatus t = trip::status();
    if (!sensors::sdMounted()) {
      sensors::remount();
      cams::reload();
      customers::reload();
    } else if (t.active) {
      trip::endManual();
      sound::play(CUE_TRIP_END);
    } else {
      if (trip::startManual()) {
        sound::play(CUE_TRIP_START);
      } else {
        sound::play(CUE_ERROR);
      }
    }
    return;
  }

  if (ui::kBtnEco.contains(x, y)) {
    screen = SCREEN_ECO;
    return;
  }

  if (ui::kBtnMiddle.contains(x, y)) {
    const TripStatus t = trip::status();
    if (t.active) {
      // Dela resan: den pagaende avslutas och en ny borjar pa samma punkt, sa
      // att inget glapp uppstar. Anvands nar man svanger in till en kund pa
      // vagen hem och vill ha tva rader i journalen.
      trip::splitHere();
      sound::play(CUE_TRIP_START);
    } else {
      openCustomers(SCREEN_MAIN);
    }
    return;
  }

  if (ui::kBtnMenu.contains(x, y)) {
    screen = SCREEN_MENU;
    return;
  }
}

void onPressPurposeAsk(int16_t x, int16_t y) {
  if (ui::kBtnAskPrivat.contains(x, y)) {
    trip::setPurpose(PURPOSE_PRIVAT);
    sound::play(CUE_TAP);
    screen = SCREEN_MAIN;
    return;
  }
  if (ui::kBtnAskForetag.contains(x, y)) {
    trip::setPurpose(PURPOSE_FORETAG);
    sound::play(CUE_TAP);
    openCustomers(SCREEN_MAIN);
    return;
  }
  if (ui::kBtnAskDiffust.contains(x, y)) {
    trip::setPurpose(PURPOSE_DIFFUST);
    sound::play(CUE_TAP);
    screen = SCREEN_MAIN;
    return;
  }
}

void onPressCustomer(int16_t x, int16_t y) {
  const uint8_t total = customers::count();
  const uint8_t perPage = 6;
  const uint8_t pages = total ? (uint8_t)((total + perPage - 1) / perPage) : 1;
  const uint8_t first = customerPage * perPage;
  const uint8_t shown =
      (total > first) ? (uint8_t)min((int)perPage, (int)(total - first)) : 0;

  for (uint8_t i = 0; i < shown; i++) {
    if (!ui::customerRow(i).contains(x, y)) continue;
    trip::setCustomer(customers::name(first + i));
    sound::play(CUE_TAP);
    screen = customerReturn;
    return;
  }

  if (ui::kBtnCustomerNone.contains(x, y)) {
    // Foretagsresa utan namngiven kund. Markningen behalls, kundfaltet lamnas
    // tomt - hellre tomt an en kund som inte var med.
    trip::setCustomer("");
    trip::setPurpose(PURPOSE_FORETAG);
    sound::play(CUE_TAP);
    screen = customerReturn;
    return;
  }

  if (pages > 1) {
    if (ui::kBtnCustomerPrev.contains(x, y)) {
      customerPage = customerPage > 0 ? customerPage - 1 : pages - 1;
      return;
    }
    if (ui::kBtnCustomerNext.contains(x, y)) {
      customerPage = (uint8_t)((customerPage + 1) % pages);
      return;
    }
  } else if (ui::kBtnCustomerPrev.contains(x, y)) {
    screen = customerReturn;
    return;
  }
}

void onPressEco(int16_t x, int16_t y) {
  if (ui::kBtnEcoReset.contains(x, y)) {
    eco::reset();
    return;
  }
  if (ui::kBtnEcoLimits.contains(x, y)) {
    screen = SCREEN_ECO_LIMITS;
    return;
  }
  if (ui::kBtnEcoBack.contains(x, y)) screen = SCREEN_MAIN;
}

// Granserna gar att andra aven under pagaende resa. De paverkar bara hur skarmen
// bedomer korningen - resans innehall ar detsamma oavsett var de star, sa det
// finns ingen fil som kan bli inkonsekvent.
void onPressEcoLimits(int16_t x, int16_t y) {
  for (uint8_t row = 0; row < 5; row++) {
    const bool minus = ui::ecoMinus(row).contains(x, y);
    const bool plus = ui::ecoPlus(row).contains(x, y);
    if (!minus && !plus) continue;

    uint8_t *value = nullptr;
    uint8_t count = 0;
    switch (row) {
      case 0: value = &cfg.ecoSoftIdx; count = kEcoSoftCount; break;
      case 1: value = &cfg.ecoHardIdx; count = kEcoHardCount; break;
      case 2: value = &cfg.ecoBubbleIdx; count = kEcoBubbleCount; break;
      case 3: value = &cfg.ecoPenaltyIdx; count = kEcoPenaltyCount; break;
      case 4: value = &cfg.ecoWindowIdx; count = kEcoWindowCount; break;
    }
    if (!value) continue;

    if (minus && *value > 0) {
      (*value)--;
    } else if (plus && *value + 1 < count) {
      (*value)++;
    } else {
      return;
    }

    applySettings();
    saveSettings();
    return;
  }

  if (ui::kBtnEcoBack.contains(x, y)) screen = SCREEN_ECO;
}

void onPressMenu(int16_t x, int16_t y) {
  for (uint8_t row = 0; row < 2; row++) {
    const bool minus = ui::menuMinus(row).contains(x, y);
    const bool plus = ui::menuPlus(row).contains(x, y);
    if (!minus && !plus) continue;

    if (row == 0) {
      toggleSound();
      return;
    }

    if (minus && cfg.screenIdx > 0) {
      cfg.screenIdx--;
    } else if (plus && cfg.screenIdx + 1 < kScreenTimeoutCount) {
      cfg.screenIdx++;
    } else {
      return;
    }
    saveSettings();
    return;
  }

  if (ui::kBtnTare.contains(x, y)) {
    // Taran sparar vilket hall som ar ned. Det gar bara nar kortet star stilla:
    // under rorelse ar det inte tyngdkraften man skulle spara utan en manover,
    // och da skulle lodlinjen bli fel for all framtid.
    if (eco::tare()) {
      ui::drawMessage("TARAT", "Monteringsläget är sparat.",
                      "Riktningen lärs in när du kör.");
    } else {
      ui::drawMessage("STÅ STILL", "Kortet måste ligga stilla.",
                      "Försök igen när bilen står.");
      sound::play(CUE_ERROR);
    }
    delay(1500);
    lastDrawMs = 0;  // rita om direkt nar meddelandet slapper
    return;
  }

  if (ui::kBtnBack.contains(x, y)) screen = SCREEN_MAIN;
}

void onPressRecovered(int16_t x, int16_t y) {
  if (ui::kBtnBack.contains(x, y)) {
    trip::clearRecovered();
    screen = SCREEN_MAIN;
  }
}

void handleTouch() {
  if (!touchOk) return;

  const TouchPoints &points = touch.getTouchPoints();
  const bool pressed = points.hasPoints();

  if (pressed && !wasTouched) {
    lastActivityMs = millis();

    if (!screenOn) {
      // Forsta trycket nar skarmen ar slackt tander bara skarmen, sa att man
      // inte rakar markera en resa av misstag.
      setScreen(true);
    } else {
      const TouchPoint &p = points.getPoint(0);
      int16_t x = (int16_t)p.x;
      int16_t y = (int16_t)p.y;
      mapTouch(x, y);
      switch (screen) {
        case SCREEN_MAIN: onPressMain(x, y); break;
        case SCREEN_PURPOSE: onPressPurposeAsk(x, y); break;
        case SCREEN_CUSTOMER: onPressCustomer(x, y); break;
        case SCREEN_ECO: onPressEco(x, y); break;
        case SCREEN_ECO_LIMITS: onPressEcoLimits(x, y); break;
        case SCREEN_MENU: onPressMenu(x, y); break;
        case SCREEN_RECOVERED: onPressRecovered(x, y); break;
      }
    }
  }
  wasTouched = pressed;
}

void handleButton() {
  const bool state = digitalRead(PIN_BOOT_BUTTON);
  // Knappen drar ingangen till noll nar den trycks ned.
  if (state == LOW && lastButtonState == HIGH) {
    setScreen(!screenOn);
    delay(50);  // enkel studsfiltrering
  }
  lastButtonState = state;
}

// ------------------------------------------------------------------------ --

void setup() {
  Serial.begin(115200);
  // Webbflasharens konsol hinner inte koppla upp sig forran usb-porten raknats
  // upp pa nytt efter omstarten. Utan pausen forsvinner rubriken.
  delay(1500);
  Serial.println();
  Serial.println("=== DRIVELOGGER ===");
  Serial.println("version " FW_VERSION);
  Serial.println("byggd " __DATE__ " " __TIME__);

  // Skarmens matning maste sla pa forst av allt.
  pinMode(PIN_PANEL_POWER, OUTPUT);
  digitalWrite(PIN_PANEL_POWER, HIGH);
  delay(200);

  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  gfx->begin();
  gfx->fillScreen(RGB565(6, 9, 15));
  gfx->flush();
  panel->setBrightness(200);

  ui::begin(gfx);
  ui::drawMessage("DRIVELOGGER", "startar ...", nullptr);

  loadSettings();
  sound::begin();

  const bool imuOk = sensors::begin();
  applySettings();

  customers::reload();

  // Har ar i2c-bussen uppe, sa nu gar det att se vad som faktiskt sitter pa den.
  // Saknas 0x42 ar det kabeln eller kontakten, inte satelliterna.
  scanI2C();
  Serial.printf("rorelsesensor: %s\n", imuOk ? "OK" : "SVARAR INTE");
  Serial.printf("minneskort: %s\n", sensors::sdMounted() ? "OK" : "saknas");
  Serial.printf("fartkameror: %lu\n", (unsigned long)cams::count());
  Serial.printf("kunder: %u\n", (unsigned)customers::count());
  printStatusLine();

  touch.setPins(PIN_TOUCH_RST, TOUCH_IRQ_NOT_CONNECTED);
  touchOk = touch.begin(Wire, FT6X36_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);

  if (!imuOk) {
    // Rorelsesensorn behovs bara till ecodrive. Resan loggas anda, sa det ar en
    // upplysning och inte ett stopp.
    ui::drawMessage("SENSORFEL", "Rörelsesensorn svarar inte.",
                    "Resor loggas ändå - ecodrive gör det inte.");
    delay(3000);
  }

  if (!gnss::present()) {
    // Utan mottagare finns ingenting att logga. Det sags rakt ut, i stallet for
    // att enheten later som om den arbetade.
    ui::drawMessage("INGEN GPS", "Mottagaren svarar inte på 0x42.",
                    "Kontrollera Qwiic-kabeln i I2C-porten.");
    sound::play(CUE_ERROR);
    delay(4000);
  }

  // En resa som stromavbrottet tog ar redan lagad har. Beskedet visas, sa att
  // man far veta det i stallet for att undra.
  if (trip::recovered().valid) {
    screen = SCREEN_RECOVERED;
  }

  lastActivityMs = millis();
}

uint32_t lastSerialMs = 0;

void loop() {
  handleButton();
  handleTouch();
  sound::tick();

  const TripStatus t = trip::status();

  // ---- fragan om syftet efter en avslutad resa
  if (t.awaitingPurpose && screen != SCREEN_PURPOSE &&
      screen != SCREEN_CUSTOMER) {
    screen = SCREEN_PURPOSE;
    purposeAskStartMs = millis();
    setScreen(true);
    sound::play(CUE_TRIP_END);
  }

  if (screen == SCREEN_PURPOSE) {
    if (!t.awaitingPurpose) {
      // Svaret ar registrerat, av oss eller av nedrakningen.
      screen = SCREEN_MAIN;
    } else if (millis() - purposeAskStartMs > kPurposeAskMs) {
      // Ingen svarade. Resan var diffus, och det ar det som skrivs - att gissa
      // privat eller foretag ur tystnad vore att hitta pa.
      trip::setPurpose(PURPOSE_DIFFUST);
      screen = SCREEN_MAIN;
    }
  }

  // En rad var femte sekund racker for att folja en uppstart utan att dranka
  // konsolen. Den fortsatter aven med slackt skarm, vilket ar precis nar man
  // behover den.
  if (millis() - lastSerialMs > 5000) {
    lastSerialMs = millis();
    printStatusLine();
  }

  // ---- skarmens timeout. Under en pagaende resa ar skarmen hela poangen och
  // slacks aldrig av sig sjalv. Nar bilen star parkerad slacks den daremot, bade
  // for stromen och for att en amoled inte mar bra av en stillastaende bild i
  // timmar.
  const uint16_t timeout = kScreenTimeouts[cfg.screenIdx];
  if (screenOn && timeout > 0 && !t.active && screen == SCREEN_MAIN &&
      millis() - lastActivityMs > (uint32_t)timeout * 1000UL) {
    setScreen(false);
  }

  if (screenOn && millis() - lastDrawMs >= 200) {
    lastDrawMs = millis();
    switch (screen) {
      case SCREEN_MAIN:
        ui::drawMain(t, cams::warning(), cams::currentLimitKmh(), t.speedKmh,
                     cfg);
        break;
      case SCREEN_PURPOSE: {
        const uint32_t gone = millis() - purposeAskStartMs;
        const uint32_t left = (gone < kPurposeAskMs) ? (kPurposeAskMs - gone) : 0;
        ui::drawPurposeAsk(t, left / 1000);
        break;
      }
      case SCREEN_CUSTOMER: {
        const uint8_t total = customers::count();
        const uint8_t perPage = 6;
        const uint8_t pages = total ? (uint8_t)((total + perPage - 1) / perPage) : 1;
        const uint8_t first = customerPage * perPage;
        const uint8_t shown =
            (total > first) ? (uint8_t)min((int)perPage, (int)(total - first)) : 0;
        const char *names[6];
        for (uint8_t i = 0; i < shown; i++) names[i] = customers::name(first + i);
        ui::drawCustomers(names, shown, customerPage, pages);
        break;
      }
      case SCREEN_ECO: ui::drawEco(eco::status()); break;
      case SCREEN_ECO_LIMITS: ui::drawEcoLimits(cfg, eco::status()); break;
      case SCREEN_MENU: ui::drawMenu(cfg, FW_VERSION); break;
      case SCREEN_RECOVERED: ui::drawRecovered(trip::recovered()); break;
    }
  }

  delay(10);
}
