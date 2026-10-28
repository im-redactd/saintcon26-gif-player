// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// TinyUSB mass-storage callbacks: expose the flash disk to the host as the GIF drive.
#include <string.h>

#include "flash_disk.h"
#include "gif_store.h"
#include "tusb.h"

static bool ejected;

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "SC26    ", 8);
    memcpy(product_id, "GIF Drive       ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    if (ejected || !flash_disk_sector_count()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);  // medium not present
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = flash_disk_sector_count();
    *block_size = FLASH_DISK_SECTOR_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    (void)power_condition;
    flash_disk_flush();
    if (load_eject) ejected = !start;
    return true;
}

// A new USB session (re-enumeration, host reboot) brings the drive back after an eject.
void tud_mount_cb(void) {
    ejected = false;
}

void tud_umount_cb(void) {
    flash_disk_flush();
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    flash_disk_flush();
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    // TinyUSB hands us whole sectors (buffer size is a multiple of 512, offset is 0)
    uint32_t count = bufsize / FLASH_DISK_SECTOR_SIZE;
    if (offset || bufsize % FLASH_DISK_SECTOR_SIZE || !flash_disk_range_ok(lba, count)) return -1;
    flash_disk_read(lba, buffer, count);
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    uint32_t count = bufsize / FLASH_DISK_SECTOR_SIZE;
    if (offset || bufsize % FLASH_DISK_SECTOR_SIZE || !flash_disk_range_ok(lba, count)) return -1;
    flash_disk_write(lba, buffer, count);
    flash_disk_note_host_write(lba, count);
    gif_store_host_wrote(lba, count);
    return (int32_t)bufsize;
}

// The host assumes write-through (we report no write cache), so each WRITE command is committed
// to flash before the next one: a reset or unplug right after a copy cannot drop acknowledged data.
void tud_msc_write10_complete_cb(uint8_t lun) {
    (void)lun;
    flash_disk_flush();
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)buffer;
    (void)bufsize;
    switch (scsi_cmd[0]) {
    case 0x35:  // SYNCHRONIZE CACHE (10)
        flash_disk_flush();
        return 0;
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        return 0;
    default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}
