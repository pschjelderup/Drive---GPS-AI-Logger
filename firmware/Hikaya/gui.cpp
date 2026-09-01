#include "gui.h"
#include "panel35.h"

#include <lvgl.h>

#include "cams.h"
#include "cloudsync.h"
#include "config.h"
#include "customers.h"
#include "eco.h"
#include "gnss.h"
#include "gui_model.h"
#include "gui_screens.h"
#include "logg.h"
#include "obd.h"
#include "sensors.h"
#include "sound.h"
#include "stats.h"
#include "trip.h"
#include "websync.h"

// Kundvaljaren behover fyllas fran flera hall; definitionen star langre ned.
void openCustomersFromGui();

namespace {

Arduino_GFX *g_panel = nullptr;
TouchDrvFT6X36 *g_touch = nullptr;
bool g_touchOk = false;
AppSettings *g_cfg = nullptr;
void (*g_save)() = nullptr;
void (*g_apply)() = nullptr;

lv_display_t *g_disp = nullptr;
bool g_displayOn = true;

// Forsta trycket pa en slackt skarm ska bara tanda den - inte trycka pa det
// som rakade ligga under fingret. Trycket svaljs tills fingret slappt.
bool g_swallowTouch = false;

// ---- fragan efter resan: nedrakningen bor har, inte i skarmkoden
uint32_t g_askStartMs = 0;
bool g_askArmed = false;
const uint32_t kAskMs = 60000;

// ---- kundlistan: namnen kopieras hit nar valjaren oppnas, sa att de lever
// sa lange arket visas oavsett vad kundmodulen gor under tiden.
char g_custNames[24][40];
const char *g_custPtrs[24];

// ------------------------------------------------------------- prestanda --
// Var hamnar tiden nar skarmen kanns seg? Tre matt samlas per minut:
// lvgl-varvet (rendering + inmatning), sjalva flushen till panelen (bussens
// pris), och varvluckan - langsta uppehallet mellan tva gui-varv, dvs nar
// NAGOT ANNAT i huvudloopen (sd-skrivning, kameraskanning) holl skarmen
// vantande. Raden gar till serieporten varje minut och till enhetsloggen
// var femte, sa att kanslan gar att felsoka fran webappen i efterhand.
uint32_t g_pfLvUs = 0, g_pfLvMaxUs = 0, g_pfLvN = 0;
uint32_t g_pfFlUs = 0, g_pfFlMaxUs = 0, g_pfFlN = 0;
uint32_t g_pfGapMaxUs = 0;
int64_t g_pfLastTickUs = 0;
uint32_t g_pfLastReportMs = 0;
uint8_t g_pfRounds = 0;

void perfReport() {
  if (millis() - g_pfLastReportMs < 60000UL) return;
  g_pfLastReportMs = millis();
  if (g_pfLvN == 0) return;

  char line[176];
  snprintf(line, sizeof(line),
           "prestanda: lvgl %lu.%lu ms medel %lu.%lu varst (%lu varv), "
           "flush %lu.%lu ms medel %lu.%lu varst (%lu st), "
           "varvlucka %lu.%lu ms, internminne %lu",
           (unsigned long)(g_pfLvUs / g_pfLvN / 1000),
           (unsigned long)(g_pfLvUs / g_pfLvN % 1000 / 100),
           (unsigned long)(g_pfLvMaxUs / 1000),
           (unsigned long)(g_pfLvMaxUs % 1000 / 100),
           (unsigned long)g_pfLvN,
           (unsigned long)(g_pfFlN ? g_pfFlUs / g_pfFlN / 1000 : 0),
           (unsigned long)(g_pfFlN ? g_pfFlUs / g_pfFlN % 1000 / 100 : 0),
           (unsigned long)(g_pfFlMaxUs / 1000),
           (unsigned long)(g_pfFlMaxUs % 1000 / 100),
           (unsigned long)g_pfFlN,
           (unsigned long)(g_pfGapMaxUs / 1000),
           (unsigned long)(g_pfGapMaxUs % 1000 / 100),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  // Enhetsloggen far var femte rad och bara nar skarmen ar pa - det ar da
  // kanslan finns och siffrorna sager nagot.
  if (++g_pfRounds >= 5 && g_displayOn) {
    g_pfRounds = 0;
    logg::event("%s", line);
  } else {
    Serial.println(line);
  }

  g_pfLvUs = g_pfLvMaxUs = g_pfLvN = 0;
  g_pfFlUs = g_pfFlMaxUs = g_pfFlN = 0;
  g_pfGapMaxUs = 0;
}

// ---------------------------------------------------------------- display --

void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  const int64_t t0 = esp_timer_get_time();
#if defined(BOARD_LCD35)
  panel35::blit(area->x1, area->y1, w, h, (uint16_t *)px);
#else
  g_panel->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
#endif
  const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
  g_pfFlUs += us;
  g_pfFlN++;
  if (us > g_pfFlMaxUs) g_pfFlMaxUs = us;
  lv_display_flush_ready(disp);
}

#if !defined(BOARD_LCD35)
// RM690B0-panelens adressfonster kraver jamna koordinater; ett fonster som
// borjar pa udda kolumn ritas forskjutet sa att bilden delas och halva
// hamnar pa andra sidan. Varje omritad yta knuffas darfor ut till narmast
// jamna granser innan den nar panelen.
void rounder_cb(lv_event_t *e) {
  lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}
#endif

void touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  data->state = LV_INDEV_STATE_RELEASED;
  if (!g_touchOk) return;

