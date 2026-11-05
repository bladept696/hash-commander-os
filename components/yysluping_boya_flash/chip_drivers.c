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

extern const spi_flash_chip_t esp_flash_chip_boya_32mb;

/*
 * CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST is enabled only by the
 * YYSLUPING 32 MB profile. The exact BOYA driver is tried first; the generic
 * fallback retains a diagnosable boot path if different flash is encountered.
 */
const spi_flash_chip_t *default_registered_chips[] = {
    &esp_flash_chip_boya_32mb,
    &esp_flash_chip_generic,
    NULL,
};

#endif
