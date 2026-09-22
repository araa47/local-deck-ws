#include "led_control.h"

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int getLedIndex(int x, int y) {
    return y * COLS + x;
}


void updateLED(int x, int y, const JsonObject& state) {
    SERIAL_PRINTF("Updating LED at (%d, %d)\n", x, y);
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        EntityState& currentState = entityStates[y][x];

        if (!state.isNull()) {
            // as<const char*>() yields nullptr for a non-string value, so the
            // null check has to happen before strcmp() dereferences it.
            const char* stateValue = state["s"].as<const char*>();
            if (stateValue) {
                currentState.is_on = (strcmp(stateValue, "on") == 0 ||
                                    strcmp(stateValue, "playing") == 0 ||
                                    strcmp(stateValue, "open") == 0);
            }

            JsonObject attributes = state["a"];
            if (attributes.isNull()) {
                // If attributes are null, this might be a switch or media player. Update only the on/off state.
                currentState.brightness = currentState.is_on ? 255 : 0;
            } else {
                char entity_id[ENTITY_ID_MAX_LEN];
                bool cover = getButtonEntityId(x, y, entity_id, sizeof(entity_id)) &&
                             isCover(entity_id);

                // Covers: track position and capability whether open or closed,
                // so a ramp can start from where the cover actually is. Only read
                // supported_features for covers -- the same attribute means
                // something entirely different on a light or media player.
                if (cover) {
                    if (!attributes["supported_features"].isNull()) {
                        int features = attributes["supported_features"];
                        currentState.supports_position = (features & COVER_SET_POSITION) != 0;
                    }
                    if (!attributes["current_position"].isNull()) {
                        currentState.position = attributes["current_position"];
                    }
                }

                if (currentState.is_on) {
                    if (!attributes["rgb_color"].isNull()) {
                        JsonArray rgb = attributes["rgb_color"];
                        currentState.r = rgb[0];
                        currentState.g = rgb[1];
                        currentState.b = rgb[2];
                    }
                    if (!attributes["brightness"].isNull()) {
                        currentState.brightness = attributes["brightness"];
                    } else if (!attributes["volume_level"].isNull()) {
                        currentState.volume = attributes["volume_level"];
                        currentState.brightness = currentState.volume * 255;
                    } else if (cover && !attributes["current_position"].isNull()) {
                        // Show how far open the cover is, not just that it is open.
                        currentState.brightness = (currentState.position * 255) / 100;
                    } else {
                        currentState.brightness = 255; // Default to full brightness if not specified
                    }
                } else {
                    // Store the volume level even when off so that when we start playing, it doesn't start at 0
                    if (!attributes["volume_level"].isNull()) {
                        currentState.volume = attributes["volume_level"];
                    }
                    currentState.brightness = 0;
                }
            }
        }

        float scaleFactor = isNightMode ? NIGHT_BRIGHTNESS_SCALE : 1.0f;
        
        uint32_t color;
        if (currentState.is_on) {
            color = strip.Color(
                map(currentState.r, 0, 255, 0, currentState.brightness * scaleFactor),
                map(currentState.g, 0, 255, 0, currentState.brightness * scaleFactor),
                map(currentState.b, 0, 255, 0, currentState.brightness * scaleFactor)
            );
        } else {
            color = strip.Color(0, 0, 0);
        }

        int ledIndex = getLedIndex(x, y);
        strip.setPixelColor(ledIndex, color);
        strip.show();

        xSemaphoreGive(xMutex);

        SERIAL_PRINTF("Updated LED at (%d, %d): R=%d, G=%d, B=%d, Brightness=%d, Scaled Brightness=%d, Is On=%d\n",
                      x, y, currentState.r, currentState.g, currentState.b, currentState.brightness,
                      (int)(currentState.brightness * scaleFactor), currentState.is_on);
    }
}

void displayBrightnessLevel(int brightness, uint8_t r, uint8_t g, uint8_t b) {
    float scaleFactor = isNightMode ? NIGHT_BRIGHTNESS_SCALE : 1.0f;
    int numLEDs = (brightness * NUM_LEDS) / 255;
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i < numLEDs) {
            uint8_t scaledR = (uint8_t)(r * scaleFactor);
            uint8_t scaledG = (uint8_t)(g * scaleFactor);
            uint8_t scaledB = (uint8_t)(b * scaleFactor);
            strip.setPixelColor(i, strip.Color(scaledR, scaledG, scaledB));
        } else {
            strip.setPixelColor(i, strip.Color(0, 0, 0));
        }
    }
    strip.show();
}

uint32_t applyBrightnessScalar(uint32_t color) {
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >> 8);
    uint8_t b = (uint8_t)color;
    
    r = (uint8_t)(r * ANIMATION_BRIGHTNESS_SCALAR);
    g = (uint8_t)(g * ANIMATION_BRIGHTNESS_SCALAR);
    b = (uint8_t)(b * ANIMATION_BRIGHTNESS_SCALAR);
    
    return strip.Color(r, g, b);
}
