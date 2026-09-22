#ifndef WEB_UI_H
#define WEB_UI_H

#include "common.h"
#include "config.h"

// Browser UI for remapping buttons live, served from the deck itself at
// http://<DEVICE_HOSTNAME>.local/ (or the deck's IP).
//
// Optional, in secrets.h:
//   #define WEB_UI_PASSWORD "..."   require HTTP basic auth (user WEB_UI_USERNAME)
//   #define WEB_UI_USERNAME "..."   defaults to "admin"
// Optional, in config.h or as a build flag:
//   #define ENABLE_WEB_UI false     leave the web UI out entirely

#ifndef ENABLE_WEB_UI
#define ENABLE_WEB_UI true
#endif

void initWebUI();
// Call whenever Wi-Fi (re)connects: (re)announces the deck over mDNS.
void onWebUIWiFiConnected();
// Call from loop(). Serves at most one pending request.
void handleWebUI();

#endif // WEB_UI_H
