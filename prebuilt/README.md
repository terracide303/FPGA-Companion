# Prebuilt firmware (dev branch)

`fpga_companion_msp20k_dev.uf2` — FPGA-Companion for the Raspberry Pi Pico
(RP2040) on the MiSTeryShield20k (`BOARD=MSP20K`), built from this branch.

**What it adds:** two entries at the bottom of every core's F12 main menu.

- **Setup Gamepad** — let go of all buttons, then press each control it asks
  for (UP, DOWN, LEFT, RIGHT, I/A, II/B, SELECT, RUN/Start, III/X, IV/Y, L, R).
  ESC skips one. The layout is saved in the Pico's own flash per gamepad model
  (USB VID:PID), survives power cycles and firmware updates, and replaces the
  automatic mapping for that gamepad only.
- **Remove Gamepad Setup** — press a button on the pad; its setup is deleted
  and it goes back to automatic detection.

**Also:** WiFi and Bluetooth are off, as on `msp20k-rp2040-fit-no-wifi`, so it
fits the RP2040's RAM. Wired USB pads only.

**Install:** hold BOOTSEL, plug the Pico into a computer, copy the `.uf2` onto
the `RPI-RP2` drive.

**Status:** experimental. On hardware 2026-09-30: setup and save work on one
gamepad. The first builds froze the USB keyboard when saving (ESC and F12
stopped working); this build saves in about 1 ms instead of ~50 ms and should
fix that -- not yet confirmed on hardware. This file is rebuilt and replaced
whenever the branch changes; the commit that added it is the one it was built
from.
