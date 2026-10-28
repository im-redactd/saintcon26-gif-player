# SC26 badge GIF player

Firmware for the **SC26 badge**: an RP2350 board with a 360×360 round LCD, 9 RGB LEDs and a
D-pad. It plays animated GIFs and matches the LEDs to the picture. Plug it into a computer and
it shows up as a USB drive: drop GIFs on it and they start playing.

* Plays GIFs up to 1024 px wide, decoding faster than real time. Square GIFs fill the round
  screen; wide or tall ones are fitted inside the circle.
* 15 MB GIF drive in the on-board flash. Exposed over USB mass storage, and power-loss safe for
  the FAT and directory.
* WS2812 LEDs follow the average colour of the frame ("ambient"), run a green rain animation,
  or turn off.
* USB serial console for control and diagnostics.
* `picotool` can reboot and reflash the badge without touching the BOOTSEL button.
* Comes with original hacker-con animations (digital rain, a HACK THE PLANET globe, the DNS
  haiku, an ACCESS GRANTED terminal, radar) and tools to convert your own GIFs.

Hardware details and the full pin map are in [docs/HARDWARE.md](docs/HARDWARE.md).

## Using it

| Control | Action |
|---|---|
| D-pad left / right | previous / next GIF |
| D-pad up / down | screen brightness |
| D-pad centre | pause / resume |
| A | LED mode: ambient → rain → off |
| B | LED brightness |

**Adding GIFs:** copy `.gif` files onto the `SC26GIFS` drive. When the copy finishes, the badge
rescans the drive and jumps to the new file. Writes go to NOR flash at roughly 60-150 KB/s, so a
few MB takes a minute or two. On the very first boot the drive is formatted and seeded with
`matrix_rain.gif` and a README.

**Console:** open the badge's serial port (any baud rate except 1200, which reboots it into
BOOTSEL) and press `h`:

```
n / p   next / previous GIF        m   LED mode
l       list GIFs                  b   LED brightness
space   pause / resume             r   rescan the drive
+ / -   backlight                  i   info: decode time, flash stats
t       button test
```

## Building

You need:

