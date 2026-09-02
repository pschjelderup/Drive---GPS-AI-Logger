#include "gui_screens.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- paletten -
// Djupsvart botten - pa en AMOLED ar svart slackta pixlar, sa allt ljus i
// bilden ar avsiktligt. En brandfarg (vagbla, samma som webben), tre
// signalfarger i neonstyrka, och syftesfargerna fran diagrampaletten.

#define COL_BG lv_color_hex(0x05070C)
#define COL_PANEL_W lv_color_hex(0xFFFFFF)
#define COL_TEXT lv_color_hex(0xEAF0F8)
#define COL_DIM lv_color_hex(0x8C9AAC)
#define COL_FAINT lv_color_hex(0x46505E)
#define COL_ACCENT lv_color_hex(0x2F7BFF)
#define COL_CYAN lv_color_hex(0x22D3EE)
#define COL_GREEN lv_color_hex(0x2FC47E)
#define COL_AMBER lv_color_hex(0xF5A623)
#define COL_RED lv_color_hex(0xF0524A)
#define COL_PRIVAT lv_color_hex(0x3987E5)
#define COL_FORETAG lv_color_hex(0x199E70)
#define COL_DIFFUST lv_color_hex(0xC98500)
#define COL_VIOLET lv_color_hex(0x7C5CFF)

// ---------------------------------------------------------------- skalan --
// Allt nedan ar ritat i 450x600. SX/SY raknar om till skarmens verkliga
// matt - pa AMOLED-kortet ar de identitetsfunktioner. Runda saker maste
// skalas med SAMMA faktor pa bada axlarna (annars blir cirklar agg), sa de
// anvander SX for bade bredd och hojd.
#define SX(v) ((int)(((long)(v) * GUI_W) / 450))
#define SY(v) ((int)(((long)(v) * GUI_H) / 600))

// Typsnitten i samma skala: 3.5-kortets uppsattning ar genererad ur samma
// snitt i 0,71 ganger storleken.
#if defined(BOARD_LCD35)
#define F16 (&ui_font_12)
#define F20 (&ui_font_14)
#define F26 (&ui_font_18)
#define F44 (&ui_font_32)
#define F150 (&ui_font_110)
#define FSYM20 (&lv_font_montserrat_14)
#define FSYM28 (&lv_font_montserrat_20)
#else
#define F16 (&ui_font_16)
#define F20 (&ui_font_20)
#define F26 (&ui_font_26)
#define F44 (&ui_font_44)
#define F150 (&ui_font_150)
#define FSYM20 (&lv_font_montserrat_20)
#define FSYM28 (&lv_font_montserrat_28)
#endif

static const GuiActions *g_act = nullptr;
static GuiModel g_m = {};  // senaste modellen, for uppdateringarna
static GuiScreen g_current = GUI_SCR_DRIVE;

// ---------------------------------------------------------------- hjalpare -

static lv_obj_t *g_screens[GUI_SCR_COUNT];

// Glaspanel: vit med lag opacitet och en svagt ljusare kant. Det ar
// genomskinligheten som gor att panelerna lever mot den morka botten.
static lv_obj_t *glass(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_bg_color(o, COL_PANEL_W, 0);
  lv_obj_set_style_bg_opa(o, 20, 0);
  lv_obj_set_style_radius(o, 20, 0);
  lv_obj_set_style_border_color(o, COL_PANEL_W, 0);
  lv_obj_set_style_border_opa(o, 40, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font,
                       lv_color_t color, const char *txt) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, txt);
  return l;
}

// Satt bara texten nar den andrats. lv_label_set_text ritar alltid om, aven
// nar texten ar exakt densamma - och med varden som skrivs flera ganger i
// sekunden blir det en standig omritningsstorm som gor hela gui:t trogt och
// touchen kolik. Jamforelsen har ar vad som gor skarmen stilla i vila.
static void set_txt(lv_obj_t *l, const char *txt) {
  if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
}

// Samma sak for stilarna. lv_obj_set_style_* gar via lv_obj_refresh_style,
// som ogiltigforklarar objektet OVILLKORLIGT - aven nar vardet ar exakt det
// som redan galler. Korskarmen satte varje 150 ms farg pa tva bagar om
// 386 pixlar, fartsiffran, startknappen med skugga och tre syftesknappar
// med fem stilar var: nastan hela skarmen ritades om sju ganger i sekunden
// medan bilen stod parkerad och ingenting andrades. Det ar det som kandes
// som seghet. Vakterna nedan gor att bara det som faktiskt andrats ritas.
// (Flaggor, tillstand och bagvarden har redan egna early-outs i LVGL.)
static void set_text_color(lv_obj_t *o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) {
    lv_obj_set_style_text_color(o, c, 0);
  }
}
static void set_bg_color(lv_obj_t *o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) {
    lv_obj_set_style_bg_color(o, c, 0);
  }
}
static void set_bg_opa(lv_obj_t *o, lv_opa_t v) {
  if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != v) {
    lv_obj_set_style_bg_opa(o, v, 0);
  }
}
static void set_border_color(lv_obj_t *o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_border_color(o, LV_PART_MAIN), c)) {
    lv_obj_set_style_border_color(o, c, 0);
  }
}
static void set_border_opa(lv_obj_t *o, lv_opa_t v) {
  if (lv_obj_get_style_border_opa(o, LV_PART_MAIN) != v) {
    lv_obj_set_style_border_opa(o, v, 0);
  }
}
static void set_border_width(lv_obj_t *o, int32_t v) {
  if (lv_obj_get_style_border_width(o, LV_PART_MAIN) != v) {
    lv_obj_set_style_border_width(o, v, 0);
  }
}
static void set_shadow_color(lv_obj_t *o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_shadow_color(o, LV_PART_MAIN), c)) {
    lv_obj_set_style_shadow_color(o, c, 0);
  }
}
static void set_arc_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t part) {
  if (!lv_color_eq(lv_obj_get_style_arc_color(o, part), c)) {
    lv_obj_set_style_arc_color(o, c, part);
  }
}

// Fylld knapp med tryckkansla: morkare vid tryck, rundade horn.
static lv_obj_t *button(lv_obj_t *parent, lv_color_t bg, const char *txt,
                        const lv_font_t *font, lv_color_t fg,
                        lv_event_cb_t cb, void *user) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_style_bg_color(b, lv_color_darken(bg, 60), LV_STATE_PRESSED);
  lv_obj_t *l = label(b, font, fg, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

// Genomskinlig knapp med fargad kant - syskonet till webbens ghost-knappar.
static lv_obj_t *ghost_button(lv_obj_t *parent, lv_color_t tint,
                              const char *txt, const lv_font_t *font,
                              lv_event_cb_t cb, void *user) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_style_bg_color(b, tint, 0);
  lv_obj_set_style_bg_opa(b, 36, 0);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_style_border_color(b, tint, 0);
  lv_obj_set_style_border_opa(b, 130, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_bg_opa(b, 90, LV_STATE_PRESSED);
  lv_obj_t *l = label(b, font, tint, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

// Gesterna: svep NEDAT fran skarmens ovankant ger menyn, och svep i sidled
// bladdrar mellan skarmarna i en oandlig slinga.
//
// Underkanten ar med flit fri fran gester. Dar bor knapparna som markerar
// resan - privat, foretag, diffust - och ett svep uppat darifran tog trycken
// ur dem lika ofta som det ledde hem. En gest ska inte konkurrera med en
// knapp om samma fingerrorelse.
static const GuiScreen kOrder[] = {
    GUI_SCR_DRIVE, GUI_SCR_ECO, GUI_SCR_OBD,  GUI_SCR_STATS,
    GUI_SCR_CLOUD, GUI_SCR_HOME, GUI_SCR_SETTINGS,
};
static const uint8_t kOrderCount = sizeof(kOrder) / sizeof(kOrder[0]);

static void step_screen(int dir) {
  uint8_t at = 0;
  for (uint8_t i = 0; i < kOrderCount; i++) {
    if (kOrder[i] == g_current) { at = i; break; }
  }
  const uint8_t next = (uint8_t)((at + kOrderCount + dir) % kOrderCount);
  gui_screens_show(kOrder[next], true);
}

static void gesture_cb(lv_event_t *e) {
  (void)e;
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_BOTTOM: gui_screens_show(GUI_SCR_HOME, true); break;
    case LV_DIR_LEFT: step_screen(1); break;
    case LV_DIR_RIGHT: step_screen(-1); break;
    default: break;
  }
}

static void add_gestures(lv_obj_t *scr) {
  // Ett kort streck hogst upp: samma loft som telefonens greppstreck, fast
  // i den ande gesten faktiskt borjar.
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, SX(120), SY(5));
  lv_obj_align(bar, LV_ALIGN_TOP_MID, SX(0), SY(2));
  lv_obj_set_style_bg_color(bar, COL_PANEL_W, 0);
  lv_obj_set_style_bg_opa(bar, 80, 0);
  lv_obj_set_style_radius(bar, 3, 0);

  lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, nullptr);
}

// Statusraden: klockan till vanster, matthalsan till hoger. Samma rad pa
// alla skarmar, sa att ogat alltid vet var svaren finns.
struct StatusRefs {
  lv_obj_t *clock;
  lv_obj_t *gps;
  lv_obj_t *sd;
  lv_obj_t *cloud;
};
static StatusRefs g_status[GUI_SCR_COUNT];

static void add_status(lv_obj_t *scr, GuiScreen idx) {
  StatusRefs &r = g_status[idx];
  r.clock = label(scr, F26, COL_TEXT, "--:--");
  lv_obj_align(r.clock, LV_ALIGN_TOP_LEFT, SX(18), SY(8));

  r.cloud = label(scr, FSYM20, COL_FAINT, LV_SYMBOL_WIFI);
  lv_obj_align(r.cloud, LV_ALIGN_TOP_RIGHT, SX(-18), SY(12));
  r.sd = label(scr, FSYM20, COL_FAINT, LV_SYMBOL_SD_CARD);
  lv_obj_align(r.sd, LV_ALIGN_TOP_RIGHT, SX(-56), SY(12));
  r.gps = label(scr, FSYM20, COL_FAINT, LV_SYMBOL_GPS " 0");
  lv_obj_align(r.gps, LV_ALIGN_TOP_RIGHT, SX(-94), SY(12));
}

