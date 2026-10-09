/*
 * ============================================================
 *  THULIR — Web Dashboard Telemetry Manager Implementation
 *  WebManager.cpp
 * ============================================================
 */

#include "WebManager.h"

#if WEB_DASHBOARD_ENABLED

WebManager::WebManager()
    : _lastPostTime(0)
    , _lastWifiAttempt(0)
    , _wifiStarted(false)
    , _hasPending(false)
    , _lastCmdId(0)
    , _ackId(0)
    , _ackOk(false)
{
    memset(&_pending, 0, sizeof(_pending));
    _ackMsg[0] = '\0';
}

// ------------------------------------------------------------
//  Tiny JSON helpers for the (server-generated, compact) reply.
//  No JSON library: the server emits  "cmd":{"id":N,"name":"start",
//  "h":[a,b,c,d],"t2":X,"t4":Y}  and nothing else is trusted.
// ------------------------------------------------------------
static bool jsonNum(const char* s, const char* key, double& out) {
    char pat[16];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(s, pat);
    if (!p) return false;
    p += strlen(pat);
    char* end = nullptr;
    out = strtod(p, &end);
    return end != p;
}

void WebManager::parseCommand(const char* body) {
    const char* c = strstr(body, "\"cmd\":{");
    if (!c) return;
    double id = 0;
    if (!jsonNum(c, "id", id) || id < 1) return;
    uint32_t cid = (uint32_t)id;
    if (cid == _lastCmdId || _hasPending) return;   // already seen / busy

    WebCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.id = cid;
    const char* n = strstr(c, "\"name\":\"");
    if (!n) return;
    n += 8;
    size_t i = 0;
    while (*n && *n != '"' && i < sizeof(cmd.name) - 1) cmd.name[i++] = *n++;
    cmd.name[i] = '\0';

    const char* h = strstr(c, "\"h\":[");
    if (h) {
        h += 5;
        uint8_t k = 0;
        bool ok = true;
        for (; k < 4; k++) {
            char* end = nullptr;
            long v = strtol(h, &end, 10);
            if (end == h || v < 0 || v > 65535) { ok = false; break; }
            cmd.hold[k] = (uint16_t)v;
            h = end;
            if (*h == ',') h++;
        }
        double t2 = 0, t4 = 0;
        if (ok && jsonNum(c, "t2", t2) && jsonNum(c, "t4", t4)) {
            cmd.t2 = (float)t2;
            cmd.t4 = (float)t4;
            cmd.hasRecipe = true;
        }
    }
    _pending = cmd;
    _hasPending = true;
    _lastCmdId = cid;
    Serial.printf("[WEB] Command received: id=%lu name=%s\n", (unsigned long)cid, cmd.name);
}

bool WebManager::takeCommand(WebCommand& out) {
    if (!_hasPending) return false;
    out = _pending;
    _hasPending = false;
    return true;
}

void WebManager::setAck(uint32_t id, bool ok, const char* msg) {
    _ackId = id;
    _ackOk = ok;
    strncpy(_ackMsg, msg, sizeof(_ackMsg) - 1);
    _ackMsg[sizeof(_ackMsg) - 1] = '\0';
    _lastPostTime = 0;          // report the outcome in the very next update()
    Serial.printf("[WEB] Ack id=%lu ok=%d (%s)\n", (unsigned long)id, ok, _ackMsg);
}

void WebManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    _wifiStarted = true;
    _lastWifiAttempt = millis();
    Serial.printf("[WEB] Connecting to WiFi '%s'...\n", WIFI_SSID);
}

bool WebManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

void WebManager::buildJson(char* buf, size_t bufSize, const SystemStatus& status) {
    unsigned long elapsed = 0, remaining = 0;
    if (status.state == STATE_STEP_HOLD && status.holdDurationMs > 0) {
        elapsed = status.holdElapsedMs;
        remaining = (status.holdDurationMs > elapsed)
                    ? (status.holdDurationMs - elapsed) : 0;
    } else if (status.state == STATE_STEP_APPROACH && status.stepStartTime > 0) {
        elapsed = millis() - status.stepStartTime;
    }

    snprintf(buf, bufSize,
        "{"
        "\"state\":\"%s\","
        "\"error\":\"%s\","
        "\"step\":%d,"
        "\"actualTemp\":%.2f,"
        "\"filteredTemp\":%.2f,"
        "\"targetTemp\":%.2f,"
        "\"setpoint\":%.2f,"
        "\"humidity\":%.1f,"
        "\"humidityValid\":%s,"
        "\"pidOutput\":%.1f,"
        "\"bottomPWM\":%.1f,"
        "\"middlePWM\":%.1f,"
        "\"topPWM\":%.1f,"
        "\"sensorValid\":%s,"
        "\"elapsedMs\":%lu,"
        "\"remainingMs\":%lu,"
        "\"rampLag\":%s,"
        "\"rampLagAmount\":%.2f,"
        "\"uptimeMs\":%lu,"
        "\"servosOpen\":%s,"
        "\"remoteCtl\":%s,"
        "\"ackId\":%lu,"
        "\"ackOk\":%s,"
        "\"ackMsg\":\"%s\""
        "}",
        getStateName(status.state),
        getErrorName(status.errorCode),
        status.currentStep,
        status.actualTemp,
        status.filteredTemp,
        status.targetTemp,
        status.currentSetpoint,
        status.humidity,
        status.humidityValid ? "true" : "false",
        status.pidOutput,
        status.bottomPWM,
        status.middlePWM,
        status.topPWM,
        status.sensorValid ? "true" : "false",
        elapsed,
        remaining,
        status.rampLag ? "true" : "false",
        status.rampLagAmount,
        millis(),
        status.servosOpen ? "true" : "false",
        WEB_REMOTE_CONTROL_ENABLED ? "true" : "false",
        (unsigned long)_ackId,
        _ackOk ? "true" : "false",
        _ackMsg
    );
}

void WebManager::update(const SystemStatus& status) {
    unsigned long now = millis();

    if (!_wifiStarted) return;

    // --- WiFi connection management (non-blocking) ---
    if (WiFi.status() != WL_CONNECTED) {
        if ((now - _lastWifiAttempt) >= WEB_WIFI_RETRY_MS) {
            _lastWifiAttempt = now;
            Serial.println("[WEB] WiFi not connected — retrying...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        return;  // Skip POST attempt while disconnected
    }

    // --- Telemetry POST (time-gated) ---
    if ((now - _lastPostTime) < WEB_POST_INTERVAL_MS) return;
    _lastPostTime = now;

    char json[768];
    buildJson(json, sizeof(json), status);

    HTTPClient http;
    char url[80];
    snprintf(url, sizeof(url), "http://%s:%d%s",
             WEB_SERVER_HOST, WEB_SERVER_PORT, WEB_SERVER_PATH);

    // Bounded timeout so a dead/unreachable server can't stall the
    // control loop for more than ~1s worst case (POST is still a
    // genuine blocking call under the hood — kept short and infrequent
    // by design, same tradeoff already accepted for the display's
    // ID-readback health check).
    http.setTimeout(1000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(json);
    if (code <= 0) {
        Serial.printf("[WEB] POST failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();
}

#else  // WEB_DASHBOARD_ENABLED == false — stub, no WiFi/HTTP code compiled

WebManager::WebManager() {}
void WebManager::begin() {}
void WebManager::update(const SystemStatus&) {}
bool WebManager::isConnected() const { return false; }
bool WebManager::takeCommand(WebCommand&) { return false; }
void WebManager::setAck(uint32_t, bool, const char*) {}

#endif
