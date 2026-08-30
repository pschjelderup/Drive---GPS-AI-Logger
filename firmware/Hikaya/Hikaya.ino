// Hikaya - reselogg for Waveshare ESP32-S3-Touch-AMOLED-2.41
//
// Loggar varje resa i bilen till en gpx-fil och en rad i en resedagbok, och
// markerar den som privat, foretag eller diffus. Resan startar och slutar av sig
// sjalv, eftersom en korjournal som bara innehaller de resor nagon kom ihag att
// trycka igang inte ar en korjournal.
//
// Skarmarna ritas med LVGL och bor i gui_screens; det har ar bara uppstarten,
// installningarna och serieportens felsokningsrader. Hemskarmen ar en appmeny
// i telefonstil, korskarmen en renderad matare, och ett svep uppat fran
// underkanten leder alltid hem.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <TouchDrv.hpp>
#include <Wire.h>

#include "axp.h"
#include "cams.h"
#include "cloudsync.h"
#include "config.h"
#include "customers.h"
#include "eco.h"
#include "gnss.h"
#include "expander.h"
#include "gui.h"
#include "logg.h"
#include "obd.h"
#include "panel35.h"
#include "sensors.h"
#include "sound.h"
#include "stats.h"
#include "storage.h"
#include "trip.h"
#include "websync.h"

// ------------------------------------------------------------- skarmen ----
#if defined(BOARD_LCD35)
// Panelen drivs av panel35 (esp_lcd, samma stack som Waveshares
// factory-firmware) - Arduino_GFX:s SPI-klasser lamnar den har
// kortrevisionen svart. Ingen Arduino_GFX-panel finns alltsa har;
// gui-koden ropar pa panel35 direkt.
Arduino_GFX *panel = nullptr;
#else
Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_D0,
                                             PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);

Arduino_RM690B0 *panel =
    new Arduino_RM690B0(bus, PIN_LCD_RST, 0 /* rotation */, SCREEN_W, SCREEN_H,
                        LCD_COL_OFFSET, 0, LCD_COL_OFFSET, 0);
#endif

// Ljusstyrkan ar det enda skarmglittet som skiljer korten at: AMOLED:en har
// ett eget ljusregister, LCD:n en pwm:ad bakgrundsbelysning.
namespace gui {
void panelBrightness(uint8_t level) {
#if defined(BOARD_LCD35)
  ledcWrite(PIN_LCD_BL, level);
#else
  static_cast<Arduino_RM690B0 *>(panel)->setBrightness(level);
#endif
}
}  // namespace gui

TouchDrvFT6X36 touch;
bool touchOk = false;

Preferences prefs;
AppSettings cfg = {DEFAULT_SCREEN_TIMEOUT_INDEX, DEFAULT_SOUND_ON,
                   DEFAULT_ECO_SOFT_INDEX,       DEFAULT_ECO_HARD_INDEX,
                   DEFAULT_ECO_BUBBLE_INDEX,     DEFAULT_ECO_PENALTY_INDEX,
                   DEFAULT_ECO_WINDOW_INDEX,     DEFAULT_AUTO_SYNC,
                   DEFAULT_OBD_ON};

bool lastButtonState = HIGH;

// Versionsstrangen och PR-numret kommer fran bygget, via config.h.

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
  cfg.autoSync = prefs.getUChar("autoSync", DEFAULT_AUTO_SYNC);
  cfg.obdOn = prefs.getUChar("obd", DEFAULT_OBD_ON);
  prefs.end();

  // Ett trasigt eller gammalt sparat varde far inte gora enheten obrukbar.
  if (cfg.screenIdx >= kScreenTimeoutCount) {
    cfg.screenIdx = DEFAULT_SCREEN_TIMEOUT_INDEX;
  }
  if (cfg.soundOn > 1) cfg.soundOn = DEFAULT_SOUND_ON;
  if (cfg.autoSync > 1) cfg.autoSync = DEFAULT_AUTO_SYNC;
  if (cfg.obdOn > 1) cfg.obdOn = DEFAULT_OBD_ON;
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
  prefs.putUChar("autoSync", cfg.autoSync);
  prefs.putUChar("obd", cfg.obdOn);
  prefs.end();
}

