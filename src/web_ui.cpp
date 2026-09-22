#include "web_ui.h"

#if ENABLE_WEB_UI

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <lwip/sockets.h>
#include <esp_system.h>
#include "button_control.h"
#include "button_config.h"
#include "entity_state.h"
#include "homeassistant_handler.h"
#include "led_control.h"
#include "secrets.h"
#include "web_ui_page.h"

#ifndef WEB_UI_USERNAME
#define WEB_UI_USERNAME "admin"
#endif

// Everything here runs on the loop task -- the same task that runs
// webSocket.loop() -- so it can talk to Home Assistant directly. Only the
// button layout is shared with the button task, and that goes through
// button_config's locked copies.

static WebServer server(80);
static bool mdnsStarted = false;

// Scratch space for a whole layout, kept off the loop task's stack.
static ButtonConfig currentLayout[ROWS][COLS];
static ButtonConfig nextLayout[ROWS][COLS];

// DNS rebinding guard. A page on some-site.example can point its own name at
// the deck's IP and then call this API as same-origin, so only answer to names
// that cannot come from the public internet: an IP, a bare hostname, or a
// LAN-only suffix.
static bool isLocalHost(String host) {
    int colon = host.indexOf(':');
    if (colon >= 0) {
        host = host.substring(0, colon);
    }
    host.toLowerCase();
    if (host.length() == 0) {
        return false;
    }

    bool dotted = true;
    for (size_t i = 0; i < host.length(); i++) {
        if (!isDigit(host[i]) && host[i] != '.') {
            dotted = false;
            break;
        }
    }
    if (dotted) {
        return true; // IPv4 literal
    }

    return host.indexOf('.') < 0 ||
           host.endsWith(".local") ||
           host.endsWith(".lan") ||
           host.endsWith(".home") ||
           host.endsWith(".internal") ||
           host.endsWith(".home.arpa");
}

// Common gate for every route. Returns false once it has already answered.
static bool admit(bool mutating) {
    if (!isLocalHost(server.hostHeader())) {
        server.send(403, "text/plain", "Open the deck by its IP or " DEVICE_HOSTNAME ".local");
        return false;
    }
#ifdef WEB_UI_PASSWORD
    if (!server.authenticate(WEB_UI_USERNAME, WEB_UI_PASSWORD)) {
        server.requestAuthentication(BASIC_AUTH, "LocalDeck");
        return false;
    }
#endif
    // A cross-site form can POST text/plain here without asking; it cannot
    // send application/json without a CORS preflight, which is never granted.
    if (mutating && !server.header("Content-Type").startsWith("application/json")) {
        server.send(415, "text/plain", "Expected application/json");
        return false;
    }
    return true;
}

// Write straight to the socket, waiting out a full send buffer.
//
// WiFiClient::write() -- which WebServer::send() and sendContent() use --
// gives up and closes the connection on any send() error other than EAGAIN,
// and lwIP also reports a momentarily full send queue as ENOMEM. So anything
// much bigger than the ~5.7KB TCP send buffer was cut off part way through,
// more often than not.
static bool writeAll(const char* data, size_t len) {
    int fd = server.client().fd();
    if (fd < 0) {
        return false;
    }
    unsigned long lastProgress = millis();
    while (len > 0) {
        int sent = send(fd, data, len, MSG_DONTWAIT);
        if (sent > 0) {
            data += sent;
            len -= sent;
            lastProgress = millis();
            continue;
        }
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOMEM) {
            SERIAL_PRINTF("Web UI send failed, errno %d\n", errno);
            return false;
        }
        if (millis() - lastProgress > 10000) {
            SERIAL_PRINTLN("Web UI send stalled, giving up");
            return false;
        }
        esp_task_wdt_reset();
        delay(2);
    }
    return true;
}

// WebServer writes the (small) headers; the body goes through writeAll().
static void sendBody(int code, const char* contentType, const char* body, size_t len) {
    server.setContentLength(len);
    server.send(code, contentType, "");
    writeAll(body, len);
}

static void sendJson(int code, const JsonDocument& doc) {
    String body;
    serializeJson(doc, body);
    sendBody(code, "application/json", body.c_str(), body.length());
}

static void sendError(int code, const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    sendJson(code, doc);
}

static void colorToHex(uint8_t r, uint8_t g, uint8_t b, char out[8]) {
    snprintf(out, 8, "#%02x%02x%02x", r, g, b);
}

static bool hexToColor(const char* hex, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (!hex || hex[0] != '#' || strlen(hex) != 7) {
        return false;
    }
    for (int i = 1; i < 7; i++) {
        if (!isHexadecimalDigit(hex[i])) {
            return false;
        }
    }
    unsigned long value = strtoul(hex + 1, NULL, 16);
    r = (value >> 16) & 0xFF;
    g = (value >> 8) & 0xFF;
    b = value & 0xFF;
    return true;
}

