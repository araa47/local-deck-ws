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
// Add this near the top of the file, after the includes
#define ENABLE_SERIAL_LOGGING false

// Function to conditionally print to serial
#define SERIAL_PRINT(x) if (ENABLE_SERIAL_LOGGING) Serial.print(x)
#define SERIAL_PRINTLN(x) if (ENABLE_SERIAL_LOGGING) Serial.println(x)
#define SERIAL_PRINTF(format, ...) if (ENABLE_SERIAL_LOGGING) Serial.printf(format, __VA_ARGS__)

#define MAX_QUEUED_MESSAGES 50
#define BRIGHTNESS_UPDATE_TIMEOUT_MS 20000
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
void updateButtonStates();

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
void displayBrightnessLevel(int brightness, uint8_t r, uint8_t g, uint8_t b);
void sendBrightnessUpdate(const char* entity_id, int brightness);

// Add this global variable
volatile bool isBrightnessUpdateInProgress = false;

void setup() {
    if (ENABLE_SERIAL_LOGGING) {
        Serial.begin(115200);
        delay(300); // Give some time for serial to initialize
    }
    SERIAL_PRINTLN("Starting setup...");
    printMemoryUsage();

    strip.begin();
    strip.show();

    xMutex = xSemaphoreCreateMutex();
    if (xMutex == NULL) {
        SERIAL_PRINTLN("Failed to create mutex");
        return;
    } else {
        SERIAL_PRINTLN("Mutex created");
    }

    queueMutex = xSemaphoreCreateMutex();
    if (queueMutex == NULL) {
        SERIAL_PRINTLN("Failed to create queue mutex");
        return;
    }

    showConnectingAnimation();

    if (connectToWiFi(10000)) {
        SERIAL_PRINTLN("\nConnected to WiFi");
        showWiFiConnectedAnimation();

        webSocket.begin(HA_HOST, HA_PORT, "/api/websocket");
        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(5000);

        initializeEntityStates();
    } else {
        SERIAL_PRINTLN("\nFailed to connect to WiFi");
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

    SERIAL_PRINTLN("Setup complete.");
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
        // SERIAL_PRINTLN("Starting to process queued messages");
        if (!isBrightnessUpdateInProgress) {
            processQueuedMessages();
        } else {
            SERIAL_PRINTLN("Skipping message processing due to brightness update in progress");
            // Add a timeout for brightness update
            if (millis() - brightnessUpdateStartTime > BRIGHTNESS_UPDATE_TIMEOUT_MS) {  
                SERIAL_PRINTLN("Brightness update timeout reached, resetting flag");
                isBrightnessUpdateInProgress = false;
            }
        }
        lastMessageProcess = millis();
        // SERIAL_PRINTLN("Finished processing queued messages");
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
    SERIAL_PRINTF("Updating LED at (%d, %d)\n", x, y);
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

        SERIAL_PRINTF("Updated LED at (%d, %d): R=%d, G=%d, B=%d, Brightness=%d, Scaled Brightness=%d, Is On=%d\n",
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

            SERIAL_PRINTF("Toggling entity at (%d, %d): %s\n", x, y, entityMappings[i].entity_id);
            return;
        }
    }
    SERIAL_PRINTF("No entity found at (%d, %d) to toggle\n", x, y);
}

void buttonCheckTask(void * parameter) {
    SERIAL_PRINTLN("Button check task started");
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
                                    SERIAL_PRINTF("Long press detected at (x: %d, y: %d)\n", x, y);
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
            SERIAL_PRINTLN("Entering brightness adjustment block");
            isBrightnessUpdateInProgress = true;
            unsigned long adjustmentStartTime = millis();

            while ((upButtonPressed || downButtonPressed) && (millis() - adjustmentStartTime <= BRIGHTNESS_UPDATE_TIMEOUT_MS)) {
                for (int y = 0; y < ROWS; y++) {
                    for (int x = 0; x < COLS; x++) {
                        if (buttonState[y][x] && !(x == UP_BUTTON_X && y == UP_BUTTON_Y) && !(x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y)) {
                            SERIAL_PRINTF("Calling adjustBrightness for button at (%d, %d)\n", x, y);
                            adjustBrightness(x, y, upButtonPressed);
                        }
                    }
                }

                // Add a small delay to prevent overwhelming the system
                vTaskDelay(pdMS_TO_TICKS(10));

                // Update button states
                updateButtonStates();
            }

            lastBrightnessAdjustTime = millis();
            isBrightnessUpdateInProgress = false;
            if (millis() - adjustmentStartTime > BRIGHTNESS_UPDATE_TIMEOUT_MS) {
                SERIAL_PRINTLN("Brightness adjustment timeout reached");
                isBrightnessAdjustmentMode = false;
                restoreStates();
            } else {
                // Finalize the brightness adjustment
                for (int i = 0; i < NUM_MAPPINGS; i++) {
                    if (entityMappings[i].x == lastAdjustedX && entityMappings[i].y == lastAdjustedY) {
                        SERIAL_PRINTF("Sending final brightness update for entity at (%d, %d)\n", lastAdjustedX, lastAdjustedY);
                        sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                        entityStates[lastAdjustedY][lastAdjustedX].brightness = currentAdjustmentBrightness;
                        break;
                    }
                }
            }
            SERIAL_PRINTLN("Exiting brightness adjustment block");
        } else if (!upButtonPressed && !downButtonPressed && isBrightnessAdjustmentMode) {
            SERIAL_PRINTLN("Finalizing brightness adjustment");
            isBrightnessUpdateInProgress = true;
            for (int i = 0; i < NUM_MAPPINGS; i++) {
                if (entityMappings[i].x == lastAdjustedX && entityMappings[i].y == lastAdjustedY) {
                    SERIAL_PRINTF("Sending final brightness update for entity at (%d, %d)\n", lastAdjustedX, lastAdjustedY);
                    sendBrightnessUpdate(entityMappings[i].entity_id, currentAdjustmentBrightness);
                    entityStates[lastAdjustedY][lastAdjustedX].brightness = currentAdjustmentBrightness;
                    break;
                }
            }
            isBrightnessAdjustmentMode = false;
            isBrightnessUpdateInProgress = false;
            restoreStates();
            SERIAL_PRINTLN("Brightness adjustment finalized");
        }

        static unsigned long lastTaskMemoryPrint = 0;
        if (millis() - lastTaskMemoryPrint > 30000) {  // Print task memory usage every 30 seconds
            SERIAL_PRINTLN("Button check task running");
            printMemoryUsage();
            lastTaskMemoryPrint = millis();
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        vTaskDelay(pdMS_TO_TICKS(10)); // Add a small delay to prevent task starvation
    }
}

