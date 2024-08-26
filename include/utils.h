#ifndef UTILS_H
#define UTILS_H

#include "common.h"
#include <WiFi.h>
#include "secrets.h"
#include "config.h"
#include "constants.h"
#include "websocket_handler.h"
#include "led_control.h"

void printMemoryUsage();
void updateTimeAndCheckNightMode(const char* time_str);
void toggleEntity(int x, int y);

#endif // UTILS_H
