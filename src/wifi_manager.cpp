#include "wifi_manager.h"

bool connectToWiFi(unsigned long timeout) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(100);
        SERIAL_PRINT(".");
    }

    return WiFi.status() == WL_CONNECTED;
}
