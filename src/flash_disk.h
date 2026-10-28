// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// Block device stored in the on-board QSPI flash, shared by USB mass storage
// (the host's view of the GIF drive) and FatFs (the firmware's view).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FLASH_DISK_SECTOR_SIZE 512u

// The drive lives after the firmware: [FLASH_DISK_OFFSET, end of flash). The 8 KB just below it
// hold the metadata journal, so the firmware image must end before FLASH_DISK_OFFSET - 8 KB.
#ifndef FLASH_DISK_OFFSET
#define FLASH_DISK_OFFSET (1024u * 1024u)
#endif

// Sizes the drive from the flash chip and finishes any metadata write a power loss interrupted.
// Call once at boot, before anything reads the drive.
void flash_disk_init(void);

uint32_t flash_disk_sector_count(void);
bool flash_disk_range_ok(uint32_t lba, uint32_t count);  // overflow-safe bounds check
void flash_disk_read(uint32_t lba, uint8_t *dst, uint32_t count);
void flash_disk_write(uint32_t lba, const uint8_t *src, uint32_t count);

// Writes go through a one-erase-block cache; flush commits it to flash.
void flash_disk_flush(void);

struct flash_disk_stats {
    uint32_t flushes, erases, pages;  // blocks committed, of which needed an erase; pages programmed
    uint32_t journaled;               // metadata blocks that went through the journal
    uint64_t busy_us;                 // time spent erasing/programming
};
const struct flash_disk_stats *flash_disk_get_stats(void);

// Host (USB) write activity, recorded by the MSC layer: time of the last write in ms since
// boot (0 if never), total sectors written, and the most recent LBA written.
void flash_disk_note_host_write(uint32_t lba, uint32_t count);
uint32_t flash_disk_host_write_ms(void);
uint32_t flash_disk_host_sectors(void);
uint32_t flash_disk_host_last_lba(void);