void applySettings() {
  eco::setLimits(kEcoSoft[cfg.ecoSoftIdx], kEcoHard[cfg.ecoHardIdx],
                 kEcoBubble[cfg.ecoBubbleIdx], kEcoPenalty[cfg.ecoPenaltyIdx],
                 kEcoWindowS[cfg.ecoWindowIdx]);
  sound::setEnabled(cfg.soundOn != 0);
  cloudsync::setAutoSync(cfg.autoSync != 0);
  obd::setEnabled(cfg.obdOn != 0);
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
#if defined(GNSS_UART)
    Serial.println("GPS: inga nmea-meningar pa uarten (GPIO44)");
#else
    Serial.println("GPS: ingen modul pa 0x42");
#endif
  } else {
    // Paket utan satelliter ar antennfallet: modulen mar bra, den ser bara
    // ingenting. Inga paket alls ar ett busproblem.
    // Fartraden avslojar varfor autostarten eventuellt tvekar: betrodd fart
    // kraver 3d-fix och en rimlig osakerhetssiffra fran mottagaren.
    const GnssFix f = gnss::fix();
    Serial.printf("GPS: avlasningar %lu  paket %lu  fixtyp %u  satelliter %u  "
                  "fart %.1f±%.1f km/h %s\n",
                  (unsigned long)d.polls, (unsigned long)d.packets,
                  (unsigned)d.fixType, (unsigned)d.sats, f.speedKmh,
                  f.speedAccKmh, f.speedTrusted ? "betrodd" : "obetrodd");
  }

  Serial.printf(
      "resa: %s  nr %lu  %.2f km  %lu punkter  grans %u  kameror %lu\n",
      t.active ? "pagar" : "ingen", (unsigned long)t.index, t.distanceM / 1000.0,
      (unsigned long)t.points, (unsigned)cams::currentLimitKmh(),
      (unsigned long)cams::count());
}

// Knappen pa kortet slacker och tander skarmen.
void handleButton() {
  const bool state = digitalRead(PIN_BOOT_BUTTON);
  if (state == LOW && lastButtonState == HIGH) {
    gui::setDisplayOn(!gui::displayOn());
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
  Serial.println("=== HIKAYA ===");
  Serial.print("version ");
  Serial.println(fwVersionFull());
  Serial.println("byggd " __DATE__ " " __TIME__);
  // Kortvarianten i klartext. Fel firmware pa ratt kort ser ut precis som
  // en trasig skarm - den har raden ar det som skiljer dem at i loggen.
#if defined(BOARD_LCD35)
  Serial.println("kort: LCD 3.5");
#else
  Serial.println("kort: AMOLED 2.41");
#endif

#if defined(BOARD_LCD35)
  // Ordningen ar viktig: i2c-bussen forst, sedan strommen (panelens
  // matning ligger pa PMIC:ens LDO-skenor och ar av efter kallstart),
  // sedan expandern som ager skarmens reset.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  axp::begin();
  expander::begin();
  expander::lcdReset();

  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  // Versionskvitto som syns aven om panelen ar dod: bakgrundsljuset
  // blinkar tva ganger innan det tands pa riktigt. Blinkar det inte
  // kor enheten en aldre firmware.
  pinMode(PIN_LCD_BL, OUTPUT);
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_LCD_BL, HIGH);
    delay(150);
    digitalWrite(PIN_LCD_BL, LOW);
    delay(150);
  }

  // esp_lcd-vagen, verifierad pa hardvaran: init som factory, rita
  // innan ljuset tands.
  const bool panelOk = panel35::begin();
  panel35::fill(0xF800);

  ledcAttach(PIN_LCD_BL, 20000, 8);
  gui::panelBrightness(235);
  // Bring-up-kvitto: rott betyder att panelen svarar och ritningen gar
  // fram. Fastnar skarmen svart ar det strom, reset eller bussen;
  // fastnar den rod ar det LVGL-steget.
  delay(1000);
  panel35::fill(0x0000);
  Serial.printf("skarm: begin %s\n", panelOk ? "ok" : "MISSLYCKADES");
