// Copyright (C) 2026 bladept696. Part of Hash Commander OS, licensed under the GNU GPL v3 (see LICENSE).
#include "hcos_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "boards/board.h"
#include "global_state.h"
#include "hcos.h"

LV_FONT_DECLARE(ui_font_OpenSansBold13);
LV_FONT_DECLARE(ui_font_OpenSansBold14);
LV_FONT_DECLARE(ui_font_OpenSansBold24);
LV_FONT_DECLARE(ui_font_OpenSansBold45);

namespace hcos {

// ---------------------------------------------------------------- colours
#define C_BG1 lv_color_hex(0x0e0820)
#define C_BG2 lv_color_hex(0x030306)
#define C_GREEN lv_color_hex(0x3dff9a)
#define C_LIVE lv_color_hex(0x21ffb0)
#define C_PINK lv_color_hex(0xff4fd8)
#define C_PURPLE lv_color_hex(0xb14dff)
#define C_VIOLET lv_color_hex(0x7b4dff)
#define C_CYAN lv_color_hex(0x4fd6ff)
#define C_GOLD lv_color_hex(0xffd23f)
#define C_YELLOW lv_color_hex(0xfff27a)
#define C_WHITE lv_color_hex(0xffffff)
#define C_TXT lv_color_hex(0xe8eef6)
#define C_MUTED lv_color_hex(0x9fb6bf)
#define C_PANEL lv_color_hex(0x1c2130)
#define C_RED lv_color_hex(0xff5050)

static const int FRAME_MS = 66;          // ~15 frames per second, only while this screen is shown
static const int CHART_W = 104, CHART_H = 42, CHART_PTS = 27, CHART_STEP = 4;
static const int FAN_S = 30;
static const int TH_W = 42, TH_H = 72;
static const char *TITLE = "NERDQaxe";

struct Screen {
    Board *board = nullptr;
    lv_obj_t *scr = nullptr;
    lv_obj_t *liveDot = nullptr, *liveLbl = nullptr, *ip = nullptr, *wifi[4] = {};
    lv_obj_t *letters[8] = {};
    lv_obj_t *uptime = nullptr, *bd = nullptr, *bdBox = nullptr, *shares = nullptr;
    lv_obj_t *hr = nullptr, *hrGlow = nullptr, *hrUnit = nullptr, *effBar = nullptr, *effLbl = nullptr;
    lv_obj_t *chart = nullptr, *chartDot = nullptr, *chartBox = nullptr;
    lv_obj_t *fan = nullptr, *rpm = nullptr;
    lv_obj_t *thermo = nullptr, *temp = nullptr, *deg = nullptr, *cLbl = nullptr;
    lv_obj_t *asic = nullptr, *rows[5] = {}, *chips[4] = {};
    lv_obj_t *scan = nullptr;
    lv_color_t *chartBuf = nullptr, *fanBuf = nullptr, *thBuf = nullptr;
    float hist[CHART_PTS] = {};
    int histN = 0;
    int64_t lastSample = 0, lastData = 0;
    uint32_t frame = 0, lastSeq = 0;
    uint64_t lastBest = 0;
    float fanAngle = 0, tempNow = 0, hrNow = 0;
    int rpmNow = 0;
};

static Screen S;

// ---------------------------------------------------------------- helpers
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg, lv_opa_t opa, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *canvas(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t **buf)
{
    *buf = (lv_color_t *) heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h), MALLOC_CAP_SPIRAM);
    lv_obj_t *c = lv_canvas_create(parent);
    lv_canvas_set_buffer(c, *buf, w, h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(c, x, y);
    return c;
}

// small "o" drawn as a ring: the screen fonts have no degree sign
static lv_obj_t *degree(lv_obj_t *parent, lv_obj_t *after, lv_color_t color)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 5, 5);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 1, 0);
    lv_obj_set_style_border_color(d, color, 0);
    lv_obj_align_to(d, after, LV_ALIGN_OUT_RIGHT_TOP, 1, 2);
    return d;
}

static lv_color_t mix(lv_color_t a, lv_color_t b, float k)
{
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return lv_color_mix(b, a, (uint8_t) (k * 255));
}

static void fmt_hashrate(char *buf, size_t n, float ghs)
{
    snprintf(buf, n, "%.1f", ghs);
}

