#ifndef HOMEASSISTANT_HANDLER_H
#define HOMEASSISTANT_HANDLER_H

#include <Arduino.h>
#include "websocket_handler.h"
#include "led_control.h"
#include "entity_state.h"
#include "utils.h"
#include "config.h"
#include "animations.h"
#include <ArduinoJson.h>
#include "common.h"

void handleHomeAssistantMessage(uint8_t* payload, size_t length);
void updateTimeAndCheckNightMode(const char* time_str);
void toggleEntity(int x, int y);
void subscribeToEntities();
void sendBrightnessOrVolumeUpdate(const char* entity_id, int value, bool is_media_player);

#endif // HOMEASSISTANT_HANDLER_H
