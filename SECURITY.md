# Security

The badge parses files that come from whatever computer it is plugged into: the GIFs and the
FAT file system on its USB drive. Bugs in that parsing, such as a crafted GIF or FAT image that
crashes, hangs or corrupts the badge, are worth reporting.

**Please report them privately** using GitHub's private vulnerability reporting: open the
repository's **Security** tab and click **Report a vulnerability**. Include the file that triggers
the problem and what the badge's serial console printed, if anything. Plain bugs with no security
impact can go in a normal issue.

## What is in scope

* **GIF decoding and drawing** (`src/gif_player.c`, `lib/AnimatedGIF`).
* **The USB mass-storage and flash layer** (`src/msc_disk.c`, `src/flash_disk.c`, `src/gif_store.c`,
  `lib/fatfs`).
* **The PC tools** (`tools/`), when fed hostile input.

## What is not a vulnerability

The badge deliberately keeps the standard RP2350 recovery paths: USB access means full control.
Anyone who can plug into it can reboot it into the boot ROM (with `picotool` or a 1200-baud
serial touch) and flash any firmware. The RP2350 boot ROM also accepts absolute-address UF2
files, which is how `tools/gif2uf2.py` works.
