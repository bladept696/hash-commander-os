// Copyright (C) 2026 bladept696. Part of Hash Commander OS, licensed under the GNU GPL v3 (see LICENSE).
// Hash Commander OS pages shown with the top button after the mining screen: pool and machine, Bitcoin,
// Bitcoin network. Same neon look as the mining screen. They replace the stock settings / BTC price /
// global stats screens; the stock ones are still created (never shown) so the display driver can keep
// writing their labels. The screen fonts are ASCII only, so the texts have no accents.
#include "hcos_screen.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

#include "boards/board.h"
#include "global_state.h"
#include "stratum/stratum_manager_dual_pool.h"
#include "stratum/stratum_manager_fallback.h"

LV_FONT_DECLARE(ui_font_OpenSansBold13);
LV_FONT_DECLARE(ui_font_OpenSansBold14);
LV_FONT_DECLARE(ui_font_OpenSansBold24);
LV_FONT_DECLARE(ui_font_OpenSansBold45);

namespace hcos {

#define P_BG1 lv_color_hex(0x0e0820)
#define P_BG2 lv_color_hex(0x030306)
#define P_PINK lv_color_hex(0xff7be5)
#define P_MUTED lv_color_hex(0x8f88a8)
#define P_WHITE lv_color_hex(0xffffff)
#define P_SOFT lv_color_hex(0xd6c2ff)
#define P_CYAN lv_color_hex(0x4fd6ff)
#define P_GOLD lv_color_hex(0xffd23f)
#define P_GREEN lv_color_hex(0x3dff9a)
#define P_RED lv_color_hex(0xff8a9a)

static lv_obj_t *pbox(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg, lv_opa_t opa, int radius)
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

static lv_obj_t *plabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

// dark card with a thin light border, like the mining screen panels
static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t border)
{
    lv_obj_t *c = pbox(parent, x, y, w, h, lv_color_hex(0x140d26), LV_OPA_COVER, 6);
    lv_obj_set_style_border_color(c, border, 0);
    lv_obj_set_style_border_opa(c, 110, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    return c;
}

static lv_obj_t *pbar(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2a2140), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_color(b, P_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(b, lv_color_hex(0xff4fd8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
    lv_bar_set_range(b, 0, 1000);
    return b;
}

void nav_dots(lv_obj_t *scr, int active)
{
    for (int i = 0; i < 4; i++) {
        lv_obj_t *d = pbox(scr, 290 + i * 7, 6, 5, 5, i == active ? lv_color_hex(0xff4fd8) : lv_color_hex(0x4a4060), LV_OPA_COVER,
                           LV_RADIUS_CIRCLE);
        (void) d;
    }
}

// background, grid, title with a green dot, page dots
static lv_obj_t *page(const char *title, int idx)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, P_BG1, 0);
    lv_obj_set_style_bg_grad_color(scr, P_BG2, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    pbox(scr, -60, -80, 220, 160, lv_color_hex(0x5a2bd6), 22, LV_RADIUS_CIRCLE);
    for (int x = 16; x < 320; x += 16) pbox(scr, x, 0, 1, 170, P_WHITE, 4, 0);
    for (int y = 16; y < 170; y += 16) pbox(scr, 0, y, 320, 1, P_WHITE, 4, 0);
    pbox(scr, 8, 7, 6, 6, P_GREEN, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    plabel(scr, &ui_font_OpenSansBold13, P_PINK, 18, 2, title);
    nav_dots(scr, idx);
    return scr;
}

// "80298" -> "80 298"
static void thousands(char *out, size_t n, unsigned long long v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", v);
    int len = strlen(tmp), o = 0;
    for (int i = 0; i < len && o < (int) n - 2; i++) {
        if (i && (len - i) % 3 == 0) out[o++] = ' ';
        out[o++] = tmp[i];
    }
    out[o] = 0;
}

static struct {
    lv_obj_t *pool = nullptr, *poolHost = nullptr, *poolPort = nullptr, *poolNr = nullptr, *bd = nullptr;
    lv_obj_t *shBar = nullptr, *shAcc = nullptr, *shRej = nullptr;
    lv_obj_t *freq = nullptr, *volt = nullptr, *fan = nullptr, *ip = nullptr, *up = nullptr;

    lv_obj_t *btc = nullptr, *price = nullptr, *block = nullptr, *feeLo = nullptr, *feeMid = nullptr, *feeHi = nullptr, *mine = nullptr;

    lv_obj_t *net = nullptr, *netHash = nullptr, *diff = nullptr, *halvBar = nullptr, *halvPct = nullptr, *halvLeft = nullptr, *share = nullptr;
    lv_timer_t *timer = nullptr;
} P;

// ---------------------------------------------------------------- pool and machine
static void update_pool()
{
    char buf[64];
    Board *board = SYSTEM_MODULE.getBoard();
    StratumManager *m = STRATUM_MANAGER;
    if (m) {
        const char *host = "";
        int port = 0;
        uint64_t acc = m->getSharesAccepted(), rej = m->getSharesRejected();
        if (m->isDualPool()) {
            auto *d = static_cast<StratumManagerDualPool *>(m);
            host = d->getPoolHost(0);
            port = d->getPoolPort(0);
            lv_label_set_text(P.poolNr, "POOL 1 + 2");
        } else if (m->isFallback()) {
            auto *f = static_cast<StratumManagerFallback *>(m);
            host = f->getCurrentPoolHost();
            port = f->getCurrentPoolPort();
            lv_label_set_text(P.poolNr, "POOL ATIVA");
        }
        lv_label_set_text(P.poolHost, host && host[0] ? host : "-");
        snprintf(buf, sizeof(buf), ":%d", port);
        lv_label_set_text(P.poolPort, buf);
        lv_label_set_text(P.bd, m->getBestDiffString());
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long) acc);
        lv_label_set_text(P.shAcc, buf);
        snprintf(buf, sizeof(buf), "/ %llu", (unsigned long long) rej);
        lv_label_set_text(P.shRej, buf);
        lv_obj_align_to(P.shRej, P.shAcc, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, 0);
        lv_bar_set_value(P.shBar, acc + rej ? (int) (acc * 1000 / (acc + rej)) : 0, LV_ANIM_OFF);
    }
    if (board) {
        snprintf(buf, sizeof(buf), "%d MHz", (int) board->getAsicFrequency());
        lv_label_set_text(P.freq, buf);
        snprintf(buf, sizeof(buf), "%d mV", (int) board->getAsicVoltageMillis());
        lv_label_set_text(P.volt, buf);
        int mode = board->getFanMode(0);
        if (mode == 1) {
            lv_label_set_text(P.fan, "AUTO");
        } else if (mode == 2) {
            lv_label_set_text(P.fan, "PID");
        } else {
            snprintf(buf, sizeof(buf), "%d %%", (int) POWER_MANAGEMENT_MODULE.getFanPerc());
            lv_label_set_text(P.fan, buf);
        }
    }
    lv_label_set_text(P.ip, SYSTEM_MODULE.getIPAddress());
    int64_t up = (esp_timer_get_time() - SYSTEM_MODULE.getStartTime()) / 1000000;
    snprintf(buf, sizeof(buf), "ligada %dd %dh %02dm", (int) (up / 86400), (int) (up % 86400 / 3600), (int) (up % 3600 / 60));
    lv_label_set_text(P.up, buf);
}

lv_obj_t *page_pool_create()
{
    lv_obj_t *s = page("POOL E MAQUINA", 1);
    P.pool = s;
    lv_obj_t *c = card(s, 8, 22, 204, 46, lv_color_hex(0x9b5cff));
    P.poolNr = plabel(c, &lv_font_montserrat_10, P_MUTED, 6, 3, "POOL ATIVA");
    P.poolPort = plabel(c, &lv_font_montserrat_10, P_GREEN, 150, 3, ":0");
    P.poolHost = plabel(c, &ui_font_OpenSansBold14, P_WHITE, 6, 20, "-");
    lv_label_set_long_mode(P.poolHost, LV_LABEL_LONG_DOT);
    lv_obj_set_width(P.poolHost, 192);

    lv_obj_t *b = card(s, 218, 22, 94, 46, P_GOLD);
    plabel(b, &lv_font_montserrat_10, P_MUTED, 6, 3, "MELHOR DIFF");
    P.bd = plabel(b, &ui_font_OpenSansBold24, P_GOLD, 6, 15, "-");

    plabel(s, &lv_font_montserrat_10, P_MUTED, 8, 72, "SHARES ACEITES / REJEITADAS");
    P.shBar = pbar(s, 8, 88, 204, 7);
    P.shAcc = plabel(s, &ui_font_OpenSansBold14, P_WHITE, 220, 82, "0");
    P.shRej = plabel(s, &ui_font_OpenSansBold14, P_RED, 250, 82, "/ 0");

    const char *keys[3] = {"FREQUENCIA", "TENSAO", "VENTOINHA"};
    lv_obj_t **vals[3] = {&P.freq, &P.volt, &P.fan};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *k = card(s, 8 + i * 102, 102, 98, 38, lv_color_hex(0x4a4060));
        plabel(k, &lv_font_montserrat_10, P_MUTED, 6, 3, keys[i]);
        *vals[i] = plabel(k, &ui_font_OpenSansBold14, P_WHITE, 6, 17, "-");
    }

    lv_obj_t *ipb = pbox(s, 8, 148, 104, 16, lv_color_hex(0x0b1520), LV_OPA_COVER, 8);
    lv_obj_set_style_border_color(ipb, P_CYAN, 0);
    lv_obj_set_style_border_width(ipb, 1, 0);
    P.ip = plabel(ipb, &lv_font_montserrat_10, P_CYAN, 7, 2, "0.0.0.0");
    P.up = plabel(s, &ui_font_OpenSansBold13, P_SOFT, 150, 149, "ligada");
    update_pool();
    return s;
}

