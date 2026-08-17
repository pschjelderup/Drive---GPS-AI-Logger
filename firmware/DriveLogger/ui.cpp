#include "ui.h"

#include <math.h>

#include "config.h"
#include "gnss.h"
#include "sensors.h"
#include "sound.h"

namespace {

Arduino_Canvas *gfx = nullptr;

const uint16_t C_BG = RGB565(6, 9, 15);
const uint16_t C_PANEL = RGB565(22, 30, 44);
const uint16_t C_GLASS = RGB565(120, 170, 230);
const uint16_t C_ACCENT = RGB565(60, 160, 255);
const uint16_t C_GREEN = RGB565(40, 200, 120);
const uint16_t C_RED = RGB565(240, 60, 60);
const uint16_t C_TEXT = RGB565(240, 245, 250);
const uint16_t C_DIM = RGB565(140, 158, 180);
const uint16_t C_FAINT = RGB565(60, 72, 92);
const uint16_t C_WARN = RGB565(255, 185, 40);

// ---- huvudskarmens vaningar. Talen ar hojder, inte gissningar: farten far mest
// utrymme eftersom den lases oftast, och knappraden narmast handen ar den man
// trycker pa under fard.
const int16_t Y_STATUS = 6;
const int16_t H_STATUS = 32;
const int16_t Y_SPEED = 46;
const int16_t H_SPEED = 180;
const int16_t Y_DELTA = 234;
const int16_t H_DELTA = 34;
const int16_t Y_CAM = 276;
const int16_t H_CAM = 62;
const int16_t Y_TRIP = 346;
const int16_t H_TRIP = 62;

const int16_t kEcoRowH = 68;
int16_t ecoRowY(uint8_t row) { return (int16_t)(86 + row * 76); }
int16_t menuRowY(uint8_t row) { return (int16_t)(90 + row * 92); }

// Det inbyggda typsnittet har en glyf per byte enligt teckentabellen CP437. De
// svenska tecknen finns dar, men pa andra platser an i UTF-8, sa texten
// oversatts innan den skrivs ut.
const char *sv(const char *utf8) {
  static char buf[128];
  size_t o = 0;
  for (size_t i = 0; utf8[i] && o < sizeof(buf) - 1;) {
    const uint8_t c = (uint8_t)utf8[i];
    if (c == 0xC3 && utf8[i + 1]) {
      const uint8_t n = (uint8_t)utf8[i + 1];
      char out = '?';
      switch (n) {
        case 0xA5: out = (char)0x86; break;  // a med ring
        case 0xA4: out = (char)0x84; break;  // a med prickar
        case 0xB6: out = (char)0x94; break;  // o med prickar
        case 0x85: out = (char)0x8F; break;  // A med ring
        case 0x84: out = (char)0x8E; break;  // A med prickar
        case 0x96: out = (char)0x99; break;  // O med prickar
        case 0xA9: out = (char)0x82; break;  // e med accent
        default: out = '?'; break;
      }
      buf[o++] = out;
      i += 2;
    } else if (c == 0xC2 && utf8[i + 1]) {
      const uint8_t n = (uint8_t)utf8[i + 1];
      char out = '?';
      switch (n) {
        case 0xB0: out = (char)0xF8; break;  // gradtecken
        case 0xB7: out = (char)0xFA; break;  // mittpunkt, som avdelare
        default: out = '?'; break;
      }
      buf[o++] = out;
      i += 2;
    } else {
      buf[o++] = utf8[i++];
    }
  }
  buf[o] = '\0';
  return buf;
}

int16_t textWidth(const char *s, uint8_t size) {
  return (int16_t)(strlen(s) * 6 * size);
}

void printAt(int16_t x, int16_t y, uint8_t size, uint16_t color,
             const char *text) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(sv(text));
}

void printCentered(int16_t cx, int16_t y, uint8_t size, uint16_t color,
                   const char *text) {
  const char *converted = sv(text);
  printAt(cx - textWidth(converted, size) / 2, y, size, color, text);
}

void printRight(int16_t rx, int16_t y, uint8_t size, uint16_t color,
                const char *text) {
  const char *converted = sv(text);
  printAt(rx - textWidth(converted, size), y, size, color, text);
}

// ------------------------------------------------------ genomskinliga lager -
//
// Skarmen ritas forst i en bildbuffert i psram, och den bufferten gar att lasa
// tillbaka. Darfor kan ett lager blandas in i det som redan star under det, i
// stallet for att tacka over det. Det ar skillnaden mellan en varning som
// laggs ovanpa farten och en som slar ut den.

void blendPixel(uint16_t *fb, int16_t x, int16_t y, uint8_t r, uint8_t g,
                uint8_t b, uint8_t alpha) {
  uint16_t *p = fb + (int32_t)y * SCREEN_W + x;
  const uint16_t d = *p;
  const uint8_t dr = (d >> 11) & 0x1F;
  const uint8_t dg = (d >> 5) & 0x3F;
  const uint8_t db = d & 0x1F;

  const uint16_t inv = 255 - alpha;
  const uint8_t nr = (uint8_t)((r * alpha + dr * inv) / 255);
  const uint8_t ng = (uint8_t)((g * alpha + dg * inv) / 255);
  const uint8_t nb = (uint8_t)((b * alpha + db * inv) / 255);
  *p = (uint16_t)((nr << 11) | (ng << 5) | nb);
}

