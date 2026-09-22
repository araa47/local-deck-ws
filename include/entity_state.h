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
    uint8_t position;          // covers: 0-100, 0 = closed
    bool supports_position;    // covers: can this one be sent to a position?
};

// CoverEntityFeature.SET_POSITION. A cover that lacks this bit reports
// current_position but rejects cover.set_cover_position, so the ramp has to
// check before offering to drive it.
#define COVER_SET_POSITION 4

// isCover() lives here rather than alongside isLight()/isSwitch()/isMediaPlayer()
// in config.h on purpose: config.h is user-owned and gitignored, so adding a
// helper there would break every existing install on the next build. Note that
// the isSwitch() already in config.h also matches "cover.", which is what keeps
// a plain tap on a cover toggling it.
inline bool isCover(const char* entity_id) {
    return strncmp(entity_id, "cover.", 6) == 0;
}

extern EntityState entityStates[ROWS][COLS];

void initializeEntityStates();
void refreshAllLEDs();
const char* entityIdAt(int x, int y);

#endif // ENTITY_STATE_H