static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "power on";
        case ESP_RST_EXT:       return "external reset";
        case ESP_RST_SW:        return "software restart";
        case ESP_RST_PANIC:     return "crash";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "reset";  // USB / flashing, among others
    }
}

static void addCoords(JsonArray arr, int x, int y) {
    arr.add(x);
    arr.add(y);
}

static void sendLayout() {
    getAllButtonConfigs(currentLayout);

    JsonDocument doc;
    doc["rows"] = ROWS;
    doc["cols"] = COLS;
    addCoords(doc["up"].to<JsonArray>(), UP_BUTTON_X, UP_BUTTON_Y);
    addCoords(doc["down"].to<JsonArray>(), DOWN_BUTTON_X, DOWN_BUTTON_Y);
    JsonArray lock = doc["child_lock"].to<JsonArray>();
    addCoords(lock.add<JsonArray>(), CHILD_LOCK_BUTTON1_X, CHILD_LOCK_BUTTON1_Y);
    addCoords(lock.add<JsonArray>(), CHILD_LOCK_BUTTON2_X, CHILD_LOCK_BUTTON2_Y);
    doc["ha_connected"] = isHomeAssistantConnected();
    doc["customised"] = isButtonConfigCustomised();
    doc["child_lock_on"] = isChildLockMode;
    doc["night"] = isNightMode;

    // So a reboot, or a dropped Home Assistant connection, is visible from
    // the browser. Stack figures are the least free the task has ever had,
    // in bytes.
    JsonObject diag = doc["device"].to<JsonObject>();
    diag["uptime_s"] = millis() / 1000;
    diag["reset_reason"] = resetReasonName(esp_reset_reason());
    diag["ha_disconnects"] = homeAssistantDisconnectCount();
    diag["free_heap"] = esp_get_free_heap_size();
    diag["min_free_heap"] = esp_get_minimum_free_heap_size();
    diag["loop_stack_free"] = uxTaskGetStackHighWaterMark(NULL);
    if (buttonTaskHandle) {
        diag["button_stack_free"] = uxTaskGetStackHighWaterMark(buttonTaskHandle);
    }

    JsonArray buttons = doc["buttons"].to<JsonArray>();
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            const ButtonConfig& cfg = currentLayout[y][x];
            if (cfg.entity_id[0] == '\0') {
                continue;
            }

            EntityState state;
            if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                state = entityStates[y][x];
                xSemaphoreGive(xMutex);
            } else {
                state = {false, cfg.r, cfg.g, cfg.b, cfg.brightness, x, y};
            }

            char hex[8];
            JsonObject button = buttons.add<JsonObject>();
            button["x"] = x;
            button["y"] = y;
            button["entity_id"] = (const char*)cfg.entity_id;
            colorToHex(cfg.r, cfg.g, cfg.b, hex);
            button["color"] = hex;
            button["brightness"] = cfg.brightness;

            // What the key is showing right now, before night-mode dimming.
            JsonObject live = button["live"].to<JsonObject>();
            live["on"] = state.is_on;
            colorToHex(state.r, state.g, state.b, hex);
            live["color"] = hex;
            live["brightness"] = state.brightness;
        }
    }
    sendJson(200, doc);
}

// Make a new layout live: repaint what changed and point the Home Assistant
// subscription at the new set of entities.
static void applyLayoutChange() {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (memcmp(&currentLayout[y][x], &nextLayout[y][x], sizeof(ButtonConfig)) != 0) {
                resetEntityState(x, y);
            }
        }
    }
    refreshAllLEDs();
    resubscribeToEntities();
}

static void handleRoot() {
    if (!admit(false)) {
        return;
    }
    server.sendHeader("Cache-Control", "no-cache");
    sendBody(200, "text/html", WEB_UI_PAGE, strlen(WEB_UI_PAGE));
}

static void handleGetConfig() {
    if (!admit(false)) {
        return;
    }
    sendLayout();
}