static void update_status(GuiScreen idx, const GuiModel *m) {
  StatusRefs &r = g_status[idx];
  if (!r.clock) return;
  set_txt(r.clock, m->clock[0] ? m->clock : "--:--");

  char buf[16];
  snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " %u", (unsigned)m->sats);
  set_txt(r.gps, buf);
  set_text_color(r.gps,
                 !m->gpsPresent ? COL_FAINT : (m->gpsFix ? COL_GREEN : COL_AMBER));
  set_text_color(r.sd, m->sdOk ? COL_GREEN : COL_RED);
  set_text_color(r.cloud,
                 m->apClient || m->cloudBusy ? COL_CYAN
                 : m->cloudConfigured ? COL_DIM : COL_FAINT);
}

static lv_obj_t *make_screen() {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

// ------------------------------------------------------------- hemskarmen -
// Telefonkanslan: gradient, klocka, appikoner i rutnat. Ikonerna ar ritade,
// inte bilder - gradienter och symboler racker langt och foljer paletten.

struct AppDef {
  const char *name;
  const char *symbol;
  uint32_t c1, c2;  // gradient topp/botten
  GuiScreen target;
};

static const AppDef kApps[7] = {
    {"Körning", LV_SYMBOL_GPS, 0x2F7BFF, 0x123B8F, GUI_SCR_DRIVE},
    {"Ecodrive", LV_SYMBOL_CHARGE, 0x2FC47E, 0x0B5A3A, GUI_SCR_ECO},
    {"Bilen", LV_SYMBOL_POWER, 0xFF6B6B, 0x8A2020, GUI_SCR_OBD},
    {"Statistik", LV_SYMBOL_BARS, 0x7C5CFF, 0x3B2A80, GUI_SCR_STATS},
    {"Moln", LV_SYMBOL_WIFI, 0x22D3EE, 0x0E5B6B, GUI_SCR_CLOUD},
    {"Kund", LV_SYMBOL_DIRECTORY, 0xF5A623, 0x8A5A0E, GUI_SCR_HOME},
    {"Inställn.", LV_SYMBOL_SETTINGS, 0x8C9AAC, 0x3A4250, GUI_SCR_SETTINGS},
};
static const uint8_t kAppCount = 7;

static lv_obj_t *g_tripChip;       // "resa pagar" pa hemskarmen
static lv_obj_t *g_tripChipLabel;

static void app_tap_cb(lv_event_t *e) {
  const AppDef *a = (const AppDef *)lv_event_get_user_data(e);
  if (a->target == GUI_SCR_HOME) {
    // Kund-appen ar en dialog, inte en skarm.
    if (g_act && g_act->openCustomers) g_act->openCustomers();
    return;
  }
  gui_screens_show(a->target, true);
}

static void trip_chip_cb(lv_event_t *e) {
  (void)e;
  gui_screens_show(GUI_SCR_DRIVE, true);
}

static void build_home() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_HOME] = scr;

  // Gradienten: natthimmel mot svart, med en svag gloed bakom rutnatet.
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1A36), 0);
  lv_obj_set_style_bg_grad_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  lv_obj_t *glow = lv_obj_create(scr);
  lv_obj_remove_style_all(glow);
  lv_obj_set_size(glow, SX(380), SY(380));
  lv_obj_align(glow, LV_ALIGN_TOP_MID, SX(0), SY(-160));
  lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(glow, COL_ACCENT, 0);
  lv_obj_set_style_bg_opa(glow, 26, 0);

  add_status(scr, GUI_SCR_HOME);

  lv_obj_t *brand = label(scr, F16, COL_DIM, "Hikaya");
  lv_obj_align(brand, LV_ALIGN_TOP_LEFT, SX(18), SY(42));

  // Rutnatet: 3 x 3 ikoner (sju anvanda). Tre kolumner sedan bilen fick en
  // egen app - med tva kolumner hade fjarde raden trangt undan resechipet.
  const int16_t tile = SX(112), gapx = SX(38);
  const int16_t x0 = (GUI_W - 3 * tile - 2 * gapx) / 2;
  const int16_t y0 = SY(84);
  const int16_t step = tile + SY(44);

  for (int i = 0; i < kAppCount; i++) {
    const AppDef &a = kApps[i];
    const int col = i % 3, row = i / 3;
    const int16_t x = x0 + col * (tile + gapx);
    const int16_t y = y0 + row * step;

    lv_obj_t *b = lv_button_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, tile, tile);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(a.c1), 0);
    lv_obj_set_style_bg_grad_color(b, lv_color_hex(a.c2), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, COL_PANEL_W, 0);
    lv_obj_set_style_border_opa(b, 46, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(a.c1), 0);
    lv_obj_set_style_shadow_width(b, 26, 0);
    lv_obj_set_style_shadow_opa(b, 70, 0);
    lv_obj_set_style_transform_scale(b, 242, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, app_tap_cb, LV_EVENT_CLICKED, (void *)&a);

    lv_obj_t *sym = label(b, FSYM28, lv_color_white(),
                          a.symbol);
    lv_obj_center(sym);

    lv_obj_t *name = label(scr, F16, COL_TEXT, a.name);
    lv_obj_set_width(name, tile + SX(26));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name, x - SX(13), y + tile + SY(5));
  }

  // Chip som visar att en resa pagar - trycket leder rakt in i korningen.
  g_tripChip = lv_button_create(scr);
  lv_obj_remove_style_all(g_tripChip);
  lv_obj_set_size(g_tripChip, SX(320), SY(38));
  lv_obj_align(g_tripChip, LV_ALIGN_BOTTOM_MID, SX(0), SY(-8));
  lv_obj_set_style_radius(g_tripChip, 20, 0);
  lv_obj_set_style_bg_color(g_tripChip, COL_GREEN, 0);
  lv_obj_set_style_bg_opa(g_tripChip, 46, 0);
  lv_obj_set_style_border_color(g_tripChip, COL_GREEN, 0);
  lv_obj_set_style_border_opa(g_tripChip, 140, 0);
  lv_obj_set_style_border_width(g_tripChip, 1, 0);
  lv_obj_add_event_cb(g_tripChip, trip_chip_cb, LV_EVENT_CLICKED, nullptr);
  g_tripChipLabel = label(g_tripChip, F20, COL_GREEN, "");
  lv_obj_center(g_tripChipLabel);
  lv_obj_add_flag(g_tripChip, LV_OBJ_FLAG_HIDDEN);

  // Menyn ar med i slingan som alla andra - svep i sidled harifran bladdrar
  // vidare, och svep nedat stannar kvar.
  add_gestures(scr);
}

static void update_home(const GuiModel *m) {
  update_status(GUI_SCR_HOME, m);
  if (m->tripActive) {
    char buf[48], km[16];
    snprintf(km, sizeof(km), "%.1f", m->tripKm);
    for (char *p = km; *p; p++) if (*p == '.') *p = ',';
    snprintf(buf, sizeof(buf), "Resa %lu pågår · %s km",
             (unsigned long)m->tripIndex, km);
    set_txt(g_tripChipLabel, buf);
    lv_obj_clear_flag(g_tripChip, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_tripChip, LV_OBJ_FLAG_HIDDEN);
  }
}

// ------------------------------------------------------------- korskarmen -
// Den renderade mataren: skala med streck, en ring vars farg foljer laget,
// farten i mitten stor nog att lasas i ogonvran, skylten dar ringen oppnar
// sig, och varningar som lagger sig OVER bilden i stallet for att flytta den.

static lv_obj_t *g_scale;
static lv_obj_t *g_arcGlow;
static lv_obj_t *g_arcMain;
static lv_obj_t *g_speedLbl;
static lv_obj_t *g_kmhLbl;
static lv_obj_t *g_deltaLbl;
static lv_obj_t *g_signRing, *g_signNum, *g_signOff;
static lv_obj_t *g_camPanel, *g_camTitle, *g_camBar, *g_camLimit;
static lv_obj_t *g_tripPanel, *g_tripTitle, *g_tripSub;
static lv_obj_t *g_tripBtn, *g_tripBtnLbl;
static lv_obj_t *g_splitBtn;
static lv_obj_t *g_purBtn[3];
static lv_obj_t *g_purLbl[3];

static const int kSpeedMax = 220;

static void trip_toggle_cb(lv_event_t *e) {
  (void)e;
  if (!g_act) return;
  if (g_m.tripActive) g_act->endTrip();
  else g_act->startTrip();
}

static void split_cb(lv_event_t *e) {
  (void)e;
  if (g_act && g_act->splitTrip) g_act->splitTrip();
}

static void purpose_cb(lv_event_t *e) {
  intptr_t p = (intptr_t)lv_event_get_user_data(e);
  if (g_act && g_act->setPurpose) g_act->setPurpose((GuiPurpose)p);
}

