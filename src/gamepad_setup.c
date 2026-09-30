/*
  gamepad_setup.c

  Learns a gamepad's layout from its raw HID reports. The user is asked to
  press each control once. For every control the raw report is compared
  against the report with nothing pressed, and the change is stored as one
  of:

  - a single bit that sets or clears            (buttons)
  - a hat value in the low nibble of a byte     (d-pads reported as a hat)
  - a byte crossing a threshold                 (sticks, d-pads as axes)

  Setups are kept in RAM and saved in the MCU's own flash (a settings
  sector, see mcu_hw_settings_write()), so they work with every core and
  survive power cycles and firmware updates. MCUs without that use
  /sd/PCE/gamepad.ini instead, one line per gamepad:

    VVVV:PPPP k.oo.mm.vv k.oo.mm.vv ...   (GP_CTRLS bindings, hex)

  k = kind, oo = byte offset in the report, mm = bit mask, vv = value.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>

#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>

#include "gamepad_setup.h"
#include "sdc.h"
#include "osd.h"
#include "menu.h"
#include "debug.h"
#include "mcu_hw.h"

#define GP_MAX_PADS     8   // saved setups
#define GP_MAX_SLOTS    4   // gamepads tracked at once
#define GP_REPORT_MAX  64   // longest report looked at

#define GP_IDLE_SETTLE_MS  500  // idle phase: let the "select" press be released
#define GP_IDLE_NOISE_MS  1000  // idle phase: then record what changes on its own

// binding kinds
#define GP_NONE      0
#define GP_BIT_SET   1
#define GP_BIT_CLR   2
#define GP_HAT       3
#define GP_GE_U      4
#define GP_LE_U      5
#define GP_GE_S      6
#define GP_LE_S      7

typedef struct {
  uint8_t kind, offset, mask, value;
} gp_bind_t;

typedef struct {
  uint16_t vid, pid;
  gp_bind_t bind[GP_CTRLS];
} gp_pad_t;

static gp_pad_t pads[GP_MAX_PADS];
static int num_pads = 0;
static uint8_t generation = 1;   // bumped whenever pads[] changes

// last raw report per gamepad, so there's an idle report to compare against
// even for gamepads that only send reports when something changes
typedef struct {
  const hid_report_t *rep;
  uint16_t vid, pid;
  uint8_t gen;        // generation the cached pad index belongs to
  int8_t pad;         // index into pads[], -1 if none
  uint8_t len;
  uint8_t last[GP_REPORT_MAX];
  uint8_t idle[GP_REPORT_MAX];
  uint8_t noise[GP_REPORT_MAX];
} gp_slot_t;

static gp_slot_t slots[GP_MAX_SLOTS];

static gamepad_setup_status_t status;
static gp_pad_t learn;            // setup being learned
static gp_slot_t *locked = NULL;  // gamepad being set up
static TickType_t idle_start;
static TimerHandle_t idle_timer = NULL;

static const char *ctrl_names[GP_CTRLS] = {
  "UP", "DOWN", "LEFT", "RIGHT", "Button I (A)", "Button II (B)",
  "SELECT", "RUN (Start)", "Button III (X)", "Button IV (Y)",
  "L shoulder", "R shoulder"
};

const char *gamepad_setup_ctrl_name(int ctrl) {
  return (ctrl >= 0 && ctrl < GP_CTRLS)?ctrl_names[ctrl]:"";
}

const gamepad_setup_status_t *gamepad_setup_status(void) {
  return &status;
}

/* ========================= SD card file ========================= */

static char *gp_filename(bool dir_only) {
  static char name[40];
  if(dir_only) sprintf(name, "%s/%s", CARD_MOUNTPOINT, GAMEPAD_SETUP_DIR);
  else         sprintf(name, "%s/%s/%s", CARD_MOUNTPOINT, GAMEPAD_SETUP_DIR, GAMEPAD_SETUP_FILE);
  return name;
}

static bool gp_parse_line(char *line, gp_pad_t *pad) {
  char *p = line;
  unsigned long vid = strtoul(p, &p, 16);
  if(*p++ != ':') return false;
  unsigned long pid = strtoul(p, &p, 16);

  pad->vid = vid;
  pad->pid = pid;
  for(int i=0;i<GP_CTRLS;i++) {
    unsigned long v[4];
    for(int j=0;j<4;j++) {
      v[j] = strtoul(p, &p, 16);
      if(j < 3 && *p++ != '.') return false;
    }
    pad->bind[i].kind = v[0];
    pad->bind[i].offset = v[1];
    pad->bind[i].mask = v[2];
    pad->bind[i].value = v[3];
  }
  return true;
}

