// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// The badge's seven push buttons (active low, internal pull-ups), debounced.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Bit positions, matching the original firmware's mask layout.
enum button {
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_CENTER,
    BTN_B,
    BTN_A,
    BUTTON_COUNT
};

void buttons_init(void);

// Call often; returns a mask (1 << enum button) of buttons newly pressed since the last call.
uint32_t buttons_poll(void);

uint32_t buttons_held(void);
int button_gpio(int index);
const char *button_name(int index);