static void fmt_diff(char *buf, size_t n, double d)
{
    const char *u[] = {"", "k", "M", "G", "T", "P"};
    int i = 0;
    while (d >= 1000 && i < 5) {
        d /= 1000;
        i++;
    }
    snprintf(buf, n, i ? "%.1f%s" : "%.0f%s", d, u[i]);
}

// ---------------------------------------------------------------- drawing on the canvases
static void draw_chart()
{
    lv_canvas_fill_bg(S.chart, lv_color_hex(0x0b0716), LV_OPA_COVER);
    if (S.histN < 2) {
        return;
    }
    float lo = 1e9, hi = 0;
    for (int i = 0; i < S.histN; i++) {
        lo = fminf(lo, S.hist[i]);
        hi = fmaxf(hi, S.hist[i]);
    }
    float span = fmaxf(hi - lo, hi * 0.02f + 1);
    lo -= span * 0.25f;
    span *= 1.5f;
    lv_point_t pts[CHART_PTS];
    int off = CHART_PTS - S.histN;
    for (int i = 0; i < S.histN; i++) {
        pts[i].x = (off + i) * CHART_STEP;
        pts[i].y = CHART_H - 4 - (int) ((S.hist[i] - lo) / span * (CHART_H - 8));
    }
    // area under the line: one vertical line per pixel column, pink fading down
    lv_draw_line_dsc_t fill;
    lv_draw_line_dsc_init(&fill);
    fill.width = 1;
    for (int x = pts[0].x; x <= pts[S.histN - 1].x; x++) {
        int seg = (x - pts[0].x) / CHART_STEP;
        if (seg >= S.histN - 1) seg = S.histN - 2;
        float k = (float) (x - pts[seg].x) / CHART_STEP;
        int y = pts[seg].y + (int) ((pts[seg + 1].y - pts[seg].y) * k);
        lv_point_t v[2] = {{(lv_coord_t) x, (lv_coord_t) y}, {(lv_coord_t) x, CHART_H}};
        fill.color = mix(C_PURPLE, C_PINK, (float) x / CHART_W);
        fill.opa = 70;
        lv_canvas_draw_line(S.chart, v, 2, &fill);
    }
    // the line, cyan -> purple -> pink
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 2;
    line.round_start = line.round_end = 1;
    for (int i = 0; i < S.histN - 1; i++) {
        float k = (float) pts[i].x / CHART_W;
        line.color = k < 0.5f ? mix(C_CYAN, C_PURPLE, k * 2) : mix(C_PURPLE, C_PINK, (k - 0.5f) * 2);
        lv_canvas_draw_line(S.chart, &pts[i], 2, &line);
    }
    lv_obj_set_pos(S.chartDot, 74 + pts[S.histN - 1].x - 3, 58 + pts[S.histN - 1].y - 3);
}

static void draw_fan()
{
    lv_canvas_fill_bg(S.fan, lv_color_hex(0x121d2b), LV_OPA_COVER);
    lv_draw_rect_dsc_t ring;
    lv_draw_rect_dsc_init(&ring);
    ring.bg_color = lv_color_hex(0x0b1520);
    ring.radius = LV_RADIUS_CIRCLE;
    ring.border_color = C_CYAN;
    ring.border_width = 1;
    lv_canvas_draw_rect(S.fan, 0, 0, FAN_S, FAN_S, &ring);
    lv_draw_rect_dsc_t blade;
    lv_draw_rect_dsc_init(&blade);
    blade.bg_color = lv_color_hex(0x8be3ff);
    const float c = FAN_S / 2.0f;
    for (int b = 0; b < 5; b++) {
        float a = S.fanAngle + b * (2 * M_PI / 5);
        lv_point_t p[4];
        p[0] = {(lv_coord_t) (c + cosf(a - 0.35f) * 3), (lv_coord_t) (c + sinf(a - 0.35f) * 3)};
        p[1] = {(lv_coord_t) (c + cosf(a - 0.15f) * 12.5f), (lv_coord_t) (c + sinf(a - 0.15f) * 12.5f)};
        p[2] = {(lv_coord_t) (c + cosf(a + 0.45f) * 11.5f), (lv_coord_t) (c + sinf(a + 0.45f) * 11.5f)};
        p[3] = {(lv_coord_t) (c + cosf(a + 0.35f) * 3), (lv_coord_t) (c + sinf(a + 0.35f) * 3)};
        lv_canvas_draw_polygon(S.fan, p, 4, &blade);
    }
    lv_draw_rect_dsc_t hub;
    lv_draw_rect_dsc_init(&hub);
    hub.bg_color = lv_color_hex(0xddffff);
    hub.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(S.fan, (lv_coord_t) c - 3, (lv_coord_t) c - 3, 6, 6, &hub);
}

