// Copyright (C) 2026 bladept696. Part of Hash Commander OS, licensed under the GNU GPL v3 (see LICENSE).
// Hash Commander OS — additions on top of the NerdQAxe++ firmware (GPL-3.0, like the base firmware).
//
//  * share log:   every share sent to the pool is kept in PSRAM with its result (GET /api/hc/shares)
//  * live data:   a WebSocket (/api/hc/live) pushes the miner state every second and each share as it happens
//  * boot guard:  after an update, if the new firmware keeps rebooting before it mines, the previous
//                 firmware is booted again (the stock bootloader has no app rollback)
//  * info:        GET /api/hc/info tells apps that this is Hash Commander OS
#pragma once

#include <stdint.h>
#include "esp_http_server.h"

#define HCOS_VERSION "0.1.0"

namespace hcos {

// call right after nvs_flash_init(), before anything that could crash
void boot_guard();

// call after the http server started
void register_http(httpd_handle_t server);

// start the live push task (needs the stratum manager and the board)
void start();

// called by the OTA handler just before switching the boot partition
void remember_running_partition();

// share hooks: found (asic result task, right before the submit), sent (stratum manager, pool connected), result
void on_share_found(int pool, double diff, uint32_t pool_diff, int asic_nr);
void on_share_sent(int pool);
void on_share_result(int pool, bool accepted);

} // namespace hcos