static void build_drive() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_DRIVE] = scr;
  add_status(scr, GUI_SCR_DRIVE);

  const int16_t cx = GUI_W / 2, cy = SY(240), r = SX(180);

  // Gloden bakom mataren - det ar den som gor att ringen ser tand ut.
  lv_obj_t *glow = lv_obj_create(scr);
  lv_obj_remove_style_all(glow);
  lv_obj_set_size(glow, 2 * r + 40, 2 * r + 40);
  lv_obj_set_pos(glow, cx - r - 20, cy - r - 20);
  lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(glow, COL_ACCENT, 0);
  lv_obj_set_style_bg_opa(glow, 18, 0);

  // Skalan: streck och siffror runt ringen.
  g_scale = lv_scale_create(scr);
  lv_scale_set_mode(g_scale, LV_SCALE_MODE_ROUND_INNER);
  lv_obj_set_size(g_scale, 2 * r, 2 * r);
  lv_obj_set_pos(g_scale, cx - r, cy - r);
  lv_scale_set_range(g_scale, 0, kSpeedMax);
  lv_scale_set_total_tick_count(g_scale, 23);
  lv_scale_set_major_tick_every(g_scale, 2);
  lv_scale_set_angle_range(g_scale, 240);
  lv_scale_set_rotation(g_scale, 150);
  lv_scale_set_label_show(g_scale, true);
  lv_obj_set_style_line_color(g_scale, COL_FAINT, LV_PART_ITEMS);
  lv_obj_set_style_line_width(g_scale, 2, LV_PART_ITEMS);
  lv_obj_set_style_length(g_scale, 10, LV_PART_ITEMS);
  lv_obj_set_style_line_color(g_scale, COL_DIM, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(g_scale, 3, LV_PART_INDICATOR);
  lv_obj_set_style_length(g_scale, 16, LV_PART_INDICATOR);
  lv_obj_set_style_text_font(g_scale, F16, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(g_scale, COL_DIM, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(g_scale, 0, LV_PART_MAIN);

  // Ringen i tva lager: en bred, svag - gloden - och en smal, skarp.
  auto make_arc = [&](int16_t width, lv_opa_t opa) {
    lv_obj_t *a = lv_arc_create(scr);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, 2 * r + 26, 2 * r + 26);
    lv_obj_set_pos(a, cx - r - 13, cy - r - 13);
    lv_arc_set_rotation(a, 150);
    lv_arc_set_bg_angles(a, 0, 240);
    lv_arc_set_range(a, 0, kSpeedMax);
    lv_arc_set_value(a, 0);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, COL_PANEL_W, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(a, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    return a;
  };
  g_arcGlow = make_arc(22, 60);
  g_arcMain = make_arc(10, LV_OPA_COVER);

  // Farten. Hogerstalld hade hallit entalssiffran stilla, men centrerat ar
  // lugnare mot ringen - och siffrorna ar breda nog att inte hoppa mycket.
  g_speedLbl = label(scr, F150, COL_TEXT, "0");
  lv_obj_align(g_speedLbl, LV_ALIGN_TOP_MID, SX(0), SY(130));
  g_kmhLbl = label(scr, F20, COL_DIM, "km/h");
  lv_obj_align(g_kmhLbl, LV_ALIGN_TOP_MID, SX(0), SY(292));
  g_deltaLbl = label(scr, F26, COL_DIM, "");
  lv_obj_align(g_deltaLbl, LV_ALIGN_TOP_MID, SX(0), SY(330));

  // Skylten: rund med rod ring, dar matarringen oppnar sig.
  g_signRing = lv_obj_create(scr);
  lv_obj_remove_style_all(g_signRing);
  lv_obj_set_size(g_signRing, SX(92), SX(92));
  lv_obj_align(g_signRing, LV_ALIGN_TOP_MID, SX(0), SY(372));
  lv_obj_set_style_radius(g_signRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_signRing, lv_color_hex(0xF5F5F5), 0);
  lv_obj_set_style_bg_opa(g_signRing, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_signRing, lv_color_hex(0xD42B2B), 0);
  lv_obj_set_style_border_width(g_signRing, 11, 0);
  lv_obj_set_style_shadow_color(g_signRing, lv_color_hex(0xD42B2B), 0);
  lv_obj_set_style_shadow_width(g_signRing, 22, 0);
  lv_obj_set_style_shadow_opa(g_signRing, 60, 0);
  g_signNum = label(g_signRing, F44, lv_color_hex(0x101010), "50");
  lv_obj_center(g_signNum);
  g_signOff = label(scr, F16, COL_FAINT, "gräns okänd");
  lv_obj_align(g_signOff, LV_ALIGN_TOP_MID, SX(0), SY(412));

  // Kameravarningen: ett lager OVER mataren, genomskinligt nog att farten
  // fortfarande syns bakom.
  g_camPanel = glass(scr);
  lv_obj_set_size(g_camPanel, SX(414), SY(66));
  lv_obj_align(g_camPanel, LV_ALIGN_TOP_MID, SX(0), SY(40));
  lv_obj_set_style_bg_color(g_camPanel, COL_AMBER, 0);
  lv_obj_set_style_bg_opa(g_camPanel, 90, 0);
  lv_obj_set_style_border_color(g_camPanel, COL_AMBER, 0);
  lv_obj_set_style_border_opa(g_camPanel, 200, 0);
  g_camTitle = label(g_camPanel, F26, COL_TEXT, "FARTKAMERA 800 m");
  lv_obj_align(g_camTitle, LV_ALIGN_TOP_LEFT, SX(14), SY(6));
  g_camLimit = label(g_camPanel, F44, COL_TEXT, "80");
  lv_obj_align(g_camLimit, LV_ALIGN_RIGHT_MID, SX(-12), SY(0));
  g_camBar = lv_bar_create(g_camPanel);
  lv_obj_remove_style_all(g_camBar);
  lv_obj_set_size(g_camBar, SX(300), SY(8));
  lv_obj_align(g_camBar, LV_ALIGN_BOTTOM_LEFT, SX(14), SY(-8));
  lv_bar_set_range(g_camBar, 0, 800);
  lv_obj_set_style_bg_color(g_camBar, COL_PANEL_W, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_camBar, 60, LV_PART_MAIN);
  lv_obj_set_style_radius(g_camBar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_camBar, COL_TEXT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(g_camBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_camBar, 4, LV_PART_INDICATOR);
  lv_obj_add_flag(g_camPanel, LV_OBJ_FLAG_HIDDEN);

  // Resepanelen.
  g_tripPanel = glass(scr);
  lv_obj_set_size(g_tripPanel, SX(414), SY(64));
  lv_obj_align(g_tripPanel, LV_ALIGN_TOP_MID, SX(0), SY(470));
  g_tripTitle = label(g_tripPanel, F20, COL_TEXT, "Ingen resa pågår");
  lv_obj_align(g_tripTitle, LV_ALIGN_TOP_LEFT, SX(14), SY(8));
  lv_label_set_long_mode(g_tripTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_tripTitle, SX(230));
  lv_obj_set_height(g_tripTitle, lv_font_get_line_height(F20) + 2);
  g_tripSub = label(g_tripPanel, F16, COL_DIM, "");
  lv_obj_align(g_tripSub, LV_ALIGN_BOTTOM_LEFT, SX(14), SY(-8));
  lv_label_set_long_mode(g_tripSub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_tripSub, SX(230));
  lv_obj_set_height(g_tripSub, lv_font_get_line_height(F16) + 2);

  // Start/stopp ar skarmens viktigaste knapp och sitter i en bil - den ska
  // ga att traffa med tummen utan att titta. Darfor ar den stor och ligger
  // pa skarmen, inte i panelen: en 96-pixlars knapp far inte plats i en
  // 64 pixlar hog panel, sa den flyter over panelkanten.
  g_tripBtn = lv_button_create(scr);
  lv_obj_remove_style_all(g_tripBtn);
  lv_obj_set_size(g_tripBtn, SX(96), SX(96));
  lv_obj_align(g_tripBtn, LV_ALIGN_TOP_RIGHT, SX(-18), SY(440));
  lv_obj_set_style_radius(g_tripBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_tripBtn, COL_GREEN, 0);
  lv_obj_set_style_bg_opa(g_tripBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(g_tripBtn, COL_GREEN, 0);
  lv_obj_set_style_shadow_width(g_tripBtn, 24, 0);
  lv_obj_set_style_shadow_opa(g_tripBtn, 90, 0);
  lv_obj_add_event_cb(g_tripBtn, trip_toggle_cb, LV_EVENT_CLICKED, nullptr);
  g_tripBtnLbl = label(g_tripBtn, FSYM28, lv_color_white(),
                       LV_SYMBOL_PLAY);
  lv_obj_center(g_tripBtnLbl);

  g_splitBtn = lv_button_create(scr);
  lv_obj_remove_style_all(g_splitBtn);
  lv_obj_set_size(g_splitBtn, SX(64), SX(64));
  lv_obj_align(g_splitBtn, LV_ALIGN_TOP_RIGHT, SX(-126), SY(470));
  lv_obj_set_style_radius(g_splitBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_splitBtn, COL_ACCENT, 0);
  lv_obj_set_style_bg_opa(g_splitBtn, 90, 0);
  lv_obj_set_style_border_color(g_splitBtn, COL_ACCENT, 0);
  lv_obj_set_style_border_opa(g_splitBtn, 160, 0);
  lv_obj_set_style_border_width(g_splitBtn, 1, 0);
  lv_obj_add_event_cb(g_splitBtn, split_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sp = label(g_splitBtn, FSYM20, COL_TEXT,
                       LV_SYMBOL_CUT);
  lv_obj_center(sp);
  lv_obj_add_flag(g_splitBtn, LV_OBJ_FLAG_HIDDEN);

  // Hemsvepet laggs FORE syftesknapparna: det som skapas sist ligger
  // overst, och med baren under stjal den inte langre trycken ur
  // knapparnas nederkant - en av anledningarna till att de var svara
  // att traffa.
  add_gestures(scr);

  // Syftesknapparna: den valda fylls, de andra ar glas. Stora nog att
  // traffas med tummen i farthållarlage - 140 x 60 i designmatt.
  static const char *names[3] = {"PRIVAT", "FÖRETAG", "DIFFUST"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, SX(140), SY(60));
    lv_obj_set_pos(b, SX(9 + i * 146), SY(532));
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_add_event_cb(b, purpose_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(i + 1));
    g_purBtn[i] = b;
    g_purLbl[i] = label(b, F20, COL_DIM, names[i]);
    lv_obj_center(g_purLbl[i]);
  }
}

static void style_purpose(int i, lv_color_t tint, bool active) {
  lv_obj_t *b = g_purBtn[i];
  if (active) {
    set_bg_color(b, tint);
    set_bg_opa(b, LV_OPA_COVER);
    set_border_width(b, 0);
    set_text_color(g_purLbl[i], lv_color_white());
  } else {
    set_bg_color(b, tint);
    set_bg_opa(b, 30);
    set_border_color(b, tint);
    set_border_opa(b, 110);
    set_border_width(b, 1);
    set_text_color(g_purLbl[i], tint);
  }
}

static void update_drive(const GuiModel *m) {
  update_status(GUI_SCR_DRIVE, m);

  // Fart och ring. Fargen betyder samma sak overallt: gron under gransen,
  // barnsten intill, rod over, och lugn vagbla nar gransen ar okand.
  const int kmh = (int)(m->speedKmh < 0 ? 0 : m->speedKmh + 0.5f);
  lv_color_t zone = COL_ACCENT;
  if (m->limitKmh > 0) {
    if (m->speedKmh > m->limitKmh + 3.0f) zone = COL_RED;
    else if (m->speedKmh > m->limitKmh - 5.0f) zone = COL_AMBER;
    else zone = COL_GREEN;
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "%d", kmh);
  set_txt(g_speedLbl, buf);
  set_text_color(g_speedLbl, zone);
  lv_arc_set_value(g_arcMain, kmh > kSpeedMax ? kSpeedMax : kmh);
  lv_arc_set_value(g_arcGlow, kmh > kSpeedMax ? kSpeedMax : kmh);
  set_arc_color(g_arcMain, zone, LV_PART_INDICATOR);
  set_arc_color(g_arcGlow, zone, LV_PART_INDICATOR);

  if (m->limitKmh > 0) {
    lv_obj_clear_flag(g_signRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_signOff, LV_OBJ_FLAG_HIDDEN);
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->limitKmh);
    set_txt(g_signNum, buf);

    const float delta = m->speedKmh - (float)m->limitKmh;
    if (delta > 3.0f) {
      snprintf(buf, sizeof(buf), "+%d över", (int)(delta + 0.5f));
      set_text_color(g_deltaLbl, COL_RED);
    } else if (delta < -1.0f) {
      snprintf(buf, sizeof(buf), "%d under", (int)(delta - 0.5f));
      set_text_color(g_deltaLbl, COL_GREEN);
    } else {
      snprintf(buf, sizeof(buf), "på gränsen");
      set_text_color(g_deltaLbl, COL_AMBER);
    }
    set_txt(g_deltaLbl, buf);
  } else {
    lv_obj_add_flag(g_signRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_signOff, LV_OBJ_FLAG_HIDDEN);
    set_txt(g_signOff, m->limitsLoaded
        ? "ingen skyltad hastighet här" : "hastighetsfilen saknas");
    set_txt(g_deltaLbl, "");
  }

  // Kameravarningen.
  if (m->camActive) {
    lv_obj_clear_flag(g_camPanel, LV_OBJ_FLAG_HIDDEN);
    const bool near = m->camDistanceM <= 250;
    lv_color_t t = near ? COL_RED : COL_AMBER;
    set_bg_color(g_camPanel, t);
    set_border_color(g_camPanel, t);
    snprintf(buf, sizeof(buf), "FARTKAMERA  %lu m",
             (unsigned long)m->camDistanceM);
    set_txt(g_camTitle, buf);
    if (m->camLimitKmh > 0) {
      snprintf(buf, sizeof(buf), "%u", (unsigned)m->camLimitKmh);
      set_txt(g_camLimit, buf);
    } else {
      set_txt(g_camLimit, "");
    }
    int32_t v = 800 - (int32_t)m->camDistanceM;
    lv_bar_set_value(g_camBar, v < 0 ? 0 : v, LV_ANIM_OFF);
  } else {
    lv_obj_add_flag(g_camPanel, LV_OBJ_FLAG_HIDDEN);
  }

  // Resan.
  if (m->tripActive) {
    char km[16];
    snprintf(km, sizeof(km), "%.1f", m->tripKm);
    for (char *p = km; *p; p++) if (*p == '.') *p = ',';
    snprintf(buf, sizeof(buf), "Resa %lu · %s km · %02lu:%02lu",
             (unsigned long)m->tripIndex, km,
             (unsigned long)(m->tripElapsedS / 3600),
             (unsigned long)((m->tripElapsedS % 3600) / 60));
    set_txt(g_tripTitle, buf);
    if (m->waitingForFix) {
      set_txt(g_tripSub, "väntar på GPS-fix");
    } else if (m->stoppedS > 15) {
      snprintf(buf, sizeof(buf), "står stilla %lu s – avslutas vid %lu s",
               (unsigned long)m->stoppedS, (unsigned long)m->stopAfterS);
      set_txt(g_tripSub, buf);
    } else if (m->customer[0]) {
      snprintf(buf, sizeof(buf), "kund: %s", m->customer);
      set_txt(g_tripSub, buf);
    } else {
      snprintf(buf, sizeof(buf), "max %d km/h", (int)(m->maxSpeedKmh + 0.5f));
      set_txt(g_tripSub, buf);
    }
    set_bg_color(g_tripBtn, COL_RED);
    set_shadow_color(g_tripBtn, COL_RED);
    set_txt(g_tripBtnLbl, LV_SYMBOL_STOP);
    lv_obj_clear_flag(g_splitBtn, LV_OBJ_FLAG_HIDDEN);
  } else {
    set_txt(g_tripTitle,
                      m->sdOk ? "Ingen resa pågår" : "Inget minneskort");
    set_txt(g_tripSub,
                      m->sdOk ? "startar själv när bilen rullar"
                              : "resor kan inte sparas utan kort");
    set_bg_color(g_tripBtn, COL_GREEN);
    set_shadow_color(g_tripBtn, COL_GREEN);
    set_txt(g_tripBtnLbl, LV_SYMBOL_PLAY);
    lv_obj_add_flag(g_splitBtn, LV_OBJ_FLAG_HIDDEN);
  }

  style_purpose(0, COL_PRIVAT, m->purpose == GUI_PURPOSE_PRIVAT);
  style_purpose(1, COL_FORETAG, m->purpose == GUI_PURPOSE_FORETAG);
  style_purpose(2, COL_DIFFUST, m->purpose == GUI_PURPOSE_DIFFUST);
}

// ------------------------------------------------------------- ecodrive ---

static lv_obj_t *g_ecoArc, *g_ecoScoreLbl, *g_ecoAvgLbl;
static lv_obj_t *g_ecoBubbleWrap, *g_ecoBubble, *g_ecoMagLbl;
static lv_obj_t *g_ecoRingSoft, *g_ecoRingHard;
static lv_obj_t *g_ecoInfo;
static int16_t kEcoR = SX(130);

static void eco_reset_cb(lv_event_t *e) {
  (void)e;
  if (g_act && g_act->ecoReset) g_act->ecoReset();
}

static lv_obj_t *g_tareBtnLbl;
static void tare_done(bool ok) {
  if (g_tareBtnLbl) {
    set_txt(g_tareBtnLbl, ok ? "TARERAD" : "STÅ STILL");
  }
}
static void tare_cb(lv_event_t *e) {
  (void)e;
  if (g_act && g_act->tare) g_act->tare(tare_done);
}

static void build_eco() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_ECO] = scr;
  add_status(scr, GUI_SCR_ECO);

  lv_obj_t *title = label(scr, F26, COL_TEXT, "Ecodrive");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, SX(18), SY(44));

  // Resans medel som ring uppe till hoger.
  g_ecoArc = lv_arc_create(scr);
  lv_obj_remove_style_all(g_ecoArc);
  lv_obj_set_size(g_ecoArc, SX(96), SY(96));
  lv_obj_align(g_ecoArc, LV_ALIGN_TOP_RIGHT, SX(-18), SY(52));
  lv_arc_set_rotation(g_ecoArc, 135);
  lv_arc_set_bg_angles(g_ecoArc, 0, 270);
  lv_arc_set_range(g_ecoArc, 0, 100);
  lv_obj_remove_flag(g_ecoArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(g_ecoArc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_color(g_ecoArc, COL_PANEL_W, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(g_ecoArc, 20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(g_ecoArc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_ecoArc, COL_GREEN, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g_ecoArc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(g_ecoArc, 0, LV_PART_KNOB);
  g_ecoScoreLbl = label(g_ecoArc, F44, COL_TEXT, "100");
  lv_obj_center(g_ecoScoreLbl);
  g_ecoAvgLbl = label(scr, F16, COL_DIM, "resans medel");
  lv_obj_align(g_ecoAvgLbl, LV_ALIGN_TOP_RIGHT, SX(-18), SY(140));

  // Bubblan: ett vattenpass baklanges - den ska sta stilla i mitten.
  g_ecoBubbleWrap = lv_obj_create(scr);
  lv_obj_remove_style_all(g_ecoBubbleWrap);
  lv_obj_set_size(g_ecoBubbleWrap, 2 * kEcoR + 4, 2 * kEcoR + 4);
  lv_obj_align(g_ecoBubbleWrap, LV_ALIGN_TOP_MID, SX(0), SY(170));
  lv_obj_set_style_radius(g_ecoBubbleWrap, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(g_ecoBubbleWrap, COL_FAINT, 0);
  lv_obj_set_style_border_width(g_ecoBubbleWrap, 1, 0);
  lv_obj_set_style_bg_color(g_ecoBubbleWrap, COL_PANEL_W, 0);
  lv_obj_set_style_bg_opa(g_ecoBubbleWrap, 8, 0);
  lv_obj_clear_flag(g_ecoBubbleWrap, LV_OBJ_FLAG_SCROLLABLE);

  auto ring = [&](lv_color_t c) {
    lv_obj_t *o = lv_obj_create(g_ecoBubbleWrap);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_opa(o, 150, 0);
    lv_obj_center(o);
    return o;
  };
  g_ecoRingSoft = ring(COL_GREEN);
  g_ecoRingHard = ring(COL_RED);

  g_ecoBubble = lv_obj_create(g_ecoBubbleWrap);
  lv_obj_remove_style_all(g_ecoBubble);
  lv_obj_set_size(g_ecoBubble, SX(34), SY(34));
  lv_obj_set_style_radius(g_ecoBubble, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_ecoBubble, COL_GREEN, 0);
  lv_obj_set_style_bg_opa(g_ecoBubble, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(g_ecoBubble, COL_GREEN, 0);
  lv_obj_set_style_shadow_width(g_ecoBubble, 24, 0);
  lv_obj_set_style_shadow_opa(g_ecoBubble, 140, 0);
  lv_obj_center(g_ecoBubble);

  g_ecoMagLbl = label(g_ecoBubbleWrap, F26, COL_TEXT, "0,00 g");
  lv_obj_align(g_ecoMagLbl, LV_ALIGN_CENTER, SX(0), SY(-6));

  g_ecoInfo = label(scr, F16, COL_DIM, "");
  lv_obj_align(g_ecoInfo, LV_ALIGN_TOP_MID, SX(0), SY(448));
  lv_obj_set_style_text_align(g_ecoInfo, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *reset = ghost_button(scr, COL_ACCENT, "NOLLSTÄLL", F20,
                                 eco_reset_cb, nullptr);
  lv_obj_set_size(reset, SX(200), SY(48));
  lv_obj_align(reset, LV_ALIGN_BOTTOM_LEFT, SX(18), SY(-34));

  lv_obj_t *tare = ghost_button(scr, COL_CYAN, "TARA", F20,
                                tare_cb, nullptr);
  lv_obj_set_size(tare, SX(200), SY(48));
  lv_obj_align(tare, LV_ALIGN_BOTTOM_RIGHT, SX(-18), SY(-34));
  g_tareBtnLbl = lv_obj_get_child(tare, 0);

  add_gestures(scr);
}

static void update_eco(const GuiModel *m) {
  update_status(GUI_SCR_ECO, m);
  char buf[96];

  const int score = (int)(m->ecoTripScore + 0.5f);
  snprintf(buf, sizeof(buf), "%d", score);
  set_txt(g_ecoScoreLbl, buf);
  lv_arc_set_value(g_ecoArc, score);
  lv_color_t sc = score >= 75 ? COL_GREEN : score >= 40 ? COL_AMBER : COL_RED;
  set_arc_color(g_ecoArc, sc, LV_PART_INDICATOR);
  set_txt(g_ecoAvgLbl,
                    m->ecoMeasured ? "resans medel" : "mäter …");

  // Granserna ritas dar de ligger; flyttas de i granssnittet foljer ringarna.
  const float full = m->ecoBubbleG > 0.05f ? m->ecoBubbleG : 0.4f;
  const float pxPerG = (float)kEcoR / full;
  const int16_t rs = (int16_t)(m->ecoSoftG * pxPerG);
  const int16_t rh = (int16_t)(m->ecoHardG * pxPerG);
  lv_obj_set_size(g_ecoRingSoft, rs * 2, rs * 2);
  lv_obj_set_size(g_ecoRingHard, rh * 2, rh * 2);

  // Fargen foljer belastningen, inte den rada accelerationen: annars skulle
  // bubblan lysa rott i en kurva som inte kostar en enda poang.
  lv_color_t zone = m->ecoLoadG >= m->ecoHardG ? COL_RED
                    : m->ecoLoadG >= m->ecoSoftG ? COL_AMBER : COL_GREEN;
  if (m->ecoLevelled) {
    // Sidleden kramas ihop precis som i bedomningen. Ringarna forblir darmed
    // cirklar som betyder samma sak i alla riktningar - bubblan nar den roda
    // ringen exakt nar det borjar kosta - medan det i verkligheten kravs
    // ecoLatTolerance ganger sa mycket kraft at sidan for att komma dit.
    // g-talet i mitten ar och forblir den verkliga accelerationen.
    const float tol =
        m->ecoLatTolerance > 0.01f ? m->ecoLatTolerance : 1.0f;
    float px = m->ecoLonG * pxPerG;
    float py = (m->ecoLatG / tol) * pxPerG;
    float d2 = px * px + py * py;
    const float rmax = (float)(kEcoR - 18);
    if (d2 > rmax * rmax) {
      float d = d2 > 0 ? rmax / __builtin_sqrtf(d2) : 0;
      px *= d; py *= d;
    }
    // Faltet ar vridet ett kvarts varv medurs mot givarens egna axlar:
    // vanster blir upp, upp blir hoger, hoger blir ner, ner blir vanster.
    // Skarmens x far darfor langsleden och y sidleden, bada med tecknet
    // rakt av. (Vridningen ar en ren rotation, sa avstandet till mitten -
    // och darmed ringarna och klippningen ovan - ror sig inte.)
    lv_obj_align(g_ecoBubble, LV_ALIGN_CENTER, (int16_t)px, (int16_t)py);
    set_bg_color(g_ecoBubble, zone);
    set_shadow_color(g_ecoBubble, zone);
    snprintf(buf, sizeof(buf), "%.2f g", m->ecoMagG);
    for (char *p = buf; *p; p++) if (*p == '.') *p = ',';
    set_txt(g_ecoMagLbl, buf);
    lv_obj_clear_flag(g_ecoBubble, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_ecoBubble, LV_OBJ_FLAG_HIDDEN);
    set_txt(g_ecoMagLbl, "hittar lodlinjen …");
  }

  char dir[40];
  if (m->ecoForwardKnown && m->ecoForwardQuality >= 0.99f) {
    snprintf(dir, sizeof(dir), "riktning inlärd · sidled ×%.1f",
             m->ecoLatTolerance);
  } else if (m->ecoForwardKnown) {
    snprintf(dir, sizeof(dir), "riktning lär sig %d%%",
             (int)(m->ecoForwardQuality * 100 + 0.5f));
  } else {
    snprintf(dir, sizeof(dir), "riktning: kör, gasa och bromsa");
  }
  snprintf(buf, sizeof(buf), "hårt: gas %lu · broms %lu · kurva %lu\ntopp %.2f g · %s",
           (unsigned long)m->ecoHardAccel, (unsigned long)m->ecoHardBrake,
           (unsigned long)m->ecoHardTurn, m->ecoPeakG, dir);
  for (char *p = buf; *p; p++) if (*p == '.') *p = ',';
  set_txt(g_ecoInfo, buf);
}

// ------------------------------------------------------------ statistiken -

static lv_obj_t *g_statKm, *g_statTiles[6], *g_statBars[3], *g_statBarLbl[3];
static lv_obj_t *g_statCard;

static lv_obj_t *stat_tile(lv_obj_t *parent, int16_t x, int16_t y,
                           const char *cap, lv_obj_t **valOut) {
  lv_obj_t *p = glass(parent);
  lv_obj_set_size(p, SX(198), SY(74));
  lv_obj_set_pos(p, x, y);
  *valOut = label(p, F26, COL_TEXT, "–");
  lv_obj_align(*valOut, LV_ALIGN_TOP_LEFT, SX(14), SY(8));
  lv_obj_t *c = label(p, F16, COL_DIM, cap);
  lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, SX(14), SY(-8));
  return p;
}

static void build_stats() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_STATS] = scr;
  add_status(scr, GUI_SCR_STATS);

  lv_obj_t *title = label(scr, F26, COL_TEXT, "Statistik");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, SX(18), SY(44));

  g_statKm = label(scr, F44, COL_TEXT, "0");
  lv_obj_align(g_statKm, LV_ALIGN_TOP_LEFT, SX(18), SY(84));

  stat_tile(scr, SX(18), SY(150), "resor", &g_statTiles[0]);
  stat_tile(scr, SX(234), SY(150), "rullande tid", &g_statTiles[1]);
  stat_tile(scr, SX(18), SY(232), "högsta fart", &g_statTiles[2]);
  stat_tile(scr, SX(234), SY(232), "över gränsen", &g_statTiles[3]);
  stat_tile(scr, SX(18), SY(314), "ledigt på kortet", &g_statTiles[4]);
  stat_tile(scr, SX(234), SY(314), "räcker till", &g_statTiles[5]);

  // Per syfte: tre staplar med samma farger som knapparna.
  g_statCard = glass(scr);
  lv_obj_set_size(g_statCard, SX(414), SY(130));
  lv_obj_align(g_statCard, LV_ALIGN_TOP_MID, SX(0), SY(400));
  lv_obj_t *cap = label(g_statCard, F16, COL_DIM, "km per syfte");
  lv_obj_align(cap, LV_ALIGN_TOP_LEFT, SX(14), SY(6));
  static const lv_color_t pc[3] = {COL_PRIVAT, COL_FORETAG, COL_DIFFUST};
  static const char *pn[3] = {"Privat", "Företag", "Diffust"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *n = label(g_statCard, F16, COL_DIM, pn[i]);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, SX(14), 30 + i * 30);
    g_statBars[i] = lv_bar_create(g_statCard);
    lv_obj_remove_style_all(g_statBars[i]);
    lv_obj_set_size(g_statBars[i], SX(220), SY(12));
    lv_obj_align(g_statBars[i], LV_ALIGN_TOP_LEFT, SX(84), 34 + i * 30);
    lv_bar_set_range(g_statBars[i], 0, 100);
    lv_obj_set_style_bg_color(g_statBars[i], COL_PANEL_W, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_statBars[i], 24, LV_PART_MAIN);
    lv_obj_set_style_radius(g_statBars[i], 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_statBars[i], pc[i], LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_statBars[i], LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_statBars[i], 6, LV_PART_INDICATOR);
    g_statBarLbl[i] = label(g_statCard, F16, COL_TEXT, "0");
    lv_obj_align(g_statBarLbl[i], LV_ALIGN_TOP_RIGHT, SX(-14), SY(30) + i * SY(30));
  }

  add_gestures(scr);
}

static void update_stats(const GuiModel *m) {
  update_status(GUI_SCR_STATS, m);
  char buf[48];

  snprintf(buf, sizeof(buf), "%lu km",
           (unsigned long)(m->statTotalKm + 0.5));
  set_txt(g_statKm, buf);

  snprintf(buf, sizeof(buf), "%lu", (unsigned long)m->statTrips);
  set_txt(g_statTiles[0], buf);
  snprintf(buf, sizeof(buf), "%lu h %lu m",
           (unsigned long)(m->statMovingS / 3600),
           (unsigned long)((m->statMovingS % 3600) / 60));
  set_txt(g_statTiles[1], buf);
  snprintf(buf, sizeof(buf), "%d km/h", (int)(m->statMaxKmh + 0.5f));
  set_txt(g_statTiles[2], buf);
  snprintf(buf, sizeof(buf), "%lu min", (unsigned long)(m->statSpeedingS / 60));
  set_txt(g_statTiles[3], buf);
  set_text_color(g_statTiles[3], m->statSpeedingS >= 60 ? COL_RED : COL_TEXT);
  snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)m->statFreeMb);
  set_txt(g_statTiles[4], buf);
  if (m->statKmLeft >= 1000000.0) {
    snprintf(buf, sizeof(buf), "> miljon km");
  } else {
    snprintf(buf, sizeof(buf), "~%lu km", (unsigned long)m->statKmLeft);
  }
  set_txt(g_statTiles[5], buf);

  const double top = m->statPrivatKm > m->statForetagKm
      ? (m->statPrivatKm > m->statDiffustKm ? m->statPrivatKm : m->statDiffustKm)
      : (m->statForetagKm > m->statDiffustKm ? m->statForetagKm : m->statDiffustKm);
  const double kmv[3] = {m->statPrivatKm, m->statForetagKm, m->statDiffustKm};
  for (int i = 0; i < 3; i++) {
    lv_bar_set_value(g_statBars[i],
                     top > 0 ? (int32_t)(kmv[i] / top * 100.0) : 0,
                     LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(kmv[i] + 0.5));
    set_txt(g_statBarLbl[i], buf);
  }
}

// ---------------------------------------------------------------- molnet --

static lv_obj_t *g_cloudAp, *g_cloudState, *g_cloudCams, *g_cloudAuto;

static void cloud_sync_cb(lv_event_t *e) {
  (void)e;
  if (g_act && g_act->requestCloudSync) g_act->requestCloudSync();
}

static void auto_sync_cb(lv_event_t *e) {
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  if (g_act && g_act->toggleAutoSync) {
    g_act->toggleAutoSync(lv_obj_has_state(sw, LV_STATE_CHECKED));
  }
}

static void build_cloud() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_CLOUD] = scr;
  add_status(scr, GUI_SCR_CLOUD);

  lv_obj_t *title = label(scr, F26, COL_TEXT, "Moln & wifi");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, SX(18), SY(44));

  lv_obj_t *p1 = glass(scr);
  lv_obj_set_size(p1, SX(414), SY(120));
  lv_obj_align(p1, LV_ALIGN_TOP_MID, SX(0), SY(92));
  lv_obj_t *c1 = label(p1, F16, COL_DIM, "ENHETENS EGET WIFI");
  lv_obj_align(c1, LV_ALIGN_TOP_LEFT, SX(14), SY(8));
  g_cloudAp = label(p1, F20, COL_TEXT, "");
  lv_obj_align(g_cloudAp, LV_ALIGN_TOP_LEFT, SX(14), SY(34));
  lv_label_set_long_mode(g_cloudAp, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_cloudAp, SX(386));

  lv_obj_t *p2 = glass(scr);
  lv_obj_set_size(p2, SX(414), SY(150));
  lv_obj_align(p2, LV_ALIGN_TOP_MID, SX(0), SY(226));
  lv_obj_t *c2 = label(p2, F16, COL_DIM, "MOLNSYNKEN");
  lv_obj_align(c2, LV_ALIGN_TOP_LEFT, SX(14), SY(8));

  // Autosynken av eller pa. Avslagen synkar enheten bara pa knappen nedanfor
  // - for den som vill valja nat och tillfalle sjalv.
  lv_obj_t *al = label(p2, F16, COL_DIM, "AUTO");
  lv_obj_align(al, LV_ALIGN_TOP_RIGHT, SX(-84), SY(12));
  g_cloudAuto = lv_switch_create(p2);
  lv_obj_set_size(g_cloudAuto, SX(64), SY(34));
  lv_obj_align(g_cloudAuto, LV_ALIGN_TOP_RIGHT, SX(-10), SY(4));
  lv_obj_set_style_bg_color(g_cloudAuto, COL_ACCENT,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(g_cloudAuto, auto_sync_cb, LV_EVENT_VALUE_CHANGED,
                      nullptr);
  g_cloudState = label(p2, F20, COL_TEXT, "");
  lv_obj_align(g_cloudState, LV_ALIGN_TOP_LEFT, SX(14), SY(34));
  lv_label_set_long_mode(g_cloudState, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_cloudState, SX(386));

  g_cloudCams = label(scr, F16, COL_DIM, "");
  lv_obj_align(g_cloudCams, LV_ALIGN_TOP_MID, SX(0), SY(396));

  lv_obj_t *sync = ghost_button(scr, COL_CYAN, "SYNKA NU", F20,
                                cloud_sync_cb, nullptr);
  lv_obj_set_size(sync, SX(414), SY(52));
  lv_obj_align(sync, LV_ALIGN_TOP_MID, SX(0), SY(430));

  add_gestures(scr);
}

