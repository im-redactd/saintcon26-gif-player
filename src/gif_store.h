// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// The FAT volume on the flash disk that holds the GIFs (also exported over USB).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GIF_STORE_MAX 64
#define GIF_STORE_NAME_MAX 96

// Mounts the volume, formatting and seeding it with the built-in GIF on first use.
bool gif_store_mount(void);

// Re-reads the directory after the USB host changed the volume (files opened earlier stay
// usable as long as the host did not modify them). Returns the number of GIFs found.
int gif_store_rescan(void);

// Size/date fingerprint of a file, to notice the host overwriting it; 0 if missing.
unsigned gif_store_stamp(const char *name);

// True once a GIF looks fully written (ends with the GIF trailer byte 0x3B).
bool gif_store_complete(const char *name);

// Called by the USB layer for every host write, so FatFs never uses a stale cached sector.
void gif_store_host_wrote(uint32_t lba, uint32_t n);

// True (once) if the last rescan had to remount because the host reformatted the drive;
// any file opened before is then invalid and must be reopened.
bool gif_store_take_remounted(void);

int gif_store_count(void);
const char *gif_store_name(int index);  // path usable with f_open

// The GIF linked into the firmware image (played when the drive has none).
const unsigned char *gif_store_builtin(unsigned *len);
