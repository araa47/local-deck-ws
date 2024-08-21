#ifndef COMMON_H
#define COMMON_H

#include <Arduino.h>

#define ENABLE_SERIAL_LOGGING false

#define SERIAL_PRINT(x) if (ENABLE_SERIAL_LOGGING) Serial.print(x)
#define SERIAL_PRINTLN(x) if (ENABLE_SERIAL_LOGGING) Serial.println(x)
#define SERIAL_PRINTF(format, ...) if (ENABLE_SERIAL_LOGGING) Serial.printf(format, __VA_ARGS__)

#define MAX_QUEUED_MESSAGES 50
#define BRIGHTNESS_UPDATE_TIMEOUT_MS 20000

extern unsigned long messageId;
extern SemaphoreHandle_t xMutex;
extern SemaphoreHandle_t queueMutex;
extern volatile int queuedMessageCount;
extern volatile bool isBrightnessUpdateInProgress;
extern bool isNightMode;
extern int currentHour;

#endif // COMMON_H