  const TouchPoints &points = g_touch->getTouchPoints();
  if (!points.hasPoints()) {
    g_swallowTouch = false;
    return;
  }

  const TouchPoint &p = points.getPoint(0);
  int16_t x = (int16_t)p.x;
  int16_t y = (int16_t)p.y;
#if TOUCH_SWAP_XY
  { const int16_t t = x; x = y; y = t; }
#endif
#if TOUCH_FLIP_X
  x = SCREEN_W - 1 - x;
#endif
#if TOUCH_FLIP_Y
  y = SCREEN_H - 1 - y;
#endif

  if (!g_displayOn) {
    gui::setDisplayOn(true);
    g_swallowTouch = true;
  }
  if (g_swallowTouch) return;

  data->state = LV_INDEV_STATE_PRESSED;
  data->point.x = x;
  data->point.y = y;
}

// ---------------------------------------------------------------- atgarder -

void actSetPurpose(GuiPurpose p) {
  trip::setPurpose((TripPurpose)p);
  sound::play(CUE_TAP);
  // Foretagsresa och kundval ar samma handling i praktiken - listan oppnas
  // direkt, och "INGEN KUND" finns dar for den som vill lamna faltet tomt.
  if (p == GUI_PURPOSE_FORETAG) ::openCustomersFromGui();
}

void actStartTrip() {
  if (!sensors::sdMounted()) {
    sensors::remount();
    cams::reload();
    customers::reload();
    stats::begin();
    return;
  }
  if (trip::startManual()) sound::play(CUE_TRIP_START);
  else sound::play(CUE_ERROR);
}

void actEndTrip() {
  trip::endManual();
  sound::play(CUE_TRIP_END);
}

void actSplit() {
  trip::splitHere();
  sound::play(CUE_TRIP_START);
}

void actPickCustomer(const char *name) {
  if (name) {
    trip::setCustomer(name);
  } else {
    trip::setCustomer("");
    trip::setPurpose(PURPOSE_FORETAG);
  }
  sound::play(CUE_TAP);
}

void actToggleSound(bool on) {
  g_cfg->soundOn = on ? 1 : 0;
  g_apply();
  g_save();
  if (on) sound::play(CUE_TAP);
}

void actScreenIdx(uint8_t idx) {
  g_cfg->screenIdx = idx;
  g_save();
}

void actTare(void (*done)(bool ok)) {
  const bool ok = eco::tare();
  if (!ok) sound::play(CUE_ERROR);
  done(ok);
}

void actEcoReset() { eco::reset(); }

void actCloudSync() { cloudsync::requestSync(); }

void actToggleAutoSync(bool on) {
  g_cfg->autoSync = on ? 1 : 0;
  g_apply();
  g_save();
  sound::play(CUE_TAP);
}

void actToggleObd(bool on) {
  g_cfg->obdOn = on ? 1 : 0;
  g_apply();
  g_save();
  sound::play(CUE_TAP);
}

void actForgetObd() {
  obd::forget();
  sound::play(CUE_TAP);
}

}  // namespace