// Genomskinlig ruta med rundade horn. Radien raknas per rad, sa hornen blir
// mjuka utan att en enda pixel ritas utanfor.
void blendRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color,
               uint8_t alpha, int16_t radius) {
  if (!gfx) return;
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) return;

  const uint8_t r = (color >> 11) & 0x1F;
  const uint8_t g = (color >> 5) & 0x3F;
  const uint8_t b = color & 0x1F;

  if (radius * 2 > w) radius = w / 2;
  if (radius * 2 > h) radius = h / 2;

  for (int16_t dy = 0; dy < h; dy++) {
    const int16_t py = y + dy;
    if (py < 0 || py >= SCREEN_H) continue;

    int16_t inset = 0;
    if (radius > 0) {
      // Avstandet in fran kanten pa just den har raden, om raden ligger i ett
      // horn.
      int16_t d = -1;
      if (dy < radius) {
        d = radius - dy;
      } else if (dy >= h - radius) {
        d = radius - (h - 1 - dy);
      }
      if (d > 0) {
        const float k = sqrtf((float)(radius * radius - d * d));
        inset = (int16_t)(radius - k);
      }
    }

    for (int16_t dx = inset; dx < w - inset; dx++) {
      const int16_t px = x + dx;
      if (px < 0 || px >= SCREEN_W) continue;
      blendPixel(fb, px, py, r, g, b, alpha);
    }
  }
}

// Glaspanel: en svag ljus inblandning med en aning ljusare kant. Kanten ar det
// som gor att panelen lases som ett lager och inte som en flack.
void glassPanel(const Rect &box, uint8_t alpha = 26) {
  blendRect(box.x, box.y, box.w, box.h, C_GLASS, alpha, 14);
  gfx->drawRoundRect(box.x, box.y, box.w, box.h, 14, C_FAINT);
}

void drawPanel(const Rect &r, uint16_t fill) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 14, fill);
}

void drawButton(const Rect &r, uint16_t fill, const char *label, uint8_t size,
                uint16_t textColor) {
  drawPanel(r, fill);
  const uint8_t charH = 8 * size;
  printCentered(r.x + r.w / 2, r.y + (r.h - charH) / 2, size, textColor, label);
}

// Knapp med genomskinlig botten. Anvands dar knappen ligger over nagot som ska
// synas igenom, sa att bilden hanger ihop i stallet for att bli en rad rutor.
void glassButton(const Rect &r, uint16_t tint, uint8_t alpha, const char *label,
                 uint8_t size, uint16_t textColor) {
  blendRect(r.x, r.y, r.w, r.h, tint, alpha, 14);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 14, tint);
  const uint8_t charH = 8 * size;
  printCentered(r.x + r.w / 2, r.y + (r.h - charH) / 2, size, textColor, label);
}

// ------------------------------------------------------------- stora siffror
//
// Det inbyggda typsnittet forstoras med heltalsfaktorer och blir kantigt langt
// innan det blir stort nog for att lasas i ogonvran. Farten ritas darfor med
// egna segment: de ar skarpa i vilken storlek som helst och ser ut som en
// instrumentpanel i stallet for som forstorad text.

// Segmenten i ordningen a b c d e f g, ett bitmonster per siffra.
const uint8_t kDigitSegments[10] = {
    0x3F,  // 0: a b c d e f
    0x06,  // 1: b c
    0x5B,  // 2: a b d e g
    0x4F,  // 3: a b c d g
    0x66,  // 4: b c f g
    0x6D,  // 5: a c d f g
    0x7D,  // 6: a c d e f g
    0x07,  // 7: a b c
    0x7F,  // 8: alla
    0x6F,  // 9: a b c d f g
};

void drawDigit(uint8_t digit, int16_t x, int16_t y, int16_t w, int16_t h,
               int16_t t, uint16_t color) {
  if (digit > 9) return;
  const uint8_t seg = kDigitSegments[digit];
  const int16_t round = t / 2;

  const int16_t hy1 = y;
  const int16_t hy2 = y + (h - t) / 2;
  const int16_t hy3 = y + h - t;

  if (seg & 0x01) gfx->fillRoundRect(x + t, hy1, w - 2 * t, t, round, color);
  if (seg & 0x40) gfx->fillRoundRect(x + t, hy2, w - 2 * t, t, round, color);
  if (seg & 0x08) gfx->fillRoundRect(x + t, hy3, w - 2 * t, t, round, color);

  const int16_t upH = hy2 - hy1 - t;
  const int16_t loH = hy3 - hy2 - t;
  if (seg & 0x20) gfx->fillRoundRect(x, hy1 + t, t, upH, round, color);
  if (seg & 0x02) gfx->fillRoundRect(x + w - t, hy1 + t, t, upH, round, color);
  if (seg & 0x10) gfx->fillRoundRect(x, hy2 + t, t, loH, round, color);
  if (seg & 0x04) gfx->fillRoundRect(x + w - t, hy2 + t, t, loH, round, color);
}

// Hogerstalld siffergrupp. Hogerstalld med flit: da star entalssiffran alltid
// pa samma plats, och talet hoppar inte i sidled nar det gar fran 99 till 100.
void drawBigNumber(uint32_t value, int16_t rightX, int16_t y, int16_t h,
                   uint16_t color) {
  const int16_t w = (int16_t)(h * 0.50f);
  const int16_t t = (int16_t)(h * 0.135f);
  const int16_t gap = (int16_t)(w * 0.20f);

  char buf[8];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
  const size_t n = strlen(buf);

  int16_t x = rightX - (int16_t)n * w - (int16_t)(n - 1) * gap;
  for (size_t i = 0; i < n; i++) {
    drawDigit((uint8_t)(buf[i] - '0'), x, y, w, h, t, color);
    x += w + gap;
  }
}

