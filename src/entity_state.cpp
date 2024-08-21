#include "entity_state.h"
#include "led_control.h"

EntityState entityStates[ROWS][COLS];
EntityState savedStates[ROWS][COLS];


void initializeEntityStates() {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            entityStates[y][x] = {false, 255, 255, 255, 255, x, y};
        }
    }
    
    // Set registered entities
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        int x = entityMappings[i].x;
        int y = entityMappings[i].y;
        entityStates[y][x].is_on = false;
        entityStates[y][x].r = entityMappings[i].default_r;
        entityStates[y][x].g = entityMappings[i].default_g;
        entityStates[y][x].b = entityMappings[i].default_b;
        entityStates[y][x].brightness = entityMappings[i].default_brightness;
    }
}

void saveCurrentStates() {
    memcpy(savedStates, entityStates, sizeof(entityStates));
}

void restoreStates() {
    memcpy(entityStates, savedStates, sizeof(entityStates));
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            updateLED(x, y);
        }
    }
}