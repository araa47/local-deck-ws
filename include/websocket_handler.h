#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include "common.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"

extern WebSocketsClient webSocket;

struct QueuedMessage {
    char* payload;
    size_t length;
};

extern QueuedMessage queuedMessages[MAX_QUEUED_MESSAGES];

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void subscribeToEntities();
void queueWebSocketMessage(uint8_t* payload, size_t length);
void processQueuedMessages();
void sendBrightnessUpdate(const char* entity_id, int brightness);

#endif // WEBSOCKET_HANDLER_H