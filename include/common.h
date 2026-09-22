#ifndef COMMON_H
#define COMMON_H

#include <Arduino.h>

#ifndef ENABLE_SERIAL_LOGGING
#define ENABLE_SERIAL_LOGGING false
#endif

#define SERIAL_PRINT(x) if (ENABLE_SERIAL_LOGGING) Serial.print(x)
#define SERIAL_PRINTLN(x) if (ENABLE_SERIAL_LOGGING) Serial.println(x)
#define SERIAL_PRINTF(format, ...) if (ENABLE_SERIAL_LOGGING) Serial.printf(format, __VA_ARGS__)

#define MAX_QUEUED_MESSAGES 50
#define BRIGHTNESS_UPDATE_TIMEOUT_MS 20000

// Queue drain budget: how many queued Home Assistant messages to replay per
// pass, and the wall-clock ceiling for one pass.
#define MAX_MESSAGES_PER_DRAIN 10
#define QUEUE_DRAIN_BUDGET_MS 100
#define QUEUE_MUTEX_WAIT_MS 20

extern unsigned long messageId;
extern SemaphoreHandle_t xMutex;
extern SemaphoreHandle_t queueMutex;
extern volatile int queuedMessageCount;
extern volatile bool isBrightnessUpdateInProgress;
extern bool isNightMode;
extern int currentHour;
extern bool isChildLockMode;

#endif // COMMON_H