// Skarmfamiljen, i ren LVGL.
//
// Har finns ingen Arduino och ingen hardvara: allt ritas ur GuiModel och
// allt anvandaren gor rapporteras via GuiActions. Samma fil bygger darfor
// bade firmware och host-forhandsvisningen som renderar skarmarna till PNG.
//
// Formspraket ar "renderad matarkonsol": djupsvart AMOLED-botten, en stor
// fartring med skala och neonfarg som foljer laget (gron under, barnsten
// nara, rod over), glaspaneler med genomskinlighet, och en hemskarm i
// telefonstil - appikoner i rutnat, statusrad overst och ett svep uppat
// fran underkanten som alltid leder hem.

#pragma once

#include <lvgl.h>

// Skarmens logiska matt. Skarmarna ar ritade for 450x600 (AMOLED 2.41);
// pa LCD 3.5-kortet ar ytan 320x480 och varje koordinat raknas om med
// SX/SY-makrona i gui_screens.cpp. Host-forhandsvisningen bygger med samma
// flagga och renderar da samma layout som kortet far.
#if defined(BOARD_LCD35)
#define GUI_W 320
#define GUI_H 480
#else
#define GUI_W 450
#define GUI_H 600
#endif

#include "gui_model.h"

enum GuiScreen : uint8_t {
  GUI_SCR_HOME = 0,
  GUI_SCR_DRIVE,
  GUI_SCR_ECO,
  GUI_SCR_STATS,
  GUI_SCR_CLOUD,
  GUI_SCR_OBD,
  GUI_SCR_SETTINGS,
  GUI_SCR_COUNT,
};

// Bygger alla skarmar en gang. Anropas efter lv_init + display + indev.
void gui_screens_create(const GuiActions *actions);

// For over modellens varden i widgetarna. Billigt nog att kora nagra ganger
// i sekunden; bara den aktiva skarmen rors.
void gui_screens_update(const GuiModel *m);

// Byt skarm programmatiskt (hemikonerna gor det sjalva).
void gui_screens_show(GuiScreen s, bool animate);
GuiScreen gui_screens_current();

// Kundvaljaren oppnas nar gluet fyllt modellens kundlista.
void gui_screens_open_customers(const GuiModel *m);
