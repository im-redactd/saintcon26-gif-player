// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// ST77916 360x360 round LCD on a PIO-driven QSPI bus, full-frame updates via DMA.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_WIDTH SC26_LCD_WIDTH
#define DISPLAY_HEIGHT SC26_LCD_HEIGHT

void display_init(void);

// RGB565 framebuffer, stored big-endian (high byte first) as the panel expects it on the wire.
uint16_t *display_framebuffer(void);

// Starts sending the whole framebuffer to the panel; returns immediately.
void display_present(void);
// Blocks until the previous display_present() has finished (call before touching the framebuffer).
void display_wait(void);
bool display_busy(void);

void display_set_backlight(uint8_t percent);  // 0..100

// Converts 8-bit RGB to a framebuffer pixel.
static inline uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)((r & 0xf8) << 8 | (g & 0xfc) << 3 | b >> 3);
    return (uint16_t)(c >> 8 | c << 8);
}
