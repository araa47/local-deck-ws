#include "animations.h"
#include "led_control.h"

void showConnectingAnimation() {
    SERIAL_PRINTLN("Showing connecting animation (Blue)");
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, applyBrightnessScalar(COLOR_BLUE));
        strip.show();
        delay(ANIMATION_DELAY_SHORT);
    }
    strip.clear();
    strip.show();
}

void showWiFiConnectedAnimation() {
    SERIAL_PRINTLN("Showing WiFi connected animation (Green)");
    for (int i = 0; i < ANIMATION_REPEAT_COUNT; i++) {
        for (int j = 0; j < NUM_LEDS; j++) {
            strip.setPixelColor(j, applyBrightnessScalar(COLOR_GREEN));
        }
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
        strip.clear();
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
    }
}

void showWebSocketConnectedAnimation() {
    SERIAL_PRINTLN("Showing WebSocket connected animation (Cyan and Yellow)");
    for (int i = 0; i < ANIMATION_REPEAT_COUNT; i++) {
        for (int j = 0; j < NUM_LEDS; j++) {
            strip.setPixelColor(j, applyBrightnessScalar(j % 2 == 0 ? COLOR_CYAN : COLOR_YELLOW));
        }
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
        strip.clear();
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
    }
}

void showConnectionFailedAnimation() {
    SERIAL_PRINTLN("Showing connection failed animation (Red)");
    strip.clear();
    for (int j = 0; j < NUM_LEDS; j++) {
        strip.setPixelColor(j, applyBrightnessScalar(COLOR_RED));
    }
    strip.show();
}

void showWebSocketConnectionFailedAnimation() {
    SERIAL_PRINTLN("Showing WebSocket connection failed animation (Red and Orange)");
    strip.clear();
    for (int j = 0; j < NUM_LEDS; j++) {
        strip.setPixelColor(j, applyBrightnessScalar(j % 2 == 0 ? COLOR_RED : COLOR_ORANGE));
    }
    strip.show();
}
