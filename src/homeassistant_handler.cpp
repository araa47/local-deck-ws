#include "homeassistant_handler.h"

// The id of the live subscribe_entities request; Home Assistant tags the
// subscription with it, and it is what unsubscribe_events needs.
static unsigned long entitySubscriptionId = 0;
static bool haAuthenticated = false;

// A single entity can sit on more than one button, so update all of them.
static void updateButtonsForEntity(const char* entity_id, const JsonObject& state) {
    char assigned[ENTITY_ID_MAX_LEN];
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (getButtonEntityId(x, y, assigned, sizeof(assigned)) &&
                strcmp(entity_id, assigned) == 0) {
                updateLED(x, y, state);
            }
        }
    }
}

bool isHomeAssistantConnected() {
    return haAuthenticated;
}

void onHomeAssistantDisconnected() {
    // The subscription dies with the connection; auth_ok on the next one
    // subscribes afresh.
    haAuthenticated = false;
    entitySubscriptionId = 0;
}

void handleHomeAssistantMessage(uint8_t* payload, size_t length) {
    SERIAL_PRINTLN("Entering handleHomeAssistantMessage");
    if (isBrightnessUpdateInProgress) {
        queueWebSocketMessage(payload, length);
        return;
    }
    SERIAL_PRINTF("Received WebSocket text message. Length: %d\n", length);
    SERIAL_PRINT("Message content: ");
    SERIAL_PRINTLN((char*)payload);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(10));
    
    if (error) {
        SERIAL_PRINTF("deserializeJson() failed: %s\n", error.c_str());
        SERIAL_PRINTF("Payload: %.*s\n", length, payload);
        return;
    }

    if (doc["type"] == "auth_ok") {
        SERIAL_PRINTLN("Authentication successful");
        haAuthenticated = true;
        subscribeToEntities();
    } else if (doc["type"] == "event") {
        SERIAL_PRINTLN("Received event type message");
        JsonObject event = doc["event"];
        if (!event["a"].isNull()) {
            JsonObject entities = event["a"];
            for (JsonPair entity : entities) {
                const char* entity_id = entity.key().c_str();
                if (strcmp(entity_id, "sensor.time") == 0) {
                    updateTimeAndCheckNightMode(entity.value()["s"]);
                } else {
                    updateButtonsForEntity(entity_id, entity.value().as<JsonObject>());
                }
            }
        } else if (!event["c"].isNull()) {
            JsonObject changes = event["c"];
            for (JsonPair change : changes) {
                const char* entity_id = change.key().c_str();
                JsonObject state = change.value();
                if (strcmp(entity_id, "sensor.time") == 0) {
                    if (!state["+"]["s"].isNull()) {
                        updateTimeAndCheckNightMode(state["+"]["s"]);
                    }
                } else {
                    if (!state["+"].isNull()) {
                        state = state["+"];
                    }
                    updateButtonsForEntity(entity_id, state);
                }
            }
        }
    }
    SERIAL_PRINTLN("Exiting handleHomeAssistantMessage");
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
        refreshAllLEDs();
    } else {
        SERIAL_PRINTF("Night mode unchanged: %s (Time: %02d:%02d)\n", isNightMode ? "ON" : "OFF", hour, minute);
    }
}



void toggleEntity(int x, int y) {
    char entity_id[ENTITY_ID_MAX_LEN];
    if (!getButtonEntityId(x, y, entity_id, sizeof(entity_id))) {
        SERIAL_PRINTF("No entity found at (%d, %d) to toggle\n", x, y);
        return;
    }

    JsonDocument doc;
    doc["id"] = messageId++;
    doc["type"] = "call_service";

    if (isMediaPlayer(entity_id)) {
        doc["domain"] = "media_player";
        doc["service"] = "media_play_pause";
        SERIAL_PRINTF("Attempting to play/pause media player: %s\n", entity_id);
    } else if (isLight(entity_id)) {
        doc["domain"] = "light";
        doc["service"] = "toggle";
        SERIAL_PRINTF("Attempting to toggle light: %s\n", entity_id);
    } else if (isSwitch(entity_id) || isGenericToggle(entity_id)) {
        doc["domain"] = "homeassistant";
        doc["service"] = "toggle";
        SERIAL_PRINTF("Attempting to toggle switch: %s\n", entity_id);
    } else if (isScene(entity_id)) {
        doc["domain"] = "scene";
        doc["service"] = "turn_on";
        SERIAL_PRINTF("Attempting to activate scene: %s\n", entity_id);
    } else if (isPressable(entity_id)) {
        // "button" or "input_button": both call their own domain's press.
        doc["domain"] = String(entity_id).substring(0, strchr(entity_id, '.') - entity_id);
        doc["service"] = "press";
        SERIAL_PRINTF("Attempting to press button: %s\n", entity_id);
    } else {
        SERIAL_PRINTF("Unknown entity type: %s\n", entity_id);
        return;
    }

    doc["target"]["entity_id"] = entity_id;

    String message;
    serializeJson(doc, message);
    SERIAL_PRINTF("Sending message: %s\n", message.c_str());

    bool sent = webSocket.sendTXT(message);
    if (sent) {
        SERIAL_PRINTF("Message sent successfully for entity at (%d, %d): %s\n", x, y, entity_id);
    } else {
        SERIAL_PRINTF("Failed to send message for entity at (%d, %d): %s\n", x, y, entity_id);
    }
}


void sendLevelUpdate(const char* entity_id, int value) {
    JsonDocument doc;
    doc["id"] = messageId++;
    doc["type"] = "call_service";

    bool is_media_player = isMediaPlayer(entity_id);

    if (isCover(entity_id)) {
        // The ramp works in 0-255 like everything else; covers take 0-100.
        int position = (value * 100 + 127) / 255;
        doc["domain"] = "cover";
        doc["service"] = "set_cover_position";
        doc["target"]["entity_id"] = entity_id;
        doc["service_data"]["position"] = position;
        SERIAL_PRINTF("Setting position for %s to %d\n", entity_id, position);
    } else if (is_media_player) {
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


void subscribeToEntities() {
    JsonDocument doc;
    entitySubscriptionId = messageId++;
    doc["id"] = entitySubscriptionId;
    doc["type"] = "subscribe_entities";
    JsonArray entity_ids = doc["entity_ids"].to<JsonArray>();

    char entity_id[ENTITY_ID_MAX_LEN];
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (!getButtonEntityId(x, y, entity_id, sizeof(entity_id))) {
                continue;
            }
            bool already = false;
            for (JsonVariant existing : entity_ids) {
                if (strcmp(existing.as<const char*>(), entity_id) == 0) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                entity_ids.add(entity_id);
            }
        }
    }

    // Always present, and load-bearing beyond night mode: Home Assistant reads
    // an empty entity_ids as "every entity", which would flood the deck.
    entity_ids.add("sensor.time");

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);
}

void resubscribeToEntities() {
    if (!haAuthenticated) {
        return; // auth_ok will subscribe with the current layout
    }

    if (entitySubscriptionId != 0) {
        JsonDocument doc;
        doc["id"] = messageId++;
        doc["type"] = "unsubscribe_events";
        doc["subscription"] = entitySubscriptionId;
        String message;
        serializeJson(doc, message);
        webSocket.sendTXT(message);
    }

    // The new subscription opens with the full state of every entity in it,
    // which is what repaints the buttons that just changed.
    subscribeToEntities();
}
