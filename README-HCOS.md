# Hash Commander OS

Firmware for the **NerdQAxe++**, based on the official [NerdQAxe++ firmware](https://github.com/shufps/ESP-Miner-NerdQAxePlus) by shufps and contributors, with additions for the [Hash Commander](https://github.com/bladept696/centro-de-comando-v3) app.

It is licensed under **GPL-3.0**, like the firmware it is based on. All credit for the base firmware goes to its authors; the additions live in `main/hcos/` and a few one-line hooks.

> ⚠️ This is **not an official firmware**. Installing it may affect the manufacturer's warranty. In rare cases a reinstall over USB may be needed.

## What it adds (v0.1)

| | |
|---|---|
| **Live data** | WebSocket `ws://<miner>/api/hc/live`: the miner state once a second (`{"t":"state",...}`) and every share as it happens (`{"t":"share",...}`). |
| **Share log** | `GET /api/hc/shares?since=<seq>&limit=<n>`: the last 10 000 shares sent to the pool, with time, difficulty, pool and result (1 accepted, 2 rejected, 3 no answer). Kept in PSRAM, cleared on reboot. |
| **Boot guard** | After an update, if the new firmware reboots 3 times without starting to mine, the previous firmware is booted again. The stock bootloader has no rollback, so this lives in the firmware. |
| **Go back** | `POST /api/hc/rollback` boots the firmware that was installed before (asks for the OTP code when OTP is on). |
| **Info** | `GET /api/hc/info`: Hash Commander OS version, base firmware, boot guard and share log state. |

Everything else (AxeOS web page, `/api/system/*`, OTA, pools, settings) works exactly like the base firmware, so other apps keep working.

## Install

The Hash Commander app installs it from its **Hash Commander OS** tab over the network (normal OTA update) after checking the board model. By hand: AxeOS → Update → firmware file `hcos-NerdQAxe++-<version>.bin`.

Only for the **NerdQAxe++**. Do not install it on other boards.

## Build

Every push to the `hcos` branch is built by GitHub Actions (`.github/workflows/hcos-build.yml`) with the same builder image as the base firmware (`shufps/esp-idf-builder:5.3.6`, `BOARD=NERDQAXEPLUS2`).
