#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include "common.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "led_control.h"
#include "entity_state.h"
#include "utils.h"
#include "animations.h"
#include "secrets.h"
#include "homeassistant_handler.h"

extern WebSocketsClient webSocket;

struct QueuedMessage {
    char* payload;
    size_t length;
};

extern QueuedMessage queuedMessages[MAX_QUEUED_MESSAGES];

void initializeWebSocket();
void reconnectWebSocket();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void queueWebSocketMessage(uint8_t* payload, size_t length);
void processQueuedMessages();
#endif // WEBSOCKET_HANDLER_H
