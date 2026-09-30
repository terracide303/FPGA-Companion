/*
  gamepad_setup.h

  "Setup Gamepad": learn a gamepad's layout by asking the user to press
  each control once, and save it per USB VID:PID in the MCU's flash (or
  on the SD card where the MCU has no settings area). A saved
  setup overrides the automatic (SDL database / HID descriptor) mapping
  for that gamepad only. "Remove Gamepad Setup" deletes it again, which
  gives the gamepad back to automatic detection.
*/

#ifndef GAMEPAD_SETUP_H
#define GAMEPAD_SETUP_H

#include <stdint.h>
#include <stdbool.h>
#include "hidparser.h"

// SD card fallback: the file lives in this folder
#define GAMEPAD_SETUP_DIR   "PCE"
#define GAMEPAD_SETUP_FILE  "gamepad.ini"

// the controls asked for, in this order
#define GP_CTRL_UP      0
#define GP_CTRL_DOWN    1
#define GP_CTRL_LEFT    2
#define GP_CTRL_RIGHT   3
#define GP_CTRL_A       4
#define GP_CTRL_B       5
#define GP_CTRL_SELECT  6
#define GP_CTRL_START   7
#define GP_CTRL_X       8
#define GP_CTRL_Y       9
#define GP_CTRL_L      10
#define GP_CTRL_R      11
#define GP_CTRLS       12

// what the setup dialog is doing right now
#define GP_PHASE_OFF       0
#define GP_PHASE_IDLE      1  // "release all buttons", measuring the idle report
#define GP_PHASE_PRESS     2  // waiting for the control in gp_step to be pressed
#define GP_PHASE_RELEASE   3  // control seen, waiting for it to be released
#define GP_PHASE_SAVED     4
#define GP_PHASE_REMOVED   5
#define GP_PHASE_NOTFOUND  6  // remove: this gamepad had no saved setup
#define GP_PHASE_FAILED    7  // saving failed
#define GP_PHASE_CANCELLED 8

#define GP_MODE_SETUP   0
#define GP_MODE_REMOVE  1

typedef struct {
  uint8_t phase;
  uint8_t mode;
  uint8_t step;         // current GP_CTRL_...
  uint16_t vid, pid;    // gamepad being set up, once known
} gamepad_setup_status_t;

// load the saved setups from the SD card
void gamepad_setup_init(void);

// start / stop the setup or remove dialog
void gamepad_setup_start(int mode);
void gamepad_setup_cancel(void);
// ESC during setup: skip the current control
void gamepad_setup_skip(void);
const gamepad_setup_status_t *gamepad_setup_status(void);
const char *gamepad_setup_ctrl_name(int ctrl);

// called with every raw joystick report. Returns true if the report was
// consumed by the setup dialog and must not be passed on.
bool gamepad_setup_feed(const hid_report_t *report, const uint8_t *data, uint16_t len);

// If this gamepad has a saved setup, decode the report with it into the
// usual joy/btn_extra/ax/ay bytes and return true.
bool gamepad_setup_apply(const hid_report_t *report, const uint8_t *data, uint16_t len,
			 uint8_t *joy, uint8_t *ax, uint8_t *ay, uint8_t *btn_extra);

#endif // GAMEPAD_SETUP_H