namespace {

void actOpenCustomers() { ::openCustomersFromGui(); }

const GuiActions kActions = {
    actSetPurpose, actStartTrip, actEndTrip, actSplit, actPickCustomer,
    actOpenCustomers, actToggleSound, actScreenIdx, actTare, actEcoReset,
    actCloudSync, actToggleAutoSync, actToggleObd, actForgetObd,
};

// ---------------------------------------------------------------- modellen -

void fillModel(GuiModel &m) {
  memset(&m, 0, sizeof(m));

  const uint32_t nowUtc = sensors::unixUtc();
  if (nowUtc) sensors::localClock(nowUtc, m.clock, sizeof(m.clock));

  const GnssDebug d = gnss::debug();
  const GnssFix f = gnss::fix();
  m.gpsPresent = d.present;
  m.gpsFix = d.fixType >= 2;
  m.sats = d.sats;
  m.sdOk = sensors::sdMounted();

  const ObdData od = obd::data();
  m.obdState = (uint8_t)od.state;
  m.obdHas = od.has;
  m.obdRpm = od.rpm;
  m.obdSpeedKmh = od.speedKmh;
  m.obdCoolantC = od.coolantC;
  m.obdIntakeC = od.intakeC;
  m.obdAmbientC = od.ambientC;
  m.obdOilC = od.oilC;
  m.obdLoadPct = od.loadPct;
  m.obdThrottlePct = od.throttlePct;
  m.obdFuelPct = od.fuelPct;
  m.obdHybridPct = od.hybridPct;
  m.obdFlowLh = od.flowLh;
  m.obdVoltage = od.voltage;
  m.obdRuntimeS = od.runtimeS;
  strncpy(m.obdAdapter, od.adapter, sizeof(m.obdAdapter) - 1);
  const ObdTripSummary os = obd::summary();
  m.obdTripLiters = os.fuelLiters;
  m.obdTripMaxRpm = os.maxRpm;
  m.obdTripIdleS = os.idleS;

  const CloudStatus cs = cloudsync::status();
  m.cloudConfigured = cloudsync::configured();
  m.cloudBusy = cs.state == CLOUD_SYNCING || cs.state == CLOUD_CONNECTING;
  m.apClient = websync::clientCount() > 0;

  m.speedKmh = f.speedKmh;
  m.speedTrusted = f.speedTrusted;
  m.limitKmh = cams::currentLimitKmh();
  m.camsLoaded = cams::loaded();
  m.limitsLoaded = cams::limitsLoaded();

  const CamWarning w = cams::warning();
  m.camActive = w.active;
  m.camDistanceM = w.distanceM;
  m.camLimitKmh = w.limitKmh;

  const TripStatus t = trip::status();
  m.tripActive = t.active;
  m.waitingForFix = t.waitingForFix;
  m.tripIndex = t.index;
  m.tripKm = t.distanceM / 1000.0;
  m.tripElapsedS = t.elapsedS;
  m.tripMovingS = t.movingS;
  m.stoppedS = t.stoppedS;
  m.stopAfterS = TRIP_STOP_S;
  m.maxSpeedKmh = t.maxSpeedKmh;
  m.purpose = (GuiPurpose)t.purpose;
  strncpy(m.customer, t.customer, sizeof(m.customer) - 1);

  m.askPurpose = t.awaitingPurpose;
  m.askIndex = t.awaitingIndex;
  m.askKm = t.awaitingKm;
  if (t.awaitingPurpose && g_askArmed) {
    const uint32_t gone = millis() - g_askStartMs;
    m.askSecondsLeft = gone < kAskMs ? (kAskMs - gone) / 1000 : 0;
  }

  const EcoStatus e = eco::status();
  m.ecoScore = e.score;
  m.ecoTripScore = e.tripScore;
  m.ecoMeasured = e.measured;
  m.ecoMagG = e.magG;
  m.ecoLonG = e.lonG;
  m.ecoLatG = e.latG;
  m.ecoLoadG = e.loadG;
  m.ecoLatTolerance = e.latTolerance > 0.01f ? e.latTolerance : 1.0f;
  m.ecoPeakG = e.peakG;
  m.ecoLevelled = e.levelled;
  m.ecoForwardKnown = e.forwardKnown;
  m.ecoForwardQuality = e.forwardQuality;
  m.ecoSoftG = e.softG;
  m.ecoHardG = e.hardG;
  m.ecoBubbleG = e.bubbleG;
  m.ecoHardAccel = e.hardAccel;
  m.ecoHardBrake = e.hardBrake;
  m.ecoHardTurn = e.hardTurn;
  m.ecoHardTotal = e.hardTotal;

  const StatsSummary s = stats::summary();
  m.statTotalKm = s.totalKm;
  m.statTrips = s.trips;
  m.statMovingS = s.movingS;
  m.statPoints = s.points;
  m.statMaxKmh = s.maxSpeedKmh;
  m.statSpeedingS = s.speedingS;
  m.statPrivatKm = s.privatKm;
  m.statForetagKm = s.foretagKm;
  m.statDiffustKm = s.diffustKm;
  m.statFreeMb = (uint32_t)(s.freeBytes / (1024ULL * 1024ULL));
  m.statCardMb = (uint32_t)(s.cardBytes / (1024ULL * 1024ULL));
  m.statKmLeft = s.kmLeft;

  strncpy(m.apSsid, websync::ssid(), sizeof(m.apSsid) - 1);
  strncpy(m.apPassword, WIFI_AP_PASSWORD, sizeof(m.apPassword) - 1);
  strncpy(m.cloudSsid, cloudsync::ssid().c_str(), sizeof(m.cloudSsid) - 1);
  strncpy(m.cloudDetail, cs.detail, sizeof(m.cloudDetail) - 1);
  m.cloudTrips = cs.tripsUploaded;
  m.cloudGpx = cs.gpxUploaded;
  m.cloudFiles = cs.filesDownloaded;
  m.camCount = cams::count();

  m.soundOn = g_cfg->soundOn != 0;
  m.obdOn = g_cfg->obdOn != 0;
  m.autoSyncOn = g_cfg->autoSync != 0;
  m.screenIdx = g_cfg->screenIdx;
  m.screenCount = kScreenTimeoutCount;
  m.screenTimeoutS = kScreenTimeouts[g_cfg->screenIdx];
  strncpy(m.version, fwVersionFull(), sizeof(m.version) - 1);
}

}  // namespace

