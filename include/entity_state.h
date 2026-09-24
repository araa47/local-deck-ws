#ifndef ENTITY_STATE_H
#define ENTITY_STATE_H

#include "common.h"
#include "config.h"
#include "constants.h"
#include "led_control.h"
#include "button_config.h"

struct EntityState {       
    bool is_on;
    uint8_t r, g, b;
    uint8_t brightness;
    int x, y;
    bool is_playing;
    float volume;
    // Covers and fans: 0-100 -- how far open, or fan speed.
    uint8_t level;
    // Can this one be sent to a level? Set from supported_features.
    bool supports_level;
};

// CoverEntityFeature.SET_POSITION. A cover that lacks this bit reports
// current_position but rejects cover.set_cover_position, so the ramp has to
// check before offering to drive it.
#define COVER_SET_POSITION 4

// FanEntityFeature.SET_SPEED. Same idea: a fan without it only turns on and off.
#define FAN_SET_SPEED 1

// isCover() lives here rather than alongside isLight()/isSwitch()/isMediaPlayer()
// in config.h on purpose: config.h is user-owned and gitignored, so adding a
// helper there would break every existing install on the next build. Note that
// the isSwitch() already in config.h also matches "cover.", which is what keeps
// a plain tap on a cover toggling it.
inline bool isCover(const char* entity_id) {
    return strncmp(entity_id, "cover.", 6) == 0;
}

inline bool isFan(const char* entity_id) {
    return strncmp(entity_id, "fan.", 4) == 0;
}

// Entities whose level is a 0-100 percentage in Home Assistant: cover
// position and fan speed. They share EntityState::level and the ramp.
inline bool isPercentEntity(const char* entity_id) {
    return isCover(entity_id) || isFan(entity_id);
}

// Domains added alongside the web UI, kept here for the same reason as
// isCover(). All of them are driven with homeassistant.toggle, like isSwitch().
inline bool isGenericToggle(const char* entity_id) {
    return strncmp(entity_id, "input_boolean.", 14) == 0 ||
           isFan(entity_id) ||
           strncmp(entity_id, "automation.", 11) == 0;
}

inline bool isScene(const char* entity_id) {
    return strncmp(entity_id, "scene.", 6) == 0;
}

// button.* and input_button.* have no on/off state; a press presses them.
inline bool isPressable(const char* entity_id) {
    return strncmp(entity_id, "button.", 7) == 0 ||
           strncmp(entity_id, "input_button.", 13) == 0;
}

// The same domains as isSupportedEntity(), as a Jinja list: the web UI's
// entity list has Home Assistant filter by it. Keep the two in step.
#define SUPPORTED_DOMAINS_JINJA \
    "['light','switch','script','cover','media_player','input_boolean'," \
    "'fan','automation','scene','button','input_button']"

// Can a button do something with this entity? Also what the web UI's entity
// list is filtered down to.
inline bool isSupportedEntity(const char* entity_id) {
    return isLight(entity_id) || isSwitch(entity_id) || isMediaPlayer(entity_id) ||
           isCover(entity_id) || isGenericToggle(entity_id) || isScene(entity_id) ||
           isPressable(entity_id);
}

extern EntityState entityStates[ROWS][COLS];

void initializeEntityStates();
void resetEntityState(int x, int y);
void refreshAllLEDs();

#endif // ENTITY_STATE_H