// Body: {"buttons": [{"x": 0, "y": 3, "entity_id": "light.desk",
//                     "color": "#ffffff", "brightness": 255}, ...]}
// The whole layout: any button left out ends up with nothing assigned.
static void handlePostConfig() {
    if (!admit(true)) {
        return;
    }

    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) {
        sendError(400, "Body is not valid JSON");
        return;
    }
    JsonArray buttons = body["buttons"];
    if (buttons.isNull()) {
        sendError(400, "Missing buttons array");
        return;
    }

    memset(nextLayout, 0, sizeof(nextLayout));
    for (JsonObject button : buttons) {
        int x = button["x"] | -1;
        int y = button["y"] | -1;
        const char* entity_id = button["entity_id"];
        if (x < 0 || x >= COLS || y < 0 || y >= ROWS) {
            sendError(400, "Button position is off the grid");
            return;
        }
        if (!entity_id || entity_id[0] == '\0') {
            continue;
        }
        if (!isValidEntityId(entity_id)) {
            sendError(400, "Not a valid entity_id");
            return;
        }

        ButtonConfig& cfg = nextLayout[y][x];
        if (cfg.entity_id[0] != '\0') {
            sendError(400, "Two entities on the same button");
            return;
        }
        strlcpy(cfg.entity_id, entity_id, sizeof(cfg.entity_id));
        if (!hexToColor(button["color"] | "#ffffff", cfg.r, cfg.g, cfg.b)) {
            sendError(400, "color must look like #rrggbb");
            return;
        }
        cfg.brightness = constrain(button["brightness"] | 255, 0, 255);
    }

    getAllButtonConfigs(currentLayout);
    bool saved = setAllButtonConfigs(nextLayout);
    applyLayoutChange();

    if (!saved) {
        sendError(500, "Applied, but could not be saved: it will be lost on reboot");
        return;
    }
    sendLayout();
}

static void handleReset() {
    if (!admit(true)) {
        return;
    }
    getAllButtonConfigs(currentLayout);
    if (!resetButtonConfigToDefaults()) {
        sendError(500, "Out of memory");
        return;
    }
    getAllButtonConfigs(nextLayout);
    applyLayoutChange();
    sendLayout();
}

// Body: {"x": 0, "y": 3}. Does what a short press of that key does.
static void handlePress() {
    if (!admit(true)) {
        return;
    }
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) {
        sendError(400, "Body is not valid JSON");
        return;
    }
    int x = body["x"] | -1;
    int y = body["y"] | -1;
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) {
        sendError(400, "Button position is off the grid");
        return;
    }
    if (!isHomeAssistantConnected()) {
        sendError(503, "Not connected to Home Assistant");
        return;
    }
    toggleEntity(x, y);
    server.send(204);
}

// How many entities one /api/entities page carries. Home Assistant caps a
// rendered template at 256KB; 500 of even the longest ids stays well under.
#define ENTITY_PAGE_SIZE 500

