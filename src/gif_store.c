// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
#include "gif_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "flash_disk.h"

extern const unsigned char builtin_gif_start[], builtin_gif_end[];

static FATFS fs;
static char names[GIF_STORE_MAX][GIF_STORE_NAME_MAX];
static int count;
static uint8_t mounted_bpb[62];  // boot-sector parameters the volume was mounted with
static bool remounted;

static const char readme[] =
    "SC26 badge GIF drive\r\n"
    "====================\r\n"
    "\r\n"
    "Copy animated .gif files here. After the copy finishes the badge rescans\r\n"
    "the drive and jumps to the newest file.\r\n"
    "\r\n"
    "Best results: 360x360 (the round panel) or 180x180 / 120x120, which are\r\n"
    "scaled up 2x / 3x. Anything up to 1024 px wide plays: square GIFs fill the\r\n"
    "screen, wide or tall ones are scaled to fit inside the circle.\r\n"
    "\r\n"
    "Buttons: left/right = previous/next GIF, up/down = screen brightness,\r\n"
    "centre = pause, A = LED mode (ambient/rain/off), B = LED brightness.\r\n"
    "Serial console on the badge's COM port: type 'h' for help.\r\n"
    "Delete files from this drive normally; eject before unplugging.\r\n";

const unsigned char *gif_store_builtin(unsigned *len) {
    *len = (unsigned)(builtin_gif_end - builtin_gif_start);
    return builtin_gif_start;
}

static bool write_file(const char *path, const void *data, UINT len) {
    FIL f;
    UINT written = 0;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    FRESULT r = f_write(&f, data, len, &written);
    f_close(&f);
    return r == FR_OK && written == len;
}

static bool format_and_seed(void) {
    static BYTE work[FF_MAX_SS];
    // FAT16, no partition table (a "super-floppy", like a USB stick), two FAT copies for compatibility
    const MKFS_PARM opt = {.fmt = FM_FAT | FM_SFD, .n_fat = 2, .align = 0, .n_root = 0, .au_size = 0};
    printf("gif_store: formatting %lu KB flash drive\n", (unsigned long)(flash_disk_sector_count() / 2));
    if (f_mkfs("", &opt, work, sizeof work) != FR_OK) return false;
    if (f_mount(&fs, "", 1) != FR_OK) return false;
    f_setlabel("SC26GIFS");
    unsigned len;
    const unsigned char *gif = gif_store_builtin(&len);
    write_file("matrix_rain.gif", gif, len);
    write_file("README.TXT", readme, sizeof readme - 1);
    flash_disk_flush();
    return true;
}

static void read_bpb(uint8_t out[62]) {
    static uint8_t sector[FLASH_DISK_SECTOR_SIZE];
    flash_disk_read(0, sector, 1);
    memcpy(out, sector, 62);
}

bool gif_store_mount(void) {
    FRESULT r = f_mount(&fs, "", 1);
    if (r == FR_NO_FILESYSTEM && !format_and_seed()) return false;
    else if (r != FR_OK && r != FR_NO_FILESYSTEM) return false;
    read_bpb(mounted_bpb);
    gif_store_rescan();
    remounted = false;
    return true;
}

void gif_store_host_wrote(uint32_t lba, uint32_t n) {
    // FatFs keeps one sector (FAT or directory) cached in fs.win; never serve it stale.
    if (fs.winsect >= lba && fs.winsect < lba + n) fs.winsect = (LBA_t)0 - 1;
}

bool gif_store_take_remounted(void) {
    bool r = remounted;
    remounted = false;
    return r;
}

static bool is_gif(const FILINFO *fi) {
    if (fi->fattrib & (AM_DIR | AM_HID | AM_SYS)) return false;
    if (fi->fname[0] == '.') return false;  // macOS "._name" resource forks and similar
    size_t n = strlen(fi->fname);
    return n > 4 && strcasecmp(fi->fname + n - 4, ".gif") == 0;
}

static int by_name(const void *a, const void *b) {
    return strcasecmp((const char *)a, (const char *)b);
}

int gif_store_rescan(void) {
    // The host may have rewritten FAT and directory sectors behind FatFs's back: drop the one
    // sector FatFs caches (rather than remounting, which would invalidate the open GIF).
    fs.winsect = (LBA_t)0 - 1;
    count = 0;

    // If the host reformatted the drive, FatFs's cached geometry is wrong: remount (this
    // invalidates any open file, which the caller learns from gif_store_take_remounted()).
    uint8_t bpb[sizeof mounted_bpb];
    read_bpb(bpb);
    if (memcmp(bpb, mounted_bpb, sizeof bpb) != 0) {
        printf("gif_store: the drive was reformatted, remounting\n");
        f_mount(NULL, "", 0);
        f_mount(&fs, "", 1);
        memcpy(mounted_bpb, bpb, sizeof bpb);
        remounted = true;
    }

    DIR dir;
    FILINFO fi;
    if (f_opendir(&dir, "/") != FR_OK) return 0;
    while (count < GIF_STORE_MAX && f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        if (!is_gif(&fi) || strlen(fi.fname) >= GIF_STORE_NAME_MAX) continue;
        strcpy(names[count++], fi.fname);
    }
    f_closedir(&dir);
    qsort(names, (size_t)count, sizeof names[0], by_name);
    return count;
}

unsigned gif_store_stamp(const char *name) {
    FILINFO fi;
    if (f_stat(name, &fi) != FR_OK) return 0;
    return (unsigned)fi.fsize ^ ((unsigned)fi.fdate << 16 | fi.ftime) ^ 1u;
}

bool gif_store_complete(const char *name) {
    FIL f;
    BYTE last = 0;
    UINT got = 0;
    if (f_open(&f, name, FA_READ) != FR_OK) return false;
    if (f_size(&f) > 0 && f_lseek(&f, f_size(&f) - 1) == FR_OK) f_read(&f, &last, 1, &got);
    f_close(&f);
    return got == 1 && last == 0x3b;
}

int gif_store_count(void) {
    return count;
}

const char *gif_store_name(int index) {
    return (index >= 0 && index < count) ? names[index] : NULL;
}