// --------------------------------------------------------------- statusraden

void drawStatusBar(const AppSettings &cfg) {
  const GnssDebug d = gnss::debug();

  // GPS. Skillnaden mellan gra och gul ar den viktiga: gra betyder att kabeln
  // eller modulen inte fungerar, gul att allt ar rätt inkopplat och att det
  // bara ar att vanta.
  uint16_t gpsColor = C_FAINT;
  char gpsText[24] = "ingen GPS";
  if (d.present) {
    if (d.fixType >= 2) {
      gpsColor = C_GREEN;
      snprintf(gpsText, sizeof(gpsText), "%u sat", (unsigned)d.sats);
    } else {
      gpsColor = C_WARN;
      snprintf(gpsText, sizeof(gpsText), "soker");
    }
  }
  gfx->fillCircle(24, Y_STATUS + 15, 8, gpsColor);
  printAt(38, Y_STATUS + 9, 2, C_DIM, gpsText);

  // Kameralistan. Star det ingenting har vet man inte om varningen ar tyst
  // for att vagen ar fri eller for att filen fattas.
  char camText[24];
  if (cams::loaded()) {
    snprintf(camText, sizeof(camText), "%lu kam", (unsigned long)cams::count());
    printAt(150, Y_STATUS + 9, 2, C_DIM, camText);
  } else {
    printAt(150, Y_STATUS + 9, 2, C_RED, "ingen kamerafil");
  }

  // Minneskortet.
  gfx->fillCircle(330, Y_STATUS + 15, 8,
                  sensors::sdMounted() ? C_GREEN : C_RED);

  // Ljudet. Ikonen ar ocksa knappen - det ar den man vill na snabbast av allt
  // nar nagon sover i baksatet.
  const bool on = cfg.soundOn != 0;
  printRight(442, Y_STATUS + 9, 2, on ? C_TEXT : C_FAINT,
             on ? "LJUD PA" : "LJUD AV");
}

// --------------------------------------------------------------- fartrutan

void drawSpeedBlock(float speedKmh, uint8_t limitKmh) {
  const uint32_t kmh = (uint32_t)(speedKmh < 0 ? 0 : speedKmh + 0.5f);

  // Fargen sager om farten ar laglig, och den betyder samma sak overallt pa
  // skarmen.
  uint16_t color = C_TEXT;
  if (limitKmh > 0) {
    if (speedKmh > (float)limitKmh + LIMIT_TOLERANCE_KMH) {
      color = C_RED;
    } else if (speedKmh > (float)limitKmh - 5.0f) {
      color = C_WARN;
    } else {
      color = C_GREEN;
    }
  }

  drawBigNumber(kmh, 300, Y_SPEED + 14, H_SPEED - 40, color);
  printAt(232, Y_SPEED + H_SPEED - 22, 2, C_DIM, "km/h");

  // ---- hastighetsskylten. Rund med rod ring, som den vid vagen, sa att den
  // lases utan att forklaras.
  const int16_t cx = 372;
  const int16_t cy = Y_SPEED + 74;
  if (limitKmh > 0) {
    gfx->fillCircle(cx, cy, 58, RGB565(245, 245, 245));
    gfx->fillCircle(cx, cy, 48, RGB565(220, 40, 40));
    gfx->fillCircle(cx, cy, 38, RGB565(245, 245, 245));
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)limitKmh);
    printCentered(cx, cy - 12, 3, RGB565(15, 15, 15), buf);
  } else {
    gfx->drawCircle(cx, cy, 58, C_FAINT);
    gfx->drawCircle(cx, cy, 48, C_FAINT);
    printCentered(cx, cy - 16, 2, C_FAINT, "gräns");
    printCentered(cx, cy + 2, 2, C_FAINT, "okänd");
  }
}

// Hur mycket over eller under. Han som fragade efter den vill se en siffra, inte
// en farg - sa den star i klartext med tecken.
void drawDeltaRow(float speedKmh, uint8_t limitKmh) {
  const Rect box = {16, Y_DELTA, 418, H_DELTA};

  if (limitKmh == 0) {
    printCentered(225, Y_DELTA + 10, 2, C_FAINT,
                  "ingen skyltad hastighet här");
    return;
  }

  const float delta = speedKmh - (float)limitKmh;
  char buf[40];
  uint16_t color;

  if (delta > LIMIT_TOLERANCE_KMH) {
    snprintf(buf, sizeof(buf), "+%d km/h ÖVER", (int)(delta + 0.5f));
    color = C_RED;
  } else if (delta < -1.0f) {
    snprintf(buf, sizeof(buf), "%d km/h under", (int)(delta - 0.5f));
    color = C_GREEN;
  } else {
    snprintf(buf, sizeof(buf), "på gränsen");
    color = C_WARN;
  }

  // En stapel fran mitten: hoger for over, vanster for under. Den ger
  // storleken pa avvikelsen utan att man laser siffran.
  const int16_t cx = box.x + box.w / 2;
  gfx->drawFastVLine(cx, box.y + 2, H_DELTA - 20, C_FAINT);
  const float scale = 4.0f;  // pixlar per km/h
  int16_t len = (int16_t)(fabsf(delta) * scale);
  if (len > box.w / 2 - 6) len = box.w / 2 - 6;
  if (len > 1) {
    if (delta > 0) {
      gfx->fillRect(cx, box.y + 4, len, 8, color);
    } else {
      gfx->fillRect(cx - len, box.y + 4, len, 8, color);
    }
  }

  printCentered(cx, box.y + 16, 2, color, buf);
}

// ------------------------------------------------------------- kamerarutan

