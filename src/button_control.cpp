#include "button_control.h"
#include "led_control.h"
#include "entity_state.h"
#include "utils.h"
#include <esp_task_wdt.h>


void buttonCheckTask(void * parameter) {
    SERIAL_PRINTLN("Button check task started");
    printMemoryUsage();

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset(); // Reset watchdog timer

        for (int y = 0; y < ROWS; y++) {
            pinMode(rowPins[y], OUTPUT);
            digitalWrite(rowPins[y], LOW);

            for (int x = 0; x < COLS; x++) {
                pinMode(colPins[x], INPUT_PULLUP);
                bool reading = digitalRead(colPins[x]) == LOW;

                if (reading != lastButtonState[y][x]) {
                    lastDebounceTime[y][x] = millis();
                }

                if ((millis() - lastDebounceTime[y][x]) > DEBOUNCE_TIME) {
                    if (reading != buttonState[y][x]) {
                        buttonState[y][x] = reading;

                        if (buttonState[y][x] == true) {
                            buttonPressTime[y][x] = millis();
                            
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
                                unsigned long pressDuration = millis() - buttonPressTime[y][x];
                                
                                if (pressDuration < LONG_PRESS_TIME && !upButtonPressed && !downButtonPressed) {
                                    toggleEntity(x, y);
                                } else {
                                    SERIAL_PRINTF("Long press detected at (x: %d, y: %d)\n", x, y);
                                }
                            }
                        }
                    }
                }

                lastButtonState[y][x] = reading;
                pinMode(colPins[x], INPUT);
            }

            pinMode(rowPins[y], INPUT);
        }

        if ((upButtonPressed || downButtonPressed) && (millis() - lastBrightnessAdjustTime > BRIGHTNESS_ADJUST_INTERVAL)) {
            SERIAL_PRINTLN("Entering brightness adjustment block");
            isBrightnessUpdateInProgress = true;
            unsigned long adjustmentStartTime = millis();

            while ((upButtonPressed || downButtonPressed) && (millis() - adjustmentStartTime <= BRIGHTNESS_UPDATE_TIMEOUT_MS)) {
                for (int y = 0; y < ROWS; y++) {
                    for (int x = 0; x < COLS; x++) {
                        if (buttonState[y][x] && !(x == UP_BUTTON_X && y == UP_BUTTON_Y) && !(x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y)) {
                            SERIAL_PRINTF("Calling adjustBrightness for button at (%d, %d)\n", x, y);
                            adjustBrightness(x, y, upButtonPressed);
                        }
                    }
                }

                // Add a small delay to prevent overwhelming the system
                vTaskDelay(pdMS_TO_TICKS(10));

                // Update button states
                updateButtonStates();
            }

            lastBrightnessAdjustTime = millis();
            isBrightnessUpdateInProgress = false;
            if (millis() - adjustmentStartTime > BRIGHTNESS_UPDATE_TIMEOUT_MS) {
                SERIAL_PRINTLN("Brightness adjustment timeout reached");
                isBrightnessAdjustmentMode = false;
                restoreStates();
            } else {
                // Finalize the brightness adjustment
                for (int i = 0; i < NUM_MAPPINGS; i++) {
                    if (entityMappings[i].x == lastAdjustedX && entityMappings[i].y == lastAdjustedY) {
                        SERIAL_PRINTF("Sending final brightness update for entity at (%d, %d)\n", lastAdjustedX, lastAdjustedY);
                        sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                        entityStates[lastAdjustedY][lastAdjustedX].brightness = currentAdjustmentBrightness;
                        break;
                    }
                }
            }
            SERIAL_PRINTLN("Exiting brightness adjustment block");
        } else if (!upButtonPressed && !downButtonPressed && isBrightnessAdjustmentMode) {
            SERIAL_PRINTLN("Finalizing brightness adjustment");
            isBrightnessUpdateInProgress = true;
            for (int i = 0; i < NUM_MAPPINGS; i++) {
                if (entityMappings[i].x == lastAdjustedX && entityMappings[i].y == lastAdjustedY) {
                    SERIAL_PRINTF("Sending final brightness update for entity at (%d, %d)\n", lastAdjustedX, lastAdjustedY);
                    sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                    entityStates[lastAdjustedY][lastAdjustedX].brightness = currentAdjustmentBrightness;
                    break;
                }
            }
            isBrightnessAdjustmentMode = false;
            isBrightnessUpdateInProgress = false;
            restoreStates();
            SERIAL_PRINTLN("Brightness adjustment finalized");
        }

        static unsigned long lastTaskMemoryPrint = 0;
        if (millis() - lastTaskMemoryPrint > 30000) {  // Print task memory usage every 30 seconds
            SERIAL_PRINTLN("Button check task running");
            printMemoryUsage();
            lastTaskMemoryPrint = millis();
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        vTaskDelay(pdMS_TO_TICKS(10)); // Add a small delay to prevent task starvation
    }
}