static void draw_thermo()
{
    lv_canvas_fill_bg(S.thermo, lv_color_hex(0x10283a), LV_OPA_COVER);
    float t = S.tempNow;
    float lvl = (t - 30) / 45;
    if (lvl < 0.05f) lvl = 0.05f;
    if (lvl > 0.95f) lvl = 0.95f;
    // blue, orange above 65 C, red above 75 C
    lv_color_t top = t >= 75 ? lv_color_hex(0xff8a8a) : t >= 65 ? lv_color_hex(0xffd27a) : lv_color_hex(0x9ef2ff);
    lv_color_t bot = t >= 75 ? lv_color_hex(0xe02020) : t >= 65 ? lv_color_hex(0xff7a1a) : lv_color_hex(0x1e8fff);
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.width = 1;
    float ph = S.frame * 0.33f;
    int surface = TH_H - (int) ((TH_H - 4) * lvl);
    for (int x = 0; x < TH_W; x++) {
        int y = surface + (int) (sinf(x * 0.3f + ph) * 1.6f);
        lv_point_t v[2] = {{(lv_coord_t) x, (lv_coord_t) y}, {(lv_coord_t) x, TH_H}};
        l.color = mix(top, bot, 0.5f);
        lv_canvas_draw_line(S.thermo, v, 2, &l);
        lv_canvas_set_px_color(S.thermo, x, y, top);
    }
    // bubbles going up
    for (int i = 0; i < 4; i++) {
        int range = TH_H - surface;
        if (range < 6) break;
        int y = TH_H - ((S.frame * 1 + i * 19) % range);
        lv_canvas_set_px_color(S.thermo, 6 + i * 9, y, lv_color_white());
        lv_canvas_set_px_color(S.thermo, 7 + i * 9, y, lv_color_white());
    }
}

// ---------------------------------------------------------------- animated extras
static void anim_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *) o, v); }
static void anim_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *) o, v); }
static void anim_opa(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *) o, v, 0); }
static void anim_del(lv_anim_t *a) { lv_obj_del((lv_obj_t *) a->var); }

static void anim(lv_obj_t *o, lv_anim_exec_xcb_t cb, int from, int to, int ms, int delay, bool del_after)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (del_after) {
        lv_anim_set_ready_cb(&a, anim_del);
    }
    lv_anim_start(&a);
}

// a share leaves the chart as a small green (accepted) or red (rejected) bubble
static void share_bubble(double diff, bool bad)
{
    char t[24], d[12];
    fmt_diff(d, sizeof(d), diff);
    snprintf(t, sizeof(t), "%s %s", bad ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK, d);
    lv_obj_t *b = label(S.scr, &lv_font_montserrat_10, lv_color_hex(0x04110a), 140, 76, t);
    lv_obj_set_style_bg_color(b, bad ? C_RED : C_LIVE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 3, 0);
    lv_obj_set_style_pad_hor(b, 3, 0);
    anim(b, anim_y, 76, 52, 1300, 0, true);
    anim(b, anim_opa, 255, 0, 1300, 0, false);
}

