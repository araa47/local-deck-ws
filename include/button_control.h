#ifndef BUTTON_CONTROL_H
#define BUTTON_CONTROL_H

#include "common.h"
#include "config.h"
#include "constants.h"

extern unsigned long lastDebounceTime[ROWS][COLS];
extern bool buttonState[ROWS][COLS];
extern bool lastButtonState[ROWS][COLS];
extern unsigned long buttonPressTime[ROWS][COLS];
extern bool upButtonPressed;
extern bool downButtonPressed;
extern unsigned long lastBrightnessAdjustTime;
extern bool isBrightnessAdjustmentMode;
extern int currentAdjustmentBrightness;
extern unsigned long brightnessAdjustmentStartTime;
extern int lastAdjustedX;
extern int lastAdjustedY;

void buttonCheckTask(void * parameter);
bool adjustBrightness(int x, int y, bool increase);
void updateButtonStates();
void toggleChildLock();

#endif // BUTTON_CONTROL_H