// ---------------------------------------------------------------- bitcoin
static void update_btc()
{
    char buf[48], n[32];
    uint32_t price = APIs_FETCHER.getPrice();
    if (price) {
        thousands(n, sizeof(n), price);
        snprintf(buf, sizeof(buf), "$%s", n);
    } else {
        snprintf(buf, sizeof(buf), "a carregar");
    }
    lv_label_set_text(P.price, buf);
    uint32_t h = APIs_FETCHER.getBlockHeight();
    if (h) {
        thousands(n, sizeof(n), h);
        snprintf(buf, sizeof(buf), "#%s", n);
        lv_label_set_text(P.block, buf);
    }
    snprintf(buf, sizeof(buf), "%lu", (unsigned long) APIs_FETCHER.getLowestFee());
    lv_label_set_text(P.feeLo, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long) APIs_FETCHER.getMidFee());
    lv_label_set_text(P.feeMid, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long) APIs_FETCHER.getFastestFee());
    lv_label_set_text(P.feeHi, buf);
    float hr = SYSTEM_MODULE.getCurrentHashrate();
    float power = POWER_MANAGEMENT_MODULE.getPower();
    snprintf(buf, sizeof(buf), "o teu hashrate: %.2f TH/s  -  %.1f J/TH", hr / 1000.0f, hr > 0 ? power / (hr / 1000.0f) : 0.0f);
    lv_label_set_text(P.mine, buf);
}

