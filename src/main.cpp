#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "secrets.h" 
#include "config.h"



const size_t JSON_BUFFER_SIZE = 16384; // 16KB
struct EntityState {
    bool is_on;
    uint8_t r, g, b;
    uint8_t brightness;
};


const int rowPins[ROWS] = {21, 20, 3, 7};
const int colPins[COLS] = {0, 1, 10, 4, 5, 6};

WebSocketsClient webSocket;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
EntityState entityStates[NUM_MAPPINGS];

unsigned long messageId = 1;

// Function prototypes
int getLedIndex(int x, int y);
void updateLED(const char* entity_id, JsonObject state);
void initializeEntityStates();
void subscribeToEntities();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void toggleEntity(const char* entity_id);
void buttonCheckTask(void * parameter);
void adjustBrightness(int x, int y, bool increase);

// Add these global variables
unsigned long lastDebounceTime[ROWS][COLS] = {{0}};
bool buttonState[ROWS][COLS] = {{false}};
bool lastButtonState[ROWS][COLS] = {{false}};
unsigned long buttonPressTime[ROWS][COLS] = {{0}};
bool upButtonPressed = false;
bool downButtonPressed = false;
unsigned long lastBrightnessAdjustTime = 0;

// Add these new function prototypes
void showConnectingAnimation();
void showWiFiConnectedAnimation();
void showWebSocketConnectedAnimation();
void showConnectionFailedAnimation();
void showWebSocketConnectionFailedAnimation();

void setup() {
    Serial.begin(115200);
    strip.begin();
    strip.show();

    showConnectingAnimation();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(100);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected to WiFi");
        showWiFiConnectedAnimation();

        webSocket.begin(HA_HOST, HA_PORT, "/api/websocket");
        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(5000);

        initializeEntityStates();
    } else {
        Serial.println("\nFailed to connect to WiFi");
        showConnectionFailedAnimation();
    }

    xTaskCreate(
        buttonCheckTask,
        "ButtonCheckTask",
        4096,
        NULL,
        1,
        NULL
    );
}

void loop() {
    webSocket.loop();

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
        }
        webSocket.disconnect();
        webSocket.begin(HA_HOST, HA_PORT, "/api/websocket");
    }
    
    delay(10);
}

int getLedIndex(int x, int y) {
    return y * COLS + x;
}

void updateLED(const char* entity_id, JsonObject state) {
    const EntityMapping* mapping = nullptr;
    int mappingIndex = -1;

    for (int i = 0; i < NUM_MAPPINGS; i++) {
        if (strcmp(entity_id, entityMappings[i].entity_id) == 0) {
            mapping = &entityMappings[i];
            mappingIndex = i;
            break;
        }
    }

    if (!mapping) return;

    EntityState& currentState = entityStates[mappingIndex];

    if (state.containsKey("s")) {
        currentState.is_on = (state["s"] == "on");
    }

    if (currentState.is_on) {
        if (mapping->is_switch) {
            currentState.r = mapping->default_r;
            currentState.g = mapping->default_g;
            currentState.b = mapping->default_b;
            currentState.brightness = mapping->default_brightness;
        } else {
            JsonObject attributes = state["a"];
            if (attributes.containsKey("rgb_color")) {
                JsonArray rgb = attributes["rgb_color"];
                currentState.r = rgb[0];
                currentState.g = rgb[1];
                currentState.b = rgb[2];
            }
            if (attributes.containsKey("brightness")) {
                currentState.brightness = attributes["brightness"];
            }
        }
    } else {
        currentState.r = currentState.g = currentState.b = currentState.brightness = 0;
    }

    uint32_t color = strip.Color(
        map(currentState.r, 0, 255, 0, currentState.brightness),
        map(currentState.g, 0, 255, 0, currentState.brightness),
        map(currentState.b, 0, 255, 0, currentState.brightness)
    );

    int ledIndex = getLedIndex(mapping->x, mapping->y);
    strip.setPixelColor(ledIndex, color);
    strip.show();

    Serial.printf("Updated LED for %s: R=%d, G=%d, B=%d, Brightness=%d\n",
                  entity_id, currentState.r, currentState.g, currentState.b, currentState.brightness);
}

