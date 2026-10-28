// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
#include <stdbool.h>

#include "leds.h"

#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

#ifndef SC26_WS2812_GRB
#define SC26_WS2812_GRB 1  // WS2812 wire order is G, R, B
#endif

static PIO led_pio;
static uint led_sm;
static uint8_t pixels[SC26_WS2812_COUNT][3];
static uint8_t brightness = 64;

void leds_init(void) {
    uint offset;
    // claims a free PIO + state machine that can reach this GPIO
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &led_pio, &led_sm, &offset,
                                                                 SC26_WS2812_PIN, 1, true));
    ws2812_program_init(led_pio, led_sm, offset, SC26_WS2812_PIN, 800000, false);

    gpio_init(SC26_STATUS_LED_PIN);
    gpio_set_dir(SC26_STATUS_LED_PIN, GPIO_OUT);
    leds_fill(0, 0, 0);
    leds_show();
}

int leds_count(void) {
    return SC26_WS2812_COUNT;
}

void leds_set_brightness(uint8_t level) {
    brightness = level;
}

void leds_set(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= SC26_WS2812_COUNT) return;
    pixels[index][0] = r;
    pixels[index][1] = g;
    pixels[index][2] = b;
}

void leds_fill(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < SC26_WS2812_COUNT; i++) leds_set(i, r, g, b);
}

void leds_show(void) {
    for (int i = 0; i < SC26_WS2812_COUNT; i++) {
        uint32_t r = pixels[i][0] * brightness / 255;
        uint32_t g = pixels[i][1] * brightness / 255;
        uint32_t b = pixels[i][2] * brightness / 255;
        uint32_t word = SC26_WS2812_GRB ? (g << 16 | r << 8 | b) : (r << 16 | g << 8 | b);
        pio_sm_put_blocking(led_pio, led_sm, word << 8u);
    }
}

void status_led_put(bool on) {
    gpio_put(SC26_STATUS_LED_PIN, on);
}
