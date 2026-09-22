#include "button_control.h"

// A button whose press was used as the target of a brightness/volume gesture
// must not also fire toggleEntity() when it is finally released.
static bool pressConsumedByAdjustment[ROWS][COLS] = {{false}};

static bool isModifierButton(int x, int y) {
    return (x == UP_BUTTON_X && y == UP_BUTTON_Y) ||
           (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y);
}

static void handleButtonRelease(int x, int y) {
    if (pressConsumedByAdjustment[y][x]) {
        pressConsumedByAdjustment[y][x] = false;
        SERIAL_PRINTF("Release at (%d, %d) consumed by brightness adjustment\n", x, y);
        return;
    }

    unsigned long pressDuration = millis() - buttonPressTime[y][x];

    if (isChildLockMode && pressDuration < LONG_PRESS_TIME) {
        return;
    }

    if (pressDuration < LONG_PRESS_TIME && !upButtonPressed && !downButtonPressed) {
        toggleEntity(x, y);
    } else {
        SERIAL_PRINTF("Long press detected at (x: %d, y: %d)\n", x, y);
    }
}

// Single implementation of the matrix scan, used both for normal presses and
// while a brightness/volume gesture is in progress.
//
// Columns are only pulled up for the duration of a single read, so the pin
// starts out floating every time. Reading it in the same instruction as the
// pinMode() call samples the pin before the internal pull-up (~45k) has
// charged the pin and trace capacitance, which produces phantom presses and
// missed presses -- the reason a modifier + entity button combination only
// registered some of the time. Give the pin time to settle first.
static void scanMatrix() {
    for (int y = 0; y < ROWS; y++) {
        pinMode(rowPins[y], OUTPUT);
        digitalWrite(rowPins[y], LOW);
        delayMicroseconds(MATRIX_SETTLE_US);

        for (int x = 0; x < COLS; x++) {
            pinMode(colPins[x], INPUT_PULLUP);
            delayMicroseconds(MATRIX_SETTLE_US);
            bool reading = digitalRead(colPins[x]) == LOW;

            if (reading != lastButtonState[y][x]) {
                lastDebounceTime[y][x] = millis();
            }

            if ((millis() - lastDebounceTime[y][x]) > DEBOUNCE_TIME &&
                reading != buttonState[y][x]) {
                buttonState[y][x] = reading;

                if (reading) {
                    buttonPressTime[y][x] = millis();
                    pressConsumedByAdjustment[y][x] = false;
                    if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                        upButtonPressed = true;
                    } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                        downButtonPressed = true;
                    }
                } else {
                    if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                        upButtonPressed = false;
                    } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                        downButtonPressed = false;
                    } else {
                        handleButtonRelease(x, y);
                    }
                }
            }

            lastButtonState[y][x] = reading;
            pinMode(colPins[x], INPUT);
        }

        pinMode(rowPins[y], INPUT);
    }
}

// Push the value the user dialled in to Home Assistant and hand the LEDs back
// to the normal entity grid.
//
// This deliberately does NOT restore a pre-gesture snapshot of entityStates:
// doing that threw away the brightness that was just set, so the deck fell
// back to the old level and the next gesture started from a stale value.
static void finalizeBrightnessAdjustment() {
    if (!isBrightnessAdjustmentMode) {
        return;
    }

    // Clear first so no second pass through the task loop can send a duplicate
    // service call for the same gesture.
    isBrightnessAdjustmentMode = false;

    if (lastAdjustedX >= 0 && lastAdjustedY >= 0) {
        for (int i = 0; i < NUM_MAPPINGS; i++) {
            if (entityMappings[i].x == lastAdjustedX && entityMappings[i].y == lastAdjustedY) {
                SERIAL_PRINTF("Sending final brightness or volume update for entity at (%d, %d)\n",
                              lastAdjustedX, lastAdjustedY);
                sendBrightnessOrVolumeUpdate(entityMappings[i].entity_id,
                                             currentAdjustmentBrightness,
                                             isMediaPlayer(entityMappings[i].entity_id));
                break;
            }
        }
    }

    lastAdjustedX = -1;
    lastAdjustedY = -1;

    refreshAllLEDs();
    isBrightnessUpdateInProgress = false;
    SERIAL_PRINTLN("Brightness adjustment finalized");
}