void drawCamRow(const CamWarning &w) {
  const Rect box = {16, Y_CAM, 418, H_CAM};

  if (!w.active) {
    if (!cams::loaded()) {
      glassPanel(box, 14);
      printCentered(225, Y_CAM + 14, 2, C_DIM, "Kameralistan saknas");
      printCentered(225, Y_CAM + 36, 1, C_FAINT,
                    "synka enheten for att kunna varna");
    } else {
      glassPanel(box, 14);
      printCentered(225, Y_CAM + 24, 2, C_FAINT, "ingen kamera i närheten");
    }
    return;
  }

  // Narheten avgor bade farg och hur mycket ljus panelen slapper igenom. En
  // varning pa attahundra meter ska inte se ut som en pa tvahundra.
  uint16_t tint = C_WARN;
  uint8_t alpha = 40;
  if (w.distanceM <= CAM_WARN_NEAR_M) {
    tint = C_RED;
    alpha = 90;
  } else if (w.distanceM <= CAM_WARN_MID_M) {
    tint = C_WARN;
    alpha = 64;
  }

  blendRect(box.x, box.y, box.w, box.h, tint, alpha, 14);
  gfx->drawRoundRect(box.x, box.y, box.w, box.h, 14, tint);

  char buf[48];
  if (w.averageSpeed) {
    snprintf(buf, sizeof(buf), "ATK-STRÄCKA  %lu m", (unsigned long)w.distanceM);
  } else {
    snprintf(buf, sizeof(buf), "FARTKAMERA  %lu m", (unsigned long)w.distanceM);
  }
  printAt(box.x + 14, box.y + 10, 2, C_TEXT, buf);

  if (w.limitKmh > 0) {
    snprintf(buf, sizeof(buf), "%u", (unsigned)w.limitKmh);
    printRight(box.x + box.w - 14, box.y + 8, 3, C_TEXT, buf);
  }

  // Stapeln fylls pa medan man narmar sig. Den sager "snart" tydligare an ett
  // tal gor.
  const int16_t barW = box.w - 28;
  int16_t fill = 0;
  if (w.distanceM < CAM_WARN_FAR_M) {
    fill = (int16_t)(barW * (CAM_WARN_FAR_M - w.distanceM) / CAM_WARN_FAR_M);
  }
  gfx->fillRoundRect(box.x + 14, box.y + 40, barW, 10, 5, C_FAINT);
  if (fill > 2) {
    gfx->fillRoundRect(box.x + 14, box.y + 40, fill, 10, 5, tint);
  }
}

// --------------------------------------------------------------- reserutan

void drawTripRow(const TripStatus &t) {
  const Rect box = {16, Y_TRIP, 418, H_TRIP};
  glassPanel(box, 22);

  char buf[64];
  if (!t.active) {
    if (!sensors::sdMounted()) {
      printCentered(225, Y_TRIP + 14, 2, C_RED, "Inget minneskort");
      printCentered(225, Y_TRIP + 38, 1, C_DIM,
                    "resor kan inte sparas utan kort");
      return;
    }
    printCentered(225, Y_TRIP + 14, 2, C_DIM, "Ingen resa pågår");
    printCentered(225, Y_TRIP + 38, 1, C_FAINT,
                  "startar av sig sjalv nar bilen rullar");
    return;
  }

  char km[16];
  snprintf(km, sizeof(km), "%.1f", t.distanceM / 1000.0);
  for (size_t i = 0; km[i]; i++) {
    if (km[i] == '.') km[i] = ',';
  }

  snprintf(buf, sizeof(buf), "RESA %lu  ·  %s km  ·  %02lu:%02lu",
           (unsigned long)t.index, km, (unsigned long)(t.elapsedS / 3600),
           (unsigned long)((t.elapsedS % 3600) / 60));
  printAt(box.x + 14, Y_TRIP + 10, 2, C_TEXT, buf);

  if (t.waitingForFix) {
    printAt(box.x + 14, Y_TRIP + 38, 1, C_WARN, "väntar på GPS-fix");
  } else if (t.stoppedS > 15) {
    snprintf(buf, sizeof(buf), "står stilla %lu s - avslutas vid %d s",
             (unsigned long)t.stoppedS, TRIP_STOP_S);
    printAt(box.x + 14, Y_TRIP + 38, 1, C_DIM, buf);
  } else if (t.customer[0]) {
    snprintf(buf, sizeof(buf), "kund: %s", t.customer);
    printAt(box.x + 14, Y_TRIP + 38, 1, C_ACCENT, buf);
  } else {
    snprintf(buf, sizeof(buf), "%lu punkter  ·  max %d km/h",
             (unsigned long)t.points, (int)(t.maxSpeedKmh + 0.5f));
    printAt(box.x + 14, Y_TRIP + 38, 1, C_DIM, buf);
  }
}

void drawPurposeButtons(const TripStatus &t) {
  const TripPurpose p = t.purpose;

  // Den valda knappen fylls, de andra ar genomskinliga. Da ser man vad resan
  // ar markt som utan att leta efter en bock.
  struct Item {
    const Rect *rect;
    TripPurpose purpose;
    const char *label;
    uint16_t color;
  };
  const Item items[3] = {
      {&ui::kBtnPrivat, PURPOSE_PRIVAT, "PRIVAT", C_ACCENT},
      {&ui::kBtnForetag, PURPOSE_FORETAG, "FÖRETAG", C_GREEN},
      {&ui::kBtnDiffust, PURPOSE_DIFFUST, "DIFFUST", C_WARN},
  };

  for (uint8_t i = 0; i < 3; i++) {
    const bool active = (p == items[i].purpose);
    if (active) {
      drawButton(*items[i].rect, items[i].color, items[i].label, 2, C_BG);
    } else {
      glassButton(*items[i].rect, items[i].color, 20, items[i].label, 2,
                  C_DIM);
    }
  }
}

}  // namespace

