/*
 * SPDX-FileCopyrightText: 2026 NerdQaxe++ YYSLUPING contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>

#include "sdkconfig.h"

#if CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST

#include "spi_flash_chip_driver.h"
#include "spi_flash_chip_generic.h"

#define BOYA_BY25Q256FS_JEDEC_ID 0x684019U

/*
 * BY25Q256FS defines the same dedicated 4-byte read/program/erase opcodes
 * used by ESP-IDF's Winbond/GigaDevice path. Reuse that tested transaction
 * implementation while keeping probe and capability reporting exact.
 */
extern esp_err_t spi_flash_chip_winbond_read(
    esp_flash_t *chip, void *buffer, uint32_t address, uint32_t length);
extern esp_err_t spi_flash_chip_winbond_page_program(
    esp_flash_t *chip, const void *buffer, uint32_t address, uint32_t length);
extern esp_err_t spi_flash_chip_winbond_erase_sector(
    esp_flash_t *chip, uint32_t start_address);
extern esp_err_t spi_flash_chip_winbond_erase_block(
    esp_flash_t *chip, uint32_t start_address);

static esp_err_t boya_32mb_probe(esp_flash_t *chip, uint32_t flash_id)
{
    (void)chip;
    return flash_id == BOYA_BY25Q256FS_JEDEC_ID ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static spi_flash_caps_t boya_32mb_get_caps(esp_flash_t *chip)
{
    (void)chip;
    return SPI_FLASH_CHIP_CAP_32MB_SUPPORT | SPI_FLASH_CHIP_CAP_UNIQUE_ID;
}

static const char chip_name[] = "boya-by25q256fs";

const spi_flash_chip_t esp_flash_chip_boya_32mb = {
    .name = chip_name,
    .timeout = &spi_flash_chip_generic_timeout,
    .probe = boya_32mb_probe,
    .reset = spi_flash_chip_generic_reset,
    .detect_size = spi_flash_chip_generic_detect_size,
    .erase_chip = spi_flash_chip_generic_erase_chip,
    .erase_sector = spi_flash_chip_winbond_erase_sector,
    .erase_block = spi_flash_chip_winbond_erase_block,
    .sector_size = 4 * 1024,
    .block_erase_size = 64 * 1024,

    .get_chip_write_protect = spi_flash_chip_generic_get_write_protect,
    .set_chip_write_protect = spi_flash_chip_generic_set_write_protect,

    .num_protectable_regions = 0,
    .protectable_regions = NULL,
    .get_protected_regions = NULL,
    .set_protected_regions = NULL,

    .read = spi_flash_chip_winbond_read,
    .write = spi_flash_chip_generic_write,
    .program_page = spi_flash_chip_winbond_page_program,
    .page_size = 256,
    .write_encrypted = spi_flash_chip_generic_write_encrypted,

    .wait_idle = spi_flash_chip_generic_wait_idle,
    .set_io_mode = spi_flash_chip_generic_set_io_mode,
    .get_io_mode = spi_flash_chip_generic_get_io_mode,

    .read_reg = spi_flash_chip_generic_read_reg,
    .yield = spi_flash_chip_generic_yield,
    .sus_setup = spi_flash_chip_generic_suspend_cmd_conf,
    .read_unique_id = spi_flash_chip_generic_read_unique_id,
    .get_chip_caps = boya_32mb_get_caps,
    .config_host_io_mode = spi_flash_chip_generic_config_host_io_mode,
};

#endif
