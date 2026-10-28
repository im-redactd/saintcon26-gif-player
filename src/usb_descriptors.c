// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// Based on the Pico SDK's src/rp2_common/pico_stdio_usb/stdio_usb_descriptors.c
// (MIT; Copyright (c) 2020 Raspberry Pi (Trading) Ltd., Copyright (c) 2019 Damien P. George).
//
// Composite USB device: CDC serial console + mass-storage GIF drive + the
// Raspberry Pi vendor reset interface (so picotool can reboot the badge).
#include "pico/unique_id.h"
#include "pico/usb_reset.h"
#include "tusb.h"

#define USBD_VID 0x2E8A  // Raspberry Pi
#define USBD_PID 0x0009  // Pico SDK CDC (+ reset); keeps existing drivers happy

enum {
    ITF_CDC = 0,  // CDC uses two interfaces
    ITF_CDC_DATA,
    ITF_MSC,
    ITF_RESET,
    ITF_COUNT
};
static_assert(ITF_RESET == PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF, "reset interface number mismatch");

enum { STR_LANG, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_MSC, STR_RESET, STR_COUNT };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_MSC_OUT 0x03
#define EP_MSC_IN 0x83

#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN + TUD_RPI_RESET_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,  // 2.1 so Windows asks for the MS OS 2.0 descriptor (WinUSB on the reset interface)
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USBD_VID,
    .idProduct = USBD_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t desc_config[CONFIG_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_MSC, STR_MSC, EP_MSC_OUT, EP_MSC_IN, 64),
    TUD_RPI_RESET_DESCRIPTOR(ITF_RESET, STR_RESET),
};

static char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const strings[STR_COUNT] = {
    [STR_MANUF] = "redactd",
    [STR_PRODUCT] = "klippy-cade",
    [STR_SERIAL] = serial_str,
    [STR_CDC] = "SC26 Console",
    [STR_MSC] = "klippy-cade",
    [STR_RESET] = "Reset",
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t buf[32];
    if (!serial_str[0]) pico_get_unique_board_id_string(serial_str, sizeof serial_str);

    uint8_t len;
    if (index == STR_LANG) {
        buf[1] = 0x0409;
        len = 1;
    } else {
        if (index >= STR_COUNT || !strings[index]) return NULL;
        const char *s = strings[index];
        for (len = 0; len < 31 && s[len]; len++) buf[1 + len] = (uint8_t)s[len];
    }
    buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return buf;
}