namespace ui {

// ---- huvudskarmen
const Rect kBtnPrivat = {16, 416, 134, 66};
const Rect kBtnForetag = {158, 416, 134, 66};
const Rect kBtnDiffust = {300, 416, 134, 66};
const Rect kBtnTripAction = {16, 490, 418, 44};
const Rect kBtnEco = {16, 542, 134, 52};
const Rect kBtnMiddle = {158, 542, 134, 52};
const Rect kBtnMenu = {300, 542, 134, 52};
const Rect kBtnSoundToggle = {346, 2, 98, 40};

// ---- fragan efter resan. Knapparna ar med flit orimligt stora: den har
// skarmen mots man av nar man just parkerat, och da ska svaret ga att ge med en
// tumme utan att sikta.
const Rect kBtnAskPrivat = {16, 220, 418, 104};
const Rect kBtnAskForetag = {16, 334, 418, 104};
const Rect kBtnAskDiffust = {16, 448, 418, 104};

// ---- kundlistan
Rect customerRow(uint8_t row) {
  return Rect{16, (int16_t)(90 + row * 66), 418, 58};
}
const Rect kBtnCustomerNone = {16, 490, 418, 46};
const Rect kBtnCustomerPrev = {16, 544, 200, 48};
const Rect kBtnCustomerNext = {234, 544, 200, 48};

// ---- ecodrive
const Rect kBtnEcoReset = {16, 528, 131, 56};
const Rect kBtnEcoLimits = {159, 528, 131, 56};
const Rect kBtnEcoBack = {302, 528, 132, 56};

Rect ecoMinus(uint8_t row) {
  return Rect{228, (int16_t)(ecoRowY(row) + 5), 58, 58};
}
Rect ecoPlus(uint8_t row) {
  return Rect{376, (int16_t)(ecoRowY(row) + 5), 58, 58};
}

// ---- menyn
Rect menuMinus(uint8_t row) {
  return Rect{228, (int16_t)menuRowY(row), 58, 58};
}
Rect menuPlus(uint8_t row) {
  return Rect{376, (int16_t)menuRowY(row), 58, 58};
}
const Rect kBtnTare = {16, 288, 418, 56};
const Rect kBtnBack = {16, 520, 418, 64};

void begin(Arduino_Canvas *canvas) { gfx = canvas; }

// Bakgrunden ar inte svart utan en aning ljusare mot horisonten, med ett svagt
// rutnat. Det ar det som gor att de genomskinliga lagren lases som lager - mot
// helt svart hade de sett ut som platta rutor.
void drawBackdrop() {
  gfx->fillScreen(C_BG);
  for (int16_t y = 120; y < SCREEN_H; y += 60) {
    gfx->drawFastHLine(0, y, SCREEN_W, RGB565(12, 17, 26));
  }
  for (int16_t x = 45; x < SCREEN_W; x += 90) {
    gfx->drawFastVLine(x, 120, SCREEN_H - 120, RGB565(11, 15, 24));
  }
}

void drawMain(const TripStatus &t, const CamWarning &w, uint8_t limitKmh,
              float speedKmh, const AppSettings &cfg) {
  if (!gfx) return;

  drawBackdrop();
  drawStatusBar(cfg);
  drawSpeedBlock(speedKmh, limitKmh);
  drawDeltaRow(speedKmh, limitKmh);
  drawCamRow(w);
  drawTripRow(t);
  drawPurposeButtons(t);

  // ---- resknappen
  if (!sensors::sdMounted()) {
    drawButton(kBtnTripAction, C_PANEL, "SÄTT I KORT", 2, C_DIM);
  } else if (t.active) {
    drawButton(kBtnTripAction, C_RED, "AVSLUTA RESA", 2, C_TEXT);
  } else {
    glassButton(kBtnTripAction, C_GREEN, 30, "STARTA RESA NU", 2, C_TEXT);
  }

  // ---- navigering. Mittknappen byter uppgift: under fard vill man kunna dela
  // resan, i stillhet valja kund.
  glassButton(kBtnEco, C_ACCENT, 24, "ECO", 2, C_TEXT);
  glassButton(kBtnMiddle, C_ACCENT, 24, t.active ? "DELA HÄR" : "KUND", 2,
              C_TEXT);
  glassButton(kBtnMenu, C_ACCENT, 24, "MENY", 2, C_TEXT);

  gfx->flush();
}

void drawPurposeAsk(const TripStatus &t, uint32_t secondsLeft) {
  if (!gfx) return;
  drawBackdrop();

  char buf[64];
  printCentered(225, 44, 3, C_TEXT, "RESAN ÄR KLAR");

  char km[16];
  snprintf(km, sizeof(km), "%.1f", t.awaitingKm);
  for (size_t i = 0; km[i]; i++) {
    if (km[i] == '.') km[i] = ',';
  }
  snprintf(buf, sizeof(buf), "Resa %lu  ·  %s km",
           (unsigned long)t.awaitingIndex, km);
  printCentered(225, 92, 2, C_DIM, buf);

  printCentered(225, 140, 2, C_TEXT, "Vad var resan till?");

  // Nedrakningen ar ett besked, inte ett hot: svarar man inte blir resan
  // diffus, vilket ar sant och gar att andra i webben efterat.
  snprintf(buf, sizeof(buf), "blir DIFFUST om %lu s",
           (unsigned long)secondsLeft);
  printCentered(225, 178, 2, C_FAINT, buf);

  drawButton(kBtnAskPrivat, C_ACCENT, "PRIVAT", 3, C_BG);
  drawButton(kBtnAskForetag, C_GREEN, "FÖRETAG", 3, C_BG);
  drawButton(kBtnAskDiffust, C_WARN, "DIFFUST", 3, C_BG);

  gfx->flush();
}

void drawCustomers(const char *const *names, uint8_t count, uint8_t page,
                   uint8_t pages) {
  if (!gfx) return;
  drawBackdrop();

  printAt(16, 22, 3, C_TEXT, "VÄLJ KUND");

  if (count == 0) {
    printCentered(225, 200, 2, C_DIM, "Kundlistan är tom");
    printCentered(225, 234, 1, C_FAINT, "lagg upp kunder i webben och synka");
    printCentered(225, 258, 1, C_FAINT, "eller skriv KUNDER.CSV pa kortet");
  }

  for (uint8_t i = 0; i < count; i++) {
    const Rect r = customerRow(i);
    glassButton(r, C_ACCENT, 18, names[i], 2, C_TEXT);
  }

  // Att valja kund innebar att resan ar en foretagsresa, sa den som inte hade
  // nagon kund behover ett satt att saga det utan att tappa markningen.
  glassButton(kBtnCustomerNone, C_WARN, 20, "INGEN KUND - BARA FÖRETAG", 2,
              C_TEXT);

  if (pages > 1) {
    char buf[24];
    snprintf(buf, sizeof(buf), "SIDA %u/%u", (unsigned)(page + 1),
             (unsigned)pages);
    glassButton(kBtnCustomerPrev, C_ACCENT, 24, "FÖRRA", 2, C_TEXT);
    glassButton(kBtnCustomerNext, C_ACCENT, 24, "NÄSTA", 2, C_TEXT);
    printCentered(225, 598 - 8, 1, C_FAINT, buf);
  } else {
    glassButton(kBtnCustomerPrev, C_ACCENT, 24, "TILLBAKA", 2, C_TEXT);
  }

  gfx->flush();
}

void drawEco(const EcoStatus &e) {
  if (!gfx) return;
  char buf[64];

  drawBackdrop();

  // Fargen foljer hur hart du kor just nu och ar samma overallt pa skarmen, sa
  // att man uppfattar den i ogonvran utan att lasa nagot.
  uint16_t zone;
  if (e.magG < e.softG) {
    zone = C_GREEN;
  } else if (e.magG < e.hardG) {
    zone = C_WARN;
  } else {
    zone = C_RED;
  }

  printAt(16, 14, 3, C_TEXT, "ECODRIVE");

  const int score = (int)(e.score + 0.5f);
  const char *grade;
  if (score >= 90) {
    grade = "UTMÄRKT";
  } else if (score >= 75) {
    grade = "BRA";
  } else if (score >= 60) {
    grade = "OK";
  } else if (score >= 40) {
    grade = "HACKIGT";
  } else {
    grade = "HÅRT";
  }

  uint16_t scoreColor = C_GREEN;
  if (score < 40) {
    scoreColor = C_RED;
  } else if (score < 75) {
    scoreColor = C_WARN;
  }

  drawBigNumber((uint32_t)(score < 0 ? 0 : score), 434, 8, 54, scoreColor);
  printRight(434, 68, 2, C_DIM, grade);

  // ---- bubblan. Ett vattenpass baklanges: den ska sta still i mitten.
  const int16_t cx = 225;
  const int16_t cy = 268;
  const int16_t rOuter = 148;
  const float full = e.bubbleG > 0.05f ? e.bubbleG : ECO_BUBBLE_FULL_G;
  const float pxPerG = (float)rOuter / full;

  for (int i = 1; i <= 4; i++) {
    gfx->drawCircle(cx, cy, (int16_t)(rOuter * i / 4), RGB565(45, 55, 72));
  }
  // Granserna ritas dar de faktiskt ligger i stallet for pa en fast ring.
  // Flyttar man dem i gransmenyn flyttar sig ringarna med.
  const int16_t rSoft = (int16_t)(e.softG * pxPerG);
  const int16_t rHard = (int16_t)(e.hardG * pxPerG);
  if (rSoft > 4 && rSoft <= rOuter) {
    gfx->drawCircle(cx, cy, rSoft, RGB565(60, 130, 85));
  }
  if (rHard > 4 && rHard <= rOuter) {
    gfx->drawCircle(cx, cy, rHard, RGB565(140, 55, 55));
  }
  gfx->drawFastHLine(cx - rOuter, cy, rOuter * 2, RGB565(38, 46, 60));
  gfx->drawFastVLine(cx, cy - rOuter, rOuter * 2, RGB565(38, 46, 60));

  // Etiketterna satts ut forst nar kortet vet vilket hall som ar framat. Innan
  // dess vore de en gissning, och en felmarkt axel ar samre an ingen.
  if (e.forwardKnown) {
    printCentered(cx, cy - rOuter - 22, 2, C_DIM, "GAS");
    printCentered(cx, cy + rOuter + 6, 2, C_DIM, "BROMS");
  }

  if (e.levelled) {
    if (e.peakG > 0.02f) {
      int16_t rp = (int16_t)(e.peakG * pxPerG);
      if (rp > rOuter) rp = rOuter;
      gfx->drawCircle(cx, cy, rp, RGB565(175, 135, 225));
    }

    float px = e.lonG * pxPerG;
    float py = e.latG * pxPerG;
    const float d = sqrtf(px * px + py * py);
    // Bubblan stannar vid ytterringen i stallet for att forsvinna ut ur rutan -
    // man vill se att det slog i taket, inte tappa den helt.
    if (d > rOuter) {
      px = px * rOuter / d;
      py = py * rOuter / d;
    }

    gfx->fillCircle(cx + (int16_t)py, cy - (int16_t)px, 17, zone);
    gfx->drawCircle(cx + (int16_t)py, cy - (int16_t)px, 17, C_TEXT);

    snprintf(buf, sizeof(buf), "%.2f g", e.magG);
    printCentered(cx, cy - 10, 3, C_TEXT, buf);
  } else {
    printCentered(cx, cy - 10, 2, C_DIM, "hittar lodlinjen ...");
  }

  // ---- raknarna
  const Rect box = {16, 440, 418, 82};
  glassPanel(box, 22);

  if (e.gpsClassify) {
    snprintf(buf, sizeof(buf), "Hårt: gas %lu  broms %lu  kurva %lu",
             (unsigned long)e.hardAccel, (unsigned long)e.hardBrake,
             (unsigned long)e.hardTurn);
  } else {
    // Utan GPS gar det inte att veta om ett ryck var gas, broms eller kurva. Da
    // sags det rakt ut i stallet for att gissa.
    snprintf(buf, sizeof(buf), "%lu hårda moment (ingen GPS)",
             (unsigned long)e.hardTotal);
  }
  printCentered(225, 450, 2, C_TEXT, buf);

  snprintf(buf, sizeof(buf), "Topp %.2f g", e.peakG);
  printCentered(225, 472, 2, C_ACCENT, buf);

  if (e.forwardKnown && e.forwardQuality >= 0.99f) {
    printCentered(225, 494, 2, C_GREEN, "Riktning: inlärd");
  } else if (e.forwardKnown) {
    snprintf(buf, sizeof(buf), "Riktning: lär sig %d%%",
             (int)(e.forwardQuality * 100.0f + 0.5f));
    printCentered(225, 494, 2, C_WARN, buf);
  } else if (!gnss::present()) {
    printCentered(225, 494, 2, C_DIM, "Riktning: kräver GPS");
  } else if (e.forwardNeedsGnss) {
    printCentered(225, 494, 2, C_WARN, "Riktning: väntar på GPS-fix");
  } else {
    printCentered(225, 494, 2, C_DIM, "Riktning: kör, gasa och bromsa");
  }

  glassButton(kBtnEcoReset, C_ACCENT, 24, "NOLLSTÄLL", 2, C_TEXT);
  glassButton(kBtnEcoLimits, C_ACCENT, 24, "GRÄNSER", 2, C_TEXT);
  drawButton(kBtnEcoBack, C_ACCENT, "TILLBAKA", 2, C_BG);

  gfx->flush();
}

void drawEcoLimits(const AppSettings &cfg, const EcoStatus &e) {
  if (!gfx) return;
  char buf[48];

  drawBackdrop();
  printAt(16, 14, 3, C_TEXT, "GRÄNSER");

  // Det levande vardet star kvar overst. Utan det skulle man stalla granser i
  // blindo - hela poangen med att menyn nas harifran ar att man ser vad man
  // faktiskt kor med medan man skruvar.
  snprintf(buf, sizeof(buf), "just nu %.2f g", e.magG);
  printRight(434, 22, 2,
             e.magG >= e.hardG ? C_RED : e.magG >= e.softG ? C_WARN : C_GREEN,
             buf);

  const char *labels[5] = {"Mjuk gräns", "Hård gräns", "Ytterring",
                           "Stränghet", "Poängfönster"};
  const char *hints[5] = {"börjar kosta poäng", "räknas som hårt moment",
                          "bubblans ytterkant", "poäng per g och sekund",
                          "tid tillbaka till 100"};

  char values[5][24];
  snprintf(values[0], sizeof(values[0]), "%.2f g", kEcoSoft[cfg.ecoSoftIdx]);
  snprintf(values[1], sizeof(values[1]), "%.2f g", kEcoHard[cfg.ecoHardIdx]);
  snprintf(values[2], sizeof(values[2]), "%.2f g", kEcoBubble[cfg.ecoBubbleIdx]);
  snprintf(values[3], sizeof(values[3]), "%.0f", kEcoPenalty[cfg.ecoPenaltyIdx]);
  const uint16_t win = kEcoWindowS[cfg.ecoWindowIdx];
  if (win < 60) {
    snprintf(values[4], sizeof(values[4]), "%u s", (unsigned)win);
  } else {
    snprintf(values[4], sizeof(values[4]), "%u min", (unsigned)(win / 60));
  }

  for (uint8_t row = 0; row < 5; row++) {
    const int16_t y = ecoRowY(row);
    const Rect panel = {16, y, 418, kEcoRowH};
    glassPanel(panel, 20);

    printAt(32, y + 12, 2, C_TEXT, labels[row]);
    printAt(32, y + 38, 1, C_DIM, hints[row]);

    drawButton(ecoMinus(row), C_ACCENT, "-", 3, C_BG);
    drawButton(ecoPlus(row), C_ACCENT, "+", 3, C_BG);

    printCentered(331, y + 20, 3, C_TEXT, values[row]);
  }

  drawButton(kBtnEcoBack, C_GREEN, "KLAR", 3, C_TEXT);
  gfx->flush();
}

void drawMenu(const AppSettings &cfg, const char *version) {
  if (!gfx) return;
  char buf[64];

  drawBackdrop();
  printAt(16, 14, 3, C_TEXT, "MENY");

  const char *labels[2] = {"Ljud", "Släck skärm"};
  const char *hints[2] = {"varningar och kvitton", "när ingen resa pågår"};
  char values[2][24];
  snprintf(values[0], sizeof(values[0]), "%s", cfg.soundOn ? "på" : "av");
  const uint16_t t = kScreenTimeouts[cfg.screenIdx];
  if (t == 0) {
    snprintf(values[1], sizeof(values[1]), "aldrig");
  } else if (t < 60) {
    snprintf(values[1], sizeof(values[1]), "%u s", (unsigned)t);
  } else {
    snprintf(values[1], sizeof(values[1]), "%u min", (unsigned)(t / 60));
  }

  for (uint8_t row = 0; row < 2; row++) {
    const int16_t y = menuRowY(row);
    const Rect panel = {16, (int16_t)(y - 6), 418, 70};
    glassPanel(panel, 20);
    printAt(32, y + 6, 2, C_TEXT, labels[row]);
    printAt(32, y + 32, 1, C_DIM, hints[row]);
    drawButton(menuMinus(row), C_ACCENT, "-", 3, C_BG);
    drawButton(menuPlus(row), C_ACCENT, "+", 3, C_BG);
    printCentered(331, y + 14, 3, C_TEXT, values[row]);
  }

  glassButton(kBtnTare, C_ACCENT, 24, "TARA - STÅ STILL", 2, C_TEXT);

  // ---- vad enheten vet om sig sjalv. Det ar den har rutan man laser nar nagot
  // inte stammer, sa den ska svara pa fragorna innan de stalls.
  const Rect info = {16, 356, 418, 152};
  glassPanel(info, 18);

  int16_t y = 368;
  snprintf(buf, sizeof(buf), "Version %s", version);
  printAt(32, y, 2, C_TEXT, buf);
  y += 26;

  if (cams::loaded()) {
    snprintf(buf, sizeof(buf), "Fartkameror: %lu", (unsigned long)cams::count());
    printAt(32, y, 2, C_GREEN, buf);
  } else {
    printAt(32, y, 2, C_RED, "Fartkameror: filen saknas");
  }
  y += 24;

  if (cams::limitsLoaded()) {
    printAt(32, y, 2, C_GREEN, "Hastighetsgränser: inlästa");
  } else {
    printAt(32, y, 2, C_WARN, "Hastighetsgränser: saknas");
  }
  y += 24;

  const uint64_t freeMb = sensors::freeBytes() / (1024ULL * 1024ULL);
  if (sensors::sdMounted()) {
    snprintf(buf, sizeof(buf), "Kort: %lu MB ledigt", (unsigned long)freeMb);
    printAt(32, y, 2, C_DIM, buf);
  } else {
    printAt(32, y, 2, C_RED, "Kort: saknas");
  }
  y += 24;

  const uint32_t nowUtc = sensors::unixUtc();
  if (nowUtc == 0) {
    printAt(32, y, 2, C_WARN, "Klocka: aldrig ställd");
  } else {
    char stampBuf[24];
    sensors::localStamp(nowUtc, stampBuf, sizeof(stampBuf));
    snprintf(buf, sizeof(buf), "Klocka: %s%s", stampBuf,
             sensors::clockSynced() ? " (GPS)" : "");
    printAt(32, y, 2, C_DIM, buf);
  }

  drawButton(kBtnBack, C_ACCENT, "TILLBAKA", 3, C_BG);
  gfx->flush();
}

void drawRecovered(const RecoveredTrip &r) {
  if (!gfx) return;
  char buf[64];

  drawBackdrop();

  printCentered(225, 60, 3, C_WARN, "STRÖMMEN FÖRSVANN");

  const Rect box = {16, 130, 418, 250};
  glassPanel(box, 26);

  printCentered(225, 152, 2, C_TEXT, "Förra resan avslutades inte.");
  printCentered(225, 182, 2, C_DIM, "Den är lagad och sparad:");

  char km[16];
  snprintf(km, sizeof(km), "%.1f", r.distanceM / 1000.0);
  for (size_t i = 0; km[i]; i++) {
    if (km[i] == '.') km[i] = ',';
  }

  snprintf(buf, sizeof(buf), "Resa %lu  ·  %s km", (unsigned long)r.index, km);
  printCentered(225, 220, 2, C_TEXT, buf);

  char when[24];
  sensors::localStamp(r.endUtc, when, sizeof(when));
  snprintf(buf, sizeof(buf), "Sista position %s", when);
  printCentered(225, 252, 1, C_DIM, buf);

  snprintf(buf, sizeof(buf), "%.5f, %.5f", r.lat, r.lon);
  printCentered(225, 274, 2, C_ACCENT, buf);

  printCentered(225, 310, 1, C_DIM, "Malet ar satt dar strommen forsvann.");
  printCentered(225, 332, 1, C_DIM, "Nasta resa borjar pa samma plats.");
  printCentered(225, 354, 1, C_FAINT, "Syftet blev diffust - andra det i webben.");

  drawButton(kBtnBack, C_ACCENT, "OK", 3, C_BG);
  gfx->flush();
}

void drawMessage(const char *title, const char *line1, const char *line2) {
  if (!gfx) return;
  drawBackdrop();
  printCentered(225, 230, 3, C_WARN, title);
  if (line1) printCentered(225, 300, 2, C_TEXT, line1);
  if (line2) printCentered(225, 330, 2, C_DIM, line2);
  gfx->flush();
}

}  // namespace ui