static void update_cloud(const GuiModel *m) {
  update_status(GUI_SCR_CLOUD, m);
  char buf[160];

  snprintf(buf, sizeof(buf), "nät: %s\nlösenord: %s%s",
           m->apSsid, m->apPassword,
           m->apClient ? "\ntelefon ansluten" : "");
  set_txt(g_cloudAp, buf);

  if (!m->cloudConfigured) {
    snprintf(buf, sizeof(buf),
             "inte konfigurerad – anslut till enhetens wifi och fyll i "
             "hotspot och token under Molnsynk");
  } else {
    snprintf(buf, sizeof(buf), "%s%s%s\nupp %lu resor · %lu gpx · ned %lu filer",
             m->cloudSsid, m->cloudDetail[0] ? " · " : "",
             m->cloudDetail,
             (unsigned long)m->cloudTrips, (unsigned long)m->cloudGpx,
             (unsigned long)m->cloudFiles);
  }
  set_txt(g_cloudState, buf);

  if (m->autoSyncOn) lv_obj_add_state(g_cloudAuto, LV_STATE_CHECKED);
  else lv_obj_remove_state(g_cloudAuto, LV_STATE_CHECKED);

  snprintf(buf, sizeof(buf), "%lu fartkameror på kortet",
           (unsigned long)m->camCount);
  set_txt(g_cloudCams, buf);
}

