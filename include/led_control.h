#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "common.h"
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config.h"

extern Adafruit_NeoPixel strip;

int getLedIndex(int x, int y);
void updateLED(int x, int y, const JsonObject& state = JsonObject());
void displayBrightnessLevel(int brightness, uint8_t r, uint8_t g, uint8_t b);
uint32_t applyBrightnessScalar(uint32_t color);

#endif // LED_CONTROL_H