bool adjustBrightness(int x, int y, bool increase) {
    SERIAL_PRINTF("Entering adjustBrightness: x=%d, y=%d, increase=%d\n", x, y, increase);
    static unsigned long lastAdjustmentTime = 0;
    const unsigned long ADJUSTMENT_INTERVAL = 50; // Adjust every 50ms for smoother transitions

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        SERIAL_PRINTLN("Mutex acquired in adjustBrightness");
        for (int i = 0; i < NUM_MAPPINGS; i++) {
            if (entityMappings[i].x == x && entityMappings[i].y == y && !entityMappings[i].is_switch) {
                SERIAL_PRINTF("Found matching entity mapping at index %d\n", i);
                if (!isBrightnessAdjustmentMode) {
                    SERIAL_PRINTLN("Entering brightness adjustment mode");
                    isBrightnessAdjustmentMode = true;
                    saveCurrentStates();
                    currentAdjustmentBrightness = entityStates[y][x].brightness;
                    brightnessAdjustmentStartTime = millis();
                    lastAdjustedX = x;
                    lastAdjustedY = y;
                }

                // Only adjust brightness if enough time has passed since the last adjustment
                if (millis() - lastAdjustmentTime >= ADJUSTMENT_INTERVAL) {
                    if (increase) {
                        currentAdjustmentBrightness = min(255, currentAdjustmentBrightness + BRIGHTNESS_STEP);
                    } else {
                        currentAdjustmentBrightness = max(0, currentAdjustmentBrightness - BRIGHTNESS_STEP);
                    }
                    SERIAL_PRINTF("Adjusted brightness to %d\n", currentAdjustmentBrightness);

                    // Pass the color information to displayBrightnessLevel
                    displayBrightnessLevel(currentAdjustmentBrightness, 
                                           entityStates[y][x].r, 
                                           entityStates[y][x].g, 
                                           entityStates[y][x].b);

                    lastAdjustmentTime = millis();
                }

                break;
            }
        }
        xSemaphoreGive(xMutex);
        SERIAL_PRINTLN("Mutex released in adjustBrightness");
    } else {
        SERIAL_PRINTLN("Failed to acquire mutex in adjustBrightness");
    }
    SERIAL_PRINTLN("Exiting adjustBrightness");
    return false; // We're no longer checking for timeout here
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

void showConnectingAnimation() {
    SERIAL_PRINTLN("Showing connecting animation (Blue)");
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, applyBrightnessScalar(COLOR_BLUE));
        strip.show();
        delay(ANIMATION_DELAY_SHORT);
    }
    strip.clear();
    strip.show();
}

void showWiFiConnectedAnimation() {
    SERIAL_PRINTLN("Showing WiFi connected animation (Green)");
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
    SERIAL_PRINTLN("Showing WebSocket connected animation (Cyan and Yellow)");
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
    SERIAL_PRINTLN("Showing connection failed animation (Red)");
    strip.clear();
    for (int j = 0; j < NUM_LEDS; j++) {
        strip.setPixelColor(j, applyBrightnessScalar(COLOR_RED));
    }
    strip.show();
}

void showWebSocketConnectionFailedAnimation() {
    SERIAL_PRINTLN("Showing WebSocket connection failed animation (Red and Orange)");
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

void displayBrightnessLevel(int brightness, uint8_t r, uint8_t g, uint8_t b) {
    float scaleFactor = isNightMode ? NIGHT_BRIGHTNESS_SCALE : 1.0f;
    int litLEDs = map(brightness, 0, 255, 0, NUM_LEDS);
    
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i < litLEDs) {
            uint8_t scaledR = (uint8_t)(r * scaleFactor);
            uint8_t scaledG = (uint8_t)(g * scaleFactor);
            uint8_t scaledB = (uint8_t)(b * scaleFactor);
            strip.setPixelColor(i, strip.Color(scaledR, scaledG, scaledB));
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

    SERIAL_PRINTF("Adjusting brightness for %s to %d\n", entity_id, brightness);
}

void printMemoryUsage() {
    SERIAL_PRINTF("Free heap: %d, Largest free block: %d\n", 
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

// Add this new function to update button states
void updateButtonStates() {
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

                    if (x == UP_BUTTON_X && y == UP_BUTTON_Y) {
                        upButtonPressed = buttonState[y][x];
                    } else if (x == DOWN_BUTTON_X && y == DOWN_BUTTON_Y) {
                        downButtonPressed = buttonState[y][x];
                    }
                }
            }

            lastButtonState[y][x] = reading;
            pinMode(colPins[x], INPUT);
        }

        pinMode(rowPins[y], INPUT);
    }
}