// Headers for a body whose length isn't known up front. WebServer closes the
// connection once the handler returns, which marks the end of the body.
static bool sendOpenEndedHeaders() {
    static const char headers[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n\r\n";
    return writeAll(headers, sizeof(headers) - 1);
}

static String homeAssistantUrl(const char* path) {
    return String("http://") + HA_HOST + ":" + String(HA_PORT) + path;
}

// Fallback for a token without admin rights, which /api/template refuses:
// stream all of /api/states and parse it one entity at a time, keeping only
// what the list needs. Slow on a big install -- every attribute of every
// entity goes past -- but it never holds more than one entity at once.
// Answers with everything in one go, whatever page was asked for.
static void streamAllStates() {
    HTTPClient http;
    // HTTP/1.0 so Home Assistant cannot answer chunked; the raw stream is then
    // exactly the JSON body.
    http.useHTTP10(true);
    http.setTimeout(10000);
    if (!http.begin(homeAssistantUrl("/api/states"))) {
        sendError(502, "Could not reach Home Assistant");
        return;
    }
    http.addHeader("Authorization", "Bearer " HA_API_PASSWORD);

    int status = http.GET();
    if (status != 200) {
        http.end();
        char message[64];
        snprintf(message, sizeof(message), "Home Assistant answered %d", status);
        sendError(502, message);
        return;
    }

    JsonDocument filter;
    filter["entity_id"] = true;
    filter["state"] = true;
    filter["attributes"]["friendly_name"] = true;

    bool ok = sendOpenEndedHeaders();
    Stream& stream = http.getStream();
    String out = "{\"entities\":[";
    int count = 0;

    if (ok && stream.find('[')) {
        do {
            JsonDocument entity;
            DeserializationError error = deserializeJson(entity, stream,
                                                         DeserializationOption::Filter(filter),
                                                         DeserializationOption::NestingLimit(32));
            esp_task_wdt_reset(); // a big install takes a while
            if (error) {
                SERIAL_PRINTF("Entity list parse stopped: %s\n", error.c_str());
                break;
            }

            const char* entity_id = entity["entity_id"];
            if (!entity_id || !isSupportedEntity(entity_id) || strlen(entity_id) >= ENTITY_ID_MAX_LEN) {
                continue;
            }

            JsonDocument row;
            row.add(entity_id);
            row.add(entity["attributes"]["friendly_name"] | entity_id);
            row.add(entity["state"] | "");
            if (count > 0) {
                out += ',';
            }
            // serializeJson() replaces a String's contents rather than
            // appending to it, so it can't write into `out` directly.
            String serialized;
            serializeJson(row, serialized);
            out += serialized;
            count++;

            if (out.length() > 1024) {
                ok = writeAll(out.c_str(), out.length());
                out = "";
                if (!ok) {
                    break; // browser went away
                }
            }
        } while (stream.findUntil(",", "]"));
    }

    out += "],\"total\":";
    out += count;
    out += '}';
    if (ok) {
        writeAll(out.c_str(), out.length());
    }
    http.end();
    SERIAL_PRINTF("Sent %d entities to the web UI\n", count);
}

// GET /api/entities?offset=N
//   {"total": 1234, "entities": [[entity_id, name, state], ...]}
// One page of the entities a button can be bound to, from `offset`.
//
// Home Assistant does the filtering, through a template, and the deck just
// passes the result through. The alternative -- parsing every entity's full
// state on the deck -- took ~16s on a 3800-entity install, and the deck
// answers nothing else while it is busy.
static void handleEntities() {
    if (!admit(false)) {
        return;
    }
    long offset = max(0L, server.arg("offset").toInt());

    // Ids too long for a button are left out: they could never be assigned.
    String tpl =
        "{%- set sel = states | selectattr('domain', 'in', " SUPPORTED_DOMAINS_JINJA ")"
        " | rejectattr('entity_id', 'search', '.{" + String(ENTITY_ID_MAX_LEN) + "}') | list -%}"
        "{\"total\":{{ sel | length }},\"entities\":["
        "{%- for s in sel[" + String(offset) + ":" + String(offset + ENTITY_PAGE_SIZE) + "] -%}"
        "{{- ',' if not loop.first -}}{{ [s.entity_id, s.name, s.state] | to_json }}"
        "{%- endfor -%}]}";
    JsonDocument request;
    request["template"] = tpl;
    String body;
    serializeJson(request, body);

    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(10000);
    if (!http.begin(homeAssistantUrl("/api/template"))) {
        sendError(502, "Could not reach Home Assistant");
        return;
    }
    http.addHeader("Authorization", "Bearer " HA_API_PASSWORD);
    http.addHeader("Content-Type", "application/json");

    int status = http.POST(body);
    if (status == 401 || status == 403) {
        http.end();
        SERIAL_PRINTLN("Token can't render templates, falling back to /api/states");
        streamAllStates();
        return;
    }
    if (status != 200) {
        http.end();
        char message[64];
        snprintf(message, sizeof(message), "Home Assistant answered %d", status);
        sendError(502, message);
        return;
    }

    int length = http.getSize();
    bool ok;
    if (length >= 0) {
        server.setContentLength(length);
        server.send(200, "application/json", "");
        ok = true;
    } else {
        ok = sendOpenEndedHeaders();
    }

    Stream& stream = http.getStream();
    uint8_t buffer[1024];
    long remaining = length;
    while (ok && (length < 0 || remaining > 0)) {
        size_t want = length < 0 ? sizeof(buffer) : min((long)sizeof(buffer), remaining);
        size_t got = stream.readBytes(buffer, want);
        if (got == 0) {
            break; // Home Assistant finished (or stalled past the timeout)
        }
        ok = writeAll((const char*)buffer, got);
        remaining -= got;
        esp_task_wdt_reset();
    }
    http.end();
}

void initWebUI() {
    const char* headers[] = {"Content-Type"};
    server.collectHeaders(headers, 1);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/config", HTTP_POST, handlePostConfig);
    server.on("/api/reset", HTTP_POST, handleReset);
    server.on("/api/press", HTTP_POST, handlePress);
    server.on("/api/entities", HTTP_GET, handleEntities);
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not found");
    });
    server.begin();
    SERIAL_PRINTLN("Web UI started");
}

void onWebUIWiFiConnected() {
    SERIAL_PRINTF("Web UI at http://%s/ and http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), DEVICE_HOSTNAME);
    if (!mdnsStarted && MDNS.begin(DEVICE_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
    }
}

void handleWebUI() {
    // WiFiClient::connected() decides from errno without clearing it first,
    // so whatever socket call last failed on this task -- the Home Assistant
    // websocket runs here too -- could make a brand new browser connection
    // look closed. WebServer then dropped it unanswered: a request that
    // failed for no visible reason, most often right after an entity load.
    errno = 0;
    server.handleClient();
}

#else

void initWebUI() {}
void onWebUIWiFiConnected() {}
void handleWebUI() {}

#endif // ENABLE_WEB_UI
