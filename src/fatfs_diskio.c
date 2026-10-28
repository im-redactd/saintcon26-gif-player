// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// FatFs media access layer: the single volume is the flash disk.
#include "ff.h"
#include "diskio.h"
#include "flash_disk.h"

DSTATUS disk_status(BYTE pdrv) {
    return pdrv || !flash_disk_sector_count() ? STA_NOINIT : 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !flash_disk_range_ok(sector, count)) return RES_PARERR;
    flash_disk_read(sector, buff, count);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !flash_disk_range_ok(sector, count)) return RES_PARERR;
    flash_disk_write(sector, buff, count);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC:
        flash_disk_flush();
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = flash_disk_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = FLASH_DISK_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 4096 / FLASH_DISK_SECTOR_SIZE;  // erase block, in sectors
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
