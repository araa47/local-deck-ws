#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "secrets.h" 
#include "config.h"
#include "constants.h"
#include <esp_system.h>
#include <esp_heap_caps.h>

struct EntityState {       
    bool is_on;
    uint8_t r, g, b;
    uint8_t brightness;
};


WebSocketsClient webSocket;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
EntityState entityStates[NUM_MAPPINGS];

unsigned long messageId = 1;
SemaphoreHandle_t xMutex = NULL;

// Function prototypes
int getLedIndex(int x, int y);
void updateLED(const char* entity_id, const JsonObject& state);
void initializeEntityStates();
void subscribeToEntities();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void toggleEntity(const char* entity_id);
void buttonCheckTask(void * parameter);
void adjustBrightness(int x, int y, bool increase);
void updateTimeAndCheckNightMode(const char* time_str);
bool connectToWiFi(unsigned long timeout);
void reconnectWebSocket();
void printMemoryUsage();

// Global variables
unsigned long lastDebounceTime[ROWS][COLS] = {{0}};
bool buttonState[ROWS][COLS] = {{false}};
bool lastButtonState[ROWS][COLS] = {{false}};
unsigned long buttonPressTime[ROWS][COLS] = {{0}};
bool upButtonPressed = false;
bool downButtonPressed = false;
unsigned long lastBrightnessAdjustTime = 0;

EntityState savedStates[NUM_MAPPINGS];
bool isBrightnessAdjustmentMode = false;
int currentAdjustmentBrightness = 0;
unsigned long brightnessAdjustmentStartTime = 0;
// Variables to track the last adjusted x and y coordinates
int lastAdjustedX = -1;
int lastAdjustedY = -1;

// Animation function prototypes
void showConnectingAnimation();
void showWiFiConnectedAnimation();
void showWebSocketConnectedAnimation();
void showConnectionFailedAnimation();
void showWebSocketConnectionFailedAnimation();
uint32_t applyBrightnessScalar(uint32_t color);

// Night mode variables
bool isNightMode = false;
int currentHour = -1;



// New function prototypes
void saveCurrentStates();
void restoreStates();
void displayBrightnessLevel(int brightness);
void sendBrightnessUpdate(const char* entity_id, int brightness);

