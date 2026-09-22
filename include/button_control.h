#ifndef BUTTON_CONTROL_H
#define BUTTON_CONTROL_H

#include "common.h"
#include "config.h"
#include "constants.h"
#include "led_control.h"
#include "entity_state.h"
#include "utils.h"
#include <esp_task_wdt.h>
#include "animations.h"
#include "websocket_handler.h"
#include "homeassistant_handler.h"

// Tunables. Defined here with #ifndef guards so an existing config.h that
// predates them keeps building; override any of them in config.h.

// How often the button matrix is scanned.
#ifndef BUTTON_SCAN_INTERVAL_MS
#define BUTTON_SCAN_INTERVAL_MS 10
#endif

// Settle time after driving a row low / enabling a column pull-up, before the
// column is sampled. Raise it if presses are still missed on long wiring.
#ifndef MATRIX_SETTLE_US
#define MATRIX_SETTLE_US 30
#endif

// Brightness / volume ramp speed: a constant BRIGHTNESS_ADJUST_STEP counts
// (out of 255) every BRIGHTNESS_ADJUST_INTERVAL_MS. Deliberately steady --
// a ramp that changes speed under your finger is harder to aim, not easier.
// The defaults move about 10% per 300ms of hold, sweeping 0 -> 255 in ~3s.
//
// The interval is quantised to BUTTON_SCAN_INTERVAL_MS, since a step can only
// be taken on a scan tick. Prefer changing the step to change the rate.
#ifndef BRIGHTNESS_ADJUST_STEP
#define BRIGHTNESS_ADJUST_STEP 4
#endif

#ifndef BRIGHTNESS_ADJUST_INTERVAL_MS
#define BRIGHTNESS_ADJUST_INTERVAL_MS 50
#endif

// How long to wait for the shared state mutex before skipping a ramp step.
#ifndef MUTEX_WAIT_MS
#define MUTEX_WAIT_MS 50
#endif

extern unsigned long lastDebounceTime[ROWS][COLS];
extern bool buttonState[ROWS][COLS];
extern bool lastButtonState[ROWS][COLS];
extern unsigned long buttonPressTime[ROWS][COLS];
extern bool upButtonPressed;
extern bool downButtonPressed;
extern bool isBrightnessAdjustmentMode;
extern int currentAdjustmentBrightness;
extern unsigned long brightnessAdjustmentStartTime;
extern int lastAdjustedX;
extern int lastAdjustedY;

void buttonCheckTask(void * parameter);
bool adjustBrightnessOrVolume(int x, int y, bool increase);
void toggleChildLock();

#endif // BUTTON_CONTROL_H