// new best difficulty: sparks, the BD box flashes gold and a banner
static void celebrate(const char *best)
{
    static const uint32_t cols[] = {0xffd23f, 0xff4fd8, 0x3dff9a, 0xffffff};
    for (int i = 0; i < 22; i++) {
        lv_obj_t *p = box(S.scr, 37, 71, 3, 3, lv_color_hex(cols[i % 4]), LV_OPA_COVER, 1);
        float a = i * 0.83f;
        int dist = 22 + (i * 7) % 40;
        anim(p, anim_x, 37, 37 + (int) (cosf(a) * dist), 1100, 0, false);
        anim(p, anim_y, 71, 71 + (int) (sinf(a) * dist) + 14, 1100, 0, false);
        anim(p, anim_opa, 255, 0, 1100, 0, true);
    }
    lv_obj_t *banner = box(S.scr, 4, 44, 172, 17, lv_color_hex(0x140a00), 220, 5);
    lv_obj_set_style_border_color(banner, C_GOLD, 0);
    lv_obj_set_style_border_width(banner, 1, 0);
    char t[40];
    snprintf(t, sizeof(t), "NOVA MELHOR DIFF %s", best);
    lv_obj_t *l = label(banner, &ui_font_OpenSansBold13, C_GOLD, 0, 0, t);
    lv_obj_center(l);
    anim(banner, anim_opa, 255, 0, 900, 1800, true);
    lv_obj_set_style_bg_color(S.bdBox, lv_color_hex(0x3a2c08), 0);
    lv_obj_set_style_border_color(S.bdBox, C_GOLD, 0);
}

// ---------------------------------------------------------------- data (once a second)
static void update_data(int64_t now)
{
    char buf[48];
    Board *board = S.board;
    bool shutdown = POWER_MANAGEMENT_MODULE.isShutdown();
    float hr = shutdown ? 0 : SYSTEM_MODULE.getCurrentHashrate();
    S.hrNow = hr;
    S.tempNow = POWER_MANAGEMENT_MODULE.getChipTempMax();
    S.rpmNow = POWER_MANAGEMENT_MODULE.getFanRPM(0);

    fmt_hashrate(buf, sizeof(buf), hr);
    lv_label_set_text(S.hr, buf);
    lv_label_set_text(S.hrGlow, buf);
    lv_obj_update_layout(S.hr);
    lv_obj_align_to(S.hrUnit, S.hr, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -8);

    float power = POWER_MANAGEMENT_MODULE.getPower();
    float expected = board ? board->getAsicFrequency() * board->getAsicCount() *
                                 (board->getAsics() ? board->getAsics()->getSmallCoreCount() : 0) / 1000.0f
                           : 0;
    int pct = expected > 0 ? (int) (hr * 100 / expected) : 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(S.effBar, pct, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%d%% do previsto  %.1f J/TH", pct, hr > 0 ? power / (hr / 1000.0f) : 0.0f);
    lv_label_set_text(S.effLbl, buf);

    int64_t up = (now - SYSTEM_MODULE.getStartTime()) / 1000000;
    snprintf(buf, sizeof(buf), "%dd %dh %02dm %02ds", (int) (up / 86400), (int) (up % 86400 / 3600), (int) (up % 3600 / 60),
             (int) (up % 60));
    lv_label_set_text(S.uptime, buf);

    if (STRATUM_MANAGER) {
        lv_label_set_text(S.bd, STRATUM_MANAGER->getBestDiffString());
        uint64_t best = STRATUM_MANAGER->getBestDiff();
        if (S.lastBest && best > S.lastBest) {
            celebrate(STRATUM_MANAGER->getBestDiffString());
        }
        S.lastBest = best;
    }

    snprintf(buf, sizeof(buf), "%u", (unsigned) S.rpmNow);
    lv_label_set_text(S.rpm, buf);
    snprintf(buf, sizeof(buf), "%.0f", S.tempNow);
    lv_label_set_text(S.temp, buf);
    lv_obj_update_layout(S.temp);
    lv_obj_align_to(S.deg, S.temp, LV_ALIGN_OUT_RIGHT_TOP, 1, 2);
    lv_obj_align_to(S.cLbl, S.temp, LV_ALIGN_OUT_RIGHT_MID, 7, 0);

    lv_label_set_text(S.ip, SYSTEM_MODULE.getIPAddress());
    int rssi = SYSTEM_MODULE.get_wifi_rssi();
    int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi < 0 ? 1 : 4;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(S.wifi[i], i < bars ? lv_color_hex(0x2a8cff) : lv_color_hex(0x9fb4cc), 0);
    }

    snprintf(buf, sizeof(buf), "Vi %.1fV", POWER_MANAGEMENT_MODULE.getVoltage() / 1000.0f);
    lv_label_set_text(S.rows[0], buf);
    snprintf(buf, sizeof(buf), "Vc %.2fV", board ? board->getVout() : 0.0f);
    lv_label_set_text(S.rows[1], buf);
    snprintf(buf, sizeof(buf), "A  %.2fA", POWER_MANAGEMENT_MODULE.getCurrent() / 1000.0f);
    lv_label_set_text(S.rows[2], buf);
    snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %.1fW", power);
    lv_label_set_text(S.rows[3], buf);
    snprintf(buf, sizeof(buf), "%.1f W/T", hr > 0 ? power / (hr / 1000.0f) : 0.0f);
    lv_label_set_text(S.rows[4], buf);

    int n = board ? board->getAsicCount() : 0;
    for (int i = 0; i < 4; i++) {
        float chip = i < n ? HASHRATE_MONITOR.getChipHashrate(i) : 0;
        float share = (n > 0 && hr > 0) ? chip / (hr / n) : 0;
        lv_bar_set_value(S.chips[i], (int) (fminf(share, 1.2f) / 1.2f * 100), LV_ANIM_ON);
        lv_obj_set_style_bg_color(S.chips[i], share < 0.9f ? lv_color_hex(0xffc04d) : C_GREEN, LV_PART_INDICATOR);
    }

    bool live = live_clients() > 0;
    lv_label_set_text(S.liveLbl, live ? "HC OS ao vivo" : "HC OS 0.2");
    lv_obj_set_style_text_color(S.liveLbl, live ? lv_color_hex(0x5dffb0) : C_MUTED, 0);

    snprintf(buf, sizeof(buf), "%lu shares", (unsigned long) share_last());
    lv_label_set_text(S.shares, buf);
}

