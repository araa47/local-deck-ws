#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include "common.h"
#include "led_control.h"
#include "constants.h"

void showConnectingAnimation();
void showWiFiConnectedAnimation();
void showWebSocketConnectedAnimation();
void showConnectionFailedAnimation();
void showWebSocketConnectionFailedAnimation();
void showChildLockEnabledAnimation();
void showChildLockDisabledAnimation();
#endif // ANIMATIONS_H
