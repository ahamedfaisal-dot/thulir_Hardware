/*
 * ============================================================
 *  TULIR — Web Dashboard Telemetry Manager
 *  WebManager.h
 * ============================================================
 *  Non-blocking WiFi + HTTP POST to a local Flask dashboard server.
 *  Fully isolated from the control loop: WiFi/HTTP failures never
 *  affect PID, safety, or Peltier control — same principle already
 *  used for AudioManager/DisplayManager.
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include "Config.h"

#if WEB_DASHBOARD_ENABLED
#include <WiFi.h>
#include <HTTPClient.h>
#endif

class WebManager {
public:
    WebManager();

    // Starts WiFi connection attempt (non-blocking — does not wait here).
    void begin();

    // Call every loop() iteration. Internally self-paced:
    //  - checks/retries WiFi connection
    //  - POSTs a telemetry JSON packet at WEB_POST_INTERVAL_MS
    void update(const SystemStatus& status);

    bool isConnected() const;

private:
#if WEB_DASHBOARD_ENABLED
    unsigned long _lastPostTime;
    unsigned long _lastWifiAttempt;
    bool          _wifiStarted;

    void buildJson(char* buf, size_t bufSize, const SystemStatus& status);
#endif
};
