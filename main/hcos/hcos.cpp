// Copyright (C) 2026 bladept696. Part of Hash Commander OS, licensed under the GNU GPL v3 (see LICENSE).
// Hash Commander OS — see hcos.h
#include "hcos.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "ArduinoJson.h"
#include "boards/board.h"
#include "global_state.h"
#include "http_server/http_cors.h"
#include "http_server/http_utils.h"
#include "psram_allocator.h"
#include "utils.h"

static const char *TAG = "hcos";

namespace hcos {

// newer stable release found on GitHub (see check_update_once)
static char s_update[16] = "";

// ---------------------------------------------------------------- boot guard

static const char *NVS_NS = "hcos";
static const uint8_t MAX_FAILED_BOOTS = 3;     // boots without reaching "mining" before going back
static const int64_t HEALTHY_AFTER_US = 180LL * 1000000LL;

static char s_prev[17] = "";                    // partition we go back to
static bool s_ok = false;                       // this image already mined once in this slot
static uint8_t s_fails = 0;

static bool valid_app(const esp_partition_t *p)
{
    esp_app_desc_t d;
    return p && esp_ota_get_partition_description(p, &d) == ESP_OK;
}

// the firmware that was running before this one
static const esp_partition_t *fallback_for(const esp_partition_t *running)
{
    const esp_partition_t *other = esp_ota_get_next_update_partition(running);
    if (other && other != running && valid_app(other)) {
        return other;
    }
    const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory && factory != running && valid_app(factory)) {
        return factory;
    }
    return nullptr;
}

static void save_state(nvs_handle_t h)
{
    nvs_set_str(h, "prev", s_prev);
    nvs_set_u8(h, "ok", s_ok ? 1 : 0);
    nvs_set_u8(h, "fails", s_fails);
    nvs_commit(h);
}

void boot_guard()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    nvs_handle_t h;
    if (!running || nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "boot guard not available");
        return;
    }

    char part[17] = "";
    size_t len = sizeof(part);
    nvs_get_str(h, "part", part, &len);

    if (strcmp(part, running->label) != 0) {
        // first boot of a new image in this slot
        char next_prev[17] = "";
        len = sizeof(next_prev);
        if (nvs_get_str(h, "next_prev", next_prev, &len) == ESP_OK && strcmp(next_prev, running->label) != 0) {
            strlcpy(s_prev, next_prev, sizeof(s_prev));
        } else {
            const esp_partition_t *fb = fallback_for(running);
            strlcpy(s_prev, fb ? fb->label : "", sizeof(s_prev));
        }
        nvs_erase_key(h, "next_prev");
        nvs_set_str(h, "part", running->label);
        s_ok = false;
        s_fails = 0;
        ESP_LOGI(TAG, "new firmware in %s, going back to '%s' if it never mines", running->label, s_prev);
    } else {
        len = sizeof(s_prev);
        nvs_get_str(h, "prev", s_prev, &len);
        uint8_t v = 0;
        nvs_get_u8(h, "ok", &v);
        s_ok = v != 0;
        nvs_get_u8(h, "fails", &s_fails);
    }

    if (!s_ok) {
        s_fails++;
        ESP_LOGW(TAG, "boot %d of %d without mining yet", s_fails, MAX_FAILED_BOOTS);
        if (s_fails > MAX_FAILED_BOOTS && s_prev[0]) {
            const esp_partition_t *target =
                esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, s_prev);
            if (valid_app(target) && esp_ota_set_boot_partition(target) == ESP_OK) {
                ESP_LOGE(TAG, "firmware keeps failing, going back to %s", s_prev);
                s_fails = 0;
                save_state(h);
                nvs_close(h);
                esp_restart();
            }
            ESP_LOGE(TAG, "cannot go back to %s", s_prev);
        }
    }
    save_state(h);
    nvs_close(h);
}

static void mark_healthy()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    s_ok = true;
    s_fails = 0;
    save_state(h);
    nvs_close(h);
    ESP_LOGI(TAG, "firmware is mining, boot guard satisfied");
}

void remember_running_partition()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    nvs_handle_t h;
    if (running && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "next_prev", running->label);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ---------------------------------------------------------------- share log

enum : uint8_t { ST_PENDING = 0, ST_ACCEPTED = 1, ST_REJECTED = 2, ST_NO_ANSWER = 3 };

