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
// Swap the entity subscription for one matching the current button layout.
void resubscribeToEntities();
bool isHomeAssistantConnected();
void onHomeAssistantDisconnected();
void sendLevelUpdate(const char* entity_id, int value);

#endif // HOMEASSISTANT_HANDLER_H
