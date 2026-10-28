// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// Addressable RGB LEDs (WS2812 chain) plus the single status LED.
#pragma once

#include <stdbool.h>
#include <stdint.h>

void leds_init(void);

// Set every LED in the chain to one colour (8-bit RGB, scaled by the global brightness).
void leds_fill(uint8_t r, uint8_t g, uint8_t b);
void leds_set(int index, uint8_t r, uint8_t g, uint8_t b);
void leds_show(void);
void leds_set_brightness(uint8_t level);  // 0..255
int leds_count(void);

void status_led_put(bool on);