struct Rec {
    uint32_t seq;
    uint32_t ts;      // unix seconds, 0 if the clock was not synced
    uint32_t up;      // seconds since boot
    uint32_t pdiff;   // pool difficulty
    double diff;
    uint8_t pool, asic, st, _pad;
};

static const uint32_t CAP = 10000;
static const uint32_t PENDING_CAP = 64;
static const uint32_t NO_ANSWER_S = 60;

static Rec *s_ring = nullptr;
static uint32_t s_seq = 0;                       // last seq used (first share = 1)
static uint32_t s_pending[2][PENDING_CAP];
static uint32_t s_pend_head[2], s_pend_len[2];
static SemaphoreHandle_t s_lock = nullptr;
static QueueHandle_t s_events = nullptr;         // seq of shares to push to live clients

static double s_found_diff = 0;                   // set by the asic result task right before the submit
static uint32_t s_found_pdiff = 0;
static int s_found_asic = 0;

static uint32_t uptime_s()
{
    return (uint32_t) ((esp_timer_get_time() - SYSTEM_MODULE.getStartTime()) / 1000000);
}

static Rec *rec_for(uint32_t seq)
{
    if (!s_ring || seq == 0 || seq > s_seq || s_seq - seq >= CAP) {
        return nullptr;
    }
    Rec *r = &s_ring[seq % CAP];
    return r->seq == seq ? r : nullptr;
}

static void init_log()
{
    if (s_ring) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    s_events = xQueueCreate(32, sizeof(uint32_t));
    s_ring = (Rec *) heap_caps_calloc(CAP, sizeof(Rec), MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        ESP_LOGE(TAG, "no PSRAM for the share log");
    }
}

void on_share_found(int pool, double diff, uint32_t pool_diff, int asic_nr)
{
    s_found_diff = diff;
    s_found_pdiff = pool_diff;
    s_found_asic = asic_nr;
    (void) pool;
}

// called by the stratum manager once the share really goes out to a connected pool
void on_share_sent(int pool)
{
    if (!s_ring || pool < 0 || pool > 1) {
        return;
    }
    uint32_t seq;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    seq = ++s_seq;
    Rec *r = &s_ring[seq % CAP];
    r->seq = seq;
    r->ts = is_time_synced() ? now() : 0;
    r->up = uptime_s();
    r->pdiff = s_found_pdiff;
    r->diff = s_found_diff;
    r->pool = (uint8_t) pool;
    r->asic = (uint8_t) s_found_asic;
    r->st = ST_PENDING;
    if (s_pend_len[pool] == PENDING_CAP) {     // pool never answers: forget the oldest
        s_pend_head[pool] = (s_pend_head[pool] + 1) % PENDING_CAP;
        s_pend_len[pool]--;
    }
    s_pending[pool][(s_pend_head[pool] + s_pend_len[pool]) % PENDING_CAP] = seq;
    s_pend_len[pool]++;
    xSemaphoreGive(s_lock);
    xQueueSend(s_events, &seq, 0);
}

void on_share_result(int pool, bool accepted)
{
    if (!s_ring || pool < 0 || pool > 1) {
        return;
    }
    uint32_t seq = 0, now_up = uptime_s();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // pools answer in order; shares sent before a reconnect never get an answer
    while (s_pend_len[pool]) {
        uint32_t cand = s_pending[pool][s_pend_head[pool]];
        s_pend_head[pool] = (s_pend_head[pool] + 1) % PENDING_CAP;
        s_pend_len[pool]--;
        Rec *r = rec_for(cand);
        if (!r) {
            continue;
        }
        if (now_up - r->up > NO_ANSWER_S && s_pend_len[pool]) {
            r->st = ST_NO_ANSWER;
            continue;
        }
        r->st = accepted ? ST_ACCEPTED : ST_REJECTED;
        seq = cand;
        break;
    }
    xSemaphoreGive(s_lock);
    if (seq) {
        xQueueSend(s_events, &seq, 0);
    }
}

static uint8_t status_of(const Rec &r, uint32_t now_up)
{
    return (r.st == ST_PENDING && now_up - r.up > NO_ANSWER_S) ? (uint8_t) ST_NO_ANSWER : r.st;
}

// ---------------------------------------------------------------- live websocket

static const int MAX_CLIENTS = 3;
static httpd_handle_t s_server = nullptr;
static int s_clients[MAX_CLIENTS] = {-1, -1, -1};

static void add_client(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] == fd) {
            return;
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] < 0) {
            s_clients[i] = fd;
            return;
        }
    }
    // full: the oldest client makes room
    httpd_sess_trigger_close(s_server, s_clients[0]);
    s_clients[0] = fd;
}

