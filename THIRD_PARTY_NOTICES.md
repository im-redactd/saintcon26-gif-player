# Third-party software

The code in this repository is MIT-licensed (see [LICENSE](LICENSE)) **except** the parts below,
which keep their own licenses. The license texts are in `lib/` next to the libraries and in
[`LICENSES/`](LICENSES/).

| Component | Where | License | Notes |
|---|---|---|---|
| [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) 2.2.0, © 2020 BitBank Software, Inc. (Larry Bank) | `lib/AnimatedGIF/` | Apache-2.0 ([`lib/AnimatedGIF/LICENSE`](lib/AnimatedGIF/LICENSE)) | **Modified:** `AnimatedGIF.h` lets the build override `MAX_WIDTH` (the change notice is at the top of the file). `AnimatedGIF.cpp` is upstream's C++ wrapper and is not compiled. **Adapted:** `draw_turbo_frame()` in `src/gif_player.c` follows the row/interlace loop of the library's `DecodeLZWTurbo()`. |
| [FatFs](http://elm-chan.org/fsw/ff/) R0.16, © ChaN | `lib/fatfs/` | FatFs license, 1-clause BSD style ([`lib/fatfs/LICENSE.txt`](lib/fatfs/LICENSE.txt)) | `ffconf.h` is configured for this project (LFN, mkfs, label, find, code page 437, no RTC). Upstream's sample `diskio.c` is replaced by `src/fatfs_diskio.c`. |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk), © 2020 Raspberry Pi (Trading) Ltd. | not vendored (fetched/linked at build time); `pico_sdk_import.cmake` is the SDK's standard copy-into-your-project file | BSD-3-Clause ([`LICENSES/BSD-3-Clause-RaspberryPi.txt`](LICENSES/BSD-3-Clause-RaspberryPi.txt)) | The firmware binary links SDK code (BSD-3-Clause) and the SDK's `pico_printf` (MIT, © Marco Paland). |
| `src/usb_descriptors.c` | | MIT | Based on the SDK's `pico_stdio_usb/stdio_usb_descriptors.c`, © 2020 Raspberry Pi (Trading) Ltd., © 2019 Damien P. George (MicroPython). Same MIT terms as [LICENSE](LICENSE). |
| WS2812 PIO program from [pico-examples](https://github.com/raspberrypi/pico-examples), © 2020 Raspberry Pi (Trading) Ltd. | `src/ws2812.pio` | BSD-3-Clause ([`LICENSES/BSD-3-Clause-RaspberryPi.txt`](LICENSES/BSD-3-Clause-RaspberryPi.txt)) | |
| [TinyUSB](https://github.com/hathach/tinyusb), © 2018 hathach | via the Pico SDK | MIT | USB device stack (CDC, MSC), linked into the firmware. |

Firmware binaries built from this repository (for example the CI artifacts) contain the code
listed above. Redistribute them together with `LICENSE`, this file and `LICENSES/`; the CI
workflow bundles them for you.

## Display initialisation data

`src/st77916_init.h` holds the ST77916 panel's register initialisation values (panel timing,
voltages, gamma). They were read back from the badge's original firmware. Only these register
values were taken; no code was copied. They have not been matched to a published vendor table,
and they are not covered by this repository's MIT license.

## USB IDs

The firmware enumerates with Raspberry Pi's vendor ID and the Pico SDK's default product ID
(`2E8A:0009`) so that stock `picotool` recognises its reset interface. That is fine for hobby
builds. If you make and distribute hardware with this firmware, request your own PID: Raspberry Pi
hands them out for free for RP2xxx projects at <https://github.com/raspberrypi/usb-pid>.
