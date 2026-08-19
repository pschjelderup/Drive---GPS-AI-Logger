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

#include "gui_model.h"

enum GuiScreen : uint8_t {
  GUI_SCR_HOME = 0,
  GUI_SCR_DRIVE,
  GUI_SCR_ECO,
  GUI_SCR_STATS,
  GUI_SCR_CLOUD,
  GUI_SCR_SETTINGS,
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