static void drop_client(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] == fd) {
            s_clients[i] = -1;
        }
    }
}

static int count_clients()
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        n += s_clients[i] >= 0;
    }
    return n;
}

static void broadcast(const char *msg, size_t len)
{
    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = (uint8_t *) msg;
    pkt.len = len;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = s_clients[i];
        if (fd < 0) {
            continue;
        }
        if (httpd_ws_get_fd_info(s_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET ||
            httpd_ws_send_frame_async(s_server, fd, &pkt) != ESP_OK) {
            s_clients[i] = -1;
        }
    }
}

static esp_err_t live_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        if (is_network_allowed(req) != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        }
        add_client(httpd_req_to_sockfd(req));
        ESP_LOGI(TAG, "live client connected (%d)", count_clients());
        return ESP_OK;
    }
    httpd_ws_frame_t pkt = {};
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    if (pkt.type == HTTPD_WS_TYPE_CLOSE) {
        drop_client(httpd_req_to_sockfd(req));
    }
    return ESP_OK;
}

static int share_json(char *buf, size_t size, const Rec &r, uint32_t now_up)
{
    return snprintf(buf, size, "{\"t\":\"share\",\"seq\":%lu,\"ts\":%lu,\"up\":%lu,\"diff\":%.0f,\"poolDiff\":%lu,\"pool\":%u,\"asic\":%u,\"st\":%u}",
                    (unsigned long) r.seq, (unsigned long) r.ts, (unsigned long) r.up, r.diff, (unsigned long) r.pdiff,
                    r.pool, r.asic, status_of(r, now_up));
}

static void live_task(void *)
{
    char buf[512];
    int64_t next = esp_timer_get_time();
    while (true) {
        uint32_t seq;
        // shares go out as soon as they happen, the state once a second
        while (xQueueReceive(s_events, &seq, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!count_clients()) {
                continue;
            }
            Rec copy;
            bool have = false;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            Rec *r = rec_for(seq);
            if (r) {
                copy = *r;
                have = true;
            }
            xSemaphoreGive(s_lock);
            if (have) {
                int n = share_json(buf, sizeof(buf), copy, uptime_s());
                broadcast(buf, n);
            }
        }
        if (esp_timer_get_time() < next) {
            continue;
        }
        next += 1000000;

        Board *board = SYSTEM_MODULE.getBoard();
        bool shutdown = POWER_MANAGEMENT_MODULE.isShutdown();
        float hr = shutdown ? 0 : SYSTEM_MODULE.getCurrentHashrate();
        uint64_t accepted = STRATUM_MANAGER ? STRATUM_MANAGER->getSharesAccepted() : 0;

        if (!s_ok && esp_timer_get_time() - SYSTEM_MODULE.getStartTime() > HEALTHY_AFTER_US && (hr > 0 || accepted > 0)) {
            mark_healthy();
        }
        if (!count_clients() || !board) {
            continue;
        }
        History *history = SYSTEM_MODULE.getHistory();
        int n = snprintf(buf, sizeof(buf),
                         "{\"t\":\"state\",\"ts\":%lu,\"up\":%lu,\"hashRate\":%.2f,\"hashRate1m\":%.2f,\"hashRate10m\":%.2f,"
                         "\"temp\":%.1f,\"vrTemp\":%.1f,\"power\":%.2f,\"fanRpm\":%u,\"frequency\":%d,"
                         "\"accepted\":%llu,\"rejected\":%llu,\"bestDiff\":%llu,\"bestSessionDiff\":%llu,\"lastSeq\":%lu,\"shutdown\":%s}",
                         (unsigned long) (is_time_synced() ? now() : 0), (unsigned long) uptime_s(), hr,
                         shutdown ? 0.0 : history->getCurrentHashrate1m(), shutdown ? 0.0 : history->getCurrentHashrate10m(),
                         POWER_MANAGEMENT_MODULE.getChipTempMax(), POWER_MANAGEMENT_MODULE.getVRTemp(), POWER_MANAGEMENT_MODULE.getPower(),
                         (unsigned) POWER_MANAGEMENT_MODULE.getFanRPM(0), (int) board->getAsicFrequency(),
                         (unsigned long long) accepted,
                         (unsigned long long) (STRATUM_MANAGER ? STRATUM_MANAGER->getSharesRejected() : 0),
                         (unsigned long long) (STRATUM_MANAGER ? STRATUM_MANAGER->getBestDiff() : 0),
                         (unsigned long long) (STRATUM_MANAGER ? STRATUM_MANAGER->getBestSessionDiff() : 0),
                         (unsigned long) s_seq, shutdown ? "true" : "false");
        if (n > 0 && n < (int) sizeof(buf)) {
            broadcast(buf, n);
        }
    }
}