// MCUs without a settings area in their own flash use the SD card
__attribute__((weak)) bool mcu_hw_settings_read(void *buf, int len) {
  (void)buf; (void)len; return false;
}
__attribute__((weak)) bool mcu_hw_settings_write(const void *buf, int len) {
  (void)buf; (void)len; return false;
}

#define GP_FLASH_MAGIC 0x31535047   // "GPS1"

typedef struct {
  uint32_t magic;
  uint16_t count;
  uint16_t pad_size;   // sizeof(gp_pad_t), guards against format changes
  gp_pad_t pads[GP_MAX_PADS];
} gp_flash_t;

static bool gp_load_flash(void) {
  gp_flash_t *f = pvPortMalloc(sizeof(gp_flash_t));
  if(!f) return false;

  bool ok = mcu_hw_settings_read(f, sizeof(gp_flash_t));
  if(ok) {
    // erased or foreign contents just mean "no setups"
    if(f->magic == GP_FLASH_MAGIC && f->pad_size == sizeof(gp_pad_t) &&
       f->count <= GP_MAX_PADS) {
      num_pads = f->count;
      memcpy(pads, f->pads, sizeof(pads));
    }
  }
  vPortFree(f);
  return ok;
}

static bool gp_save_flash(bool *ok) {
  gp_flash_t *f = pvPortMalloc(sizeof(gp_flash_t));
  if(!f) { *ok = false; return true; }

  memset(f, 0, sizeof(gp_flash_t));
  f->magic = GP_FLASH_MAGIC;
  f->count = num_pads;
  f->pad_size = sizeof(gp_pad_t);
  memcpy(f->pads, pads, sizeof(pads));

  // a false return from a real flash is a failed write; the weak
  // default can't be told apart from that, so check for flash first
  uint32_t probe;
  bool has_flash = mcu_hw_settings_read(&probe, sizeof(probe));
  if(has_flash) *ok = mcu_hw_settings_write(f, sizeof(gp_flash_t));
  vPortFree(f);
  return has_flash;
}

