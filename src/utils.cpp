#include "utils.h"
#include "entity_state.h"


bool connectToWiFi(unsigned long timeout) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(100);
        Serial.print(".");
    }

    return WiFi.status() == WL_CONNECTED;
}


void reconnectWebSocket() {
    webSocket.disconnect();
    webSocket.begin(HA_HOST, HA_PORT, "/api/websocket");
}

void printMemoryUsage() {
    SERIAL_PRINTF("Free heap: %d, Largest free block: %d\n", 
                  esp_get_free_heap_size(), 
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
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
            
            if (entityMappings[i].is_media_player) {
                doc["domain"] = "media_player";
                doc["service"] = "media_play_pause";
            } else {
                doc["domain"] = "homeassistant";
                doc["service"] = "toggle";
            }
            
            doc["target"]["entity_id"] = entityMappings[i].entity_id;

            String message;
            serializeJson(doc, message);
            webSocket.sendTXT(message);

            SERIAL_PRINTF("Toggling entity at (%d, %d): %s\n", x, y, entityMappings[i].entity_id);
            return;
        }
    }
    SERIAL_PRINTF("No entity found at (%d, %d) to toggle\n", x, y);
}
