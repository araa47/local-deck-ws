#include "common.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "secrets.h"
#include "config.h"
#include "constants.h"
#include "led_control.h"
#include "button_control.h"
#include "websocket_handler.h"
#include "animations.h"
#include "entity_state.h"
#include "wifi_manager.h"
#include "utils.h"

// Global variables
unsigned long messageId = 1;
SemaphoreHandle_t xMutex = NULL;
SemaphoreHandle_t queueMutex = NULL;
volatile int queuedMessageCount = 0;
volatile bool isBrightnessUpdateInProgress = false;
bool isNightMode = false;
int currentHour = -1;
bool isChildLockMode = false;
unsigned long childLockButtonPressTime = 0;

// Button control variables
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
int lastAdjustedX = -1;
int lastAdjustedY = -1;

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
        initializeWebSocket();
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
