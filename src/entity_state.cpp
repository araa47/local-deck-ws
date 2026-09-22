#include "entity_state.h"

EntityState entityStates[ROWS][COLS];


void initializeEntityStates() {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            resetEntityState(x, y);
        }
    }
}

// Forget what is known about a button's entity and go back to its configured
// defaults, until Home Assistant reports the real state.
void resetEntityState(int x, int y) {
    ButtonConfig cfg;
    bool assigned = getButtonConfig(x, y, cfg);

    // Taken after the config copy, never around it: see button_config.cpp.
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        EntityState& state = entityStates[y][x];
        state = {false, 255, 255, 255, 255, x, y};
        if (assigned) {
            state.r = cfg.r;
            state.g = cfg.g;
            state.b = cfg.b;
            state.brightness = cfg.brightness;
        }
        xSemaphoreGive(xMutex);
    }
}

// Repaint the whole grid from the current state. This replaces the old
// saveCurrentStates()/restoreStates() pair, which rolled entityStates back to
// a pre-gesture snapshot and so discarded the brightness the user had just
// dialled in.
void refreshAllLEDs() {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            updateLED(x, y);
        }
    }
}
