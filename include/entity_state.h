#ifndef ENTITY_STATE_H
#define ENTITY_STATE_H

#include "common.h"
#include "config.h"
#include "constants.h"
#include "led_control.h"

struct EntityState {       
    bool is_on;
    uint8_t r, g, b;
    uint8_t brightness;
    int x, y;
    bool is_playing;
    float volume;
};

extern EntityState entityStates[ROWS][COLS];

void initializeEntityStates();
void refreshAllLEDs();

#endif // ENTITY_STATE_H
