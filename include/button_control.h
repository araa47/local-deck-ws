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

// Brightness / volume ramp speed. The ramp accelerates: it starts fine so a
// short hold lands on the level you want, then switches to the coarse step
// once held past BRIGHTNESS_ACCEL_AFTER_MS so a full sweep is still quick.
//
// Fine phase: ~44 counts/sec, i.e. a 300ms nudge moves about 5%.
// Coarse phase: ~300 counts/sec, i.e. 0 -> 255 in well under a second.
#ifndef BRIGHTNESS_ADJUST_STEP
#define BRIGHTNESS_ADJUST_STEP 2
#endif

#ifndef BRIGHTNESS_ADJUST_INTERVAL_MS
#define BRIGHTNESS_ADJUST_INTERVAL_MS 45
#endif

#ifndef BRIGHTNESS_ADJUST_STEP_FAST
#define BRIGHTNESS_ADJUST_STEP_FAST 9
#endif

#ifndef BRIGHTNESS_ADJUST_INTERVAL_FAST_MS
#define BRIGHTNESS_ADJUST_INTERVAL_FAST_MS 30
#endif

// How long the gesture must be held before the coarse step takes over.
#ifndef BRIGHTNESS_ACCEL_AFTER_MS
#define BRIGHTNESS_ACCEL_AFTER_MS 800
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