lv_obj_t *page_btc_create()
{
    lv_obj_t *s = page("BITCOIN", 2);
    P.btc = s;
    plabel(s, &lv_font_montserrat_10, P_MUTED, 8, 22, "PRECO (USD)");
    P.price = plabel(s, &ui_font_OpenSansBold45, P_WHITE, 6, 30, "a carregar");
    lv_obj_set_style_text_letter_space(P.price, 1, 0);

    plabel(s, &lv_font_montserrat_10, P_MUTED, 8, 88, "BLOCO ATUAL");
    P.block = plabel(s, &ui_font_OpenSansBold24, P_CYAN, 8, 100, "-");

    plabel(s, &lv_font_montserrat_10, P_MUTED, 150, 88, "TAXAS (sat/vB)");
    const char *names[3] = {"BAIXA", "MEDIA", "RAPIDA"};
    lv_color_t cols[3] = {P_GREEN, P_GOLD, P_RED};
    lv_obj_t **vals[3] = {&P.feeLo, &P.feeMid, &P.feeHi};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *k = card(s, 150 + i * 55, 102, 51, 36, cols[i]);
        plabel(k, &lv_font_montserrat_10, cols[i], 5, 2, names[i]);
        *vals[i] = plabel(k, &ui_font_OpenSansBold14, P_WHITE, 5, 16, "-");
    }
    P.mine = plabel(s, &ui_font_OpenSansBold13, P_SOFT, 8, 149, "");
    update_btc();
    return s;
}