bool adjustBrightness(int x, int y, bool increase) {
    SERIAL_PRINTF("Entering adjustBrightness: x=%d, y=%d, increase=%d\n", x, y, increase);
    static unsigned long lastAdjustmentTime = 0;
    const unsigned long ADJUSTMENT_INTERVAL = 50; // Adjust every 50ms for smoother transitions

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        SERIAL_PRINTLN("Mutex acquired in adjustBrightness");
        for (int i = 0; i < NUM_MAPPINGS; i++) {
            if (entityMappings[i].x == x && entityMappings[i].y == y && !entityMappings[i].is_switch) {
                SERIAL_PRINTF("Found matching entity mapping at index %d\n", i);
                if (!isBrightnessAdjustmentMode) {
                    SERIAL_PRINTLN("Entering brightness adjustment mode");
                    isBrightnessAdjustmentMode = true;
                    saveCurrentStates();
                    currentAdjustmentBrightness = entityStates[y][x].brightness;
                    brightnessAdjustmentStartTime = millis();
                    lastAdjustedX = x;
                    lastAdjustedY = y;
                }

                // Only adjust brightness if enough time has passed since the last adjustment
                if (millis() - lastAdjustmentTime >= ADJUSTMENT_INTERVAL) {
                    if (increase) {
                        currentAdjustmentBrightness = min(255, currentAdjustmentBrightness + BRIGHTNESS_STEP);
                    } else {
                        currentAdjustmentBrightness = max(0, currentAdjustmentBrightness - BRIGHTNESS_STEP);
                    }
                    SERIAL_PRINTF("Adjusted brightness to %d\n", currentAdjustmentBrightness);

                    // Pass the color information to displayBrightnessLevel
                    displayBrightnessLevel(currentAdjustmentBrightness, 
                                           entityStates[y][x].r, 
                                           entityStates[y][x].g, 
                                           entityStates[y][x].b);

                    lastAdjustmentTime = millis();
                }

                break;
            }
        }
        xSemaphoreGive(xMutex);
        SERIAL_PRINTLN("Mutex released in adjustBrightness");
    } else {
        SERIAL_PRINTLN("Failed to acquire mutex in adjustBrightness");
    }
    SERIAL_PRINTLN("Exiting adjustBrightness");
    return false; // We're no longer checking for timeout here
}


// Add this new function to update button states
void updateButtonStates() {
    for (int y = 0; y < ROWS; y++) {
        pinMode(rowPins[y], OUTPUT);
        digitalWrite(rowPins[y], LOW);

        for (int x = 0; x < COLS; x++) {
            pinMode(colPins[x], INPUT_PULLUP);
            bool reading = digitalRead(colPins[x]) == LOW;

            if (reading != lastButtonState[y][x]) {
                lastDebounceTime[y][x] = millis();
            }

            if ((millis() - lastDebounceTime[y][x]) > DEBOUNCE_TIME) {
                if (reading != buttonState[y][x]) {
                    buttonState[y][x] = reading;

                    if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                        upButtonPressed = buttonState[y][x];
                    } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                        downButtonPressed = buttonState[y][x];
                    }
                }
            }

            lastButtonState[y][x] = reading;
            pinMode(colPins[x], INPUT);
        }

        pinMode(rowPins[y], INPUT);
    }
}