void setup() {
    Serial.begin(115200);
    delay(300); // Give some time for serial to initialize
    Serial.println("Starting setup...");
    printMemoryUsage();

    strip.begin();
    strip.show();

    xMutex = xSemaphoreCreateMutex();
    if (xMutex == NULL) {
        Serial.println("Failed to create mutex");
        return;
    } else {
        Serial.println("Mutex created");
    }

    showConnectingAnimation();

    if (connectToWiFi(10000)) {
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

    Serial.println("Setup complete.");
    printMemoryUsage();
}

void loop() {
    static unsigned long lastMemoryPrint = 0;
    if (millis() - lastMemoryPrint > 10000) {  // Print memory usage every 10 seconds
        printMemoryUsage();
        lastMemoryPrint = millis();
    }

    webSocket.loop();

    if (WiFi.status() != WL_CONNECTED) {
        if (connectToWiFi(10000)) {
            reconnectWebSocket();
        }
    }
    
    delay(10);
}

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

int getLedIndex(int x, int y) {
    return y * COLS + x;
}

void updateLED(const char* entity_id, const JsonObject& state) {
    Serial.printf("Updating LED for entity: %s\n", entity_id);
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        const EntityMapping* mapping = nullptr;
        int mappingIndex = -1;

        for (int i = 0; i < NUM_MAPPINGS; i++) {
            if (strcmp(entity_id, entityMappings[i].entity_id) == 0) {
                mapping = &entityMappings[i];
                mappingIndex = i;
                break;
            }
        }

        if (!mapping) {
            xSemaphoreGive(xMutex);
            return;
        }

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

        float scaleFactor = isNightMode ? NIGHT_BRIGHTNESS_SCALE : 1.0f;
        
        uint32_t color = strip.Color(
            map(currentState.r, 0, 255, 0, currentState.brightness * scaleFactor),
            map(currentState.g, 0, 255, 0, currentState.brightness * scaleFactor),
            map(currentState.b, 0, 255, 0, currentState.brightness * scaleFactor)
        );

        int ledIndex = getLedIndex(mapping->x, mapping->y);
        strip.setPixelColor(ledIndex, color);
        strip.show();

        xSemaphoreGive(xMutex);

        Serial.printf("Updated LED for %s: R=%d, G=%d, B=%d, Brightness=%d, Scaled Brightness=%d\n",
                      entity_id, currentState.r, currentState.g, currentState.b, currentState.brightness,
                      (int)(currentState.brightness * scaleFactor));
    }
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
    
    entity_ids.add("sensor.time");
    
    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    Serial.printf("WebSocket event type: %d\n", type);
    
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
                Serial.printf("Received WebSocket text message. Length: %d\n", length);
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
                    JsonObject event = doc["event"];
                    if (event.containsKey("a")) {
                        JsonObject entities = event["a"];
                        for (JsonPair entity : entities) {
                            const char* entity_id = entity.key().c_str();
                            if (strcmp(entity_id, "sensor.time") == 0) {
                                updateTimeAndCheckNightMode(entity.value()["s"]);
                            } else {
                                updateLED(entity_id, entity.value().as<JsonObject>());
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
                                updateLED(entity_id, state);
                            }
                        }
                    }
                }
            }
            break;
        case WStype_BIN:
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
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
    Serial.println("Button check task started");
    printMemoryUsage();

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();

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
                            buttonPressTime[y][x] = millis();
                            
                            if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                                upButtonPressed = true;
                            } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                                downButtonPressed = true;
                            }
                        } else {
                            if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                                upButtonPressed = false;
                            } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                                downButtonPressed = false;
                            } else {
                                unsigned long pressDuration = millis() - buttonPressTime[y][x];
                                
                                if (pressDuration < LONG_PRESS_TIME && !upButtonPressed && !downButtonPressed) {
                                    for (int i = 0; i < NUM_MAPPINGS; i++) {
                                        if (entityMappings[i].x == x && entityMappings[i].y == y) {
                                            toggleEntity(entityMappings[i].entity_id);
                                            break;
                                        }
                                    }
                                } else {
                                    Serial.printf("Long press detected at (x: %d, y: %d)\n", x, y);
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

        if ((upButtonPressed || downButtonPressed) && (millis() - lastBrightnessAdjustTime > BRIGHTNESS_ADJUST_INTERVAL)) {
            for (int y = 0; y < ROWS; y++) {
                for (int x = 0; x < COLS; x++) {
                    if (buttonState[y][x] && !(x == UP_BUTTON_X && y == UP_BUTTON_Y) && !(x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y)) {
                        adjustBrightness(x, y, upButtonPressed);
                    }
                }
            }
            lastBrightnessAdjustTime = millis();
        } else if (!upButtonPressed && !downButtonPressed && isBrightnessAdjustmentMode) {
            for (int i = 0; i < NUM_MAPPINGS; i++) {
                if (entityMappings[i].x == lastAdjustedX && entityMappings[i].y == lastAdjustedY) {
                    sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                    entityStates[i].brightness = currentAdjustmentBrightness;
                    break;
                }
            }
            isBrightnessAdjustmentMode = false;
            restoreStates();
        }

        static unsigned long lastTaskMemoryPrint = 0;
        if (millis() - lastTaskMemoryPrint > 30000) {  // Print task memory usage every 30 seconds
            Serial.println("Button check task running");
            printMemoryUsage();
            lastTaskMemoryPrint = millis();
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void adjustBrightness(int x, int y, bool increase) {
    Serial.printf("Adjusting brightness: x=%d, y=%d, increase=%d\n", x, y, increase);
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < NUM_MAPPINGS; i++) {
            if (entityMappings[i].x == x && entityMappings[i].y == y && !entityMappings[i].is_switch) {
                if (!isBrightnessAdjustmentMode) {
                    isBrightnessAdjustmentMode = true;
                    saveCurrentStates();
                    currentAdjustmentBrightness = entityStates[i].brightness;
                    brightnessAdjustmentStartTime = millis();
                    lastAdjustedX = x;
                    lastAdjustedY = y;
                }

                if (increase) {
                    currentAdjustmentBrightness = min(255, currentAdjustmentBrightness + BRIGHTNESS_STEP);
                } else {
                    currentAdjustmentBrightness = max(0, currentAdjustmentBrightness - BRIGHTNESS_STEP);
                }

                displayBrightnessLevel(currentAdjustmentBrightness);

                if (millis() - brightnessAdjustmentStartTime > BRIGHTNESS_ADJUST_TIMEOUT) {
                    sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                    entityStates[i].brightness = currentAdjustmentBrightness;
                    isBrightnessAdjustmentMode = false;
                    restoreStates();
                }

                break;
            }
        }
        xSemaphoreGive(xMutex);
    }
}

void updateTimeAndCheckNightMode(const char* time_str) {
    Serial.printf("Received time update: %s\n", time_str);

    if (!time_str || strlen(time_str) < 5) {
        Serial.println("Invalid time string received");
        return;
    }

    int hour = 0, minute = 0;
    if (sscanf(time_str, "%d:%d", &hour, &minute) != 2) {
        Serial.printf("Failed to parse time string: %s\n", time_str);
        return;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        Serial.printf("Invalid time values: %02d:%02d\n", hour, minute);
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
        Serial.printf("Night mode changed to: %s (Time: %02d:%02d)\n", isNightMode ? "ON" : "OFF", hour, minute);
        for (int i = 0; i < NUM_MAPPINGS; i++) {
            updateLED(entityMappings[i].entity_id, JsonObject());
        }
    } else {
        Serial.printf("Night mode unchanged: %s (Time: %02d:%02d)\n", isNightMode ? "ON" : "OFF", hour, minute);
    }
}

void showConnectingAnimation() {
    Serial.println("Showing connecting animation (Blue)");
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, applyBrightnessScalar(COLOR_BLUE));
        strip.show();
        delay(ANIMATION_DELAY_SHORT);
    }
    strip.clear();
    strip.show();
}

