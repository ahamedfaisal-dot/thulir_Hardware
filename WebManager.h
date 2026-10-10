/*
 * ============================================================
 *  THULIR — Web Dashboard Telemetry Manager
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

// A command received from the dashboard server (see WebManager.cpp).
struct WebCommand {
    uint32_t id;           // unique, never 0
    char     name[12];     // "start" | "abort"
    bool     hasRecipe;    // start only
    uint16_t hold[4];      // hold minutes, steps 1-4
    float    t2;           // step 2 target (0/15/25)
    float    t4;           // step 4 target (0/25)
};

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

    // Remote-command interface. takeCommand() returns true ONCE per new
    // command; the caller executes it and then reports the outcome with
    // setAck(), which is sent in the next telemetry packet (sent at once).
    bool takeCommand(WebCommand& out);
    void setAck(uint32_t id, bool ok, const char* msg);

private:
#if WEB_DASHBOARD_ENABLED
    unsigned long _lastPostTime;
    unsigned long _lastWifiAttempt;
    bool          _wifiStarted;
    WebCommand    _pending;
    volatile bool _hasPending;
    uint32_t      _lastCmdId;
    uint32_t      _ackId;
    bool          _ackOk;
    char          _ackMsg[48];

    // HTTP POST runs in its own FreeRTOS task so a slow/unreachable server
    // can never stall loop() (and therefore keypad scanning).
    char              _json[1024];
    volatile bool     _postReady;
    bool              _taskStarted;
    static void       postTask(void* arg);
    void              doPost();

    void parseCommand(const char* body);

    void buildJson(char* buf, size_t bufSize, const SystemStatus& status);
#endif
};
