#include "websocket_handler.h"

WebSocketsClient webSocket;
QueuedMessage queuedMessages[MAX_QUEUED_MESSAGES];

void initializeWebSocket() {
    webSocket.begin(HA_HOST, HA_PORT, "/api/websocket");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
}

void reconnectWebSocket() {
    webSocket.disconnect();
    webSocket.begin(HA_HOST, HA_PORT, "/api/websocket");
}


void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    SERIAL_PRINTF("WebSocket event type: %d\n", type);
    
    switch(type) {
        case WStype_DISCONNECTED:
            SERIAL_PRINTLN("WebSocket disconnected");
            showWebSocketConnectionFailedAnimation();
            break;
        case WStype_CONNECTED:
            SERIAL_PRINTLN("WebSocket connected");
            showWebSocketConnectedAnimation();
            webSocket.sendTXT("{\"type\": \"auth\", \"access_token\": \"" + String(HA_API_PASSWORD) + "\"}");
            break;
        case WStype_TEXT:
            handleHomeAssistantMessage(payload, length);
            break;
        case WStype_BIN:
        case WStype_ERROR:
            SERIAL_PRINTLN("WebSocket error occurred");
            break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
        default:
            SERIAL_PRINTF("Unhandled WebSocket event type: %d\n", type);
            break;
    }
}


void queueWebSocketMessage(uint8_t* payload, size_t length) {
    if (xSemaphoreTake(queueMutex, portMAX_DELAY) == pdTRUE) {
        if (queuedMessageCount < MAX_QUEUED_MESSAGES) {
            queuedMessages[queuedMessageCount].payload = (char*)malloc(length + 1);
            if (queuedMessages[queuedMessageCount].payload) {
                memcpy(queuedMessages[queuedMessageCount].payload, payload, length);
                queuedMessages[queuedMessageCount].payload[length] = '\0';
                queuedMessages[queuedMessageCount].length = length;
                queuedMessageCount++;
                SERIAL_PRINTF("Queued message. Count: %d, Length: %d\n", queuedMessageCount, length);
            } else {
                SERIAL_PRINTLN("Failed to allocate memory for queued message");
            }
        } else {
            SERIAL_PRINTLN("Message queue is full, dropping message");
        }
        xSemaphoreGive(queueMutex);
    }
}


void processQueuedMessages() {
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int processedCount = 0;
        unsigned long startTime = millis();
        while (queuedMessageCount > 0 && processedCount < 5 && (millis() - startTime) < 1000) {  // Process up to 5 messages or for 500ms max
            queuedMessageCount--;
            webSocketEvent(WStype_TEXT, (uint8_t*)queuedMessages[queuedMessageCount].payload, queuedMessages[queuedMessageCount].length);
            free(queuedMessages[queuedMessageCount].payload);
            processedCount++;
            SERIAL_PRINTF("Processed %d queued messages. Remaining: %d\n", processedCount, queuedMessageCount);
        }
        xSemaphoreGive(queueMutex);
    } else {
        SERIAL_PRINTLN("Failed to acquire queue mutex in processQueuedMessages");
    }
}