uint32_t share_last()
{
    return s_seq;
}

bool share_get(uint32_t seq, double *diff, uint8_t *st)
{
    if (!s_ring) {
        return false;
    }
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    Rec *r = rec_for(seq);
    if (r) {
        *diff = r->diff;
        *st = status_of(*r, uptime_s());
        ok = true;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

int live_clients()
{
    return count_clients();
}

// ---------------------------------------------------------------- http

static esp_err_t GET_info(httpd_req_t *req)
{
    ConGuard g(s_server, req);
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }
    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);
    const esp_app_desc_t *app = esp_app_get_description();
    Board *board = SYSTEM_MODULE.getBoard();
    doc["hcos"] = HCOS_VERSION;
    doc["firmware"] = app->version;
    doc["board"] = board ? board->getDeviceModel() : "";
    const esp_partition_t *running = esp_ota_get_running_partition();
    JsonObject guard = doc["bootGuard"].to<JsonObject>();
    guard["running"] = running ? running->label : "";
    guard["fallback"] = s_prev;
    guard["mining"] = s_ok;
    guard["failedBoots"] = s_fails;
    JsonObject log = doc["shareLog"].to<JsonObject>();
    log["capacity"] = CAP;
    log["total"] = s_seq;
    log["kept"] = s_seq < CAP ? s_seq : CAP;
    doc["liveClients"] = count_clients();
    doc["update"] = s_update;
#ifdef HCOS_BIG
    doc["display"] = "480x320";     // the app installs the "hcosbig" firmware on this machine
#else
    doc["display"] = "320x170";
#endif
    httpd_resp_set_type(req, "application/json");
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return sendJsonResponse(req, doc);
}

// GET /api/hc/shares?since=<seq>&limit=<n>  ->  {"last":seq,"up":s,"items":[[seq,ts,up,diff,poolDiff,pool,asic,st],...]}
static esp_err_t GET_shares(httpd_req_t *req)
{
    ConGuard g(s_server, req);
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }
    uint32_t since = 0, limit = 200;
    char q[96], v[24];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK) {
            since = strtoul(v, NULL, 10);
        }
        if (httpd_query_key_value(q, "limit", v, sizeof(v)) == ESP_OK) {
            limit = strtoul(v, NULL, 10);
        }
    }
    if (limit == 0 || limit > 500) {
        limit = 500;
    }
    httpd_resp_set_type(req, "application/json");
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint32_t last = s_seq, now_up = uptime_s();
    uint32_t first = since + 1;
    if (last >= CAP && first <= last - CAP) {
        first = last - CAP + 1;
    }
    if (last >= limit && first + limit <= last) {
        first = last - limit + 1;      // the newest ones when there are more than the limit
    }
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "{\"last\":%lu,\"up\":%lu,\"items\":[", (unsigned long) last, (unsigned long) now_up);
    httpd_resp_send_chunk(req, buf, n);
    bool comma = false;
    for (uint32_t seq = first; seq && seq <= last; seq++) {
        Rec copy;
        bool have = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        Rec *r = rec_for(seq);
        if (r) {
            copy = *r;
            have = true;
        }
        xSemaphoreGive(s_lock);
        if (!have) {
            continue;
        }
        n = snprintf(buf, sizeof(buf), "%s[%lu,%lu,%lu,%.0f,%lu,%u,%u,%u]", comma ? "," : "", (unsigned long) copy.seq,
                     (unsigned long) copy.ts, (unsigned long) copy.up, copy.diff, (unsigned long) copy.pdiff, copy.pool,
                     copy.asic, status_of(copy, now_up));
        httpd_resp_send_chunk(req, buf, n);
        comma = true;
    }
    httpd_resp_send_chunk(req, "]}", 2);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// POST /api/hc/rollback  ->  boots the firmware that was installed before Hash Commander OS
