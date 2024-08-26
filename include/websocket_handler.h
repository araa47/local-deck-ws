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

void initializeWebSocket();
void reconnectWebSocket();
void updateTimeAndCheckNightMode(const char* time_str);
void toggleEntity(int x, int y);
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void subscribeToEntities();
void queueWebSocketMessage(uint8_t* payload, size_t length);
void processQueuedMessages();
void sendBrightnessOrVolumeUpdate(const char* entity_id, int value, bool is_media_player);
#endif // WEBSOCKET_HANDLER_H
