#!/bin/sh
# Host test for the RP2040 settings page log in src/rp2040/mcu_hw.c,
# against a simulated flash (program only clears bits, erase sets them).
# The settings block is cut out of mcu_hw.c so the test runs the real code.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
W="$(mktemp -d)"
python3 - "$here/../../src/rp2040/mcu_hw.c" "$W/block.c" <<'P'
import sys
s=open(sys.argv[1]).read()
a=s.index('#define SETTINGS_OFFSET'); b=s.index('// Invoked when device with hid interface is un-mounted')
open(sys.argv[2],'w').write(s[a:b])
P
cp "$here/test.c" "$W/"
cd "$W" && cc -std=gnu11 -Wall -Wno-unused-function -o settings_flash_test test.c && ./settings_flash_test