// ---------------------------------------------------------------- frame (15 per second)
static void frame_cb(lv_timer_t *)
{
    if (lv_scr_act() != S.scr) {
        return;     // nothing is drawn while another screen is shown
    }
    int64_t now = esp_timer_get_time();
    S.frame++;

    if (now - S.lastData >= 1000000) {
        S.lastData = now;
        update_data(now);
    }
    if (now - S.lastSample >= 5000000 && S.hrNow > 0) {
        S.lastSample = now;
        if (S.histN == CHART_PTS) {
            memmove(S.hist, S.hist + 1, sizeof(float) * (CHART_PTS - 1));
            S.histN--;
        }
        S.hist[S.histN++] = S.hrNow;
        draw_chart();
    }

    // new shares leave the chart as bubbles (at most one per frame)
    uint32_t last = share_last();
    if (last != S.lastSeq) {
        uint32_t seq = (last - S.lastSeq > 3) ? last : S.lastSeq + 1;
        double diff;
        uint8_t st;
        if (S.lastSeq && share_get(seq, &diff, &st)) {
            share_bubble(diff, st == 2);
        }
        S.lastSeq = seq;
    }

    // fan at an eighth of its real speed (the real one would just look like a blur)
    S.fanAngle += S.rpmNow / 60.0f * 2 * M_PI * FRAME_MS / 1000.0f / 8;
    draw_fan();
    if (S.frame % 2 == 0) {
        draw_thermo();
    }

    // pulsing dots
    float p = 0.5f + 0.5f * sinf(S.frame * 0.4f);
    lv_obj_set_style_bg_opa(S.liveDot, 120 + (int) (135 * p), 0);
    lv_obj_set_style_bg_opa(S.chartDot, 150 + (int) (105 * p), 0);

    // a white shine runs over the title letters every 3 s
    float shine = (S.frame % 90) / 90.0f * 24 - 2;
    for (int i = 0; i < 8; i++) {
        lv_color_t base = i < 4 ? C_GREEN : C_PINK;
        float k = 1 - fabsf(i - shine) / 1.5f;
        lv_obj_set_style_text_color(S.letters[i], mix(base, C_WHITE, k * 0.45f), 0);
    }

}