// ------------------------------------------------------------------ bilen -
// Obd-tillvalet: bilens egna varden ur uttaget. Skarmen finns aven utan
// adapter - da berattar den vad som saknas i stallet for att visa nollor,
// och tomma falt star som streck. Bilen lamnar olika mycket ifran sig
// beroende pa marke och arsmodell, sa "-" har betyder "den har bilen sager
// inte det", inte "noll".

static lv_obj_t *g_obdState, *g_obdRpm, *g_obdRpmCap, *g_obdSpeed;
static lv_obj_t *g_obdTiles[8], *g_obdTileCaps[8], *g_obdTrip;

static void obd_forget_cb(lv_event_t *e) {
  (void)e;
  if (g_act && g_act->forgetObd) g_act->forgetObd();
}

static void obd_tile(lv_obj_t *parent, int16_t x, int16_t y, const char *cap,
                     lv_obj_t **valOut, lv_obj_t **capOut) {
  lv_obj_t *p = glass(parent);
  lv_obj_set_size(p, SX(198), SY(62));
  lv_obj_set_pos(p, x, y);
  *valOut = label(p, F20, COL_TEXT, "–");
  lv_obj_align(*valOut, LV_ALIGN_TOP_LEFT, SX(12), SY(6));
  *capOut = label(p, F16, COL_DIM, cap);
  lv_obj_align(*capOut, LV_ALIGN_BOTTOM_LEFT, SX(12), SY(-6));
}

