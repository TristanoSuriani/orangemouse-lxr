/*
 * web_sdcard.c
 *
 * Replaces sd_diskio.c / bsp_driver_sd.c for the WebAssembly build: the SD
 * card is a disk image in memory. The browser loads it from and saves it to
 * IndexedDB; every write is reported through web_sd_written() so only the
 * changed sectors need storing.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "web_port.h"
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 512

uint8_t *web_sdImage;
uint32_t web_sdSectors;
uint8_t web_sdPresent;

// Called from JavaScript before booting. Returns the image buffer, zeroed,
// for the caller to fill with a saved card (or leave empty to get a freshly
// formatted one).
WEB_EXPORT uint8_t *web_sd_alloc(uint32_t sectors)
{
	free(web_sdImage);
	web_sdImage = calloc(sectors, SECTOR_SIZE);
	web_sdSectors = web_sdImage ? sectors : 0;
	web_sdPresent = web_sdImage != 0;
	return web_sdImage;
}

uint8_t BSP_SD_IsDetected(void)
{
	return web_sdPresent ? SD_PRESENT : SD_NOT_PRESENT;
}

static DSTATUS SD_initialize(BYTE lun)
{
	(void)lun;
	return web_sdPresent ? 0 : STA_NOINIT | STA_NODISK;
}

static DSTATUS SD_status(BYTE lun)
{
	(void)lun;
	return web_sdPresent ? 0 : STA_NOINIT | STA_NODISK;
}

static DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
	(void)lun;
	if (!web_sdPresent)
		return RES_NOTRDY;
	if (sector + count > web_sdSectors)
		return RES_PARERR;
	memcpy(buff, web_sdImage + (size_t)sector * SECTOR_SIZE, (size_t)count * SECTOR_SIZE);
	return RES_OK;
}

static DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
	(void)lun;
	if (!web_sdPresent)
		return RES_NOTRDY;
	if (sector + count > web_sdSectors)
		return RES_PARERR;
	memcpy(web_sdImage + (size_t)sector * SECTOR_SIZE, buff, (size_t)count * SECTOR_SIZE);
	web_sd_written(sector, count);
	return RES_OK;
}

static DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
	(void)lun;
	if (!web_sdPresent)
		return RES_NOTRDY;

	switch (cmd)
	{
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(DWORD *)buff = web_sdSectors;
		return RES_OK;
	case GET_SECTOR_SIZE:
		*(WORD *)buff = SECTOR_SIZE;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 1;
		return RES_OK;
	default:
		return RES_PARERR;
	}
}

const Diskio_drvTypeDef SD_Driver =
{
	SD_initialize,
	SD_status,
	SD_read,
#if _USE_WRITE == 1
	SD_write,
#endif
#if _USE_IOCTL == 1
	SD_ioctl,
#endif
};
