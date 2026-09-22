#ifndef BUTTON_CONFIG_H
#define BUTTON_CONFIG_H

#include "common.h"
#include "config.h"
#include "constants.h"

// What each button is bound to, kept at runtime so the web UI can change it
// without a reflash.
//
// config.h's entityMappings[] is still the source of truth on a fresh device:
// it seeds this table on first boot, and "reset to defaults" in the web UI
// goes back to it. Once the web UI saves a layout, that layout is kept in NVS
// and wins over config.h from then on.

// Longest entity_id (including the terminator) a button can hold. Home
// Assistant allows longer, but ids that long are vanishingly rare and every
// byte here is paid 24 times.
#define ENTITY_ID_MAX_LEN 96

struct ButtonConfig {
    char entity_id[ENTITY_ID_MAX_LEN];  // "" = nothing assigned
    uint8_t r, g, b;
    uint8_t brightness;
};

// Load the saved layout from NVS, or seed it from config.h. Call once from
// setup(), before any task that reads button config is started.
void initButtonConfig();

// Readers get a copy, taken under a lock, rather than a pointer into the
// table: the web UI rewrites it from the loop task while the button task is
// reading it. Both return false for a button with nothing assigned.
bool getButtonConfig(int x, int y, ButtonConfig& out);
bool getButtonEntityId(int x, int y, char* out, size_t len);

void getAllButtonConfigs(ButtonConfig out[ROWS][COLS]);

// Replace the whole layout and persist it. Returns false if it could not be
// saved; the new layout is still live until the next reboot.
bool setAllButtonConfigs(const ButtonConfig in[ROWS][COLS]);

// Drop the saved layout and go back to config.h's entityMappings[].
bool resetButtonConfigToDefaults();

// true once a layout from the web UI has been saved over config.h's.
bool isButtonConfigCustomised();

// Is this something a button can be bound to? Checks shape only
// ("domain.object_id", lowercase, digits and underscores), not that Home
// Assistant actually has it.
bool isValidEntityId(const char* entity_id);

#endif // BUTTON_CONFIG_H
