#ifndef ENTITY_STATE_H
#define ENTITY_STATE_H

#include "common.h"
#include "config.h"
#include "constants.h"

struct EntityState {       
    bool is_on;
    uint8_t r, g, b;
    uint8_t brightness;
    int x, y;
};

extern EntityState entityStates[ROWS][COLS];
extern EntityState savedStates[ROWS][COLS];

void initializeEntityStates();
void saveCurrentStates();
void restoreStates();

#endif // ENTITY_STATE_H