// Kundvaljaren: namnen kopieras till egna buffertar innan arket byggs.
void openCustomersFromGui() {
  customers::reload();
  GuiModel m;
  fillModel(m);
  uint8_t n = customers::count();
  if (n > 24) n = 24;
  for (uint8_t i = 0; i < n; i++) {
    strncpy(g_custNames[i], customers::name(i), sizeof(g_custNames[i]) - 1);
    g_custNames[i][sizeof(g_custNames[i]) - 1] = '\0';
    g_custPtrs[i] = g_custNames[i];
  }
  m.customerCount = n;
  m.customerNames = g_custPtrs;
  gui_screens_open_customers(&m);
}

namespace gui {

void begin(Arduino_GFX *panel, TouchDrvFT6X36 *touch, bool touchOk,
           AppSettings *cfg, void (*saveSettings)(), void (*applySettings)()) {
  g_panel = panel;
  g_touch = touch;
  g_touchOk = touchOk;
  g_cfg = cfg;
  g_save = saveSettings;
  g_apply = applySettings;

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  g_disp = lv_display_create(SCREEN_W, SCREEN_H);

  // Ritbufferten: en bit av skarmen i taget, i internminnet dar renderingen
  // ar snabb. Racker inte internminnet duger psram - langsammare men helt.
  // 60 rader, inte mer: varje kilobyte har konkurrerar med molnsynkens
  // tls-anslutningar om internminnet, och en synk som svalter ar dyrare an
  // nagra extra flush-omgangar pa en 80 MHz-buss.
  // Hur manga rader som ritas at gangen ar en avvagning mot internminnet.
  // Pa 3.5-kortet ar det knappt: accesspunkt, station och tls ska samsas i
  // samma minne, och 60 rader hade lagt beslag pa 38 kilobyte av det. 24
  // rader kostar nagra fler flushar - de tar en millisekund styck - och
  // lamnar tjugotre kilobyte till handskakningen.
#if defined(BOARD_LCD35)
  const size_t bufBytes = SCREEN_W * 24 * 2;
#else
  const size_t bufBytes = SCREEN_W * 60 * 2;
#endif
  void *buf = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) buf = ps_malloc(bufBytes);
  lv_display_set_buffers(g_disp, buf, nullptr, bufBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(g_disp, flush_cb);
#if !defined(BOARD_LCD35)
  // Avrundaren behovs bara for RM690B0-panelens jamna adressfonster;
  // ST7796 tar vilka fonster som helst.
  lv_display_add_event_cb(g_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, nullptr);
#endif

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);
  // Touchen lases var 20:e ms i stallet for var 33:e - ett snabbt tryck ska
  // fangas aven om fingret bara nuddar. Kanslan sitter i avlasningstakten.
  // 10 ms mellan avlasningarna: touchen sitter pa 400 kHz-i2c och en
  // lasning kostar under en millisekund, sa taten poll ar gratis - och
  // varje halverad vantan syns direkt i hur kvickt ett tryck tar.
  lv_timer_set_period(lv_indev_get_read_timer(indev), 10);

