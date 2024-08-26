#include "websocket_handler.h"
#include "led_control.h"
#include "entity_state.h"
#include "utils.h"
#include "config.h"
#include "animations.h"
#include "secrets.h"

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


void updateTimeAndCheckNightMode(const char* time_str) {
    SERIAL_PRINTF("Received time update: %s\n", time_str);

    if (!time_str || strlen(time_str) < 5) {
        SERIAL_PRINTLN("Invalid time string received");
        return;
    }

    int hour = 0, minute = 0;
    if (sscanf(time_str, "%d:%d", &hour, &minute) != 2) {
        SERIAL_PRINTF("Failed to parse time string: %s\n", time_str);
        return;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        SERIAL_PRINTF("Invalid time values: %02d:%02d\n", hour, minute);
        return;
    }

    bool newIsNightMode;
    if (NIGHT_START_HOUR > NIGHT_END_HOUR) {
        // Night mode spans midnight
        newIsNightMode = (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
    } else {
        // Night mode doesn't span midnight
        newIsNightMode = (hour >= NIGHT_START_HOUR && hour < NIGHT_END_HOUR);
    }
    if (newIsNightMode != isNightMode) {
        isNightMode = newIsNightMode;
        SERIAL_PRINTF("Night mode changed to: %s (Time: %02d:%02d)\n", isNightMode ? "ON" : "OFF", hour, minute);
        for (int y = 0; y < ROWS; y++) {
            for (int x = 0; x < COLS; x++) {
                updateLED(x, y);
            }
        }
    } else {
        SERIAL_PRINTF("Night mode unchanged: %s (Time: %02d:%02d)\n", isNightMode ? "ON" : "OFF", hour, minute);
    }
}


void toggleEntity(int x, int y) {
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        if (entityMappings[i].x == x && entityMappings[i].y == y) {
            DynamicJsonDocument doc(1024);
            doc["id"] = messageId++;
            doc["type"] = "call_service";
            
            if (isMediaPlayer(entityMappings[i].entity_id)) {
                doc["domain"] = "media_player";
                doc["service"] = "media_play_pause";
                SERIAL_PRINTF("Attempting to play/pause media player: %s\n", entityMappings[i].entity_id);
            } else if (isLight(entityMappings[i].entity_id)) {
                doc["domain"] = "light";
                doc["service"] = "toggle";
                SERIAL_PRINTF("Attempting to toggle light: %s\n", entityMappings[i].entity_id);
            } else if (isSwitch(entityMappings[i].entity_id)) {
                doc["domain"] = "homeassistant";
                doc["service"] = "toggle";
                SERIAL_PRINTF("Attempting to toggle switch: %s\n", entityMappings[i].entity_id);
            } else {
                SERIAL_PRINTF("Unknown entity type: %s\n", entityMappings[i].entity_id);
                return;
            }
            
            doc["target"]["entity_id"] = entityMappings[i].entity_id;

            String message;
            serializeJson(doc, message);
            SERIAL_PRINTF("Sending message: %s\n", message.c_str());

            bool sent = webSocket.sendTXT(message);
            if (sent) {
                SERIAL_PRINTF("Message sent successfully for entity at (%d, %d): %s\n", x, y, entityMappings[i].entity_id);
            } else {
                SERIAL_PRINTF("Failed to send message for entity at (%d, %d): %s\n", x, y, entityMappings[i].entity_id);
            }

            return;
        }
    }
    SERIAL_PRINTF("No entity found at (%d, %d) to toggle\n", x, y);
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
            {
                SERIAL_PRINTLN("Entering WStype_TEXT case");
                if (isBrightnessUpdateInProgress) {
                    queueWebSocketMessage(payload, length);
                    return;
                }
                SERIAL_PRINTF("Received WebSocket text message. Length: %d\n", length);
                SERIAL_PRINT("Message content: ");
                SERIAL_PRINTLN((char*)payload);  // Add this line to log the message content

                DynamicJsonDocument doc(JSON_BUFFER_SIZE);
                DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(10));
                
                if (error) {
                    SERIAL_PRINTF("deserializeJson() failed: %s\n", error.c_str());
                    SERIAL_PRINTF("Payload: %.*s\n", length, payload);
                    return;
                }

                if (doc["type"] == "auth_ok") {
                    SERIAL_PRINTLN("Authentication successful");
                    subscribeToEntities();
                } else if (doc["type"] == "event") {
                    SERIAL_PRINTLN("Received event type message");
                    JsonObject event = doc["event"];
                    if (event.containsKey("a")) {
                        JsonObject entities = event["a"];
                        for (JsonPair entity : entities) {
                            const char* entity_id = entity.key().c_str();
                            if (strcmp(entity_id, "sensor.time") == 0) {
                                updateTimeAndCheckNightMode(entity.value()["s"]);
                            } else {
                                for (int i = 0; i < NUM_MAPPINGS; i++) {
                                    if (strcmp(entity_id, entityMappings[i].entity_id) == 0) {
                                        updateLED(entityMappings[i].x, entityMappings[i].y, entity.value().as<JsonObject>());
                                        break;
                                    }
                                }
                            }
                        }
                    } else if (event.containsKey("c")) {
                        JsonObject changes = event["c"];
                        for (JsonPair change : changes) {
                            const char* entity_id = change.key().c_str();
                            JsonObject state = change.value();
                            if (strcmp(entity_id, "sensor.time") == 0) {
                                if (state.containsKey("+") && state["+"].containsKey("s")) {
                                    updateTimeAndCheckNightMode(state["+"]["s"]);
                                }
                            } else {
                                if (state.containsKey("+")) {
                                    state = state["+"];
                                }
                                for (int i = 0; i < NUM_MAPPINGS; i++) {
                                    if (strcmp(entity_id, entityMappings[i].entity_id) == 0) {
                                        updateLED(entityMappings[i].x, entityMappings[i].y, state);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                SERIAL_PRINTLN("Exiting WStype_TEXT case");
            }
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


void subscribeToEntities() {
    DynamicJsonDocument doc(1024);
    doc["id"] = messageId++;
    doc["type"] = "subscribe_entities";
    JsonArray entity_ids = doc.createNestedArray("entity_ids");
    
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        entity_ids.add(entityMappings[i].entity_id);
    }
    
    entity_ids.add("sensor.time");
    
    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);
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


void sendBrightnessOrVolumeUpdate(const char* entity_id, int value, bool is_media_player) {
    DynamicJsonDocument doc(1024);
    doc["id"] = messageId++;
    doc["type"] = "call_service";
    
    if (is_media_player) {
        doc["domain"] = "media_player";
        doc["service"] = "volume_set";
        doc["target"]["entity_id"] = entity_id;
        doc["service_data"]["volume_level"] = value / 255.0f;
        SERIAL_PRINTF("Adjusting volume for %s to %.2f\n", entity_id, value / 255.0f);
    } else {
        doc["domain"] = "light";
        doc["service"] = "turn_on";
        doc["target"]["entity_id"] = entity_id;
        doc["service_data"]["brightness"] = value;
        SERIAL_PRINTF("Adjusting brightness for %s to %d\n", entity_id, value);
    }

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);
}