static void build_obd() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_OBD] = scr;
  add_status(scr, GUI_SCR_OBD);

  lv_obj_t *title = label(scr, F26, COL_TEXT, "Bilen");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, SX(18), SY(44));

  // Statusraden: vad adaptern gor just nu, och en knapp for att glomma den
  // och leta om - byter man bil ska inte den gamla adapterns adress sitta
  // kvar och blockera.
  lv_obj_t *top = glass(scr);
  lv_obj_set_size(top, SX(414), SY(64));
  lv_obj_align(top, LV_ALIGN_TOP_MID, SX(0), SY(80));
  g_obdState = label(top, F16, COL_DIM, "");
  lv_obj_align(g_obdState, LV_ALIGN_LEFT_MID, SX(14), SY(0));
  lv_label_set_long_mode(g_obdState, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_obdState, SX(270));
  lv_obj_set_style_text_line_space(g_obdState, 4, 0);
  lv_obj_t *again = ghost_button(top, COL_ACCENT, "Sök om", F16,
                                 obd_forget_cb, nullptr);
  lv_obj_set_size(again, SX(110), SY(44));
  lv_obj_align(again, LV_ALIGN_RIGHT_MID, SX(-10), SY(0));

  // De tva som en forare tittar efter: varvtalet och bilens egen hastighet.
  lv_obj_t *big = glass(scr);
  lv_obj_set_size(big, SX(414), SY(96));
  lv_obj_align(big, LV_ALIGN_TOP_MID, SX(0), SY(152));
  g_obdRpm = label(big, F44, COL_TEXT, "–");
  lv_obj_align(g_obdRpm, LV_ALIGN_LEFT_MID, SX(20), SY(-8));
  g_obdRpmCap = label(big, F16, COL_DIM, "varv/min");
  lv_obj_align(g_obdRpmCap, LV_ALIGN_LEFT_MID, SX(20), SY(26));
  g_obdSpeed = label(big, F44, COL_ACCENT, "–");
  lv_obj_align(g_obdSpeed, LV_ALIGN_RIGHT_MID, SX(-24), SY(-8));
  lv_obj_t *sc = label(big, F16, COL_DIM, "km/h ur bilen");
  lv_obj_align(sc, LV_ALIGN_RIGHT_MID, SX(-24), SY(26));

  static const char *caps[8] = {"kylvatten", "motorlast", "gaspedal",
                                "tank",      "förbrukning", "insugsluft",
                                "utetemp",   "spänning"};
  for (int i = 0; i < 8; i++) {
    obd_tile(scr, i % 2 ? SX(234) : SX(18), SY(262) + (i / 2) * SY(70),
             caps[i], &g_obdTiles[i], &g_obdTileCaps[i]);
  }

  g_obdTrip = label(scr, F16, COL_DIM, "");
  lv_obj_align(g_obdTrip, LV_ALIGN_TOP_LEFT, SX(20), SY(538));
  lv_label_set_long_mode(g_obdTrip, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_obdTrip, SX(410));

  add_gestures(scr);
}