static esp_err_t POST_rollback(httpd_req_t *req)
{
    ConGuard g(s_server, req);
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }
    if (validateOTP(req) != ESP_OK) {
        return ESP_FAIL;
    }
    const esp_partition_t *target =
        s_prev[0] ? esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, s_prev) : nullptr;
    if (!valid_app(target) || esp_ota_set_boot_partition(target) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no previous firmware to go back to");
    }
    httpd_resp_sendstr(req, "going back to the previous firmware, rebooting now\n");
    ESP_LOGW(TAG, "going back to %s on request", s_prev);
    vTaskDelay(pdMS_TO_TICKS(1000));
    POWER_MANAGEMENT_MODULE.restart();
    return ESP_OK;
}

// ---------------------------------------------------------------- new version check

const char *update_available() { return s_update; }

// "0.4.2" -> 4002 style number for comparing; -1 when it is not x.y.z
static long ver_num(const char *v)
{
    int a = 0, b = 0, c = 0;
    if (!v || sscanf(v, "%d.%d.%d", &a, &b, &c) != 3) return -1;
    return (long) a * 1000000L + (long) b * 1000L + c;
}

static void check_update_once()
{
    const int CAPB = 48 * 1024;
    char *body = (char *) heap_caps_malloc(CAPB, MALLOC_CAP_SPIRAM);
    if (!body) return;
    esp_http_client_config_t cfg = {};
    cfg.url = "https://api.github.com/repos/bladept696/hash-commander-os/releases/latest";
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 2048;
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (cl) {
        esp_http_client_set_header(cl, "User-Agent", "HashCommanderOS/" HCOS_VERSION);
        esp_http_client_set_header(cl, "Accept", "application/vnd.github+json");
        if (esp_http_client_open(cl, 0) == ESP_OK) {
            esp_http_client_fetch_headers(cl);
            int status = esp_http_client_get_status_code(cl);
            int n = 0, r;
            while (n < CAPB - 1 && (r = esp_http_client_read(cl, body + n, CAPB - 1 - n)) > 0) n += r;
            body[n] = 0;
            // the tag is near the top of the JSON: "tag_name":"hcos-v0.4.2"
            const char *t = status == 200 ? strstr(body, "\"tag_name\"") : nullptr;
            if (t && (t = strstr(t, "hcos-v"))) {
                char v[16] = "";
                t += 6;
                int i = 0;
                while (i < 15 && ((t[i] >= '0' && t[i] <= '9') || t[i] == '.')) { v[i] = t[i]; i++; }
                v[i] = 0;
                long nv = ver_num(v), cur = ver_num(HCOS_VERSION);
                if (nv > 0 && cur > 0 && nv > cur) {
                    strncpy(s_update, v, sizeof(s_update) - 1);
                    ESP_LOGI(TAG, "new HC OS on GitHub: %s (running %s)", v, HCOS_VERSION);
                } else {
                    s_update[0] = 0;
                }
            } else {
                ESP_LOGW(TAG, "update check: HTTP %d", status);
            }
        }
        esp_http_client_close(cl);
        esp_http_client_cleanup(cl);
    }
    heap_caps_free(body);
}

static void update_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(3 * 60 * 1000));   // let wifi, the pool and the boot guard settle first
    for (;;) {
        check_update_once();
        vTaskDelay(pdMS_TO_TICKS(6 * 60 * 60 * 1000));
    }
}

void register_http(httpd_handle_t server)
{
    s_server = server;
    init_log();

    httpd_uri_t info = {.uri = "/api/hc/info", .method = HTTP_GET, .handler = GET_info, .user_ctx = NULL};
    httpd_register_uri_handler(server, &info);
    httpd_uri_t shares = {.uri = "/api/hc/shares", .method = HTTP_GET, .handler = GET_shares, .user_ctx = NULL};
    httpd_register_uri_handler(server, &shares);
    httpd_uri_t rollback = {.uri = "/api/hc/rollback", .method = HTTP_POST, .handler = POST_rollback, .user_ctx = NULL};
    httpd_register_uri_handler(server, &rollback);
    httpd_uri_t live = {.uri = "/api/hc/live", .method = HTTP_GET, .handler = live_handler, .user_ctx = NULL, .is_websocket = true};
    httpd_register_uri_handler(server, &live);
}

void start()
{
    init_log();
    // internal RAM stack: this task writes NVS (mark_healthy) and the flash write turns the PSRAM cache off,
    // so a PSRAM stack crashes it (the same reason the OTA task is not created on PSRAM)
    xTaskCreate(live_task, "hcos live", 6144, NULL, 2, NULL);
    xTaskCreate(update_task, "hcos update", 8192, NULL, 1, NULL);
}

} // namespace hcos
