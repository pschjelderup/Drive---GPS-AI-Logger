// Host-forhandsvisning av enhetens skarmar.
//
// Skarmkoden ar ren LVGL och vet inget om hardvaran, sa den kan renderas
// pa en vanlig dator: en display med buffert i minnet, en pahittad modell
// med trovariga varden, och en PPM-bild per skarm ut. Det ar sa designen
// granskas - pa bild, innan nagon flashar nagot.
//
// Bygg: se bygg.sh i samma mapp.

#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gui_model.h"
#include "gui_screens.h"

// Matten foljer kortvalet: bygg med -DBOARD_LCD35 for 3.5-kortets 320x480.
static const int W = GUI_W, H = GUI_H;
static uint16_t fb[W * H];

static uint32_t g_ms = 0;
static uint32_t tick_cb() { return g_ms += 16; }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  const int aw = area->x2 - area->x1 + 1;
  for (int y = area->y1; y <= area->y2; y++) {
    memcpy(&fb[y * W + area->x1], px + (y - area->y1) * aw * 2, aw * 2);
  }
  lv_display_flush_ready(disp);
}

static void dump_ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    const uint16_t c = fb[i];
    const uint8_t rgb[3] = {
        (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
        (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
        (uint8_t)((c & 0x1F) * 255 / 31),
    };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void render(GuiScreen s, const GuiModel *m, const char *path) {
  gui_screens_show(s, false);
  gui_screens_update(m);
  for (int i = 0; i < 12; i++) {
    lv_tick_inc(30);
    lv_timer_handler();
  }
  lv_refr_now(nullptr);
  dump_ppm(path);
  printf("skrev %s\n", path);
}

// Atgarderna gor ingenting pa vardadatorn - har finns ingen resa att avsluta.
static void noopPurpose(GuiPurpose) {}
static void noop() {}
static void noopPick(const char *) {}
static void noopTare(void (*done)(bool)) { done(true); }
static void noopSound(bool) {}
static void noopIdx(uint8_t) {}

int main() {
  lv_init();
  lv_tick_set_cb(tick_cb);

  lv_display_t *disp = lv_display_create(W, H);
  static uint16_t draw_buf[W * 100];
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);

  static GuiActions act = {
      noopPurpose, noop, noop, noop, noopPick, noop,
      noopSound, noopIdx, noopTare, noop, noop, noopSound,
      noopSound, noop,
  };
  gui_screens_create(&act);

  GuiModel m = {};
  snprintf(m.clock, sizeof(m.clock), "07:32");
  m.gpsPresent = true; m.gpsFix = true; m.sats = 14;
  m.sdOk = true; m.cloudConfigured = true;
  m.speedKmh = 87.0f; m.limitKmh = 80; m.speedTrusted = true;
  m.camsLoaded = true; m.limitsLoaded = true;
  m.camActive = true; m.camDistanceM = 420; m.camLimitKmh = 80;
  m.tripActive = true; m.tripIndex = 12; m.tripKm = 37.4;
  m.tripElapsedS = 41 * 60; m.tripMovingS = 39 * 60;
  m.maxSpeedKmh = 112; m.purpose = GUI_PURPOSE_FORETAG;
  snprintf(m.customer, sizeof(m.customer), "IS Tools");
  m.stopAfterS = 240;
  m.ecoScore = 92; m.ecoTripScore = 88; m.ecoMeasured = true;
  m.ecoMagG = 0.12f; m.ecoLonG = 0.08f; m.ecoLatG = -0.05f;
  m.ecoPeakG = 0.34f; m.ecoLevelled = true;
  m.ecoForwardKnown = true; m.ecoForwardQuality = 1.0f;
  m.ecoSoftG = 0.15f; m.ecoHardG = 0.30f; m.ecoBubbleG = 0.40f;
  m.ecoHardAccel = 1; m.ecoHardBrake = 2; m.ecoHardTurn = 0;
  m.statTotalKm = 1284; m.statTrips = 46; m.statMovingS = 27 * 3600;
  m.statPoints = 31240; m.statMaxKmh = 118; m.statSpeedingS = 12 * 60;
  m.statPrivatKm = 310; m.statForetagKm = 890; m.statDiffustKm = 84;
  m.statFreeMb = 29800; m.statCardMb = 30432; m.statKmLeft = 412000;
  snprintf(m.apSsid, sizeof(m.apSsid), "Hikaya");
  snprintf(m.apPassword, sizeof(m.apPassword), "kordagbok");
  snprintf(m.cloudSsid, sizeof(m.cloudSsid), "Pelles iPhone");
  snprintf(m.cloudDetail, sizeof(m.cloudDetail), "senaste synken gick igenom");
  m.cloudTrips = 6; m.cloudGpx = 6; m.cloudFiles = 3;
  m.camCount = 2771;
  // Obd-tillvalet paslaget och en bil som svarar - forhandsvisningen ska
  // visa skarmen som den ser ut nar allt ar uppkopplat.
  m.obdOn = true;
  m.obdState = 4;  // OBD_LIVE
  m.obdHas = 0x7FF;  // allt utom hybridbatteriet - en diesel
  m.obdRpm = 1850; m.obdSpeedKmh = 86;
  m.obdCoolantC = 88; m.obdIntakeC = 31; m.obdAmbientC = 17; m.obdOilC = 96;
  m.obdLoadPct = 42; m.obdThrottlePct = 24; m.obdFuelPct = 63;
  m.obdHybridPct = 0; m.obdFlowLh = 5.4f; m.obdVoltage = 14.2f;
  snprintf(m.obdAdapter, sizeof(m.obdAdapter), "Vgate iCar Pro");
  m.obdTripLiters = 2.31f; m.obdTripMaxRpm = 3120; m.obdTripIdleS = 214;

  m.soundOn = true; m.screenIdx = 3; m.screenCount = 8;
  m.screenTimeoutS = 300;
  snprintf(m.version, sizeof(m.version), "fe6882a PR9");

  render(GUI_SCR_HOME, &m, "home.ppm");
  render(GUI_SCR_DRIVE, &m, "drive.ppm");
  render(GUI_SCR_ECO, &m, "eco.ppm");
  render(GUI_SCR_STATS, &m, "stats.ppm");
  render(GUI_SCR_CLOUD, &m, "cloud.ppm");
  render(GUI_SCR_OBD, &m, "obd.ppm");
  render(GUI_SCR_SETTINGS, &m, "settings.ppm");

  // Fragan efter resan, over korskarmen.
  m.askPurpose = true; m.askIndex = 12; m.askKm = 37.4;
  m.askSecondsLeft = 47;
  render(GUI_SCR_DRIVE, &m, "ask.ppm");
  m.askPurpose = false;

  // Kundvaljaren.
  static const char *kunder[] = {
      "Creative Enabler", "IS Tools", "VERIDA", "Searide",
      "Hayseed Studio", "Infoflow", "XL Bygg", "Blandat",
  };
  m.customerCount = 8;
  m.customerNames = kunder;
  gui_screens_open_customers(&m);
  for (int i = 0; i < 12; i++) { lv_tick_inc(30); lv_timer_handler(); }
  lv_refr_now(nullptr);
  dump_ppm("kunder.ppm");
  printf("skrev kunder.ppm\n");

  return 0;
}
