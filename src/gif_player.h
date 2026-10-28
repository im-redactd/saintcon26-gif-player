// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// Plays animated GIFs into the display framebuffer using AnimatedGIF.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Target framebuffer (RGB565 stored big-endian, i.e. wire order; width*height pixels).
void gif_player_init(uint16_t *framebuffer, int width, int height);

bool gif_player_open_file(const char *path);                 // file on the FatFs volume
bool gif_player_open_memory(const uint8_t *data, size_t len); // e.g. a GIF linked into flash
void gif_player_close(void);

// Decodes the next frame into the framebuffer.
// Returns 1 if a frame was drawn, 0 when the animation wrapped back to the start
// (a frame was still drawn), -1 on a decode error. *delay_ms is the frame's delay.
int gif_player_next_frame(int *delay_ms);

void gif_player_canvas_size(int *width, int *height);
const char *gif_player_error(void);