void buttonCheckTask(void * parameter) {
    SERIAL_PRINTLN("Button check task started");
    printMemoryUsage();

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(BUTTON_SCAN_INTERVAL_MS);
    xLastWakeTime = xTaskGetTickCount();

    bool childLockButtonsPressed = false;
    unsigned long childLockPressStartTime = 0;

    while (true) {
        esp_task_wdt_reset(); // Reset watchdog timer

        // Check for child lock activation/deactivation
        if (buttonState[CHILD_LOCK_BUTTON1_Y][CHILD_LOCK_BUTTON1_X] &&
            buttonState[CHILD_LOCK_BUTTON2_Y][CHILD_LOCK_BUTTON2_X]) {
            if (!childLockButtonsPressed) {
                childLockButtonsPressed = true;
                childLockPressStartTime = millis();
            } else if (millis() - childLockPressStartTime >= CHILD_LOCK_ACTIVATION_TIME) {
                toggleChildLock();
                childLockButtonsPressed = false;
            }
        } else {
            childLockButtonsPressed = false;
        }

        scanMatrix();

        // Brightness / volume gesture: hold UP or DOWN, then hold the entity
        // button. Runs one step per scan tick instead of spinning in a nested
        // blocking loop, so websocket traffic keeps flowing while ramping.
        bool modifierHeld = upButtonPressed || downButtonPressed;

        if (isBrightnessAdjustmentMode) {
            bool targetStillHeld = lastAdjustedX >= 0 && lastAdjustedY >= 0 &&
                                   buttonState[lastAdjustedY][lastAdjustedX];
            bool timedOut = millis() - brightnessAdjustmentStartTime > BRIGHTNESS_UPDATE_TIMEOUT_MS;

            if (timedOut) {
                SERIAL_PRINTLN("Brightness adjustment timeout reached");
            }

            if (!modifierHeld || !targetStillHeld || timedOut) {
                finalizeBrightnessAdjustment();
            } else {
                adjustBrightnessOrVolume(lastAdjustedX, lastAdjustedY, upButtonPressed);
            }
        } else if (modifierHeld) {
            // Lock on to the first pressed button that actually has something
            // to adjust; switches, scripts and covers are left alone.
            for (int y = 0; y < ROWS && !isBrightnessAdjustmentMode; y++) {
                for (int x = 0; x < COLS; x++) {
                    if (!buttonState[y][x] || isModifierButton(x, y)) {
                        continue;
                    }
                    if (adjustBrightnessOrVolume(x, y, upButtonPressed)) {
                        pressConsumedByAdjustment[y][x] = true;
                        break;
                    }
                }
            }
        }

        static unsigned long lastTaskMemoryPrint = 0;
        if (millis() - lastTaskMemoryPrint > 30000) {  // Print task memory usage every 30 seconds
            SERIAL_PRINTLN("Button check task running");
            printMemoryUsage();
            lastTaskMemoryPrint = millis();
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

bool adjustBrightnessOrVolume(int x, int y, bool increase) {
    static unsigned long lastAdjustmentTime = 0;

    if (isChildLockMode) {
        SERIAL_PRINTLN("Child lock mode active, ignoring brightness/volume adjustment");
        return false;
    }

    for (int i = 0; i < NUM_MAPPINGS; i++) {
        if (entityMappings[i].x != x || entityMappings[i].y != y) {
            continue;
        }

        const char* entity_id = entityMappings[i].entity_id;

        if (!isLight(entity_id) && !isMediaPlayer(entity_id)) {
            SERIAL_PRINTLN("Entity is neither a light nor a media player, skipping adjustment");
            return false;
        }

        // A bounded wait: the LED refresh on the websocket task holds this
        // briefly, and dropping one step of the ramp is better than stalling
        // the whole button task on it.
        if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(MUTEX_WAIT_MS)) != pdTRUE) {
            SERIAL_PRINTLN("Failed to acquire mutex in adjustBrightnessOrVolume");
            return isBrightnessAdjustmentMode;
        }

        if (!isBrightnessAdjustmentMode) {
            SERIAL_PRINTF("Entering adjustment mode for entity at (%d, %d)\n", x, y);
            isBrightnessAdjustmentMode = true;
            // Hold off Home Assistant state echoes so they don't fight the
            // value being dialled in, or repaint over the level bar.
            isBrightnessUpdateInProgress = true;
            currentAdjustmentBrightness = isMediaPlayer(entity_id)
                ? (int)(entityStates[y][x].volume * 255.0f)
                : entityStates[y][x].brightness;
            brightnessAdjustmentStartTime = millis();
            lastAdjustedX = x;
            lastAdjustedY = y;
            lastAdjustmentTime = 0; // take the first step immediately
        } else if (x != lastAdjustedX || y != lastAdjustedY) {
            // Another button was pressed mid-gesture; stay locked on the first.
            xSemaphoreGive(xMutex);
            return false;
        }

        unsigned long currentTime = millis();

        if (currentTime - lastAdjustmentTime >= BRIGHTNESS_ADJUST_INTERVAL_MS) {
            if (increase) {
                currentAdjustmentBrightness = min(255, currentAdjustmentBrightness + BRIGHTNESS_ADJUST_STEP);
            } else {
                currentAdjustmentBrightness = max(0, currentAdjustmentBrightness - BRIGHTNESS_ADJUST_STEP);
            }
            SERIAL_PRINTF("Adjusted value to %d\n", currentAdjustmentBrightness);

            if (isMediaPlayer(entity_id)) {
                entityStates[y][x].volume = currentAdjustmentBrightness / 255.0f;
            } else {
                entityStates[y][x].brightness = (uint8_t)currentAdjustmentBrightness;
            }

            displayBrightnessLevel(currentAdjustmentBrightness,
                                   entityStates[y][x].r,
                                   entityStates[y][x].g,
                                   entityStates[y][x].b);

            lastAdjustmentTime = currentTime;
        }

        xSemaphoreGive(xMutex);
        return true;
    }

    return false;
}

void toggleChildLock() {
    isChildLockMode = !isChildLockMode;
    SERIAL_PRINTF("Child lock mode %s\n", isChildLockMode ? "enabled" : "disabled");
    if (isChildLockMode) {
        showChildLockEnabledAnimation();
    } else {
        showChildLockDisabledAnimation();
    }

    // Show existing entity states after the animation
    refreshAllLEDs();
}
