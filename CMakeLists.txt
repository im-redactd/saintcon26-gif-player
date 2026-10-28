# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD sc26_badge)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_PLATFORM rp2350-arm-s)

# Finds the Pico SDK via PICO_SDK_PATH, or fetches it with -DPICO_SDK_FETCH_FROM_GIT=ON
include(pico_sdk_import.cmake)

project(sc26_gif C CXX ASM)
set(CMAKE_C_STANDARD 11)
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
    message(FATAL_ERROR "Pico SDK 2.3.0 or newer is required (pico_usb_reset, linker script overrides); found ${PICO_SDK_VERSION_STRING}")
endif()
pico_sdk_init()

# The default GIF linked into the image (copied onto the drive at first boot)
set(BUILTIN_GIF ${CMAKE_CURRENT_LIST_DIR}/gifs/matrix_rain.gif)
configure_file(src/builtin_gif.S.in ${CMAKE_CURRENT_BINARY_DIR}/builtin_gif.S @ONLY)
set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/builtin_gif.S APPEND PROPERTY OBJECT_DEPENDS ${BUILTIN_GIF})

set(SC26_SOURCES
    src/main.c
    src/display.c
    src/gif_store.c
    src/flash_disk.c
    src/fatfs_diskio.c
    src/msc_disk.c
    src/usb_descriptors.c
    src/leds.c
    src/buttons.c
)
add_executable(sc26_gif
    ${SC26_SOURCES}
    src/gif_player.c
    lib/fatfs/ff.c
    lib/fatfs/ffunicode.c
    ${CMAKE_CURRENT_BINARY_DIR}/builtin_gif.S
)

pico_generate_pio_header(sc26_gif ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
pico_generate_pio_header(sc26_gif ${CMAKE_CURRENT_LIST_DIR}/src/qspi_lcd.pio)

target_include_directories(sc26_gif PRIVATE src lib/fatfs lib/AnimatedGIF)

target_compile_definitions(sc26_gif PRIVATE
    # flash layout: firmware in the first 1 MB, the GIF drive in the rest
    FLASH_DISK_OFFSET=0x100000
    # USB: the app owns TinyUSB (CDC console + MSC drive + picotool reset interface)
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
    PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=1
    PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF=3
    PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
    # widest GIF canvas AnimatedGIF accepts (one line buffer of this many bytes)
    MAX_WIDTH=1024
)

# Our own code builds warning-free with -Wall -Wextra. gif_player.c #includes AnimatedGIF's gif.inl,
# which trips a few warnings that are not ours to fix; the vendored libraries keep the SDK defaults.
set_source_files_properties(${SC26_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
set_source_files_properties(src/gif_player.c PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Wno-unused-function;-Wno-unused-variable;-Wno-unused-parameter;-Wno-sign-compare;-Wno-shift-negative-value")

target_link_libraries(sc26_gif
    pico_stdlib
    pico_stdio_usb
    pico_usb_reset
    pico_unique_id
    tinyusb_device
    tinyusb_board
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_flash
    hardware_clocks
)

pico_enable_stdio_usb(sc26_gif 1)
pico_enable_stdio_uart(sc26_gif 0)
pico_set_program_name(sc26_gif "SC26 GIF player")
pico_set_program_version(sc26_gif "1.0")
pico_add_extra_outputs(sc26_gif)

# The image must end below the drive's metadata journal (1 MB - 8 KB). Shrinking the linker's FLASH
# region makes an oversized build fail at link time, before any .bin/.uf2 exists. (Not
# pico_override_flash_size: that would also change PICO_FLASH_SIZE_BYTES, which sizes the drive.)
set(PICO_FLASH_SIZE_BYTES_STRING 0xFE000)
configure_file(${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/pico_flash_region.template.ld
               ${CMAKE_CURRENT_BINARY_DIR}/firmware_slot/pico_flash_region.ld)
pico_add_linker_script_override_path(sc26_gif ${CMAKE_CURRENT_BINARY_DIR}/firmware_slot FILES pico_flash_region.ld)
add_custom_command(TARGET sc26_gif POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:sc26_gif>/sc26_gif.bin -DLIMIT=1040384
            -P ${CMAKE_CURRENT_LIST_DIR}/tools/check_size.cmake)
