// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
#include "flash_disk.h"

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#define ERASE_SIZE FLASH_SECTOR_SIZE  // 4 KiB, smallest erasable unit
#define SECTORS_PER_ERASE (ERASE_SIZE / FLASH_DISK_SECTOR_SIZE)

// Power-loss journal for the drive's metadata (boot sector, FATs, root directory live in the
// first JOURNAL_BLOCKS erase blocks). Before such a block is rewritten in place, its new contents
// go to JOURNAL_DATA and a record {block, crc} is appended to the JOURNAL_HDR log; at boot the
// last record is replayed if its data is intact and the block does not already match. A torn
// erase therefore can no longer leave the volume without a boot sector or FAT.
#define JOURNAL_HDR (FLASH_DISK_OFFSET - 2 * ERASE_SIZE)
#define JOURNAL_DATA (FLASH_DISK_OFFSET - ERASE_SIZE)
#define JOURNAL_BLOCKS 16
#define JOURNAL_MAGIC 0x4c4e524au  // "JRNL"

struct journal_record {
    uint32_t magic, seq, block, crc;
};
#define RECORDS_PER_PAGE (FLASH_PAGE_SIZE / sizeof(struct journal_record))
#define RECORDS_PER_BLOCK (ERASE_SIZE / sizeof(struct journal_record))

static uint8_t cache[ERASE_SIZE] __attribute__((aligned(4)));
static int32_t cache_block = -1;  // erase-block index held in cache, -1 = none
static bool cache_dirty;
static uint32_t disk_sectors;
static uint32_t journal_next, journal_seq;
static bool journal_needs_erase;
static uint32_t host_write_ms, host_sectors, host_last_lba;
static struct flash_disk_stats stats;

static inline const uint8_t *flash_ptr(uint32_t offset) {
    return (const uint8_t *)(XIP_BASE + FLASH_DISK_OFFSET + offset);
}

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}

// Everything runs on core 0 and nothing executes from flash while interrupts are off, so a plain
// disable is enough around flash operations. Sources must be in RAM (XIP is off meanwhile).
static void erase_block(uint32_t flash_offset) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(flash_offset, ERASE_SIZE);
    restore_interrupts(irq);
}

static void program(uint32_t flash_offset, const uint8_t *src, size_t len) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(flash_offset, src, len);
    restore_interrupts(irq);
}

// Flash capacity from the chip's JEDEC ID (0x9F: manufacturer, type, log2(capacity)).
static uint32_t detect_flash_size(void) {
    uint8_t tx[4] = {0x9f, 0, 0, 0}, rx[4] = {0};
    uint32_t irq = save_and_disable_interrupts();
    flash_do_cmd(tx, rx, sizeof tx);
    restore_interrupts(irq);
    if (rx[3] >= 17 && rx[3] <= 28) return 1u << rx[3];  // 128 KB .. 256 MB
    return 0;
}

void flash_disk_init(void) {
    uint32_t size = PICO_FLASH_SIZE_BYTES, detected = detect_flash_size();
    if (detected && detected < size) {
        printf("flash_disk: chip is %lu KB, smaller than the configured %lu KB\n", (unsigned long)(detected / 1024),
               (unsigned long)(size / 1024));
        size = detected;
    }
    disk_sectors = size > FLASH_DISK_OFFSET ? (size - FLASH_DISK_OFFSET) / FLASH_DISK_SECTOR_SIZE : 0;

    // Find the end of the journal log; anything that is neither a record nor erased flash
    // (e.g. leftovers of a previous firmware) means the log block must be erased before use.
    const struct journal_record *log = (const struct journal_record *)(XIP_BASE + JOURNAL_HDR);
    uint32_t n = 0;
    while (n < RECORDS_PER_BLOCK && log[n].magic == JOURNAL_MAGIC) n++;
    if (n < RECORDS_PER_BLOCK) {
        const uint32_t *w = (const uint32_t *)&log[n];
        journal_needs_erase = (w[0] & w[1] & w[2] & w[3]) != 0xffffffffu;
    } else {
        journal_needs_erase = true;
    }
    journal_next = n;
    if (!n) return;

    const struct journal_record *last = &log[n - 1];
    journal_seq = last->seq;
    const uint8_t *saved = (const uint8_t *)(XIP_BASE + JOURNAL_DATA);
    if (last->block >= JOURNAL_BLOCKS || last->block * ERASE_SIZE >= (uint32_t)disk_sectors * FLASH_DISK_SECTOR_SIZE ||
        crc32(saved, ERASE_SIZE) != last->crc || memcmp(flash_ptr(last->block * ERASE_SIZE), saved, ERASE_SIZE) == 0)
        return;
    printf("flash_disk: completing an interrupted write of metadata block %lu\n", (unsigned long)last->block);
    memcpy(cache, saved, ERASE_SIZE);  // programming needs a RAM source
    erase_block(FLASH_DISK_OFFSET + last->block * ERASE_SIZE);
    program(FLASH_DISK_OFFSET + last->block * ERASE_SIZE, cache, ERASE_SIZE);
}