* The [Pico SDK](https://github.com/raspberrypi/pico-sdk) **2.3.0 or newer** (tested with 2.3.1),
  with its `lib/tinyusb` submodule.
* An Arm GNU toolchain (`arm-none-eabi-gcc`; developed with Arm GNU Toolchain 14.2; the CI
  workflow uses Ubuntu 24.04's packaged 13.2).
* CMake 3.13 or newer, and Ninja or Make.
* `picotool` and `pioasm`. The SDK builds these from source when it can't find them, which needs
  a host C/C++ compiler (and `libusb-1.0` dev files on Linux).

The [Raspberry Pi Pico VS Code extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
installs all of this. Otherwise:

```sh
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
git -C ~/pico-sdk submodule update --init --depth 1 lib/tinyusb
export PICO_SDK_PATH=~/pico-sdk          # Windows PowerShell: $env:PICO_SDK_PATH = "..."

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                       # -> build/sc26_gif.uf2
```

Alternatively, pass `-DPICO_SDK_FETCH_FROM_GIT=ON` instead of setting `PICO_SDK_PATH`.

## Flashing

* **Badge already running this firmware (or any Pico SDK USB firmware):** run
  `picotool load -x build/sc26_gif.uf2 -f`. Add `--ser <serial>` if more than one RP2xxx board is
  plugged in; `picotool info -a` shows the serials.
* **Any state:** reboot into BOOTSEL. Either open the serial port at 1200 baud, or hold BOOTSEL
  (if your board has one) while plugging in. Then copy `sc26_gif.uf2` onto the `RP2350` drive.

Flashing new firmware leaves the GIF drive alone.

## Tools

Python 3.9+ with Pillow: `pip install -r tools/requirements.txt`.

| Tool | What it does |
|---|---|
| `tools/badgeify.py in.gif out.gif` | Turns any GIF into a tidy 360×360 version for the round screen: centre crop (or `--fit` for captioned or wide clips), corners blacked out, one shared palette, and only changed pixels stored per frame (typically 40-60% smaller). |
| `tools/gif2uf2.py out.uf2 a.gif b.gif …` | Packs GIFs into a complete FAT16 drive image as a UF2. Dropping it on the `RP2350` boot drive replaces the GIF drive's contents and leaves the firmware alone. |
| `tools/make_test_gifs.py`, `tools/make_cyber_gifs.py` | Regenerate the original animations in `gifs/`. |

Tests: `python -m unittest discover -s tests -v`.

## How it works

Everything runs on core 0 in one loop: TinyUSB, the serial console, buttons, LEDs, and the
GIF frame scheduler.

* **Display:** a two-instruction PIO program drives the ST77916's 4-bit QSPI bus at 75 MHz, and
  DMA streams the 360×360 RGB565 framebuffer (259 KB) in about 7 ms. Frames are decoded by
  [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), using its turbo decoder when the GIF
  fits a 150 K-pixel work buffer. Decoding writes straight into the framebuffer, with
  nearest-neighbour scaling and correct GIF disposal and transparency.
* **Storage:** the drive is a FAT16 volume in flash from offset 1 MB. One 4 KB write-back cache
  serves both USB mass storage (the host's view) and [FatFs](http://elm-chan.org/fsw/ff/) (the
  firmware's view, read-only after formatting). Writes are committed at the end of every USB
  WRITE command. Blocks are only erased when a bit has to go 0→1, and only changed pages are
  programmed.
* **Power-loss safety:** the drive's first 64 KB (boot sector, FATs, root directory) goes through
  a small write-ahead journal. An unplug in the middle of a metadata rewrite is completed at
  the next boot instead of leaving an unformatted drive.
* **New files:** when the host stops writing, the badge rescans the directory. Windows writes
  FAT entries lazily, so it waits until a new GIF ends with its trailer byte before playing it.
  It also notices files that are replaced or deleted, and a host-side reformat.

### Flash and RAM

| Flash range | Use |
|---|---|
| `0x10000000`-`0x100FDFFF` | firmware (≈420 KB, including the built-in GIF). The linker refuses anything bigger. |
| `0x100FE000`-`0x100FFFFF` | metadata journal (8 KB) |
| `0x10100000`-end of flash | FAT16 GIF drive (15 MB on the 16 MB part) |

RAM use is about 490 KB of 520 KB: 259 KB framebuffer, about 155 KB GIF turbo buffer, the rest
for decoder state, USB and FatFs.

## Security notes

* **Anyone with USB access can reflash it.** The firmware keeps the standard Pico SDK reset paths
  (`picotool` reset interface, 1200-baud touch), and the RP2350 boot ROM always accepts UF2 files.
  Treat USB access as full control.
* **The badge parses untrusted files.** GIFs come from the host. The decoder rejects canvases wider
  than 1024 px and frames that extend outside the canvas, drawing is clipped to the screen, and
  malformed or truncated files are skipped. Reports of crashes caused by crafted GIFs are welcome.
* **Images from `gif2uf2.py` work on any RP2350.** They use the boot ROM's absolute-address
  family, so on another RP2350 board they would overwrite flash from 1 MB - 8 KB onward. Only drop
  them on this badge.
* **The USB IDs are Raspberry Pi's.** The firmware uses Raspberry Pi's VID and the Pico SDK's
  default PID (`2E8A:0009`) so stock `picotool` works. See
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) if you distribute hardware.

## License

The code in this repository is MIT ([LICENSE](LICENSE)), except the parts listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): the vendored libraries in `lib/`, the WS2812 PIO
program, code adapted from the Pico SDK and AnimatedGIF, and the panel's register init values.
Each keeps its own license, and the license texts are in `lib/` and `LICENSES/`. The animations in
`gifs/` are original and also MIT. Please keep film and TV clips out of `gifs/`; `.gitignore`
already ignores `private/` and `gifs/private/` for those.
