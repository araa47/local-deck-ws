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
#include <esp_task_wdt.h>

struct EntityState {       
    bool is_on;
    uint8_t r, g, b;
    uint8_t brightness;
    int x, y;
};

#define MAX_QUEUED_MESSAGES 20
#define BRIGHTNESS_UPDATE_TIMEOUT_MS 10000
struct QueuedMessage {
    char* payload;
    size_t length;
};
QueuedMessage queuedMessages[MAX_QUEUED_MESSAGES];
volatile int queuedMessageCount = 0;
SemaphoreHandle_t queueMutex = NULL;

WebSocketsClient webSocket;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
EntityState entityStates[ROWS][COLS];
EntityState savedStates[ROWS][COLS];

unsigned long messageId = 1;
SemaphoreHandle_t xMutex = NULL;

// Function prototypes
int getLedIndex(int x, int y);
void updateLED(int x, int y, const JsonObject& state = JsonObject());
void initializeEntityStates();
void subscribeToEntities();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void toggleEntity(int x, int y);
void buttonCheckTask(void * parameter);
bool adjustBrightness(int x, int y, bool increase);
void updateTimeAndCheckNightMode(const char* time_str);
bool connectToWiFi(unsigned long timeout);
void reconnectWebSocket();
void printMemoryUsage();
void processQueuedMessages();
void queueWebSocketMessage(uint8_t* payload, size_t length);

// Global variables
unsigned long lastDebounceTime[ROWS][COLS] = {{0}};
bool buttonState[ROWS][COLS] = {{false}};
bool lastButtonState[ROWS][COLS] = {{false}};
unsigned long buttonPressTime[ROWS][COLS] = {{0}};
bool upButtonPressed = false;
bool downButtonPressed = false;
unsigned long lastBrightnessAdjustTime = 0;

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

// Add this global variable
volatile bool isBrightnessUpdateInProgress = false;

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

    queueMutex = xSemaphoreCreateMutex();
    if (queueMutex == NULL) {
        Serial.println("Failed to create queue mutex");
        return;
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

    esp_task_wdt_init(30, true); // 30 second timeout, panic on timeout
    esp_task_wdt_add(NULL); // Add current thread to WDT watch

    Serial.println("Setup complete.");
    printMemoryUsage();
}

void loop() {
    esp_task_wdt_reset(); // Reset watchdog timer
    
    static unsigned long lastMemoryPrint = 0;
    static unsigned long lastMessageProcess = 0;
    static unsigned long brightnessUpdateStartTime = 0;
    
    if (millis() - lastMemoryPrint > 5000) {  // Print memory usage every 5 seconds
        printMemoryUsage();
        lastMemoryPrint = millis();
    }

    webSocket.loop();

    if (millis() - lastMessageProcess > 100) {  // Process messages every 100ms
        // Serial.println("Starting to process queued messages");
        if (!isBrightnessUpdateInProgress) {
            processQueuedMessages();
        } else {
            Serial.println("Skipping message processing due to brightness update in progress");
            // Add a timeout for brightness update
            if (millis() - brightnessUpdateStartTime > BRIGHTNESS_UPDATE_TIMEOUT_MS) {  
                Serial.println("Brightness update timeout reached, resetting flag");
                isBrightnessUpdateInProgress = false;
            }
        }
        lastMessageProcess = millis();
        // Serial.println("Finished processing queued messages");
    }

    if (isBrightnessUpdateInProgress && brightnessUpdateStartTime == 0) {
        brightnessUpdateStartTime = millis();
    } else if (!isBrightnessUpdateInProgress) {
        brightnessUpdateStartTime = 0;
    }

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

void updateLED(int x, int y, const JsonObject& state) {
    Serial.printf("Updating LED at (%d, %d)\n", x, y);
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        EntityState& currentState = entityStates[y][x];

        if (!state.isNull()) {
            if (state.containsKey("s")) {
                currentState.is_on = (state["s"] == "on");
            }

            JsonObject attributes = state["a"];
            if (attributes.isNull()) {
                // If attributes are null, this might be a switch. Update only the on/off state.
                currentState.brightness = currentState.is_on ? 255 : 0;
            } else {
                if (currentState.is_on) {
                    if (attributes.containsKey("rgb_color")) {
                        JsonArray rgb = attributes["rgb_color"];
                        currentState.r = rgb[0];
                        currentState.g = rgb[1];
                        currentState.b = rgb[2];
                    }
                    if (attributes.containsKey("brightness")) {
                        currentState.brightness = attributes["brightness"];
                    } else {
                        currentState.brightness = 255; // Default to full brightness if not specified
                    }
                } else {
                    currentState.brightness = 0;
                }
            }
        }

        float scaleFactor = isNightMode ? NIGHT_BRIGHTNESS_SCALE : 1.0f;
        
        uint32_t color;
        if (currentState.is_on) {
            color = strip.Color(
                map(currentState.r, 0, 255, 0, currentState.brightness * scaleFactor),
                map(currentState.g, 0, 255, 0, currentState.brightness * scaleFactor),
                map(currentState.b, 0, 255, 0, currentState.brightness * scaleFactor)
            );
        } else {
            color = strip.Color(0, 0, 0);
        }

        int ledIndex = getLedIndex(x, y);
        strip.setPixelColor(ledIndex, color);
        strip.show();

        xSemaphoreGive(xMutex);

        Serial.printf("Updated LED at (%d, %d): R=%d, G=%d, B=%d, Brightness=%d, Scaled Brightness=%d, Is On=%d\n",
                      x, y, currentState.r, currentState.g, currentState.b, currentState.brightness,
                      (int)(currentState.brightness * scaleFactor), currentState.is_on);
    }
}

