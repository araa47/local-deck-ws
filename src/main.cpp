#include "common.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "constants.h"
#include "led_control.h"
#include "button_control.h"
#include "websocket_handler.h"
#include "animations.h"
#include "entity_state.h"
#include "wifi_manager.h"
#include "utils.h"
#include "button_config.h"
#include "web_ui.h"

// Global variables
unsigned long messageId = 1;
SemaphoreHandle_t xMutex = NULL;
SemaphoreHandle_t queueMutex = NULL;
volatile int queuedMessageCount = 0;
volatile bool isBrightnessUpdateInProgress = false;
bool isNightMode = false;
int currentHour = -1;
bool isChildLockMode = false;

// Button control variables
unsigned long lastDebounceTime[ROWS][COLS] = {{0}};
bool buttonState[ROWS][COLS] = {{false}};
bool lastButtonState[ROWS][COLS] = {{false}};
unsigned long buttonPressTime[ROWS][COLS] = {{0}};
bool upButtonPressed = false;
bool downButtonPressed = false;
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

    // Before anything that reads the layout: the button task, the websocket.
    initButtonConfig();

    showConnectingAnimation();

    if (connectToWiFi(10000)) {
        SERIAL_PRINTLN("\nConnected to WiFi");
        showWiFiConnectedAnimation();
        initializeWebSocket();
        initializeEntityStates();
        onWebUIWiFiConnected();
    } else {
        SERIAL_PRINTLN("\nFailed to connect to WiFi");
        showConnectionFailedAnimation();
    }

    // Listens whether or not Wi-Fi came up; it starts answering once it does.
    initWebUI();

    xTaskCreate(
        buttonCheckTask,
        "ButtonCheckTask",
        4096,
        NULL,
        1,
        NULL
    );

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&twdt_config); // 30 second timeout, panic on timeout
#else
    esp_task_wdt_init(30, true); // 30 second timeout, panic on timeout
#endif
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
    handleWebUI();

    // buttonCheckTask owns this flag and clears it when a gesture finishes.
    // This is only a backstop in case it somehow never does.
    if (isBrightnessUpdateInProgress) {
        if (brightnessUpdateStartTime == 0) {
            brightnessUpdateStartTime = millis();
        } else if (millis() - brightnessUpdateStartTime > BRIGHTNESS_UPDATE_TIMEOUT_MS) {
            SERIAL_PRINTLN("Brightness update timeout reached, resetting flag");
            isBrightnessUpdateInProgress = false;
            brightnessUpdateStartTime = 0;
        }
    } else {
        brightnessUpdateStartTime = 0;
    }

    // Drain often so the grid catches up quickly once a gesture ends.
    if (!isBrightnessUpdateInProgress && millis() - lastMessageProcess > 20) {
        processQueuedMessages();
        lastMessageProcess = millis();
    }

    if (WiFi.status() != WL_CONNECTED) {
        if (connectToWiFi(10000)) {
            reconnectWebSocket();
            onWebUIWiFiConnected();
        }
    }

    delay(5);
}