// ---------------------------------------------------------------- build
lv_obj_t *screen_create(Board *board)
{
    S.board = board;
    lv_obj_t *scr = lv_obj_create(NULL);
    S.scr = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, C_BG1, 0);
    lv_obj_set_style_bg_grad_color(scr, C_BG2, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // glows and grid (static)
    lv_obj_t *g1 = box(scr, -60, -80, 220, 160, lv_color_hex(0x5a2bd6), 22, LV_RADIUS_CIRCLE);
    lv_obj_t *g2 = box(scr, 230, 110, 160, 120, lv_color_hex(0x00505f), 30, LV_RADIUS_CIRCLE);
    (void) g1;
    (void) g2;
    for (int x = 16; x < 320; x += 16) box(scr, x, 0, 1, 170, C_WHITE, 4, 0);
    for (int y = 16; y < 170; y += 16) box(scr, 0, y, 320, 1, C_WHITE, 4, 0);

    // top: live + IP + wifi
    S.liveDot = box(scr, 6, 6, 5, 5, C_LIVE, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    S.liveLbl = label(scr, &lv_font_montserrat_10, lv_color_hex(0x5dffb0), 14, 2, "HC OS ao vivo");
    lv_obj_t *ipBox = box(scr, 146, 2, 100, 15, lv_color_hex(0x0b1520), LV_OPA_COVER, 4);
    lv_obj_set_style_border_color(ipBox, C_CYAN, 0);
    lv_obj_set_style_border_width(ipBox, 1, 0);
    S.ip = label(ipBox, &lv_font_montserrat_10, C_CYAN, 5, 1, "0.0.0.0");
    for (int i = 0; i < 4; i++) S.wifi[i] = box(ipBox, 79 + i * 3, 10 - i * 2, 2, 2 + i * 2, lv_color_hex(0x2a8cff), LV_OPA_COVER, 0);

    // title, one label per letter so the shine can run over it
    int x = 6;
    for (int i = 0; i < 8; i++) {
        char ch[2] = {TITLE[i], 0};
        S.letters[i] = label(scr, &ui_font_OpenSansBold24, i < 4 ? C_GREEN : C_PINK, x, 12, ch);
        x += lv_txt_get_width(ch, 1, &ui_font_OpenSansBold24, 0, LV_TEXT_FLAG_NONE);
    }
    label(scr, &ui_font_OpenSansBold13, C_PINK, x + 1, 12, "++");

    // HC OS badge + uptime
    lv_obj_t *badge = box(scr, 6, 45, 64, 14, lv_color_hex(0x1c0f3a), LV_OPA_COVER, 4);
    lv_obj_set_style_border_color(badge, lv_color_hex(0x9b5cff), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_center(label(badge, &lv_font_montserrat_10, lv_color_hex(0xd6c2ff), 0, 0, "HC OS " HCOS_VERSION));
    S.uptime = label(scr, &lv_font_montserrat_10, C_YELLOW, 74, 47, "0d 0h 00m 00s");

    // best difficulty
    S.bdBox = box(scr, 6, 63, 64, 16, lv_color_hex(0x1a0a1c), LV_OPA_COVER, 4);
    lv_obj_set_style_border_color(S.bdBox, lv_color_hex(0xff4f9a), 0);
    lv_obj_set_style_border_opa(S.bdBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(S.bdBox, 1, 0);
    lv_obj_t *bdTag = label(S.bdBox, &lv_font_montserrat_10, lv_color_hex(0xff6fae), 3, 1, "BD");
    (void) bdTag;
    S.bd = label(S.bdBox, &ui_font_OpenSansBold13, C_WHITE, 20, 0, "-");

    // chart
    S.chartBox = box(scr, 73, 57, CHART_W + 2, CHART_H + 2, C_PINK, 0, 5);
    lv_obj_set_style_border_color(S.chartBox, C_PINK, 0);
    lv_obj_set_style_border_opa(S.chartBox, 90, 0);
    lv_obj_set_style_border_width(S.chartBox, 1, 0);
    S.chart = canvas(scr, 74, 58, CHART_W, CHART_H, &S.chartBuf);
    S.shares = label(scr, &lv_font_montserrat_10, C_MUTED, 77, 59, "0 shares");
    S.chartDot = box(scr, 74 + CHART_W - 4, 70, 6, 6, C_WHITE, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    lv_obj_set_style_outline_color(S.chartDot, C_PINK, 0);
    lv_obj_set_style_outline_width(S.chartDot, 2, 0);
    lv_obj_set_style_outline_opa(S.chartDot, 160, 0);

    // hashrate with a soft purple "glow" copy behind it
    S.hrGlow = label(scr, &ui_font_OpenSansBold45, C_PURPLE, 7, 93, "0.0");
    lv_obj_set_style_text_opa(S.hrGlow, 70, 0);
    S.hr = label(scr, &ui_font_OpenSansBold45, C_WHITE, 5, 91, "0.0");
    S.hrUnit = label(scr, &ui_font_OpenSansBold13, lv_color_hex(0xd9c8ff), 150, 125, "GH/s");
    S.effBar = lv_bar_create(scr);
    lv_obj_remove_style_all(S.effBar);
    lv_obj_set_pos(S.effBar, 6, 148);
    lv_obj_set_size(S.effBar, 170, 4);
    lv_obj_set_style_bg_color(S.effBar, C_WHITE, 0);
    lv_obj_set_style_bg_opa(S.effBar, 25, 0);
    lv_obj_set_style_radius(S.effBar, 2, 0);
    lv_obj_set_style_bg_color(S.effBar, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(S.effBar, C_PINK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(S.effBar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(S.effBar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(S.effBar, 2, LV_PART_INDICATOR);
    S.effLbl = label(scr, &lv_font_montserrat_10, C_MUTED, 6, 155, "");

    // fan
    lv_obj_t *fanBox = box(scr, 184, 22, 46, 58, lv_color_hex(0x121d2b), LV_OPA_COVER, 6);
    lv_obj_set_style_border_color(fanBox, C_CYAN, 0);
    lv_obj_set_style_border_opa(fanBox, 90, 0);
    lv_obj_set_style_border_width(fanBox, 1, 0);
    S.fan = canvas(fanBox, 7, 3, FAN_S, FAN_S, &S.fanBuf);
    S.rpm = label(fanBox, &lv_font_montserrat_10, C_TXT, 0, 34, "0");
    lv_obj_set_style_text_align(S.rpm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(S.rpm, 44);
    lv_obj_t *rpmL = label(fanBox, &lv_font_montserrat_10, C_MUTED, 0, 44, "RPM");
    lv_obj_set_style_text_align(rpmL, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(rpmL, 44);

    // glass thermometer
    lv_obj_t *thBox = box(scr, 184, 86, 46, 78, lv_color_hex(0x10283a), LV_OPA_COVER, 7);
    lv_obj_set_style_border_color(thBox, lv_color_hex(0x8be3ff), 0);
    lv_obj_set_style_border_opa(thBox, 180, 0);
    lv_obj_set_style_border_width(thBox, 1, 0);
    lv_obj_set_style_clip_corner(thBox, true, 0);
    S.thermo = canvas(thBox, 2, 3, TH_W, TH_H, &S.thBuf);
    S.temp = label(thBox, &ui_font_OpenSansBold14, C_WHITE, 6, 3, "0");
    S.cLbl = label(thBox, &ui_font_OpenSansBold14, C_WHITE, 28, 3, "C");
    S.deg = degree(thBox, S.temp, C_WHITE);

    // right panel
    lv_obj_t *side = box(scr, 236, 20, 79, 145, lv_color_hex(0x121624), LV_OPA_COVER, 7);
    lv_obj_set_style_border_color(side, C_WHITE, 0);
    lv_obj_set_style_border_opa(side, 35, 0);
    lv_obj_set_style_border_width(side, 1, 0);
    S.asic = label(side, &ui_font_OpenSansBold13, C_WHITE, 5, 3, board ? board->getAsicModel() : "");
    for (int i = 0; i < 5; i++) S.rows[i] = label(side, &lv_font_montserrat_10, C_TXT, 5, 22 + i * 14, "");
    label(side, &lv_font_montserrat_10, C_MUTED, 5, 94, "CHIPS");
    for (int i = 0; i < 4; i++) {
        S.chips[i] = lv_bar_create(side);
        lv_obj_remove_style_all(S.chips[i]);
        lv_obj_set_pos(S.chips[i], 5 + i * 17, 108);
        lv_obj_set_size(S.chips[i], 13, 32);
        lv_obj_set_style_bg_color(S.chips[i], C_WHITE, 0);
        lv_obj_set_style_bg_opa(S.chips[i], 20, 0);
        lv_obj_set_style_radius(S.chips[i], 2, 0);
        lv_obj_set_style_bg_color(S.chips[i], C_GREEN, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(S.chips[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(S.chips[i], 2, LV_PART_INDICATOR);
        lv_bar_set_range(S.chips[i], 0, 100);
    }

    draw_chart();
    draw_fan();
    draw_thermo();
    S.lastSeq = share_last();
    lv_timer_create(frame_cb, FRAME_MS, NULL);
    return scr;
}

} // namespace hcos