void showWiFiConnectedAnimation() {
    Serial.println("Showing WiFi connected animation (Green)");
    for (int i = 0; i < ANIMATION_REPEAT_COUNT; i++) {
        for (int j = 0; j < NUM_LEDS; j++) {
            strip.setPixelColor(j, applyBrightnessScalar(COLOR_GREEN));
        }
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
        strip.clear();
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
    }
}

void showWebSocketConnectedAnimation() {
    Serial.println("Showing WebSocket connected animation (Cyan and Yellow)");
    for (int i = 0; i < ANIMATION_REPEAT_COUNT; i++) {
        for (int j = 0; j < NUM_LEDS; j++) {
            strip.setPixelColor(j, applyBrightnessScalar(j % 2 == 0 ? COLOR_CYAN : COLOR_YELLOW));
        }
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
        strip.clear();
        strip.show();
        delay(ANIMATION_DELAY_MEDIUM);
    }
}

void showConnectionFailedAnimation() {
    Serial.println("Showing connection failed animation (Red)");
    strip.clear();
    for (int j = 0; j < NUM_LEDS; j++) {
        strip.setPixelColor(j, applyBrightnessScalar(COLOR_RED));
    }
    strip.show();
}

void showWebSocketConnectionFailedAnimation() {
    Serial.println("Showing WebSocket connection failed animation (Red and Orange)");
    strip.clear();
    for (int j = 0; j < NUM_LEDS; j++) {
        strip.setPixelColor(j, applyBrightnessScalar(j % 2 == 0 ? COLOR_RED : COLOR_ORANGE));
    }
    strip.show();
}

uint32_t applyBrightnessScalar(uint32_t color) {
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >> 8);
    uint8_t b = (uint8_t)color;
    
    r = (uint8_t)(r * ANIMATION_BRIGHTNESS_SCALAR);
    g = (uint8_t)(g * ANIMATION_BRIGHTNESS_SCALAR);
    b = (uint8_t)(b * ANIMATION_BRIGHTNESS_SCALAR);
    
    return strip.Color(r, g, b);
}

void saveCurrentStates() {
    memcpy(savedStates, entityStates, sizeof(EntityState) * NUM_MAPPINGS);
}

void restoreStates() {
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        updateLED(entityMappings[i].entity_id, JsonObject());
    }
}

void displayBrightnessLevel(int brightness) {
    int litLEDs = map(brightness, 0, 255, 0, NUM_LEDS);
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i < litLEDs) {
            strip.setPixelColor(i, strip.Color(255, 255, 255));
        } else {
            strip.setPixelColor(i, strip.Color(0, 0, 0));
        }
    }
    strip.show();
}

void sendBrightnessUpdate(const char* entity_id, int brightness) {
    DynamicJsonDocument doc(1024);
    doc["id"] = messageId++;
    doc["type"] = "call_service";
    doc["domain"] = "light";
    doc["service"] = "turn_on";
    doc["target"]["entity_id"] = entity_id;
    doc["service_data"]["brightness"] = brightness;

    String message;
    serializeJson(doc, message);
    webSocket.sendTXT(message);

    Serial.printf("Adjusting brightness for %s to %d\n", entity_id, brightness);
}

void printMemoryUsage() {
    Serial.printf("Free heap: %d, Largest free block: %d\n", 
                  esp_get_free_heap_size(), 
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}