void initializeEntityStates() {
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        entityStates[i] = {false, 255, 255, 255, 255};
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
    
    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("WebSocket disconnected");
            showWebSocketConnectionFailedAnimation();
            break;
        case WStype_CONNECTED:
            Serial.println("WebSocket connected");
            showWebSocketConnectedAnimation();
            webSocket.sendTXT("{\"type\": \"auth\", \"access_token\": \"" + String(HA_API_PASSWORD) + "\"}");
            break;
        case WStype_TEXT:
            {
                Serial.println("Received WebSocket message:");
                Serial.println((char*)payload);

                DynamicJsonDocument doc(JSON_BUFFER_SIZE);
                DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(10));
                
                if (error) {
                    Serial.printf("deserializeJson() failed: %s\n", error.c_str());
                    return;
                }

                if (doc["type"] == "auth_ok") {
                    Serial.println("Authentication successful");
                    subscribeToEntities();
                } else if (doc["type"] == "event") {
                    Serial.println("Received event:");
                    JsonObject event = doc["event"];
                    if (event.containsKey("a")) {
                        Serial.println("Full state update:");
                        JsonObject entities = event["a"];
                        for (JsonPair entity : entities) {
                            Serial.printf("Entity: %s, State: ", entity.key().c_str());
                            serializeJson(entity.value(), Serial);
                            Serial.println();
                            updateLED(entity.key().c_str(), entity.value().as<JsonObject>());
                        }
                    } else if (event.containsKey("c")) {
                        Serial.println("Partial state update:");
                        JsonObject changes = event["c"];
                        for (JsonPair change : changes) {
                            const char* entity_id = change.key().c_str();
                            JsonObject state = change.value();
                            Serial.printf("Entity: %s, Changes: ", entity_id);
                            serializeJson(state, Serial);
                            Serial.println();
                            if (state.containsKey("+")) {
                                state = state["+"];
                            }
                            updateLED(entity_id, state);
                        }
                    }
                } else {
                    Serial.printf("Unhandled message type: %s\n", doc["type"].as<const char*>());
                }
            }
            break;
        case WStype_BIN:
            Serial.println("Received binary data");
            break;
        case WStype_ERROR:
            Serial.println("WebSocket error");
            break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            Serial.println("Received fragmented data");
            break;
    }
}

void toggleEntity(const char* entity_id) {
    DynamicJsonDocument doc(1024);
    doc["id"] = messageId++;
    doc["type"] = "call_service";
    doc["domain"] = "homeassistant";
    doc["service"] = "toggle";
    doc["target"]["entity_id"] = entity_id;

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);

    Serial.printf("Toggling entity: %s\n", entity_id);
}

