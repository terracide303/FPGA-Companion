# Prebuilt firmware (dev branch)

Both are FPGA-Companion built from this branch. They add one entry at the
bottom of every core's F12 main menu:

**Gamepad**
- **Configure** — let go of all buttons, then press each control it asks for
  (UP, DOWN, LEFT, RIGHT, I/A, II/B, SELECT, RUN/Start, III/X, IV/Y, L, R).
  ESC skips one. The layout is stored per gamepad model (USB VID:PID) and
  replaces the automatic mapping for that gamepad only.
- **Forget Configuration** — press a button on the pad; its layout is deleted
  and it goes back to automatic detection.

## `fpga_companion_msp20k_dev.uf2` — Raspberry Pi Pico (RP2040)

For the Pico on the MiSTeryShield20k (`BOARD=MSP20K`). WiFi and Bluetooth are
off so it fits the RP2040's RAM; wired USB pads only.

**Saves in the Pico's own flash:** works with every core, survives power
cycles and firmware updates, up to 4 gamepad models.

**Install:** hold BOOTSEL, plug the Pico into a computer, copy the `.uf2` onto
the `RPI-RP2` drive.

**Status:** working on hardware (2026-09-30): configure, save, the layout used
after a restart, ESC closing the OSD, saving without freezing the keyboard.
The "Gamepad" submenu layout (2026-10-05) is not yet tested on hardware.

## `fpga_companion_nano20k_v3923_dev.bin` — the Tang Nano 20K's own BL616

For Tang Nano 20K boards marked `3923`, with **FPGA Partner** installed. It
replaces `fpga_companion_nano20k_v3923.bin` from the MiSTle
[v1.4.29 release](https://github.com/MiSTle-Dev/FPGA-Companion/releases/tag/v1.4.29),
at the same address (0x40000): use that release's `flash_nano20k_v3923.ini`
with this file put in its place.

**Saves on the SD card** (`PCE/gamepad.ini`), because it is not known which
part of the BL616's flash is free under FPGA Partner. That needs a core that
can write to the SD card; PCEHeroTN cannot yet (its ticket #168).

Built on macOS with link-time optimisation switched off (the macOS compiler
cannot read the SDK's compressed LTO data — the same patch MiSTle's README
gives for Windows), so it is larger than the release file (732 KB vs 666 KB).

**Status:** builds; not yet run on hardware.

---

These files are rebuilt and replaced whenever the branch changes; the commit
that added them is the one they were built from.