void gamepad_setup_init(void) {
  FIL fil;
  char line[160];

  num_pads = 0;
  if(gp_load_flash()) {
    usb_debugf("Gamepad setups in flash: %d", num_pads);
    generation++;
    return;
  }

  sdc_lock();
  if(f_open(&fil, gp_filename(false), FA_OPEN_EXISTING | FA_READ) == FR_OK) {
    while(num_pads < GP_MAX_PADS && f_gets(line, sizeof(line), &fil)) {
      if(line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
      if(gp_parse_line(line, &pads[num_pads])) {
	usb_debugf("Gamepad setup loaded for %04x:%04x", pads[num_pads].vid, pads[num_pads].pid);
	num_pads++;
      }
    }
    f_close(&fil);
  }
  sdc_unlock();
  generation++;
}

static bool gp_save(void) {
  FIL fil;
  bool ok = false;

  if(gp_save_flash(&ok)) {
    generation++;
    return ok;
  }

  sdc_lock();
  f_mkdir(gp_filename(true));  // fails harmlessly if it exists
  if(f_open(&fil, gp_filename(false), FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
    ok = f_puts("# Gamepad setups, written by \"Setup Gamepad\" in the OSD\n", &fil) >= 0;
    for(int i=0;ok && i<num_pads;i++) {
      char line[160];
      char *p = line + sprintf(line, "%04x:%04x", pads[i].vid, pads[i].pid);
      for(int j=0;j<GP_CTRLS;j++)
	p += sprintf(p, " %x.%02x.%02x.%02x", pads[i].bind[j].kind,
		     pads[i].bind[j].offset, pads[i].bind[j].mask, pads[i].bind[j].value);
      strcpy(p, "\n");
      ok = f_puts(line, &fil) >= 0;
    }
    if(f_close(&fil) != FR_OK) ok = false;
  }
  sdc_unlock();
  generation++;
  return ok;
}

static int gp_find(uint16_t vid, uint16_t pid) {
  for(int i=0;i<num_pads;i++)
    if(pads[i].vid == vid && pads[i].pid == pid)
      return i;
  return -1;
}

/* ========================= decoding ========================= */

static int gp_axis(uint8_t v, bool is_signed) {
  return is_signed?(int)(int8_t)v:(int)v;
}

static bool gp_eval(const gp_bind_t *b, const uint8_t *data, uint16_t len) {
  if(b->kind == GP_NONE || b->offset >= len) return false;
  uint8_t v = data[b->offset];

  switch(b->kind) {
  case GP_BIT_SET: return (v & b->mask) != 0;
  case GP_BIT_CLR: return (v & b->mask) == 0;
  case GP_HAT: {
    // accept the diagonals next to the learned direction
    uint8_t n = v & 0x0f;
    return n <= 7 && (n == b->value || n == ((b->value+1)&7) || n == ((b->value+7)&7));
  }
  case GP_GE_U: return gp_axis(v, false) >= gp_axis(b->value, false);
  case GP_LE_U: return gp_axis(v, false) <= gp_axis(b->value, false);
  case GP_GE_S: return gp_axis(v, true)  >= gp_axis(b->value, true);
  case GP_LE_S: return gp_axis(v, true)  <= gp_axis(b->value, true);
  }
  return false;
}

// find what changed between the idle report and this one
static bool gp_detect(const gp_slot_t *s, const uint8_t *data, uint16_t len, gp_bind_t *out) {
  gp_bind_t hat = { 0 }, centered = { 0 }, bit = { 0 }, axis = { 0 };
  int centered_diff = 0, axis_diff = 0;

  if(len > s->len) len = s->len;
  for(int o=0;o<len;o++) {
    uint8_t i = s->idle[o], c = data[o], x = i ^ c;
    if(!x) continue;

    // a hat: low nibble moves from neutral (8..15) to a direction (0..7)
    if(!(x & 0xf0) && !(s->noise[o] & 0x0f) && !hat.kind &&
       (i & 0x0f) >= 8 && (c & 0x0f) <= 7)
      hat = (gp_bind_t){ GP_HAT, o, 0x0f, c & 0x0f };

    // one clean bit
    if(!(x & (x-1)) && !(s->noise[o] & x) && !bit.kind)
      bit = (gp_bind_t){ (c & x)?GP_BIT_SET:GP_BIT_CLR, o, x, 0 };

    // a byte that moved a long way. Idle near 0 means a signed axis.
    bool is_signed = (i < 0x20) || (i >= 0xe0);
    int vi = gp_axis(i, is_signed), vc = gp_axis(c, is_signed);
    int diff = abs(vc - vi);
    if(diff >= 0x40) {
      int thr = (vi + vc) / 2;
      gp_bind_t b = { is_signed?((vc > vi)?GP_GE_S:GP_LE_S):((vc > vi)?GP_GE_U:GP_LE_U),
		      o, 0xff, (uint8_t)thr };
      // centered idle is what sticks and axis d-pads look like
      if(i >= 0x70 && i <= 0x90) {
	if(diff > centered_diff) { centered = b; centered_diff = diff; }
      } else if(diff > axis_diff) { axis = b; axis_diff = diff; }
    }
  }

  if(hat.kind)      *out = hat;
  else if(centered.kind) *out = centered;
  else if(bit.kind) *out = bit;
  else if(axis.kind) *out = axis;
  else return false;
  return true;
}

bool gamepad_setup_apply(const hid_report_t *report, const uint8_t *data, uint16_t len,
			 uint8_t *joy, uint8_t *ax, uint8_t *ay, uint8_t *btn_extra) {
  gp_slot_t *s = NULL;
  for(int i=0;i<GP_MAX_SLOTS;i++)
    if(slots[i].rep == report) s = &slots[i];
  if(!s) return false;

  // look the gamepad up again when the device or the saved setups changed
  if(s->gen != generation || s->vid != report->vid || s->pid != report->pid) {
    s->vid = report->vid;
    s->pid = report->pid;
    s->pad = gp_find(s->vid, s->pid);
    s->gen = generation;
  }
  if(s->pad < 0) return false;

  const gp_bind_t *b = pads[s->pad].bind;
  static const uint8_t joy_bits[GP_CTRLS] =
    { 0x08, 0x04, 0x02, 0x01, 0x10, 0x20, 0, 0, 0x40, 0x80, 0, 0 };
  static const uint8_t extra_bits[GP_CTRLS] =
    { 0, 0, 0, 0, 0, 0, 0x04, 0x08, 0, 0, 0x01, 0x02 };

  *joy = *btn_extra = 0;
  for(int i=0;i<GP_CTRLS;i++) {
    if(gp_eval(&b[i], data, len)) {
      *joy |= joy_bits[i];
      *btn_extra |= extra_bits[i];
    }
  }
  *ax = (*joy & 0x02)?0x00:(*joy & 0x01)?0xff:0x80;
  *ay = (*joy & 0x08)?0x00:(*joy & 0x04)?0xff:0x80;
  return true;
}

/* ========================= learning ========================= */

// the idle phase ends by time, as some gamepads send nothing while idle
static void gp_idle_done(__attribute__((unused)) TimerHandle_t arg) {
  if(status.phase != GP_PHASE_IDLE) return;
  status.phase = GP_PHASE_PRESS;
  menu_notify(MENU_EVENT_NONE);
}

void gamepad_setup_start(int mode) {
  memset(&learn, 0, sizeof(learn));
  locked = NULL;
  status.mode = mode;
  status.step = 0;
  status.vid = status.pid = 0;

  // the last report of each gamepad is where its idle report starts from
  for(int i=0;i<GP_MAX_SLOTS;i++) {
    memcpy(slots[i].idle, slots[i].last, GP_REPORT_MAX);
    memset(slots[i].noise, 0, GP_REPORT_MAX);
  }

  idle_start = xTaskGetTickCount();
  status.phase = GP_PHASE_IDLE;

  if(!idle_timer)
    idle_timer = xTimerCreate("Gamepad idle", pdMS_TO_TICKS(GP_IDLE_SETTLE_MS + GP_IDLE_NOISE_MS),
			      pdFALSE, NULL, gp_idle_done);
  xTimerStart(idle_timer, 0);
}

void gamepad_setup_cancel(void) {
  if(status.phase >= GP_PHASE_IDLE && status.phase <= GP_PHASE_RELEASE)
    status.phase = GP_PHASE_CANCELLED;
}

static void gp_finish(void) {
  int idx = gp_find(learn.vid, learn.pid);

  if(status.mode == GP_MODE_REMOVE) {
    if(idx < 0) { status.phase = GP_PHASE_NOTFOUND; return; }
    pads[idx] = pads[--num_pads];
    status.phase = gp_save()?GP_PHASE_REMOVED:GP_PHASE_FAILED;
    return;
  }

  if(idx < 0) {
    if(num_pads == GP_MAX_PADS) idx = GP_MAX_PADS-1;  // forget the last one
    else                        idx = num_pads++;
  }
  pads[idx] = learn;
  status.phase = gp_save()?GP_PHASE_SAVED:GP_PHASE_FAILED;
}

static void gp_next_step(void) {
  if(++status.step >= GP_CTRLS_ASKED) gp_finish();
  else status.phase = GP_PHASE_PRESS;
}

void gamepad_setup_skip(void) {
  if(status.phase == GP_PHASE_PRESS && status.mode == GP_MODE_SETUP) {
    learn.bind[status.step].kind = GP_NONE;
    gp_next_step();
  }
}

static gp_slot_t *gp_slot(const hid_report_t *report) {
  gp_slot_t *s = NULL;
  for(int i=0;i<GP_MAX_SLOTS;i++)
    if(slots[i].rep == report) return &slots[i];

  // new gamepad: take a free slot, or the first one if all are taken
  for(int i=0;i<GP_MAX_SLOTS && !s;i++)
    if(!slots[i].rep) s = &slots[i];
  if(!s) s = &slots[0];

  memset(s, 0, sizeof(gp_slot_t));
  s->rep = report;
  s->pad = -1;
  return s;
}

bool gamepad_setup_feed(const hid_report_t *report, const uint8_t *data, uint16_t len) {
  if(len > GP_REPORT_MAX) len = GP_REPORT_MAX;

  bool fresh = false;
  gp_slot_t *s = NULL;
  for(int i=0;i<GP_MAX_SLOTS;i++)
    if(slots[i].rep == report) s = &slots[i];
  if(!s) { s = gp_slot(report); fresh = true; }

  if(status.phase < GP_PHASE_IDLE || status.phase > GP_PHASE_RELEASE) {
    // not learning: just remember the report
    memcpy(s->last, data, len);
    s->len = len;
    return false;
  }

  // closing the OSD cancels the setup
  if(!osd_is_visible()) {
    status.phase = GP_PHASE_CANCELLED;
    return false;
  }

  bool redraw = false;

  if(status.phase == GP_PHASE_IDLE) {
    TickType_t t = xTaskGetTickCount() - idle_start;
    if(fresh || t < pdMS_TO_TICKS(GP_IDLE_SETTLE_MS)) {
      memcpy(s->idle, data, len);
      memset(s->noise, 0, sizeof(s->noise));
    } else {
      // anything that changes now nobody is pressing a button is noise
      for(int i=0;i<len;i++) s->noise[i] |= s->idle[i] ^ data[i];
    }
  } else if(status.phase == GP_PHASE_PRESS) {
    gp_bind_t b;
    if((!locked || locked == s) && gp_detect(s, data, len, &b)) {
      locked = s;
      learn.vid = status.vid = report->vid;
      learn.pid = status.pid = report->pid;
      learn.bind[status.step] = b;
      usb_debugf("Gamepad setup %s: kind %d offset %d mask %02x value %02x",
		 ctrl_names[status.step], b.kind, b.offset, b.mask, b.value);
      status.phase = GP_PHASE_RELEASE;
      redraw = true;
    }
  } else if(status.phase == GP_PHASE_RELEASE && s == locked) {
    if(!gp_eval(&learn.bind[status.step], data, len)) {
      if(status.mode == GP_MODE_REMOVE) gp_finish();
      else gp_next_step();
      redraw = true;
    }
  }

  memcpy(s->last, data, len);
  s->len = len;
  if(redraw) menu_notify(MENU_EVENT_NONE);
  return true;
}

