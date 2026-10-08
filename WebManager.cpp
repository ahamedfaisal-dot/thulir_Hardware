/*
 * ============================================================
 *  TULIR — Web Dashboard Telemetry Manager Implementation
 *  WebManager.cpp
 * ============================================================
 */

#include "WebManager.h"

#if WEB_DASHBOARD_ENABLED

WebManager::WebManager()
    : _lastPostTime(0)
    , _lastWifiAttempt(0)
    , _wifiStarted(false)
{
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
        "\"uptimeMs\":%lu"
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
        millis()
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

    char json[512];
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

#endif
