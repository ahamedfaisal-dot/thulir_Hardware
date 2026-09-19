/*
 * ============================================================
 *  TULIR — Display Manager
 *  DisplayManager.h
 * ============================================================
 *  Industrial HMI on 2.8" ILI9341 TFT (320×240, landscape).
 *
 *  Screens:
 *    HOME         — Main operating display
 *    MENU         — Main navigation menu
 *    PROGRAM      — Recipe step list
 *    STEP_EDIT    — Edit individual step parameters
 *    NUM_ENTRY    — Numeric input overlay
 *    CONFIRM_START— Start confirmation dialog
 *    CONFIRM_STOP — Stop confirmation dialog
 *    PID          — PID parameter view/edit
 *    CALIBRATION  — Temperature offset calibration
 *    TEST         — Test mode menu
 *    TEST_COMPONENT— Individual component test
 *    ABOUT        — System info
 *    FAULT        — Fault display
 *    COMPLETE     — Process complete screen
 *    STOPPED      — Emergency stop screen
 *    MANUAL_PWM   — Manual PWM control (protected)
 *
 *  Design: Clean industrial HMI — dark background, high-contrast
 *  white/blue text, amber targets, green/red status indicators.
 *  No gaming/cyberpunk aesthetics.
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>   // TFT_eSPI was dropped: it fails to
                                // communicate with this panel on this
                                // ESP32-S3 board (confirmed via register
                                // read returning 0x00 0x00 0x00), while
                                // Adafruit_ILI9341 works on the exact
                                // same pins. See Config.h for pin defines.
#include "Config.h"

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();

    // Initialize TFT display
    bool begin();

    // Draw a specific screen (full redraw)
    void drawScreen(ScreenID screen, const SystemStatus& status, const Recipe& recipe);

    // Selective update of dynamic values on the home screen.
    // Only redraws fields that have changed since last draw.
    void updateHomeScreen(const SystemStatus& status);

    // Update PID tuning screen with live values
    void updatePIDScreen(float kp, float ki, float kd,
                         float temp, float setpoint, float output,
                         float pTerm, float iTerm, float dTerm);

    // Draw numeric entry field (overlay on current screen)
    void drawNumericEntry(const char* prompt, const char* value,
                          int minVal, int maxVal);

    // Show a temporary message (auto-clears after timeout)
    void showMessage(const char* msg, uint16_t color = COLOR_TEXT_PRIMARY);

    // Draw a progress/PWM bar
    void drawBar(int x, int y, int w, int h, float percent, uint16_t fillColor);

    // Get the underlying TFT object for advanced use
    Adafruit_ILI9341* getTFT();

private:
    Adafruit_ILI9341*  _tft;
    ScreenID           _currentScreen;
    unsigned long      _messageExpiry;

    // Cached values for selective redraw (home screen)
    float    _lastActualTemp;
    float    _lastTargetTemp;
    float    _lastSetpoint;
    float    _lastPidOutput;
    float    _lastBottomPWM;
    float    _lastMiddlePWM;
    float    _lastTopPWM;
    float    _lastHumidity;
    uint8_t  _lastStep;
    SystemState _lastState;
    unsigned long _lastHoldElapsed;
    unsigned long _lastHoldDuration;
    bool     _lastRampLag;

    // Drawing helpers
    void drawHeader();
    void drawDivider(int y);
    void drawKeyValue(int x, int y, const char* label, const char* value,
                      uint16_t labelColor, uint16_t valueColor, uint8_t textSize = 1);
    void drawCenteredText(int y, const char* text, uint16_t color, uint8_t size);
    void clearValueArea(int x, int y, int w, int h);

    // Screen-specific draw functions
    void drawHomeScreen(const SystemStatus& status);
    void drawMenuScreen(const SystemStatus& status);
    void drawProgramScreen(const Recipe& recipe);
    void drawStepEditScreen(const SystemStatus& status, const Recipe& recipe);
    void drawConfirmStartScreen();
    void drawConfirmStopScreen();
    void drawPIDScreen();
    void drawCalibrationScreen(const SystemStatus& status);
    void drawTestScreen();
    void drawTestComponentScreen(const SystemStatus& status);
    void drawAboutScreen();
    void drawFaultScreen(const SystemStatus& status);
    void drawCompleteScreen(const SystemStatus& status);
    void drawStoppedScreen();
    void drawManualPWMScreen(const SystemStatus& status);

    // Format helpers
    void formatTime(unsigned long ms, char* buf, size_t len);
    void formatTemp(float temp, char* buf, size_t len);
};