static void update_obd(const GuiModel *m) {
  update_status(GUI_SCR_OBD, m);
  char buf[160];

  switch (m->obdState) {
    case 0:  // OBD_OFF
      snprintf(buf, sizeof(buf),
               "Tillvalet är avslaget. Slå på det under Inställningar – "
               "adaptern måste vara en BLE-modell.");
      break;
    case 1:  // OBD_SEARCHING
      snprintf(buf, sizeof(buf), "Letar efter adaptern …");
      break;
    case 2:  // OBD_CONNECTING
      snprintf(buf, sizeof(buf), "Kopplar upp mot %s …", m->obdAdapter);
      break;
    case 3:  // OBD_HANDSHAKE
      snprintf(buf, sizeof(buf), "%s svarar – frågar bilen", m->obdAdapter);
      break;
    case 5:  // OBD_NO_CAR
      snprintf(buf, sizeof(buf),
               "%s är uppkopplad men bilen svarar inte – tändningen av?",
               m->obdAdapter);
      break;
    default:  // OBD_LIVE
      snprintf(buf, sizeof(buf), "%s · bilen svarar", m->obdAdapter);
      break;
  }
  set_txt(g_obdState, buf);

  const bool live = m->obdState == 4;  // OBD_LIVE

  if (live && (m->obdHas & (1u << 0))) {
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->obdRpm);
  } else {
    snprintf(buf, sizeof(buf), "–");
  }
  set_txt(g_obdRpm, buf);
  // En hybrid som rullar pa el star pa noll varv med bilen i full fart -
  // det ar inte ett fel, sa raden under sager vad noll betyder.
  set_txt(g_obdRpmCap,
          live && (m->obdHas & (1u << 0)) && m->obdRpm == 0 ? "motorn vilar"
                                                            : "varv/min");

  if (live && (m->obdHas & (1u << 1))) {
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->obdSpeedKmh);
  } else {
    snprintf(buf, sizeof(buf), "–");
  }
  set_txt(g_obdSpeed, buf);

  // Brickorna i samma ordning som de skapades.
  struct Cell { uint32_t bit; const char *fmt; float val; };
  char cells[8][24];
  const bool hasHybrid = m->obdHas & (1u << 11);
  snprintf(cells[0], sizeof(cells[0]),
           (live && (m->obdHas & (1u << 2))) ? "%d °C" : "–", m->obdCoolantC);
  snprintf(cells[1], sizeof(cells[1]),
           (live && (m->obdHas & (1u << 3))) ? "%u %%" : "–",
           (unsigned)m->obdLoadPct);
  snprintf(cells[2], sizeof(cells[2]),
           (live && (m->obdHas & (1u << 4))) ? "%u %%" : "–",
           (unsigned)m->obdThrottlePct);
  // Tanknivan byter plats med hybridbatteriet i bilar som har ett - det ar
  // den siffran man tittar efter i en hybrid.
  if (hasHybrid && live) {
    snprintf(cells[3], sizeof(cells[3]), "%u %%", (unsigned)m->obdHybridPct);
  } else {
    snprintf(cells[3], sizeof(cells[3]),
             (live && (m->obdHas & (1u << 5))) ? "%u %%" : "–",
             (unsigned)m->obdFuelPct);
  }
  set_txt(g_obdTileCaps[3], hasHybrid ? "hybridbatteri" : "tank");
  if (live && (m->obdHas & (1u << 10))) {
    snprintf(cells[4], sizeof(cells[4]), "%.1f l/h", m->obdFlowLh);
    for (char *p = cells[4]; *p; p++) if (*p == '.') *p = ',';
  } else {
    snprintf(cells[4], sizeof(cells[4]), "–");
  }
  snprintf(cells[5], sizeof(cells[5]),
           (live && (m->obdHas & (1u << 6))) ? "%d °C" : "–", m->obdIntakeC);
  snprintf(cells[6], sizeof(cells[6]),
           (live && (m->obdHas & (1u << 7))) ? "%d °C" : "–", m->obdAmbientC);
  if (live && (m->obdHas & (1u << 8))) {
    snprintf(cells[7], sizeof(cells[7]), "%.1f V", m->obdVoltage);
    for (char *p = cells[7]; *p; p++) if (*p == '.') *p = ',';
  } else {
    snprintf(cells[7], sizeof(cells[7]), "–");
  }
  for (int i = 0; i < 8; i++) set_txt(g_obdTiles[i], cells[i]);

  // Resans egen rad: det som foljer med upp i molnet nar resan ar slut.
  if (m->tripActive) {
    char liters[16] = "–";
    if (m->obdTripLiters > 0.001f) {
      snprintf(liters, sizeof(liters), "%.2f l", m->obdTripLiters);
      for (char *p = liters; *p; p++) if (*p == '.') *p = ',';
    }
    snprintf(buf, sizeof(buf), "Resan: %s bränsle · max %u varv · %lu min tomgång",
             liters, (unsigned)m->obdTripMaxRpm,
             (unsigned long)(m->obdTripIdleS / 60));
  } else {
    snprintf(buf, sizeof(buf),
             "Värdena bokförs per resa och följer med upp i molnet.");
  }
  set_txt(g_obdTrip, buf);
}

// ---------------------------------------------------------- installningar -

static lv_obj_t *g_setSound;
static lv_obj_t *g_setScreenVal;
static lv_obj_t *g_setVersion;
static lv_obj_t *g_setObd;

static void obd_switch_cb(lv_event_t *e) {
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  if (g_act && g_act->toggleObd) {
    g_act->toggleObd(lv_obj_has_state(sw, LV_STATE_CHECKED));
  }
}

static void sound_cb(lv_event_t *e) {
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  if (g_act && g_act->toggleSound) {
    g_act->toggleSound(lv_obj_has_state(sw, LV_STATE_CHECKED));
  }
}

static void screen_step_cb(lv_event_t *e) {
  intptr_t dir = (intptr_t)lv_event_get_user_data(e);
  int idx = (int)g_m.screenIdx + (int)dir;
  if (idx < 0) idx = 0;
  if (idx >= g_m.screenCount) idx = g_m.screenCount - 1;
  if (g_act && g_act->setScreenIdx) g_act->setScreenIdx((uint8_t)idx);
}

