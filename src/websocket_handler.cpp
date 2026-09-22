#include "websocket_handler.h"

WebSocketsClient webSocket;
QueuedMessage queuedMessages[MAX_QUEUED_MESSAGES];

// Ring buffer: queuedMessages is a FIFO, not a stack. queueHead is the oldest
// message still waiting, queuedMessageCount how many are queued.
static volatile int queueHead = 0;

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
            onHomeAssistantDisconnected();
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
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(QUEUE_MUTEX_WAIT_MS)) != pdTRUE) {
        SERIAL_PRINTLN("Failed to acquire queue mutex in queueWebSocketMessage");
        return;
    }

    if (queuedMessageCount >= MAX_QUEUED_MESSAGES) {
        // Full: drop the OLDEST message rather than the incoming one. These are
        // Home Assistant state snapshots, so the newest ones are what matter --
        // keeping stale ones is what left entities showing the wrong level.
        SERIAL_PRINTLN("Message queue is full, dropping oldest message");
        free(queuedMessages[queueHead].payload);
        queuedMessages[queueHead].payload = NULL;
        queueHead = (queueHead + 1) % MAX_QUEUED_MESSAGES;
        queuedMessageCount = queuedMessageCount - 1;
    }

    char* buffer = (char*)malloc(length + 1);
    if (!buffer) {
        SERIAL_PRINTLN("Failed to allocate memory for queued message");
        xSemaphoreGive(queueMutex);
        return;
    }

    int tail = (queueHead + queuedMessageCount) % MAX_QUEUED_MESSAGES;
    memcpy(buffer, payload, length);
    buffer[length] = '\0';
    queuedMessages[tail].payload = buffer;
    queuedMessages[tail].length = length;
    queuedMessageCount = queuedMessageCount + 1;
    SERIAL_PRINTF("Queued message. Count: %d, Length: %d\n", queuedMessageCount, length);

    xSemaphoreGive(queueMutex);
}


void processQueuedMessages() {
    int processedCount = 0;
    int remaining = 0;
    unsigned long startTime = millis();

    while (processedCount < MAX_MESSAGES_PER_DRAIN &&
           (millis() - startTime) < QUEUE_DRAIN_BUDGET_MS) {
        char* payload = NULL;
        size_t length = 0;

        // Pop under the mutex, then release it before handling the message:
        // handling can re-enter queueWebSocketMessage(), and this mutex is not
        // recursive.
        if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(QUEUE_MUTEX_WAIT_MS)) != pdTRUE) {
            SERIAL_PRINTLN("Failed to acquire queue mutex in processQueuedMessages");
            return;
        }

        if (queuedMessageCount > 0) {
            payload = queuedMessages[queueHead].payload;
            length = queuedMessages[queueHead].length;
            queuedMessages[queueHead].payload = NULL;
            queueHead = (queueHead + 1) % MAX_QUEUED_MESSAGES;
            queuedMessageCount = queuedMessageCount - 1;
        }

        // Snapshot under the lock; reading it after the release would report a
        // count that a concurrent enqueue has already moved on from.
        remaining = queuedMessageCount;

        xSemaphoreGive(queueMutex);

        if (!payload) {
            break; // queue drained
        }

        webSocketEvent(WStype_TEXT, (uint8_t*)payload, length);
        free(payload);
        processedCount++;
    }

    if (processedCount > 0) {
        SERIAL_PRINTF("Processed %d queued messages. Remaining: %d\n", processedCount, remaining);
    }
}
