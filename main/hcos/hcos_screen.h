// Copyright (C) 2026 bladept696. Part of Hash Commander OS, licensed under the GNU GPL v3 (see LICENSE).
// Hash Commander OS mining screen (320x170): neon look, animated fan, chart, thermometer, share bubbles
// and a celebration when a new best difficulty is found. It replaces the stock mining screen; the stock
// labels are still created (hidden) so the display driver can keep updating them.
#pragma once

#include "lvgl.h"

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