static void build_settings() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_SETTINGS] = scr;
  add_status(scr, GUI_SCR_SETTINGS);

  lv_obj_t *title = label(scr, F26, COL_TEXT, "Inställningar");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, SX(18), SY(44));

  // Ljudet.
  lv_obj_t *p1 = glass(scr);
  lv_obj_set_size(p1, SX(414), SY(70));
  lv_obj_align(p1, LV_ALIGN_TOP_MID, SX(0), SY(92));
  lv_obj_t *l1 = label(p1, F20, COL_TEXT, "Ljud");
  lv_obj_align(l1, LV_ALIGN_LEFT_MID, SX(14), SY(-10));
  lv_obj_t *h1 = label(p1, F16, COL_DIM, "varningar och kvitton");
  lv_obj_align(h1, LV_ALIGN_LEFT_MID, SX(14), SY(14));
  g_setSound = lv_switch_create(p1);
  lv_obj_set_size(g_setSound, SX(64), SY(34));
  lv_obj_align(g_setSound, LV_ALIGN_RIGHT_MID, SX(-10), SY(0));
  lv_obj_set_style_bg_color(g_setSound, COL_ACCENT,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(g_setSound, sound_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Skarmslackningen: minus och plus, som pa gamla skarmen fast rundare.
  lv_obj_t *p2 = glass(scr);
  lv_obj_set_size(p2, SX(414), SY(70));
  lv_obj_align(p2, LV_ALIGN_TOP_MID, SX(0), SY(174));
  lv_obj_t *l2 = label(p2, F20, COL_TEXT, "Släck skärm");
  lv_obj_align(l2, LV_ALIGN_LEFT_MID, SX(14), SY(-10));
  lv_obj_t *h2 = label(p2, F16, COL_DIM, "när ingen resa pågår");
  lv_obj_align(h2, LV_ALIGN_LEFT_MID, SX(14), SY(14));
  lv_obj_t *minus = button(p2, COL_ACCENT, "–", F26, lv_color_white(),
                           screen_step_cb, (void *)(intptr_t)-1);
  lv_obj_set_size(minus, SX(48), SY(44));
  lv_obj_align(minus, LV_ALIGN_RIGHT_MID, SX(-150), SY(0));
  lv_obj_t *plus = button(p2, COL_ACCENT, "+", F26, lv_color_white(),
                          screen_step_cb, (void *)(intptr_t)1);
  lv_obj_set_size(plus, SX(48), SY(44));
  lv_obj_align(plus, LV_ALIGN_RIGHT_MID, SX(-10), SY(0));
  g_setScreenVal = label(p2, F20, COL_TEXT, "5 min");
  lv_obj_align(g_setScreenVal, LV_ALIGN_RIGHT_MID, SX(-68), SY(0));

  // Obd-tillvalet. Avslaget ror enheten inte bluetooth alls - och adaptern
  // maste vara en BLE-modell, eftersom kretsen inte har bluetooth classic.
  lv_obj_t *p4 = glass(scr);
  lv_obj_set_size(p4, SX(414), SY(70));
  lv_obj_align(p4, LV_ALIGN_TOP_MID, SX(0), SY(256));
  lv_obj_t *l4 = label(p4, F20, COL_TEXT, "OBD-adapter");
  lv_obj_align(l4, LV_ALIGN_LEFT_MID, SX(14), SY(-10));
  lv_obj_t *h4 = label(p4, F16, COL_DIM, "bilens värden via bluetooth (BLE)");
  lv_obj_align(h4, LV_ALIGN_LEFT_MID, SX(14), SY(14));
  g_setObd = lv_switch_create(p4);
  lv_obj_set_size(g_setObd, SX(64), SY(34));
  lv_obj_align(g_setObd, LV_ALIGN_RIGHT_MID, SX(-10), SY(0));
  lv_obj_set_style_bg_color(g_setObd, COL_ACCENT,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(g_setObd, obd_switch_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Vad enheten vet om sig sjalv.
  lv_obj_t *p3 = glass(scr);
  lv_obj_set_size(p3, SX(414), SY(186));
  lv_obj_align(p3, LV_ALIGN_TOP_MID, SX(0), SY(338));
  g_setVersion = label(p3, F16, COL_DIM, "");
  lv_obj_align(g_setVersion, LV_ALIGN_TOP_LEFT, SX(14), SY(10));
  lv_label_set_long_mode(g_setVersion, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_setVersion, SX(386));
  lv_obj_set_style_text_line_space(g_setVersion, 7, 0);

  add_gestures(scr);
}

static void update_settings(const GuiModel *m) {
  update_status(GUI_SCR_SETTINGS, m);

  if (m->soundOn) lv_obj_add_state(g_setSound, LV_STATE_CHECKED);
  else lv_obj_remove_state(g_setSound, LV_STATE_CHECKED);

  if (m->obdOn) lv_obj_add_state(g_setObd, LV_STATE_CHECKED);
  else lv_obj_remove_state(g_setObd, LV_STATE_CHECKED);

  char buf[220];
  if (m->screenTimeoutS == 0) {
    snprintf(buf, sizeof(buf), "aldrig");
  } else {
    snprintf(buf, sizeof(buf), "%u min", (unsigned)(m->screenTimeoutS / 60));
  }
  set_txt(g_setScreenVal, buf);

  snprintf(buf, sizeof(buf),
           "Version %s\nFartkameror: %s\nHastighetsgränser: %s\n"
           "Kort: %s\nGPS: %s",
           m->version,
           m->camsLoaded ? "inlästa" : "filen saknas",
           m->limitsLoaded ? "inlästa" : "saknas",
           m->sdOk ? "ok" : "saknas",
           m->gpsPresent ? (m->gpsFix ? "fix" : "söker") : "ingen modul");
  set_txt(g_setVersion, buf);
}

// -------------------------------------------------- fragan och kundlistan -

static lv_obj_t *g_askSheet, *g_askTitle, *g_askCount;

static void ask_pick_cb(lv_event_t *e) {
  intptr_t p = (intptr_t)lv_event_get_user_data(e);
  if (g_act && g_act->setPurpose) g_act->setPurpose((GuiPurpose)p);
  lv_obj_add_flag(g_askSheet, LV_OBJ_FLAG_HIDDEN);
}

static void build_ask() {
  g_askSheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(g_askSheet);
  lv_obj_set_size(g_askSheet, SX(450), SY(600));
  lv_obj_set_style_bg_color(g_askSheet, COL_BG, 0);
  lv_obj_set_style_bg_opa(g_askSheet, 248, 0);
  lv_obj_clear_flag(g_askSheet, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = label(g_askSheet, F26, COL_TEXT, "Resan är klar");
  lv_obj_align(t, LV_ALIGN_TOP_MID, SX(0), SY(46));
  g_askTitle = label(g_askSheet, F20, COL_DIM, "");
  lv_obj_align(g_askTitle, LV_ALIGN_TOP_MID, SX(0), SY(92));
  lv_obj_t *q = label(g_askSheet, F26, COL_TEXT, "Vad var resan till?");
  lv_obj_align(q, LV_ALIGN_TOP_MID, SX(0), SY(140));
  g_askCount = label(g_askSheet, F16, COL_FAINT, "");
  lv_obj_align(g_askCount, LV_ALIGN_TOP_MID, SX(0), SY(178));

  static const char *names[3] = {"PRIVAT", "FÖRETAG", "DIFFUST"};
  static const lv_color_t cols[3] = {COL_PRIVAT, COL_FORETAG, COL_DIFFUST};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = button(g_askSheet, cols[i], names[i], F26,
                         lv_color_white(), ask_pick_cb,
                         (void *)(intptr_t)(i + 1));
    lv_obj_set_size(b, SX(414), SY(106));
    lv_obj_align(b, LV_ALIGN_TOP_MID, SX(0), 216 + i * 122);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_shadow_color(b, cols[i], 0);
    lv_obj_set_style_shadow_width(b, 30, 0);
    lv_obj_set_style_shadow_opa(b, 70, 0);
  }

  lv_obj_add_flag(g_askSheet, LV_OBJ_FLAG_HIDDEN);
}

static void update_ask(const GuiModel *m) {
  if (!m->askPurpose) {
    lv_obj_add_flag(g_askSheet, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  char buf[64], km[16];
  snprintf(km, sizeof(km), "%.1f", m->askKm);
  for (char *p = km; *p; p++) if (*p == '.') *p = ',';
  snprintf(buf, sizeof(buf), "Resa %lu · %s km",
           (unsigned long)m->askIndex, km);
  set_txt(g_askTitle, buf);
  snprintf(buf, sizeof(buf), "blir DIFFUST om %lu s",
           (unsigned long)m->askSecondsLeft);
  set_txt(g_askCount, buf);
  lv_obj_clear_flag(g_askSheet, LV_OBJ_FLAG_HIDDEN);
}

// Kundlistan: ett ark over det som visas, med rullbar lista.
static lv_obj_t *g_custSheet;

static void cust_pick_cb(lv_event_t *e) {
  const char *name = (const char *)lv_event_get_user_data(e);
  if (g_act && g_act->pickCustomer) g_act->pickCustomer(name);
  lv_obj_add_flag(g_custSheet, LV_OBJ_FLAG_HIDDEN);
}

static void cust_close_cb(lv_event_t *e) {
  (void)e;
  lv_obj_add_flag(g_custSheet, LV_OBJ_FLAG_HIDDEN);
}

void gui_screens_open_customers(const GuiModel *m) {
  if (g_custSheet) lv_obj_delete(g_custSheet);

  g_custSheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(g_custSheet);
  lv_obj_set_size(g_custSheet, SX(450), SY(600));
  lv_obj_set_style_bg_color(g_custSheet, COL_BG, 0);
  lv_obj_set_style_bg_opa(g_custSheet, 248, 0);
  lv_obj_clear_flag(g_custSheet, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = label(g_custSheet, F26, COL_TEXT, "Välj kund");
  lv_obj_align(t, LV_ALIGN_TOP_MID, SX(0), SY(24));

  lv_obj_t *listwrap = lv_obj_create(g_custSheet);
  lv_obj_remove_style_all(listwrap);
  lv_obj_set_size(listwrap, SX(414), SY(400));
  lv_obj_align(listwrap, LV_ALIGN_TOP_MID, SX(0), SY(70));
  lv_obj_set_flex_flow(listwrap, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(listwrap, 10, 0);
  lv_obj_set_scroll_dir(listwrap, LV_DIR_VER);

  if (m->customerCount == 0) {
    lv_obj_t *empty = label(listwrap, F20, COL_DIM,
                            "Kundlistan är tom – lägg upp kunder i webben "
                            "och synka.");
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty, SX(380));
  }
  for (uint8_t i = 0; i < m->customerCount; i++) {
    lv_obj_t *b = ghost_button(listwrap, COL_ACCENT, m->customerNames[i],
                               F20, cust_pick_cb,
                               (void *)m->customerNames[i]);
    lv_obj_set_size(b, SX(400), SY(56));
  }

  lv_obj_t *none = ghost_button(g_custSheet, COL_DIFFUST,
                                "INGEN KUND – BARA FÖRETAG", F20,
                                cust_pick_cb, nullptr);
  lv_obj_set_size(none, SX(414), SY(52));
  lv_obj_align(none, LV_ALIGN_BOTTOM_MID, SX(0), SY(-76));

  lv_obj_t *close = ghost_button(g_custSheet, COL_ACCENT, "STÄNG",
                                 F20, cust_close_cb, nullptr);
  lv_obj_set_size(close, SX(414), SY(52));
  lv_obj_align(close, LV_ALIGN_BOTTOM_MID, SX(0), SY(-16));
}

// ----------------------------------------------------------------- apiet --

void gui_screens_create(const GuiActions *actions) {
  g_act = actions;
  build_home();
  build_drive();
  build_eco();
  build_stats();
  build_obd();
  build_cloud();
  build_settings();
  build_ask();
  // Korskarmen ar startlaget: det ar farten man vill se nar tandningen slas
  // pa, inte en appmeny. Menyn ar ett svep nedat bort.
  lv_screen_load(g_screens[GUI_SCR_DRIVE]);
  g_current = GUI_SCR_DRIVE;
}

void gui_screens_show(GuiScreen s, bool animate) {
  if (s == g_current) return;
  g_current = s;
  // Inga skarmbytesanimationer: varje bildruta i en sadan ritar om hela
  // skarmen med skuggor och gradienter i mjukvara, och pa den har processorn
  // blir det en seg svepning i laga bildrutor. Ett omedelbart byte kanns
  // snabbare an en langsam animation - sa byte sker direkt.
  (void)animate;
  lv_screen_load(g_screens[s]);
}

GuiScreen gui_screens_current() { return g_current; }

void gui_screens_update(const GuiModel *m) {
  g_m = *m;
  switch (g_current) {
    case GUI_SCR_HOME: update_home(m); break;
    case GUI_SCR_DRIVE: update_drive(m); break;
    case GUI_SCR_ECO: update_eco(m); break;
    case GUI_SCR_STATS: update_stats(m); break;
    case GUI_SCR_OBD: update_obd(m); break;
    case GUI_SCR_CLOUD: update_cloud(m); break;
    case GUI_SCR_SETTINGS: update_settings(m); break;
  }
  update_ask(m);
}
