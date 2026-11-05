// Copyright (C) 2026 bladept696. Part of Hash Commander OS, licensed under the GNU GPL v3 (see LICENSE).
// Hash Commander OS mining screen (320x170): neon look, animated fan, chart, thermometer, share bubbles
// and a celebration when a new best difficulty is found. It replaces the stock mining screen; the stock
// labels are still created (hidden) so the display driver can keep updating them.
#pragma once

#include "lvgl.h"

// Two screens: the stock 320x170 (1.9") and the 480x320 (3.5") of the YYSLUPING big-screen NerdQAxe++ (build with HCOS_BIG).
// The layout is written for 320x170; on the big screen every position and size is 1.5x (HS) and drawn at that size,
// with fonts rendered at their final size (hcos_fonts_big.c), so nothing is a blown-up copy of the small screen.
#ifdef HCOS_BIG
#define HS(v) ((int) ((v) * 3 / 2))
#define HCOS_W 480
#define HCOS_H 320
#define HCOS_LH 213   // logical height: 320 / 1.5
LV_FONT_DECLARE(hcos_big_m15);
LV_FONT_DECLARE(hcos_big_m21);
LV_FONT_DECLARE(hcos_big_o20);
LV_FONT_DECLARE(hcos_big_o21);
LV_FONT_DECLARE(hcos_big_o36);
LV_FONT_DECLARE(hcos_big_o68);
#define F_M10 (&hcos_big_m15)
#define F_M14 (&hcos_big_m21)
#define F_O13 (&hcos_big_o20)
#define F_O14 (&hcos_big_o21)
#define F_O24 (&hcos_big_o36)
#define F_O45 (&hcos_big_o68)
#else
#define HS(v) ((int) (v))
#define HCOS_W 320
#define HCOS_H 170
#define HCOS_LH 170
LV_FONT_DECLARE(ui_font_OpenSansBold13);
LV_FONT_DECLARE(ui_font_OpenSansBold14);
LV_FONT_DECLARE(ui_font_OpenSansBold24);
LV_FONT_DECLARE(ui_font_OpenSansBold45);
#define F_M10 (&lv_font_montserrat_10)
#define F_M14 (&lv_font_montserrat_14)
#define F_O13 (&ui_font_OpenSansBold13)
#define F_O14 (&ui_font_OpenSansBold14)
#define F_O24 (&ui_font_OpenSansBold24)
#define F_O45 (&ui_font_OpenSansBold45)
#endif

class Board;

namespace hcos {

// builds the screen; call from the LVGL context (UI::miningScreenInit)
lv_obj_t *screen_create(Board *board);

// boot splash (logo, version and a filling bar); replaces the stock first splash
lv_obj_t *splash_create();

// pages after the mining screen (top button): pool and machine, Bitcoin, Bitcoin network (hcos_pages.cpp)
lv_obj_t *page_pool_create();
lv_obj_t *page_btc_create();
lv_obj_t *page_net_create();
void pages_start();

// the four page dots at the top right (active = 0..3)
void nav_dots(lv_obj_t *scr, int active);

} // namespace hcos