#else
  // Skarmens matning maste sla pa forst av allt.
  pinMode(PIN_PANEL_POWER, OUTPUT);
  digitalWrite(PIN_PANEL_POWER, HIGH);
  delay(200);

  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  // 80 MHz pa qspi-bussen i stallet for standardens 40: halva flushtiden,
  // och det ar flusharna som satter kanslan i hela granssnittet. Panelen
  // klarar det - borjar bilden brusa ar det har man backar till 40.
  panel->begin(80000000L);
  panel->fillScreen(0x0000);
  panel->setBrightness(235);
#endif

  loadSettings();
  sound::begin();

  const bool imuOk = sensors::begin();
  applySettings();

  // Loggen kraver kortet, sa den borjar har - och forsta raden ar starten
  // sjalv, med orsaken. En krasch eller ett spanningsfall som startar om
  // enheten mitt i vardagen ar exakt det man vill kunna se i webappen.
  logg::begin();
  {
    const esp_reset_reason_t rr = esp_reset_reason();
    const char *orsak =
        rr == ESP_RST_POWERON    ? "kallstart"
        : rr == ESP_RST_SW       ? "omstart"
        : rr == ESP_RST_PANIC    ? "KRASCH"
        : rr == ESP_RST_BROWNOUT ? "SPANNINGSFALL"
        : (rr == ESP_RST_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT)
            ? "VAKTHUND"
            : "annan orsak";
#if defined(BOARD_LCD35)
    const char *kort = "LCD 3.5";
#else
    const char *kort = "AMOLED 2.41";
#endif
    logg::event("start: %s, kort %s, %s", fwVersionFull(), kort, orsak);
    if (!sensors::sdMounted()) logg::event("minneskort saknas");
  }

  // Datafilernas tillstand i klartext: finns filen, hur stor ar den, bar den
  // ratt signatur, och blev den faktiskt inlast? Det ar exakt raderna man
  // behover nar en manuellt ditlagd fil "inte syns" - skillnaden mellan fel
  // namn, fel mapp, trasig fil och fel version star har.
  if (sensors::sdMounted()) {
    const struct {
      const char *path;
      const char *namn;
      uint32_t magic;
      bool loaded;
    } fils[] = {
        {LIMITS_FILE, "hastighetsfilen", 0x31484C44, cams::limitsLoaded()},
        {CAMS_FILE, "kamerafilen", 0x31434C44, cams::loaded()},
    };
    for (const auto &fi : fils) {
      File f = SDCARD.open(fi.path, FILE_READ);
      if (!f) {
        logg::event("%s: finns inte pa kortet (%s)", fi.namn, fi.path);
        continue;
      }
      uint32_t m = 0;
      f.read((uint8_t *)&m, 4);
      const unsigned long sz = (unsigned long)f.size();
      f.close();
      logg::event("%s: %lu byte, %s, %s", fi.namn, sz,
                  m == fi.magic ? "ratt signatur" : "FEL SIGNATUR",
                  fi.loaded ? "inlast" : "INTE inlast");
    }
  }

  customers::reload();
  stats::begin();
  // Obd startas efter installningarna: ar tillvalet avslaget ror traden
  // aldrig radion.
  obd::begin(cfg.obdOn != 0);
  websync::begin();
  cloudsync::begin();

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

  gui::begin(panel, &touch, touchOk, &cfg, saveSettings, applySettings);

  // En resa som strommen tog ar redan lagad har. Med tandningsstyrd strom ar
  // det varje resa, sa skarmen gor inget vasen av det - saknar resan syfte
  // staller gui:t fragan, annars sags ingenting. Raden nedan ar for den som
  // felsoker over serieporten.
  const RecoveredTrip rec = trip::recovered();
  if (rec.valid) {
    Serial.printf("lakt resa %lu: %.2f km, sista position %.5f,%.5f\n",
                  (unsigned long)rec.index, rec.distanceM / 1000.0, rec.lat,
                  rec.lon);
  }
}

uint32_t lastSerialMs = 0;

void loop() {
  handleButton();
  sound::tick();
  websync::tick();
  gui::tick();

  // En rad var femte sekund racker for att folja en uppstart utan att dranka
  // konsolen. Den fortsatter aven med slackt skarm, vilket ar precis nar man
  // behover den.
  if (millis() - lastSerialMs > 5000) {
    lastSerialMs = millis();
    printStatusLine();
  }

  delay(5);
}