void initializeEntityStates() {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            entityStates[y][x] = {false, 255, 255, 255, 255, x, y};
        }
    }
    
    // Set registered entities
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        int x = entityMappings[i].x;
        int y = entityMappings[i].y;
        entityStates[y][x].is_on = false;
        entityStates[y][x].r = entityMappings[i].default_r;
        entityStates[y][x].g = entityMappings[i].default_g;
        entityStates[y][x].b = entityMappings[i].default_b;
        entityStates[y][x].brightness = entityMappings[i].default_brightness;
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
                Serial.println("Entering WStype_TEXT case");
                if (isBrightnessUpdateInProgress) {
                    queueWebSocketMessage(payload, length);
                    return;
                }

                Serial.printf("Received WebSocket text message. Length: %d\n", length);
                DynamicJsonDocument doc(JSON_BUFFER_SIZE);
                DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(10));
                
                if (error) {
                    Serial.printf("deserializeJson() failed: %s\n", error.c_str());
                    Serial.printf("Payload: %.*s\n", length, payload);
                    return;
                }

                if (doc["type"] == "auth_ok") {
                    Serial.println("Authentication successful");
                    subscribeToEntities();
                } else if (doc["type"] == "event") {
                    Serial.println("Received event type message");
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
                Serial.println("Exiting WStype_TEXT case");
            }
            break;
        case WStype_BIN:
        case WStype_ERROR:
            Serial.println("WebSocket error occurred");
            break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
        default:
            Serial.printf("Unhandled WebSocket event type: %d\n", type);
            break;
    }
}

void toggleEntity(int x, int y) {
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        if (entityMappings[i].x == x && entityMappings[i].y == y) {
            DynamicJsonDocument doc(1024);
            doc["id"] = messageId++;
            doc["type"] = "call_service";
            doc["domain"] = "homeassistant";
            doc["service"] = "toggle";
            doc["target"]["entity_id"] = entityMappings[i].entity_id;

            String message;
            serializeJson(doc, message);
            webSocket.sendTXT(message);

            Serial.printf("Toggling entity at (%d, %d): %s\n", x, y, entityMappings[i].entity_id);
            return;
        }
    }
    Serial.printf("No entity found at (%d, %d) to toggle\n", x, y);
}

void buttonCheckTask(void * parameter) {
    Serial.println("Button check task started");
    printMemoryUsage();

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(10);
    xLastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset(); // Reset watchdog timer

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
                                    toggleEntity(x, y);
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
            Serial.println("Entering brightness adjustment block");
            isBrightnessUpdateInProgress = true;
            bool timeoutReached = false;
            for (int y = 0; y < ROWS; y++) {
                for (int x = 0; x < COLS; x++) {
                    if (buttonState[y][x] && !(x == UP_BUTTON_X && y == UP_BUTTON_Y) && !(x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y)) {
                        Serial.printf("Calling adjustBrightness for button at (%d, %d)\n", x, y);
                        timeoutReached = adjustBrightness(x, y, upButtonPressed);
                        if (timeoutReached) {
                            break;
                        }
                        // Add a small delay to prevent overwhelming the system
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                }
                if (timeoutReached) {
                    break;
                }
            }
            lastBrightnessAdjustTime = millis();
            isBrightnessUpdateInProgress = false;
            if (timeoutReached) {
                isBrightnessAdjustmentMode = false;
                restoreStates();
            }
            Serial.println("Exiting brightness adjustment block");
        } else if (!upButtonPressed && !downButtonPressed && isBrightnessAdjustmentMode) {
            Serial.println("Finalizing brightness adjustment");
            isBrightnessUpdateInProgress = true;
            for (int i = 0; i < NUM_MAPPINGS; i++) {
                if (entityMappings[i].x == lastAdjustedX && entityMappings[i].y == lastAdjustedY) {
                    Serial.printf("Sending final brightness update for entity at (%d, %d)\n", lastAdjustedX, lastAdjustedY);
                    sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                    entityStates[lastAdjustedY][lastAdjustedX].brightness = currentAdjustmentBrightness;
                    break;
                }
            }
            isBrightnessAdjustmentMode = false;
            isBrightnessUpdateInProgress = false;
            restoreStates();
            Serial.println("Brightness adjustment finalized");
        }

        static unsigned long lastTaskMemoryPrint = 0;
        if (millis() - lastTaskMemoryPrint > 30000) {  // Print task memory usage every 30 seconds
            Serial.println("Button check task running");
            printMemoryUsage();
            lastTaskMemoryPrint = millis();
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        vTaskDelay(pdMS_TO_TICKS(10)); // Add a small delay to prevent task starvation
    }
}

