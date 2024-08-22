#include "led_control.h"
#include "entity_state.h"

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int getLedIndex(int x, int y) {
    return y * COLS + x;
}


void updateLED(int x, int y, const JsonObject& state) {
    SERIAL_PRINTF("Updating LED at (%d, %d)\n", x, y);
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        EntityState& currentState = entityStates[y][x];

        if (!state.isNull()) {
            if (state.containsKey("s")) {
                currentState.is_on = (state["s"] == "on" || state["s"] == "playing");
            }

            JsonObject attributes = state["a"];
            if (attributes.isNull()) {
                // If attributes are null, this might be a switch or media player. Update only the on/off state.
                currentState.brightness = currentState.is_on ? 255 : 0;
            } else {
                if (currentState.is_on) {
                    if (attributes.containsKey("rgb_color")) {
                        JsonArray rgb = attributes["rgb_color"];
                        currentState.r = rgb[0];
                        currentState.g = rgb[1];
                        currentState.b = rgb[2];
                    }
                    if (attributes.containsKey("brightness")) {
                        currentState.brightness = attributes["brightness"];
                    } else {
                        currentState.brightness = 255; // Default to full brightness if not specified
                    }
                } else {
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
    int litLEDs = map(brightness, 0, 255, 0, NUM_LEDS);
    
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i < litLEDs) {
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
