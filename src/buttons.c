// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
#include "buttons.h"

#include "pico/stdlib.h"

#define DEBOUNCE_MS 25

static const uint8_t pins[BUTTON_COUNT] = {
    [BTN_UP] = SC26_BTN_UP_PIN,         [BTN_DOWN] = SC26_BTN_DOWN_PIN, [BTN_LEFT] = SC26_BTN_LEFT_PIN,
    [BTN_RIGHT] = SC26_BTN_RIGHT_PIN,   [BTN_CENTER] = SC26_BTN_CENTER_PIN,
    [BTN_B] = SC26_BTN_B_PIN,           [BTN_A] = SC26_BTN_A_PIN,
};
static const char *const names[BUTTON_COUNT] = {"up", "down", "left", "right", "center", "B", "A"};

static uint32_t stable;        // debounced "held" mask
static uint32_t last_raw;
static uint32_t last_change_ms;

void buttons_init(void) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
}

static uint32_t read_raw(void) {
    uint32_t all = gpio_get_all();
    uint32_t mask = 0;
    for (int i = 0; i < BUTTON_COUNT; i++)
        if (!(all & (1u << pins[i]))) mask |= 1u << i;  // pressed pulls the pin low
    return mask;
}

uint32_t buttons_poll(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t raw = read_raw();
    if (raw != last_raw) {
        last_raw = raw;
        last_change_ms = now;
        return 0;
    }
    if (now - last_change_ms < DEBOUNCE_MS || raw == stable) return 0;
    uint32_t pressed = raw & ~stable;
    stable = raw;
    return pressed;
}

uint32_t buttons_held(void) {
    return stable;
}

int button_gpio(int index) {
    return (index >= 0 && index < BUTTON_COUNT) ? pins[index] : -1;
}

const char *button_name(int index) {
    return (index >= 0 && index < BUTTON_COUNT) ? names[index] : "?";
}
