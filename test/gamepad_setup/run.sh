#!/bin/sh
# Host test for src/gamepad_setup.c: learns two simulated gamepads
# (axis d-pad + noisy counter byte; hat + button nibbles), saves to a
# fake flash, reloads, removes one. Run on any PC with a C compiler.
cd "$(dirname "$0")"
S=../../src
cc -std=gnu11 -Wall -Wno-unused-function -DCONTROLLERDB_ATTR= -DCONTROLLERDB_STR_ATTR= \
   -Istub -I$S -o /tmp/gamepad_setup_test test.c $S/gamepad_setup.c && /tmp/gamepad_setup_test