void buttonCheckTask(void * parameter) {
    while (true) {
        for (int y = 0; y < ROWS; y++) {
            pinMode(rowPins[y], OUTPUT);
            digitalWrite(rowPins[y], LOW);

            for (int x = 0; x < COLS; x++) {
                pinMode(colPins[x], INPUT_PULLUP);
                bool reading = digitalRead(colPins[x]) == LOW;

                if (reading != lastButtonState[y][x]) {
                    lastDebounceTime[y][x] = millis();
                }

                if ((millis() - lastDebounceTime[y][x]) > DEBOUNCE_TIME) {
                    if (reading != buttonState[y][x]) {
                        buttonState[y][x] = reading;

                        if (buttonState[y][x] == true) {
                            // Button is pressed
                            buttonPressTime[y][x] = millis();
                            
                            // Check if it's the up or down button
                            if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                                upButtonPressed = true;
                            } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                                downButtonPressed = true;
                            }
                        } else {
                            // Button is released
                            if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                                upButtonPressed = false;
                            } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                                downButtonPressed = false;
                            } else {
                                unsigned long pressDuration = millis() - buttonPressTime[y][x];
                                
                                if (pressDuration < LONG_PRESS_TIME && !upButtonPressed && !downButtonPressed) {
                                    // Short press
                                    for (int i = 0; i < NUM_MAPPINGS; i++) {
                                        if (entityMappings[i].x == x && entityMappings[i].y == y) {
                                            toggleEntity(entityMappings[i].entity_id);
                                            break;
                                        }
                                    }
                                } else {
                                    // Long press - you can add different functionality here if needed
                                    Serial.printf("Long press detected at x %d, y %d\n", x, y);
                                }
                            }
                        }
                    }
                }

                lastButtonState[y][x] = reading;
                pinMode(colPins[x], INPUT);
            }

            pinMode(rowPins[y], INPUT);
        }

        // Check for continuous brightness adjustment
        if ((upButtonPressed || downButtonPressed) && (millis() - lastBrightnessAdjustTime > BRIGHTNESS_ADJUST_INTERVAL)) {
            for (int y = 0; y < ROWS; y++) {
                for (int x = 0; x < COLS; x++) {
                    if (buttonState[y][x] && !(x == UP_BUTTON_X && y == UP_BUTTON_Y) && !(x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y)) {
                        adjustBrightness(x, y, upButtonPressed);
                    }
                }
            }
            lastBrightnessAdjustTime = millis();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void adjustBrightness(int x, int y, bool increase) {
    unsigned long currentTime = millis();
    if (currentTime - lastBrightnessAdjustTime < BRIGHTNESS_ADJUST_INTERVAL) {
        return; // Exit if not enough time has passed since the last adjustment
    }
    lastBrightnessAdjustTime = currentTime;

    for (int i = 0; i < NUM_MAPPINGS; i++) {
        if (entityMappings[i].x == x && entityMappings[i].y == y && !entityMappings[i].is_switch) {
            int newBrightness = entityStates[i].brightness;
            if (increase) {
                newBrightness = min(255, newBrightness + BRIGHTNESS_STEP);
            } else {
                newBrightness = max(0, newBrightness - BRIGHTNESS_STEP);
            }

            if (newBrightness != entityStates[i].brightness) {
                entityStates[i].brightness = newBrightness; // Update local state immediately

                // Update LED immediately based on local state
                uint32_t color = strip.Color(
                    map(entityStates[i].r, 0, 255, 0, newBrightness),
                    map(entityStates[i].g, 0, 255, 0, newBrightness),
                    map(entityStates[i].b, 0, 255, 0, newBrightness)
                );
                int ledIndex = getLedIndex(entityMappings[i].x, entityMappings[i].y);
                strip.setPixelColor(ledIndex, color);
                strip.show();

                // Send update to Home Assistant
                DynamicJsonDocument doc(1024);
                doc["id"] = messageId++;
                doc["type"] = "call_service";
                doc["domain"] = "light";
                doc["service"] = "turn_on";
                doc["target"]["entity_id"] = entityMappings[i].entity_id;
                doc["service_data"]["brightness"] = newBrightness;

                String message;
                serializeJson(doc, message);
                webSocket.sendTXT(message);

                Serial.printf("Adjusting brightness for %s to %d\n", entityMappings[i].entity_id, newBrightness);
            }
            break;
        }
    }
}

void showConnectingAnimation() {
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(0, 0, 50)); // Blue color
        strip.show();
        delay(50);
    }
    strip.clear();
    strip.show();
}

void showWiFiConnectedAnimation() {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < NUM_LEDS; j++) {
            strip.setPixelColor(j, strip.Color(0, 255, 0)); // Green color
        }
        strip.show();
        delay(100);
        strip.clear();
        strip.show();
        delay(100);
    }
}

void showWebSocketConnectedAnimation() {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < NUM_LEDS; j++) {
            if (j % 2 == 0) {
                strip.setPixelColor(j, strip.Color(0, 255, 255)); // Cyan color
            } else {
                strip.setPixelColor(j, strip.Color(255, 255, 0)); // Yellow color
            }
        }
        strip.show();
        delay(100);
        strip.clear();
        strip.show();
        delay(100);
    }
}

void showConnectionFailedAnimation() {
    strip.clear();
    for (int j = 0; j < NUM_LEDS; j++) {
        strip.setPixelColor(j, strip.Color(255, 0, 0)); // Red color
    }
    strip.show();
    // No clearing of LEDs, they stay red
}

void showWebSocketConnectionFailedAnimation() {
    strip.clear();
    for (int j = 0; j < NUM_LEDS; j++) {
        if (j % 2 == 0) {
            strip.setPixelColor(j, strip.Color(255, 0, 0)); // Red color
        } else {
            strip.setPixelColor(j, strip.Color(255, 165, 0)); // Orange color
        }
    }
    strip.show();
    // No clearing of LEDs, they stay in the red-orange pattern
}