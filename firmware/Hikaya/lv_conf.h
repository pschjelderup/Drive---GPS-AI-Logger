/**
 * LVGL-konfiguration for Hikaya.
 *
 * Filen kopieras till Arduino-biblioteksmappen av byggena (CI och lokalt),
 * eftersom LVGL letar efter den dar - en sokvag in i skissen gar inte att
 * ange portabelt. Andra HAR, inte kopian.
 */
#if 1  /* Enable content */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Standardbibliotekets minneshantering: pa ESP32 hamnar stora block i psram
 * av sig sjalva, och pa vardadatorn ar det vanliga malloc. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DPI_DEF 130

/* Tidsbasen far LVGL skota sjalv fran millis-kallan vi anger i gluet. */
#define LV_USE_OS LV_OS_NONE

/* Ritning: mjukvarurenderaren racker gott; ingen GPU pa kortet. */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0

/* Typsnitten ar vara egna (Inter, med svenska tecken). Montserrat 14 far
 * sta kvar som nodfallback och for LV_SYMBOL-ikonerna. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Bada kortens typsnittsuppsattningar deklareras; skarmkoden valjer per
 * kort. 12/14/18/32/110 ar 3.5-kortets skala av 16/20/26/44/150. */
#define LV_FONT_CUSTOM_DECLARE \
  LV_FONT_DECLARE(ui_font_12)  \
  LV_FONT_DECLARE(ui_font_14)  \
  LV_FONT_DECLARE(ui_font_16)  \
  LV_FONT_DECLARE(ui_font_18)  \
  LV_FONT_DECLARE(ui_font_20)  \
  LV_FONT_DECLARE(ui_font_26)  \
  LV_FONT_DECLARE(ui_font_32)  \
  LV_FONT_DECLARE(ui_font_44)  \
  LV_FONT_DECLARE(ui_font_110) \
  LV_FONT_DECLARE(ui_font_150)

/* Widgets: bara det som anvands, resten ar flash i onodan. */
#define LV_USE_ARC 1
#define LV_USE_BUTTON 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_IMAGE 1
#define LV_USE_SCALE 1
#define LV_USE_TABLE 0
#define LV_USE_CHART 0
#define LV_USE_CALENDAR 0
#define LV_USE_KEYBOARD 0
#define LV_USE_TEXTAREA 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SPAN 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ANIMIMG 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_SCALE_LINE_NEEDLE 0

/* Teman: grundtemat racker - all stil satts for hand anda. */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_MUSIC 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_SCROLL 0
#define LV_USE_DEMO_MULTILANG 0
#define LV_USE_DEMO_FLEX_LAYOUT 0
#define LV_USE_DEMO_TRANSFORM 0
#define LV_USE_DEMO_VECTOR_GRAPHIC 0
#define LV_USE_DEMO_EBIKE 0
#define LV_USE_DEMO_HIGH_RES 0
#define LV_USE_DEMO_SMARTWATCH 0

#endif /* LV_CONF_H */
#endif /* Enable content */
