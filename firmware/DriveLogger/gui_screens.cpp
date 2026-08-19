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

static const GuiActions *g_act = nullptr;
static GuiModel g_m = {};  // senaste modellen, for uppdateringarna
static GuiScreen g_current = GUI_SCR_HOME;

// ---------------------------------------------------------------- hjalpare -

static lv_obj_t *g_screens[6];

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

// Hem-gesten: svep uppat var som helst pa en appskarm leder hem, precis som
// pa en telefon. Strecket i underkanten ar samma loft som tryckyta.
static void go_home_cb(lv_event_t *e) {
  (void)e;
  gui_screens_show(GUI_SCR_HOME, true);
}

static void gesture_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
    gui_screens_show(GUI_SCR_HOME, true);
  }
}

static void add_home_bar(lv_obj_t *scr) {
  lv_obj_t *hit = lv_button_create(scr);
  lv_obj_remove_style_all(hit);
  lv_obj_set_size(hit, 220, 26);
  lv_obj_align(hit, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(hit, 0, 0);
  lv_obj_add_event_cb(hit, go_home_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *bar = lv_obj_create(hit);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, 120, 5);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(bar, COL_PANEL_W, 0);
  lv_obj_set_style_bg_opa(bar, 110, 0);
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
static StatusRefs g_status[6];

static void add_status(lv_obj_t *scr, GuiScreen idx) {
  StatusRefs &r = g_status[idx];
  r.clock = label(scr, &ui_font_26, COL_TEXT, "--:--");
  lv_obj_align(r.clock, LV_ALIGN_TOP_LEFT, 18, 8);

  r.cloud = label(scr, &lv_font_montserrat_20, COL_FAINT, LV_SYMBOL_WIFI);
  lv_obj_align(r.cloud, LV_ALIGN_TOP_RIGHT, -18, 12);
  r.sd = label(scr, &lv_font_montserrat_20, COL_FAINT, LV_SYMBOL_SD_CARD);
  lv_obj_align(r.sd, LV_ALIGN_TOP_RIGHT, -56, 12);
  r.gps = label(scr, &lv_font_montserrat_20, COL_FAINT, LV_SYMBOL_GPS " 0");
  lv_obj_align(r.gps, LV_ALIGN_TOP_RIGHT, -94, 12);
}

static void update_status(GuiScreen idx, const GuiModel *m) {
  StatusRefs &r = g_status[idx];
  if (!r.clock) return;
  set_txt(r.clock, m->clock[0] ? m->clock : "--:--");

  char buf[16];
  snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " %u", (unsigned)m->sats);
  set_txt(r.gps, buf);
  lv_obj_set_style_text_color(
      r.gps,
      !m->gpsPresent ? COL_FAINT : (m->gpsFix ? COL_GREEN : COL_AMBER), 0);
  lv_obj_set_style_text_color(r.sd, m->sdOk ? COL_GREEN : COL_RED, 0);
  lv_obj_set_style_text_color(
      r.cloud,
      m->apClient || m->cloudBusy ? COL_CYAN
      : m->cloudConfigured ? COL_DIM : COL_FAINT, 0);
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

static const AppDef kApps[6] = {
    {"Körning", LV_SYMBOL_GPS, 0x2F7BFF, 0x123B8F, GUI_SCR_DRIVE},
    {"Ecodrive", LV_SYMBOL_CHARGE, 0x2FC47E, 0x0B5A3A, GUI_SCR_ECO},
    {"Statistik", LV_SYMBOL_BARS, 0x7C5CFF, 0x3B2A80, GUI_SCR_STATS},
    {"Moln", LV_SYMBOL_WIFI, 0x22D3EE, 0x0E5B6B, GUI_SCR_CLOUD},
    {"Kund", LV_SYMBOL_DIRECTORY, 0xF5A623, 0x8A5A0E, GUI_SCR_HOME},
    {"Inställn.", LV_SYMBOL_SETTINGS, 0x8C9AAC, 0x3A4250, GUI_SCR_SETTINGS},
};

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
  lv_obj_set_size(glow, 380, 380);
  lv_obj_align(glow, LV_ALIGN_TOP_MID, 0, -160);
  lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(glow, COL_ACCENT, 0);
  lv_obj_set_style_bg_opa(glow, 26, 0);

  add_status(scr, GUI_SCR_HOME);

  lv_obj_t *brand = label(scr, &ui_font_16, COL_DIM, "DriveLogger");
  lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 18, 42);

  // Rutnatet: 2 x 3 ikoner. Matten ar valda sa att alla tre rader och
  // resechipet far plats pa 600 pixlar utan att trangas.
  const int16_t tile = 112, gapx = 58;
  const int16_t x0 = (450 - 2 * tile - gapx) / 2;
  const int16_t y0 = 84;
  const int16_t step = tile + 44;

  for (int i = 0; i < 6; i++) {
    const AppDef &a = kApps[i];
    const int col = i % 2, row = i / 2;
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

    lv_obj_t *sym = label(b, &lv_font_montserrat_28, lv_color_white(),
                          a.symbol);
    lv_obj_center(sym);

    lv_obj_t *name = label(scr, &ui_font_16, COL_TEXT, a.name);
    lv_obj_set_width(name, tile + 40);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name, x - 20, y + tile + 5);
  }

  // Chip som visar att en resa pagar - trycket leder rakt in i korningen.
  g_tripChip = lv_button_create(scr);
  lv_obj_remove_style_all(g_tripChip);
  lv_obj_set_size(g_tripChip, 320, 38);
  lv_obj_align(g_tripChip, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_radius(g_tripChip, 20, 0);
  lv_obj_set_style_bg_color(g_tripChip, COL_GREEN, 0);
  lv_obj_set_style_bg_opa(g_tripChip, 46, 0);
  lv_obj_set_style_border_color(g_tripChip, COL_GREEN, 0);
  lv_obj_set_style_border_opa(g_tripChip, 140, 0);
  lv_obj_set_style_border_width(g_tripChip, 1, 0);
  lv_obj_add_event_cb(g_tripChip, trip_chip_cb, LV_EVENT_CLICKED, nullptr);
  g_tripChipLabel = label(g_tripChip, &ui_font_20, COL_GREEN, "");
  lv_obj_center(g_tripChipLabel);
  lv_obj_add_flag(g_tripChip, LV_OBJ_FLAG_HIDDEN);
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

  const int16_t cx = 225, cy = 240, r = 180;

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
  lv_obj_set_style_text_font(g_scale, &ui_font_16, LV_PART_INDICATOR);
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
  g_speedLbl = label(scr, &ui_font_150, COL_TEXT, "0");
  lv_obj_align(g_speedLbl, LV_ALIGN_TOP_MID, 0, 130);
  g_kmhLbl = label(scr, &ui_font_20, COL_DIM, "km/h");
  lv_obj_align(g_kmhLbl, LV_ALIGN_TOP_MID, 0, 292);
  g_deltaLbl = label(scr, &ui_font_26, COL_DIM, "");
  lv_obj_align(g_deltaLbl, LV_ALIGN_TOP_MID, 0, 330);

  // Skylten: rund med rod ring, dar matarringen oppnar sig.
  g_signRing = lv_obj_create(scr);
  lv_obj_remove_style_all(g_signRing);
  lv_obj_set_size(g_signRing, 92, 92);
  lv_obj_align(g_signRing, LV_ALIGN_TOP_MID, 0, 372);
  lv_obj_set_style_radius(g_signRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_signRing, lv_color_hex(0xF5F5F5), 0);
  lv_obj_set_style_bg_opa(g_signRing, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_signRing, lv_color_hex(0xD42B2B), 0);
  lv_obj_set_style_border_width(g_signRing, 11, 0);
  lv_obj_set_style_shadow_color(g_signRing, lv_color_hex(0xD42B2B), 0);
  lv_obj_set_style_shadow_width(g_signRing, 22, 0);
  lv_obj_set_style_shadow_opa(g_signRing, 60, 0);
  g_signNum = label(g_signRing, &ui_font_44, lv_color_hex(0x101010), "50");
  lv_obj_center(g_signNum);
  g_signOff = label(scr, &ui_font_16, COL_FAINT, "gräns okänd");
  lv_obj_align(g_signOff, LV_ALIGN_TOP_MID, 0, 412);

  // Kameravarningen: ett lager OVER mataren, genomskinligt nog att farten
  // fortfarande syns bakom.
  g_camPanel = glass(scr);
  lv_obj_set_size(g_camPanel, 414, 66);
  lv_obj_align(g_camPanel, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_color(g_camPanel, COL_AMBER, 0);
  lv_obj_set_style_bg_opa(g_camPanel, 90, 0);
  lv_obj_set_style_border_color(g_camPanel, COL_AMBER, 0);
  lv_obj_set_style_border_opa(g_camPanel, 200, 0);
  g_camTitle = label(g_camPanel, &ui_font_26, COL_TEXT, "FARTKAMERA 800 m");
  lv_obj_align(g_camTitle, LV_ALIGN_TOP_LEFT, 14, 6);
  g_camLimit = label(g_camPanel, &ui_font_44, COL_TEXT, "80");
  lv_obj_align(g_camLimit, LV_ALIGN_RIGHT_MID, -12, 0);
  g_camBar = lv_bar_create(g_camPanel);
  lv_obj_remove_style_all(g_camBar);
  lv_obj_set_size(g_camBar, 300, 8);
  lv_obj_align(g_camBar, LV_ALIGN_BOTTOM_LEFT, 14, -8);
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
  lv_obj_set_size(g_tripPanel, 414, 64);
  lv_obj_align(g_tripPanel, LV_ALIGN_TOP_MID, 0, 470);
  g_tripTitle = label(g_tripPanel, &ui_font_20, COL_TEXT, "Ingen resa pågår");
  lv_obj_align(g_tripTitle, LV_ALIGN_TOP_LEFT, 14, 8);
  lv_label_set_long_mode(g_tripTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_tripTitle, 230);
  g_tripSub = label(g_tripPanel, &ui_font_16, COL_DIM, "");
  lv_obj_align(g_tripSub, LV_ALIGN_BOTTOM_LEFT, 14, -8);
  lv_label_set_long_mode(g_tripSub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_tripSub, 230);

  // Start/stopp ar skarmens viktigaste knapp och sitter i en bil - den ska
  // ga att traffa med tummen utan att titta. Darfor ar den stor och ligger
  // pa skarmen, inte i panelen: en 96-pixlars knapp far inte plats i en
  // 64 pixlar hog panel, sa den flyter over panelkanten.
  g_tripBtn = lv_button_create(scr);
  lv_obj_remove_style_all(g_tripBtn);
  lv_obj_set_size(g_tripBtn, 96, 96);
  lv_obj_align(g_tripBtn, LV_ALIGN_TOP_RIGHT, -18, 440);
  lv_obj_set_style_radius(g_tripBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_tripBtn, COL_GREEN, 0);
  lv_obj_set_style_bg_opa(g_tripBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(g_tripBtn, COL_GREEN, 0);
  lv_obj_set_style_shadow_width(g_tripBtn, 24, 0);
  lv_obj_set_style_shadow_opa(g_tripBtn, 90, 0);
  lv_obj_add_event_cb(g_tripBtn, trip_toggle_cb, LV_EVENT_CLICKED, nullptr);
  g_tripBtnLbl = label(g_tripBtn, &lv_font_montserrat_28, lv_color_white(),
                       LV_SYMBOL_PLAY);
  lv_obj_center(g_tripBtnLbl);

  g_splitBtn = lv_button_create(scr);
  lv_obj_remove_style_all(g_splitBtn);
  lv_obj_set_size(g_splitBtn, 64, 64);
  lv_obj_align(g_splitBtn, LV_ALIGN_TOP_RIGHT, -126, 470);
  lv_obj_set_style_radius(g_splitBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_splitBtn, COL_ACCENT, 0);
  lv_obj_set_style_bg_opa(g_splitBtn, 90, 0);
  lv_obj_set_style_border_color(g_splitBtn, COL_ACCENT, 0);
  lv_obj_set_style_border_opa(g_splitBtn, 160, 0);
  lv_obj_set_style_border_width(g_splitBtn, 1, 0);
  lv_obj_add_event_cb(g_splitBtn, split_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sp = label(g_splitBtn, &lv_font_montserrat_20, COL_TEXT,
                       LV_SYMBOL_CUT);
  lv_obj_center(sp);
  lv_obj_add_flag(g_splitBtn, LV_OBJ_FLAG_HIDDEN);

  // Syftesknapparna: den valda fylls, de andra ar glas.
  static const char *names[3] = {"PRIVAT", "FÖRETAG", "DIFFUST"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 132, 44);
    lv_obj_set_pos(b, 18 + i * 141, 540);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_add_event_cb(b, purpose_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(i + 1));
    g_purBtn[i] = b;
    g_purLbl[i] = label(b, &ui_font_20, COL_DIM, names[i]);
    lv_obj_center(g_purLbl[i]);
  }

  add_home_bar(scr);
}

static void style_purpose(int i, lv_color_t tint, bool active) {
  lv_obj_t *b = g_purBtn[i];
  if (active) {
    lv_obj_set_style_bg_color(b, tint, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_text_color(g_purLbl[i], lv_color_white(), 0);
  } else {
    lv_obj_set_style_bg_color(b, tint, 0);
    lv_obj_set_style_bg_opa(b, 30, 0);
    lv_obj_set_style_border_color(b, tint, 0);
    lv_obj_set_style_border_opa(b, 110, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_text_color(g_purLbl[i], tint, 0);
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
  lv_obj_set_style_text_color(g_speedLbl, zone, 0);
  lv_arc_set_value(g_arcMain, kmh > kSpeedMax ? kSpeedMax : kmh);
  lv_arc_set_value(g_arcGlow, kmh > kSpeedMax ? kSpeedMax : kmh);
  lv_obj_set_style_arc_color(g_arcMain, zone, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_arcGlow, zone, LV_PART_INDICATOR);

  if (m->limitKmh > 0) {
    lv_obj_clear_flag(g_signRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_signOff, LV_OBJ_FLAG_HIDDEN);
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->limitKmh);
    set_txt(g_signNum, buf);

    const float delta = m->speedKmh - (float)m->limitKmh;
    if (delta > 3.0f) {
      snprintf(buf, sizeof(buf), "+%d över", (int)(delta + 0.5f));
      lv_obj_set_style_text_color(g_deltaLbl, COL_RED, 0);
    } else if (delta < -1.0f) {
      snprintf(buf, sizeof(buf), "%d under", (int)(delta - 0.5f));
      lv_obj_set_style_text_color(g_deltaLbl, COL_GREEN, 0);
    } else {
      snprintf(buf, sizeof(buf), "på gränsen");
      lv_obj_set_style_text_color(g_deltaLbl, COL_AMBER, 0);
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
    lv_obj_set_style_bg_color(g_camPanel, t, 0);
    lv_obj_set_style_border_color(g_camPanel, t, 0);
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
    lv_obj_set_style_bg_color(g_tripBtn, COL_RED, 0);
    lv_obj_set_style_shadow_color(g_tripBtn, COL_RED, 0);
    set_txt(g_tripBtnLbl, LV_SYMBOL_STOP);
    lv_obj_clear_flag(g_splitBtn, LV_OBJ_FLAG_HIDDEN);
  } else {
    set_txt(g_tripTitle,
                      m->sdOk ? "Ingen resa pågår" : "Inget minneskort");
    set_txt(g_tripSub,
                      m->sdOk ? "startar själv när bilen rullar"
                              : "resor kan inte sparas utan kort");
    lv_obj_set_style_bg_color(g_tripBtn, COL_GREEN, 0);
    lv_obj_set_style_shadow_color(g_tripBtn, COL_GREEN, 0);
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
static int16_t kEcoR = 130;

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

  lv_obj_t *title = label(scr, &ui_font_26, COL_TEXT, "Ecodrive");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 44);

  // Resans medel som ring uppe till hoger.
  g_ecoArc = lv_arc_create(scr);
  lv_obj_remove_style_all(g_ecoArc);
  lv_obj_set_size(g_ecoArc, 96, 96);
  lv_obj_align(g_ecoArc, LV_ALIGN_TOP_RIGHT, -18, 40);
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
  g_ecoScoreLbl = label(g_ecoArc, &ui_font_44, COL_TEXT, "100");
  lv_obj_center(g_ecoScoreLbl);
  g_ecoAvgLbl = label(scr, &ui_font_16, COL_DIM, "resans medel");
  lv_obj_align(g_ecoAvgLbl, LV_ALIGN_TOP_RIGHT, -18, 140);

  // Bubblan: ett vattenpass baklanges - den ska sta stilla i mitten.
  g_ecoBubbleWrap = lv_obj_create(scr);
  lv_obj_remove_style_all(g_ecoBubbleWrap);
  lv_obj_set_size(g_ecoBubbleWrap, 2 * kEcoR + 4, 2 * kEcoR + 4);
  lv_obj_align(g_ecoBubbleWrap, LV_ALIGN_TOP_MID, 0, 170);
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
  lv_obj_set_size(g_ecoBubble, 34, 34);
  lv_obj_set_style_radius(g_ecoBubble, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_ecoBubble, COL_GREEN, 0);
  lv_obj_set_style_bg_opa(g_ecoBubble, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(g_ecoBubble, COL_GREEN, 0);
  lv_obj_set_style_shadow_width(g_ecoBubble, 24, 0);
  lv_obj_set_style_shadow_opa(g_ecoBubble, 140, 0);
  lv_obj_center(g_ecoBubble);

  g_ecoMagLbl = label(g_ecoBubbleWrap, &ui_font_26, COL_TEXT, "0,00 g");
  lv_obj_align(g_ecoMagLbl, LV_ALIGN_CENTER, 0, -6);

  g_ecoInfo = label(scr, &ui_font_16, COL_DIM, "");
  lv_obj_align(g_ecoInfo, LV_ALIGN_TOP_MID, 0, 448);
  lv_obj_set_style_text_align(g_ecoInfo, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *reset = ghost_button(scr, COL_ACCENT, "NOLLSTÄLL", &ui_font_20,
                                 eco_reset_cb, nullptr);
  lv_obj_set_size(reset, 200, 48);
  lv_obj_align(reset, LV_ALIGN_BOTTOM_LEFT, 18, -34);

  lv_obj_t *tare = ghost_button(scr, COL_CYAN, "TARA", &ui_font_20,
                                tare_cb, nullptr);
  lv_obj_set_size(tare, 200, 48);
  lv_obj_align(tare, LV_ALIGN_BOTTOM_RIGHT, -18, -34);
  g_tareBtnLbl = lv_obj_get_child(tare, 0);

  add_home_bar(scr);
}

static void update_eco(const GuiModel *m) {
  update_status(GUI_SCR_ECO, m);
  char buf[96];

  const int score = (int)(m->ecoTripScore + 0.5f);
  snprintf(buf, sizeof(buf), "%d", score);
  set_txt(g_ecoScoreLbl, buf);
  lv_arc_set_value(g_ecoArc, score);
  lv_color_t sc = score >= 75 ? COL_GREEN : score >= 40 ? COL_AMBER : COL_RED;
  lv_obj_set_style_arc_color(g_ecoArc, sc, LV_PART_INDICATOR);
  set_txt(g_ecoAvgLbl,
                    m->ecoMeasured ? "resans medel" : "mäter …");

  // Granserna ritas dar de ligger; flyttas de i granssnittet foljer ringarna.
  const float full = m->ecoBubbleG > 0.05f ? m->ecoBubbleG : 0.4f;
  const float pxPerG = (float)kEcoR / full;
  const int16_t rs = (int16_t)(m->ecoSoftG * pxPerG);
  const int16_t rh = (int16_t)(m->ecoHardG * pxPerG);
  lv_obj_set_size(g_ecoRingSoft, rs * 2, rs * 2);
  lv_obj_set_size(g_ecoRingHard, rh * 2, rh * 2);

  lv_color_t zone = m->ecoMagG >= m->ecoHardG ? COL_RED
                    : m->ecoMagG >= m->ecoSoftG ? COL_AMBER : COL_GREEN;
  if (m->ecoLevelled) {
    float px = m->ecoLonG * pxPerG;
    float py = m->ecoLatG * pxPerG;
    float d2 = px * px + py * py;
    const float rmax = (float)(kEcoR - 18);
    if (d2 > rmax * rmax) {
      float d = d2 > 0 ? rmax / __builtin_sqrtf(d2) : 0;
      px *= d; py *= d;
    }
    lv_obj_align(g_ecoBubble, LV_ALIGN_CENTER, (int16_t)py, (int16_t)-px);
    lv_obj_set_style_bg_color(g_ecoBubble, zone, 0);
    lv_obj_set_style_shadow_color(g_ecoBubble, zone, 0);
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
    snprintf(dir, sizeof(dir), "riktning inlärd");
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
  lv_obj_set_size(p, 198, 74);
  lv_obj_set_pos(p, x, y);
  *valOut = label(p, &ui_font_26, COL_TEXT, "–");
  lv_obj_align(*valOut, LV_ALIGN_TOP_LEFT, 14, 8);
  lv_obj_t *c = label(p, &ui_font_16, COL_DIM, cap);
  lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 14, -8);
  return p;
}

static void build_stats() {
  lv_obj_t *scr = make_screen();
  g_screens[GUI_SCR_STATS] = scr;
  add_status(scr, GUI_SCR_STATS);

  lv_obj_t *title = label(scr, &ui_font_26, COL_TEXT, "Statistik");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 44);

  g_statKm = label(scr, &ui_font_44, COL_TEXT, "0");
  lv_obj_align(g_statKm, LV_ALIGN_TOP_LEFT, 18, 84);

  stat_tile(scr, 18, 150, "resor", &g_statTiles[0]);
  stat_tile(scr, 234, 150, "rullande tid", &g_statTiles[1]);
  stat_tile(scr, 18, 232, "högsta fart", &g_statTiles[2]);
  stat_tile(scr, 234, 232, "över gränsen", &g_statTiles[3]);
  stat_tile(scr, 18, 314, "ledigt på kortet", &g_statTiles[4]);
  stat_tile(scr, 234, 314, "räcker till", &g_statTiles[5]);

  // Per syfte: tre staplar med samma farger som knapparna.
  g_statCard = glass(scr);
  lv_obj_set_size(g_statCard, 414, 130);
  lv_obj_align(g_statCard, LV_ALIGN_TOP_MID, 0, 400);
  lv_obj_t *cap = label(g_statCard, &ui_font_16, COL_DIM, "km per syfte");
  lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 14, 6);
  static const lv_color_t pc[3] = {COL_PRIVAT, COL_FORETAG, COL_DIFFUST};
  static const char *pn[3] = {"Privat", "Företag", "Diffust"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *n = label(g_statCard, &ui_font_16, COL_DIM, pn[i]);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, 14, 30 + i * 30);
    g_statBars[i] = lv_bar_create(g_statCard);
    lv_obj_remove_style_all(g_statBars[i]);
    lv_obj_set_size(g_statBars[i], 220, 12);
    lv_obj_align(g_statBars[i], LV_ALIGN_TOP_LEFT, 84, 34 + i * 30);
    lv_bar_set_range(g_statBars[i], 0, 100);
    lv_obj_set_style_bg_color(g_statBars[i], COL_PANEL_W, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_statBars[i], 24, LV_PART_MAIN);
    lv_obj_set_style_radius(g_statBars[i], 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_statBars[i], pc[i], LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_statBars[i], LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_statBars[i], 6, LV_PART_INDICATOR);
    g_statBarLbl[i] = label(g_statCard, &ui_font_16, COL_TEXT, "0");
    lv_obj_align(g_statBarLbl[i], LV_ALIGN_TOP_RIGHT, -14, 30 + i * 30);
  }

  add_home_bar(scr);
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
  lv_obj_set_style_text_color(g_statTiles[3],
                              m->statSpeedingS >= 60 ? COL_RED : COL_TEXT, 0);
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

  lv_obj_t *title = label(scr, &ui_font_26, COL_TEXT, "Moln & wifi");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 44);

  lv_obj_t *p1 = glass(scr);
  lv_obj_set_size(p1, 414, 120);
  lv_obj_align(p1, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_t *c1 = label(p1, &ui_font_16, COL_DIM, "ENHETENS EGET WIFI");
  lv_obj_align(c1, LV_ALIGN_TOP_LEFT, 14, 8);
  g_cloudAp = label(p1, &ui_font_20, COL_TEXT, "");
  lv_obj_align(g_cloudAp, LV_ALIGN_TOP_LEFT, 14, 34);
  lv_label_set_long_mode(g_cloudAp, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_cloudAp, 386);

  lv_obj_t *p2 = glass(scr);
  lv_obj_set_size(p2, 414, 150);
  lv_obj_align(p2, LV_ALIGN_TOP_MID, 0, 226);
  lv_obj_t *c2 = label(p2, &ui_font_16, COL_DIM, "MOLNSYNKEN");
  lv_obj_align(c2, LV_ALIGN_TOP_LEFT, 14, 8);

  // Autosynken av eller pa. Avslagen synkar enheten bara pa knappen nedanfor
  // - for den som vill valja nat och tillfalle sjalv.
  lv_obj_t *al = label(p2, &ui_font_16, COL_DIM, "AUTO");
  lv_obj_align(al, LV_ALIGN_TOP_RIGHT, -84, 12);
  g_cloudAuto = lv_switch_create(p2);
  lv_obj_set_size(g_cloudAuto, 64, 34);
  lv_obj_align(g_cloudAuto, LV_ALIGN_TOP_RIGHT, -10, 4);
  lv_obj_set_style_bg_color(g_cloudAuto, COL_ACCENT,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(g_cloudAuto, auto_sync_cb, LV_EVENT_VALUE_CHANGED,
                      nullptr);
  g_cloudState = label(p2, &ui_font_20, COL_TEXT, "");
  lv_obj_align(g_cloudState, LV_ALIGN_TOP_LEFT, 14, 34);
  lv_label_set_long_mode(g_cloudState, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_cloudState, 386);

  g_cloudCams = label(scr, &ui_font_16, COL_DIM, "");
  lv_obj_align(g_cloudCams, LV_ALIGN_TOP_MID, 0, 396);

  lv_obj_t *sync = ghost_button(scr, COL_CYAN, "SYNKA NU", &ui_font_20,
                                cloud_sync_cb, nullptr);
  lv_obj_set_size(sync, 414, 52);
  lv_obj_align(sync, LV_ALIGN_TOP_MID, 0, 430);

  add_home_bar(scr);
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

// ---------------------------------------------------------- installningar -

static lv_obj_t *g_setSound;
static lv_obj_t *g_setScreenVal;
static lv_obj_t *g_setVersion;

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

  lv_obj_t *title = label(scr, &ui_font_26, COL_TEXT, "Inställningar");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 44);

  // Ljudet.
  lv_obj_t *p1 = glass(scr);
  lv_obj_set_size(p1, 414, 70);
  lv_obj_align(p1, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_t *l1 = label(p1, &ui_font_20, COL_TEXT, "Ljud");
  lv_obj_align(l1, LV_ALIGN_LEFT_MID, 14, -10);
  lv_obj_t *h1 = label(p1, &ui_font_16, COL_DIM, "varningar och kvitton");
  lv_obj_align(h1, LV_ALIGN_LEFT_MID, 14, 14);
  g_setSound = lv_switch_create(p1);
  lv_obj_set_size(g_setSound, 64, 34);
  lv_obj_align(g_setSound, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_bg_color(g_setSound, COL_ACCENT,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(g_setSound, sound_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Skarmslackningen: minus och plus, som pa gamla skarmen fast rundare.
  lv_obj_t *p2 = glass(scr);
  lv_obj_set_size(p2, 414, 70);
  lv_obj_align(p2, LV_ALIGN_TOP_MID, 0, 174);
  lv_obj_t *l2 = label(p2, &ui_font_20, COL_TEXT, "Släck skärm");
  lv_obj_align(l2, LV_ALIGN_LEFT_MID, 14, -10);
  lv_obj_t *h2 = label(p2, &ui_font_16, COL_DIM, "när ingen resa pågår");
  lv_obj_align(h2, LV_ALIGN_LEFT_MID, 14, 14);
  lv_obj_t *minus = button(p2, COL_ACCENT, "–", &ui_font_26, lv_color_white(),
                           screen_step_cb, (void *)(intptr_t)-1);
  lv_obj_set_size(minus, 48, 44);
  lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -150, 0);
  lv_obj_t *plus = button(p2, COL_ACCENT, "+", &ui_font_26, lv_color_white(),
                          screen_step_cb, (void *)(intptr_t)1);
  lv_obj_set_size(plus, 48, 44);
  lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -10, 0);
  g_setScreenVal = label(p2, &ui_font_20, COL_TEXT, "5 min");
  lv_obj_align(g_setScreenVal, LV_ALIGN_RIGHT_MID, -68, 0);

  // Vad enheten vet om sig sjalv.
  lv_obj_t *p3 = glass(scr);
  lv_obj_set_size(p3, 414, 210);
  lv_obj_align(p3, LV_ALIGN_TOP_MID, 0, 256);
  g_setVersion = label(p3, &ui_font_16, COL_DIM, "");
  lv_obj_align(g_setVersion, LV_ALIGN_TOP_LEFT, 14, 10);
  lv_label_set_long_mode(g_setVersion, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_setVersion, 386);
  lv_obj_set_style_text_line_space(g_setVersion, 7, 0);

  add_home_bar(scr);
}

static void update_settings(const GuiModel *m) {
  update_status(GUI_SCR_SETTINGS, m);

  if (m->soundOn) lv_obj_add_state(g_setSound, LV_STATE_CHECKED);
  else lv_obj_remove_state(g_setSound, LV_STATE_CHECKED);

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
  lv_obj_set_size(g_askSheet, 450, 600);
  lv_obj_set_style_bg_color(g_askSheet, COL_BG, 0);
  lv_obj_set_style_bg_opa(g_askSheet, 248, 0);
  lv_obj_clear_flag(g_askSheet, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = label(g_askSheet, &ui_font_26, COL_TEXT, "Resan är klar");
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 46);
  g_askTitle = label(g_askSheet, &ui_font_20, COL_DIM, "");
  lv_obj_align(g_askTitle, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_t *q = label(g_askSheet, &ui_font_26, COL_TEXT, "Vad var resan till?");
  lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 140);
  g_askCount = label(g_askSheet, &ui_font_16, COL_FAINT, "");
  lv_obj_align(g_askCount, LV_ALIGN_TOP_MID, 0, 178);

  static const char *names[3] = {"PRIVAT", "FÖRETAG", "DIFFUST"};
  static const lv_color_t cols[3] = {COL_PRIVAT, COL_FORETAG, COL_DIFFUST};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = button(g_askSheet, cols[i], names[i], &ui_font_26,
                         lv_color_white(), ask_pick_cb,
                         (void *)(intptr_t)(i + 1));
    lv_obj_set_size(b, 414, 106);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 216 + i * 122);
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
  lv_obj_set_size(g_custSheet, 450, 600);
  lv_obj_set_style_bg_color(g_custSheet, COL_BG, 0);
  lv_obj_set_style_bg_opa(g_custSheet, 248, 0);
  lv_obj_clear_flag(g_custSheet, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = label(g_custSheet, &ui_font_26, COL_TEXT, "Välj kund");
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);

  lv_obj_t *listwrap = lv_obj_create(g_custSheet);
  lv_obj_remove_style_all(listwrap);
  lv_obj_set_size(listwrap, 414, 400);
  lv_obj_align(listwrap, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_set_flex_flow(listwrap, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(listwrap, 10, 0);
  lv_obj_set_scroll_dir(listwrap, LV_DIR_VER);

  if (m->customerCount == 0) {
    lv_obj_t *empty = label(listwrap, &ui_font_20, COL_DIM,
                            "Kundlistan är tom – lägg upp kunder i webben "
                            "och synka.");
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty, 380);
  }
  for (uint8_t i = 0; i < m->customerCount; i++) {
    lv_obj_t *b = ghost_button(listwrap, COL_ACCENT, m->customerNames[i],
                               &ui_font_20, cust_pick_cb,
                               (void *)m->customerNames[i]);
    lv_obj_set_size(b, 400, 56);
  }

  lv_obj_t *none = ghost_button(g_custSheet, COL_DIFFUST,
                                "INGEN KUND – BARA FÖRETAG", &ui_font_20,
                                cust_pick_cb, nullptr);
  lv_obj_set_size(none, 414, 52);
  lv_obj_align(none, LV_ALIGN_BOTTOM_MID, 0, -76);

  lv_obj_t *close = ghost_button(g_custSheet, COL_ACCENT, "STÄNG",
                                 &ui_font_20, cust_close_cb, nullptr);
  lv_obj_set_size(close, 414, 52);
  lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -16);
}

// ----------------------------------------------------------------- apiet --

void gui_screens_create(const GuiActions *actions) {
  g_act = actions;
  build_home();
  build_drive();
  build_eco();
  build_stats();
  build_cloud();
  build_settings();
  build_ask();
  lv_screen_load(g_screens[GUI_SCR_HOME]);
  g_current = GUI_SCR_HOME;
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
    case GUI_SCR_CLOUD: update_cloud(m); break;
    case GUI_SCR_SETTINGS: update_settings(m); break;
  }
  update_ask(m);
}