  gui_screens_create(&kActions);
}

void tick() {
  // ---- fragan efter en avslutad resa: nedrakningen och ljudet
  const TripStatus t = trip::status();
  if (t.awaitingPurpose && !g_askArmed) {
    g_askArmed = true;
    g_askStartMs = millis();
    setDisplayOn(true);
    sound::play(CUE_TRIP_END);
  } else if (!t.awaitingPurpose && g_askArmed) {
    g_askArmed = false;
    // Resan ar avslutad och fardigmarkt - ratt ogonblick att synka: forst
    // nu ar syfte och kund satta, sa uppladdningen far med sig allt.
    cloudsync::requestSync();
  }
  if (g_askArmed && millis() - g_askStartMs > kAskMs) {
    // Ingen svarade. Resan var diffus, och det ar det som skrivs - att gissa
    // privat eller foretag ur tystnad vore att hitta pa.
    trip::setPurpose(PURPOSE_DIFFUST);
    g_askArmed = false;
    cloudsync::requestSync();
  }

  // ---- modellen in i skarmarna, nagra ganger i sekunden
  static uint32_t lastModelMs = 0;
  if (millis() - lastModelMs >= 150) {
    lastModelMs = millis();
    GuiModel m;
    fillModel(m);
    gui_screens_update(&m);
  }

  // ---- skarmslackningen. Under en pagaende resa ar skarmen hela poangen
  // och slacks aldrig; med fragan uppe likasa. Nar bilen star parkerad
  // slacks den - bade for strommen och for att en amoled inte mar bra av en
  // stillastaende bild i timmar.
  const uint16_t timeout = kScreenTimeouts[g_cfg->screenIdx];
  if (g_displayOn && timeout > 0 && !t.active && !g_askArmed &&
      lv_display_get_inactive_time(g_disp) > (uint32_t)timeout * 1000UL) {
    setDisplayOn(false);
  }

  // Varvluckan: tiden sedan forra gui-varvet, minus det egna arbetet. Ar
  // den stor har nagot annat i huvudloopen hallit skarmen vantande.
  const int64_t entry = esp_timer_get_time();
  if (g_pfLastTickUs != 0) {
    const uint32_t gap = (uint32_t)(entry - g_pfLastTickUs);
    if (gap > g_pfGapMaxUs) g_pfGapMaxUs = gap;
  }

  lv_timer_handler();

  const int64_t done = esp_timer_get_time();
  const uint32_t us = (uint32_t)(done - entry);
  g_pfLvUs += us;
  g_pfLvN++;
  if (us > g_pfLvMaxUs) g_pfLvMaxUs = us;
  g_pfLastTickUs = done;

  perfReport();
}

void setDisplayOn(bool on) {
  if (on == g_displayOn) return;
  g_displayOn = on;
  if (on) {
#if defined(BOARD_LCD35)
    panel35::displayOn(true);
#else
    g_panel->displayOn();
#endif
    panelBrightness(235);
    lv_display_trigger_activity(g_disp);
    lv_obj_invalidate(lv_screen_active());
  } else {
    panelBrightness(0);
#if defined(BOARD_LCD35)
    panel35::displayOn(false);
#else
    g_panel->displayOff();
#endif
  }
}

bool displayOn() { return g_displayOn; }

}  // namespace gui
