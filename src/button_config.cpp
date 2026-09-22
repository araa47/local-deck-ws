#include "button_config.h"
#include <Preferences.h>

#define PREFS_NAMESPACE "localdeck"
#define PREFS_KEY_BUTTONS "buttons"
// Bump when ButtonConfig or the grid changes shape; a saved layout from
// another version is then ignored instead of being misread.
#define BUTTON_CONFIG_VERSION 1

struct StoredButtonConfig {
    uint16_t version;
    uint8_t rows;
    uint8_t cols;
    ButtonConfig buttons[ROWS][COLS];
};

static ButtonConfig buttonConfigs[ROWS][COLS];
static SemaphoreHandle_t configMutex = NULL;
static bool customised = false;

// Every function below holds configMutex only for a copy, and never while
// taking another lock, so it cannot take part in a lock-order deadlock with
// xMutex.
static void lockConfig() {
    xSemaphoreTake(configMutex, portMAX_DELAY);
}

static void unlockConfig() {
    xSemaphoreGive(configMutex);
}

static void loadDefaults(ButtonConfig out[ROWS][COLS]) {
    memset(out, 0, sizeof(ButtonConfig) * ROWS * COLS);
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        int x = entityMappings[i].x;
        int y = entityMappings[i].y;
        if (x < 0 || x >= COLS || y < 0 || y >= ROWS) {
            SERIAL_PRINTF("Ignoring config.h mapping for %s: (%d, %d) is off the grid\n",
                          entityMappings[i].entity_id, x, y);
            continue;
        }
        ButtonConfig& cfg = out[y][x];
        if (cfg.entity_id[0] != '\0') {
            continue; // first mapping for a button wins, as it always has
        }
        strlcpy(cfg.entity_id, entityMappings[i].entity_id, sizeof(cfg.entity_id));
        cfg.r = entityMappings[i].default_r;
        cfg.g = entityMappings[i].default_g;
        cfg.b = entityMappings[i].default_b;
        cfg.brightness = entityMappings[i].default_brightness;
    }
}

static bool loadSaved(ButtonConfig out[ROWS][COLS]) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        return false;  // namespace does not exist yet: nothing saved
    }

    // Heap, not stack: this is ~2.4KB and setup() runs on the loop task.
    StoredButtonConfig* stored = (StoredButtonConfig*)malloc(sizeof(StoredButtonConfig));
    bool ok = false;
    if (stored && prefs.getBytesLength(PREFS_KEY_BUTTONS) == sizeof(StoredButtonConfig) &&
        prefs.getBytes(PREFS_KEY_BUTTONS, stored, sizeof(StoredButtonConfig)) == sizeof(StoredButtonConfig) &&
        stored->version == BUTTON_CONFIG_VERSION && stored->rows == ROWS && stored->cols == COLS) {
        memcpy(out, stored->buttons, sizeof(stored->buttons));
        // Never trust a stored string to be terminated.
        for (int y = 0; y < ROWS; y++) {
            for (int x = 0; x < COLS; x++) {
                out[y][x].entity_id[ENTITY_ID_MAX_LEN - 1] = '\0';
            }
        }
        ok = true;
    }
    free(stored);
    prefs.end();
    return ok;
}

static bool save(const ButtonConfig in[ROWS][COLS]) {
    StoredButtonConfig* stored = (StoredButtonConfig*)malloc(sizeof(StoredButtonConfig));
    if (!stored) {
        SERIAL_PRINTLN("Failed to allocate memory to save button config");
        return false;
    }
    stored->version = BUTTON_CONFIG_VERSION;
    stored->rows = ROWS;
    stored->cols = COLS;
    memcpy(stored->buttons, in, sizeof(stored->buttons));

    Preferences prefs;
    bool ok = prefs.begin(PREFS_NAMESPACE, false) &&
              prefs.putBytes(PREFS_KEY_BUTTONS, stored, sizeof(StoredButtonConfig)) == sizeof(StoredButtonConfig);
    prefs.end();
    free(stored);

    SERIAL_PRINTLN(ok ? "Button config saved" : "Failed to save button config");
    return ok;
}

void initButtonConfig() {
    if (configMutex == NULL) {
        configMutex = xSemaphoreCreateMutex();
    }

    if (loadSaved(buttonConfigs)) {
        customised = true;
        SERIAL_PRINTLN("Loaded button config saved from the web UI");
    } else {
        loadDefaults(buttonConfigs);
        customised = false;
        SERIAL_PRINTLN("Using button config from config.h");
    }
}

bool getButtonConfig(int x, int y, ButtonConfig& out) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) {
        return false;
    }
    lockConfig();
    out = buttonConfigs[y][x];
    unlockConfig();
    return out.entity_id[0] != '\0';
}

bool getButtonEntityId(int x, int y, char* out, size_t len) {
    if (len == 0) {
        return false;
    }
    out[0] = '\0';
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) {
        return false;
    }
    lockConfig();
    strlcpy(out, buttonConfigs[y][x].entity_id, len);
    unlockConfig();
    return out[0] != '\0';
}

void getAllButtonConfigs(ButtonConfig out[ROWS][COLS]) {
    lockConfig();
    memcpy(out, buttonConfigs, sizeof(buttonConfigs));
    unlockConfig();
}

bool setAllButtonConfigs(const ButtonConfig in[ROWS][COLS]) {
    lockConfig();
    memcpy(buttonConfigs, in, sizeof(buttonConfigs));
    customised = true;
    unlockConfig();
    // Written outside the lock: NVS writes can take tens of milliseconds, and
    // the button task should not wait on flash.
    return save(in);
}

bool resetButtonConfigToDefaults() {
    ButtonConfig* defaults = (ButtonConfig*)malloc(sizeof(buttonConfigs));
    if (!defaults) {
        SERIAL_PRINTLN("Failed to allocate memory to reset button config");
        return false;
    }
    loadDefaults((ButtonConfig(*)[COLS])defaults);

    Preferences prefs;
    if (prefs.begin(PREFS_NAMESPACE, false)) {
        prefs.remove(PREFS_KEY_BUTTONS);
        prefs.end();
    }

    lockConfig();
    memcpy(buttonConfigs, defaults, sizeof(buttonConfigs));
    customised = false;
    unlockConfig();
    free(defaults);
    return true;
}

bool isButtonConfigCustomised() {
    return customised;
}

bool isValidEntityId(const char* entity_id) {
    if (!entity_id) {
        return false;
    }
    size_t len = strlen(entity_id);
    if (len < 3 || len >= ENTITY_ID_MAX_LEN) {
        return false;
    }
    const char* dot = strchr(entity_id, '.');
    if (!dot || dot == entity_id || dot[1] == '\0' || strchr(dot + 1, '.')) {
        return false;
    }
    for (const char* c = entity_id; *c; c++) {
        if (*c != '.' && *c != '_' && !(*c >= 'a' && *c <= 'z') && !(*c >= '0' && *c <= '9')) {
            return false;
        }
    }
    return true;
}