static void journal_commit(uint32_t block) {
    if (journal_needs_erase || journal_next >= RECORDS_PER_BLOCK) {
        erase_block(JOURNAL_HDR);
        journal_next = 0;
        journal_needs_erase = false;
    }
    erase_block(JOURNAL_DATA);
    program(JOURNAL_DATA, cache, ERASE_SIZE);
    // Records are 16 bytes but programming works in 256-byte pages: write the page with 0xFF
    // everywhere except the new record (programming 1s leaves existing bits alone).
    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page, 0xff, sizeof page);
    struct journal_record rec = {JOURNAL_MAGIC, ++journal_seq, block, crc32(cache, ERASE_SIZE)};
    memcpy(page + (journal_next % RECORDS_PER_PAGE) * sizeof rec, &rec, sizeof rec);
    program(JOURNAL_HDR + (journal_next / RECORDS_PER_PAGE) * FLASH_PAGE_SIZE, page, sizeof page);
    journal_next++;
    stats.journaled++;
}

uint32_t flash_disk_sector_count(void) {
    return disk_sectors;
}

// True if programming `want` over `have` only clears bits (NOR flash can do that without an erase).
static bool programmable_without_erase(const uint8_t *have, const uint8_t *want, size_t n) {
    for (size_t i = 0; i < n; i++)
        if ((have[i] & want[i]) != want[i]) return false;
    return true;
}

void flash_disk_flush(void) {
    if (!cache_dirty) return;
    cache_dirty = false;
    uint32_t offset = (uint32_t)cache_block * ERASE_SIZE;
    const uint8_t *old = flash_ptr(offset);
    if (memcmp(old, cache, ERASE_SIZE) == 0) return;
    uint64_t t0 = time_us_64();
    stats.flushes++;

    if ((uint32_t)cache_block < JOURNAL_BLOCKS) journal_commit((uint32_t)cache_block);

    // New file data usually lands on erased (0xFF) space: then only the changed pages are
    // programmed, which is several times faster than erase + full program.
    if (!programmable_without_erase(old, cache, ERASE_SIZE)) {
        erase_block(FLASH_DISK_OFFSET + offset);
        stats.erases++;
    }
    // Program only the pages that differ from what is in flash now (all 0xFF after an erase),
    // one call per run of consecutive pages: each call costs an XIP exit/re-entry.
    for (uint32_t page = 0; page < ERASE_SIZE;) {
        if (memcmp(old + page, cache + page, FLASH_PAGE_SIZE) == 0) {
            page += FLASH_PAGE_SIZE;
            continue;
        }
        uint32_t end = page + FLASH_PAGE_SIZE;
        while (end < ERASE_SIZE && memcmp(old + end, cache + end, FLASH_PAGE_SIZE) != 0) end += FLASH_PAGE_SIZE;
        program(FLASH_DISK_OFFSET + offset + page, cache + page, end - page);
        stats.pages += (end - page) / FLASH_PAGE_SIZE;
        page = end;
    }
    stats.busy_us += time_us_64() - t0;
}

const struct flash_disk_stats *flash_disk_get_stats(void) {
    return &stats;
}

static void cache_load(int32_t block) {
    if (block == cache_block) return;
    flash_disk_flush();
    memcpy(cache, flash_ptr((uint32_t)block * ERASE_SIZE), ERASE_SIZE);
    cache_block = block;
}

bool flash_disk_range_ok(uint32_t lba, uint32_t count) {
    return lba < disk_sectors && count <= disk_sectors - lba;  // no overflow for huge LBAs
}

void flash_disk_read(uint32_t lba, uint8_t *dst, uint32_t count) {
    for (; count; count--, lba++, dst += FLASH_DISK_SECTOR_SIZE) {
        int32_t block = (int32_t)(lba / SECTORS_PER_ERASE);
        uint32_t within = (lba % SECTORS_PER_ERASE) * FLASH_DISK_SECTOR_SIZE;
        if (block == cache_block)
            memcpy(dst, cache + within, FLASH_DISK_SECTOR_SIZE);
        else
            memcpy(dst, flash_ptr(lba * FLASH_DISK_SECTOR_SIZE), FLASH_DISK_SECTOR_SIZE);
    }
}

void flash_disk_write(uint32_t lba, const uint8_t *src, uint32_t count) {
    for (; count; count--, lba++, src += FLASH_DISK_SECTOR_SIZE) {
        cache_load((int32_t)(lba / SECTORS_PER_ERASE));
        memcpy(cache + (lba % SECTORS_PER_ERASE) * FLASH_DISK_SECTOR_SIZE, src, FLASH_DISK_SECTOR_SIZE);
        cache_dirty = true;
    }
}

void flash_disk_note_host_write(uint32_t lba, uint32_t count) {
    host_write_ms = to_ms_since_boot(get_absolute_time()) | 1;
    host_sectors += count;
    host_last_lba = lba;
}

uint32_t flash_disk_host_write_ms(void) {
    return host_write_ms;
}

uint32_t flash_disk_host_sectors(void) {
    return host_sectors;
}

uint32_t flash_disk_host_last_lba(void) {
    return host_last_lba;
}