// ---------------------------------------------------------------- bitcoin network
static void update_net()
{
    char buf[128];
    uint64_t nh = APIs_FETCHER.getNetHash();
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) nh);
    lv_label_set_text(P.netHash, nh ? buf : "-");
    uint64_t d = APIs_FETCHER.getNetDifficulty();
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) d);
    lv_label_set_text(P.diff, d ? buf : "-");

    uint32_t pct = APIs_FETCHER.getHalvingPercent();
    uint32_t left = APIs_FETCHER.getBlocksToHalving();
    lv_bar_set_value(P.halvBar, pct > 100 ? 1000 : (int) pct * 10, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long) pct);
    lv_label_set_text(P.halvPct, buf);
    if (left) {
        char n[32];
        thousands(n, sizeof(n), left);
        long days = (long) left * 10 / 60 / 24;   // one block every ~10 minutes
        int years = days / 365, months = (days % 365) / 30;
        if (years > 0) {
            snprintf(buf, sizeof(buf), "faltam %s blocos  -  ~%d ano%s e %d mes%s", n, years, years > 1 ? "s" : "", months, months == 1 ? "" : "es");
        } else {
            snprintf(buf, sizeof(buf), "faltam %s blocos  -  ~%ld dias", n, days);
        }
        lv_label_set_text(P.halvLeft, buf);
    }
    float hr = SYSTEM_MODULE.getCurrentHashrate();   // GH/s
    if (nh && hr > 0) {
        double share = (double) hr * 1e9 / ((double) nh * 1e18) * 100.0;
        snprintf(buf, sizeof(buf), "a tua parte da rede: %.7f %%", share);
        lv_label_set_text(P.share, buf);
    }
}

lv_obj_t *page_net_create()
{
    lv_obj_t *s = page("REDE BITCOIN", 3);
    P.net = s;
    lv_obj_t *a = card(s, 8, 22, 148, 54, lv_color_hex(0x9b5cff));
    plabel(a, &lv_font_montserrat_10, P_MUTED, 6, 3, "HASHRATE DA REDE");
    P.netHash = plabel(a, &ui_font_OpenSansBold24, P_WHITE, 6, 18, "-");
    plabel(a, &ui_font_OpenSansBold13, P_SOFT, 104, 30, "EH/s");
    lv_obj_t *b = card(s, 164, 22, 148, 54, lv_color_hex(0x9b5cff));
    plabel(b, &lv_font_montserrat_10, P_MUTED, 6, 3, "DIFICULDADE");
    P.diff = plabel(b, &ui_font_OpenSansBold24, P_WHITE, 6, 18, "-");
    plabel(b, &ui_font_OpenSansBold13, P_SOFT, 104, 30, "T");

    plabel(s, &lv_font_montserrat_10, P_MUTED, 8, 86, "PROXIMO HALVING");
    P.halvPct = plabel(s, &ui_font_OpenSansBold14, P_PINK, 262, 82, "0 %");
    P.halvBar = pbar(s, 8, 102, 304, 10);
    P.halvLeft = plabel(s, &ui_font_OpenSansBold13, P_WHITE, 8, 118, "");
    P.share = plabel(s, &ui_font_OpenSansBold13, P_MUTED, 8, 149, "");
    update_net();
    return s;
}

// one timer refreshes whichever of these pages is on screen
static void pages_cb(lv_timer_t *)
{
    lv_obj_t *act = lv_scr_act();
    if (act == P.pool) {
        update_pool();
    } else if (act == P.btc) {
        update_btc();
    } else if (act == P.net) {
        update_net();
    }
}

void pages_start()
{
    if (!P.timer) {
        P.timer = lv_timer_create(pages_cb, 1000, NULL);
    }
}

} // namespace hcos