bool adjustBrightness(int x, int y, bool increase) {
    Serial.printf("Entering adjustBrightness: x=%d, y=%d, increase=%d\n", x, y, increase);
    bool timeoutReached = false;
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        Serial.println("Mutex acquired in adjustBrightness");
        for (int i = 0; i < NUM_MAPPINGS; i++) {
            if (entityMappings[i].x == x && entityMappings[i].y == y && !entityMappings[i].is_switch) {
                Serial.printf("Found matching entity mapping at index %d\n", i);
                if (!isBrightnessAdjustmentMode) {
                    Serial.println("Entering brightness adjustment mode");
                    isBrightnessAdjustmentMode = true;
                    saveCurrentStates();
                    currentAdjustmentBrightness = entityStates[y][x].brightness;
                    brightnessAdjustmentStartTime = millis();
                    lastAdjustedX = x;
                    lastAdjustedY = y;
                }

                if (increase) {
                    currentAdjustmentBrightness = min(255, currentAdjustmentBrightness + BRIGHTNESS_STEP);
                } else {
                    currentAdjustmentBrightness = max(0, currentAdjustmentBrightness - BRIGHTNESS_STEP);
                }
                Serial.printf("Adjusted brightness to %d\n", currentAdjustmentBrightness);

                displayBrightnessLevel(currentAdjustmentBrightness);

                if (millis() - brightnessAdjustmentStartTime > BRIGHTNESS_ADJUST_TIMEOUT) {
                    Serial.println("Brightness adjustment timeout reached");
                    sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                    entityStates[y][x].brightness = currentAdjustmentBrightness;
                    timeoutReached = true;
                }

                break;
            }
        }
        xSemaphoreGive(xMutex);
        Serial.println("Mutex released in adjustBrightness");
    } else {
        Serial.println("Failed to acquire mutex in adjustBrightness");
    }
    Serial.println("Exiting adjustBrightness");
    return timeoutReached;
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
        for (int y = 0; y < ROWS; y++) {
            for (int x = 0; x < COLS; x++) {
                updateLED(x, y);
            }
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
    memcpy(savedStates, entityStates, sizeof(entityStates));
}

void restoreStates() {
    memcpy(entityStates, savedStates, sizeof(entityStates));
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            updateLED(x, y);
        }
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

void queueWebSocketMessage(uint8_t* payload, size_t length) {
    if (xSemaphoreTake(queueMutex, portMAX_DELAY) == pdTRUE) {
        if (queuedMessageCount < MAX_QUEUED_MESSAGES) {
            queuedMessages[queuedMessageCount].payload = (char*)malloc(length + 1);
            if (queuedMessages[queuedMessageCount].payload) {
                memcpy(queuedMessages[queuedMessageCount].payload, payload, length);
                queuedMessages[queuedMessageCount].payload[length] = '\0';
                queuedMessages[queuedMessageCount].length = length;
                queuedMessageCount++;
                Serial.printf("Queued message. Count: %d, Length: %d\n", queuedMessageCount, length);
            } else {
                Serial.println("Failed to allocate memory for queued message");
            }
        } else {
            Serial.println("Message queue is full, dropping message");
        }
        xSemaphoreGive(queueMutex);
    }
}

void processQueuedMessages() {
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int processedCount = 0;
        unsigned long startTime = millis();
        while (queuedMessageCount > 0 && processedCount < 5 && (millis() - startTime) < 500) {  // Process up to 5 messages or for 500ms max
            queuedMessageCount--;
            webSocketEvent(WStype_TEXT, (uint8_t*)queuedMessages[queuedMessageCount].payload, queuedMessages[queuedMessageCount].length);
            free(queuedMessages[queuedMessageCount].payload);
            processedCount++;
            Serial.printf("Processed %d queued messages. Remaining: %d\n", processedCount, queuedMessageCount);
        }
        xSemaphoreGive(queueMutex);
    } else {
        Serial.println("Failed to acquire queue mutex in processQueuedMessages");
    }
}