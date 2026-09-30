#!/bin/sh
# Host test for src/gamepad_setup.c: learns two simulated gamepads
# (axis d-pad + noisy counter byte; hat + button nibbles), saves to a
# fake flash, reloads, removes one. Run on any PC with a C compiler.
#
# The sources are copied next to the stubs first: a quoted #include is
# looked up in the source file's own folder before -I, so compiling
# src/gamepad_setup.c in place would pick up the real osd.h, menu.h etc.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
S="$here/../../src"
W="$(mktemp -d)"
cp "$here"/stub/*.h "$here/test.c" "$W/"
cp "$S/gamepad_setup.c" "$S/gamepad_setup.h" "$S/hidparser.h" "$S/usb_controller_maps.h" "$W/"
cd "$W"
cc -std=gnu11 -Wall -Wno-unused-function -DCONTROLLERDB_ATTR= -DCONTROLLERDB_STR_ATTR= \
   -I. -o gamepad_setup_test test.c gamepad_setup.c
./gamepad_setup_test
