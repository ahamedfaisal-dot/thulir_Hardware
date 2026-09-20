/*
 * ============================================================
 *  TULIR — Display Manager Implementation
 *  DisplayManager.cpp
 * ============================================================
 *  Industrial HMI for ILI9341 320×240 TFT (landscape).
 *  Uses Adafruit_ILI9341 + Adafruit_GFX. Pins are passed directly in
 *  begin() (see Config.h) — no separate library config file needed.
 * ============================================================
 */

#include "DisplayManager.h"

DisplayManager::DisplayManager()
    : _tft(nullptr)
    , _currentScreen(SCREEN_HOME)
    , _messageExpiry(0)
    , _lastActualTemp(-999.0f)
    , _lastTargetTemp(-999.0f)
    , _lastSetpoint(-999.0f)
    , _lastPidOutput(-1.0f)
    , _lastBottomPWM(-1.0f)
    , _lastMiddlePWM(-1.0f)
    , _lastTopPWM(-1.0f)
    , _lastHumidity(-1.0f)
    , _lastStep(0)
    , _lastState(STATE_BOOT)
    , _lastHoldElapsed(0xFFFFFFFF)
    , _lastHoldDuration(0xFFFFFFFF)
    , _lastRampLag(false)
    , _homeFrameReady(false)
{
}

DisplayManager::~DisplayManager() {
    // _tft points to a static instance — do NOT delete it.
    _tft = nullptr;
}

bool DisplayManager::begin(bool showSplash) {
    // Adafruit_ILI9341 + the SPIClass it drives MUST be static/global —
    // never heap-allocated — for the same reason TFT_eSPI required it:
    // the underlying SPI peripheral state must persist for the life of
    // the program. A function-local static is constructed once on first
    // call and persists thereafter (same as a global).
    static SPIClass tftSPI(FSPI);
    static Adafruit_ILI9341 tftInstance(&tftSPI, TFT_DC_PIN, TFT_CS_PIN, TFT_RST_PIN);
    _tft = &tftInstance;

    tftSPI.begin(TFT_SCK_PIN, TFT_MISO_PIN, TFT_MOSI_PIN, TFT_CS_PIN);
    _tft->begin();
    delay(150);   // ILI9341 stabilization after hardware reset
    _tft->setRotation(3);  // Landscape, 320×240, USB connector on left
    _tft->fillScreen(COLOR_BG);

    if (showSplash) {
        // Splash screen — skipped on a silent watchdog recovery re-init
        // so the live dashboard doesn't disappear for over a second
        // every time the display glitches back to life.
        drawCenteredText(95, "TULIR", COLOR_TEXT_PRIMARY, 3);
        drawCenteredText(135, "3-Stage Peltier Controller", COLOR_TEMP_ACTUAL, 1);
        drawCenteredText(160, "Initializing...", COLOR_TEXT_SECONDARY, 1);
        delay(1200);  // Hold splash screen so user sees boot progress
    }

    Serial.println("[DISPLAY] ILI9341 via Adafruit_ILI9341 initialized (320x240 landscape)");
    Serial.printf("[DISPLAY] MOSI=%d SCK=%d MISO=%d CS=%d DC=%d RST=%d\n",
                  TFT_MOSI_PIN, TFT_SCK_PIN, TFT_MISO_PIN, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

    return true;
}

// ============================================================
//  SCREEN ROUTER
// ============================================================

void DisplayManager::drawScreen(ScreenID screen, const SystemStatus& status,
                                 const Recipe& recipe) {
    // ---------------------------------------------------------------
    //  HOME screen fast-path: if we are already on HOME and the static
    //  frame is painted, skip fillScreen (eliminates the visible black
    //  flash that occurs on every step transition / state change).
    //  All dynamic fields are invalidated so updateHomeScreen redraws them.
    // ---------------------------------------------------------------
    bool wasAlreadyHome = (_currentScreen == SCREEN_HOME) && _homeFrameReady;
    _currentScreen = screen;

    if (screen == SCREEN_HOME && wasAlreadyHome) {
        _lastActualTemp   = -999.0f;
        _lastTargetTemp   = -999.0f;
        _lastSetpoint     = -999.0f;
        _lastPidOutput    = -1.0f;
        _lastBottomPWM    = -1.0f;
        _lastMiddlePWM    = -1.0f;
        _lastTopPWM       = -1.0f;
        _lastHumidity     = -1.0f;
        _lastStep         = 255;
        _lastState        = (SystemState)255;
        _lastHoldElapsed  = 0xFFFFFFFF;
        _lastHoldDuration = 0xFFFFFFFF;
        _lastRampLag      = false;
        updateHomeScreen(status);
        return;
    }

    // Full redraw for any other screen (or first time on HOME)
    _homeFrameReady = false;

    // Reset cached values to force full redraw
    _lastActualTemp = -999.0f;
    _lastTargetTemp = -999.0f;
    _lastSetpoint = -999.0f;
    _lastPidOutput = -1.0f;
    _lastBottomPWM = -1.0f;
    _lastMiddlePWM = -1.0f;
    _lastTopPWM = -1.0f;
    _lastStep = 0;
    _lastState = STATE_BOOT;

    _tft->fillScreen(COLOR_BG);

    switch (screen) {
        case SCREEN_HOME:
            drawHomeScreen(status);
            _homeFrameReady = true;
            break;
        case SCREEN_MENU:           drawMenuScreen(status); break;
        case SCREEN_PROGRAM:        drawProgramScreen(recipe); break;
        case SCREEN_STEP_EDIT:      drawStepEditScreen(status, recipe); break;
        case SCREEN_CONFIRM_START:  drawConfirmStartScreen(); break;
        case SCREEN_CONFIRM_STOP:   drawConfirmStopScreen(); break;
        case SCREEN_PID:            drawPIDScreen(); break;
        case SCREEN_CALIBRATION:    drawCalibrationScreen(status); break;
        case SCREEN_TEST:           drawTestScreen(); break;
        case SCREEN_TEST_COMPONENT: drawTestComponentScreen(status); break;
        case SCREEN_ABOUT:          drawAboutScreen(); break;
        case SCREEN_FAULT:          drawFaultScreen(status); break;
        case SCREEN_COMPLETE:       drawCompleteScreen(status); break;
        case SCREEN_STOPPED:        drawStoppedScreen(); break;
        case SCREEN_MANUAL_PWM:     drawManualPWMScreen(status); break;
        default:                    drawHomeScreen(status); _homeFrameReady = true; break;
    }
}

// ============================================================
//  HOME SCREEN — Main operating display
// ============================================================
/*
 *  Layout (320×240 landscape):
 *  ┌──────────────────────────────────┐
 *  │  TULIR                    IDLE   │ Y=0-22    Header
 *  ├──────────────────────────────────┤
 *  │  ACTUAL          TARGET          │ Y=24-30   Labels
 *  │   25.3°C          25.0°C         │ Y=32-56   Big values
 *  ├──────────────────────────────────┤
 *  │  STEP 1/5   STATUS: APPROACHING │ Y=60-78   Step/Status
 *  ├──────────────────────────────────┤
 *  │  BOT [████████░░] 67%           │ Y=82-96   PWM bars
 *  │  MID [████░░░░░░] 34%           │ Y=98-112
 *  │  TOP [██░░░░░░░░] 13%           │ Y=114-128
 *  ├──────────────────────────────────┤
 *  │  PID: 67%   SETPOINT: -14.0°C   │ Y=132-148  PID info
 *  ├──────────────────────────────────┤
 *  │  ELAPSED: 14:30  REMAIN: 05:30  │ Y=152-186  Timers
 *  ├──────────────────────────────────┤
 *  │  D=MENU  C=STOP                 │ Y=220-240  Footer
 *  └──────────────────────────────────┘
 */

void DisplayManager::drawHomeScreen(const SystemStatus& status) {
    drawHeader();

    // Status text in header
    const char* stName = getStateName(status.state);
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_HEADER_BG);
    _tft->setCursor(320 - strlen(stName) * 6 - 8, 6);
    _tft->print(stName);

    drawDivider(22);

    // --- Temperature + Humidity labels ---
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    _tft->setCursor(6, 26);
    _tft->print("ACTUAL");
    _tft->setCursor(114, 26);
    _tft->print("HUMIDITY");
    _tft->setCursor(196, 26);
    _tft->print("TARGET");

    drawDivider(58);

    // --- Step and Status labels ---
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    _tft->setCursor(10, 63);
    _tft->print("STEP");
    _tft->setCursor(130, 63);
    _tft->print("STATUS");

    drawDivider(80);

    // --- PWM bar labels ---
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    _tft->setCursor(4, 86);  _tft->print("BOT");
    _tft->setCursor(4, 102); _tft->print("MID");
    _tft->setCursor(4, 118); _tft->print("TOP");

    // PWM bar backgrounds
    drawBar(30, 84, 210, 12, status.bottomPWM, COLOR_BAR_FILL);
    drawBar(30, 100, 210, 12, status.middlePWM, COLOR_BAR_FILL);
    drawBar(30, 116, 210, 12, status.topPWM, COLOR_BAR_FILL);

    drawDivider(132);

    // --- PID/Setpoint label ---
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    _tft->setCursor(10, 138);
    _tft->print("PID");
    _tft->setCursor(130, 138);
    _tft->print("SETPOINT");

    drawDivider(152);

    // --- Timer labels ---
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    _tft->setCursor(10, 160);
    _tft->print("ELAPSED");
    _tft->setCursor(170, 160);
    _tft->print("REMAINING");

    // Ramp info area (used in Step 5)
    _tft->setCursor(10, 195);
    _tft->setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    if (status.state == STATE_STEP5_RAMP) {
        _tft->print("RAMP: -1.0 C/min");
    }

    drawDivider(210);

    // --- Footer ---
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    _tft->setCursor(10, 222);
    _tft->print("[D] MENU");
    _tft->setCursor(240, 222);
    _tft->print("[C] STOP");

    // Prime cache so updateHomeScreen draws everything on first pass
    _lastActualTemp = -999.0f;
    _lastTargetTemp = -999.0f;
    _lastSetpoint = -999.0f;
    _lastPidOutput = -1.0f;
    _lastBottomPWM = -1.0f;
    _lastMiddlePWM = -1.0f;
    _lastTopPWM = -1.0f;
    _lastHumidity = -1.0f;
    _lastStep = 255;
    _lastState = (SystemState)255;
    _lastHoldElapsed = 0xFFFFFFFF;
    _lastHoldDuration = 0xFFFFFFFF;

    // Now update the dynamic values
    updateHomeScreen(status);
}

void DisplayManager::updateHomeScreen(const SystemStatus& status) {
    if (!_tft) return;
    char buf[16];

    // Auto-clear temporary popup message after timeout
    if (_messageExpiry > 0 && millis() > _messageExpiry) {
        _messageExpiry = 0;
        _tft->fillRect(15, 95, 290, 50, COLOR_BG);
        _lastBottomPWM = -1.0f;
        _lastMiddlePWM = -1.0f;
        _lastTopPWM = -1.0f;
        _lastPidOutput = -1.0f;
        _lastSetpoint = -999.0f;
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
        _tft->setCursor(4, 102); _tft->print("MID");
        _tft->setCursor(4, 118); _tft->print("TOP");
        _tft->setCursor(10, 138); _tft->print("PID");
        _tft->setCursor(130, 138); _tft->print("SETPOINT");
        drawDivider(132);
    }

    // --- Actual Temperature ---
    if (fabsf(status.filteredTemp - _lastActualTemp) > 0.05f) {
        clearValueArea(6, 36, 104, 20);
        formatTemp(status.filteredTemp, buf, sizeof(buf));
        _tft->setTextSize(2);
        _tft->setTextColor(COLOR_TEMP_ACTUAL, COLOR_BG);
        _tft->setCursor(6, 38);
        _tft->print(buf);
        _lastActualTemp = status.filteredTemp;
    }

    // --- Humidity (between ACTUAL and TARGET) ---
    if (fabsf(status.humidity - _lastHumidity) > 0.5f) {
        clearValueArea(114, 36, 78, 20);
        _tft->setTextSize(2);
        _tft->setCursor(114, 38);
        if (status.humidityValid) {
            _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
            snprintf(buf, sizeof(buf), "%.0f%%", status.humidity);
        } else {
            _tft->setTextColor(COLOR_TEXT_DIM, COLOR_BG);
            snprintf(buf, sizeof(buf), "--");
        }
        _tft->print(buf);
        _lastHumidity = status.humidity;
    }

    // --- Target Temperature ---
    float displayTarget = (status.state == STATE_STEP5_RAMP)
                          ? status.currentSetpoint : status.targetTemp;
    if (fabsf(displayTarget - _lastTargetTemp) > 0.05f) {
        clearValueArea(196, 36, 124, 20);
        formatTemp(displayTarget, buf, sizeof(buf));
        _tft->setTextSize(2);
        _tft->setTextColor(COLOR_TEMP_TARGET, COLOR_BG);
        _tft->setCursor(196, 38);
        _tft->print(buf);
        _lastTargetTemp = displayTarget;
    }

    // --- Step number ---
    if (status.currentStep != _lastStep) {
        clearValueArea(36, 63, 80, 12);
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
        _tft->setCursor(36, 63);
        if (status.currentStep == 0) {
            _tft->print("READY");
        } else {
            snprintf(buf, sizeof(buf), "%d / 5", status.currentStep);
            _tft->print(buf);
        }
        _lastStep = status.currentStep;
    }

    // --- Status ---
    if (status.state != _lastState) {
        clearValueArea(166, 63, 150, 12);
        uint16_t stColor;
        switch (status.state) {
            case STATE_STEP_HOLD:    stColor = COLOR_STATUS_OK;   break;
            case STATE_STEP5_RAMP:   stColor = COLOR_TEMP_TARGET; break;
            case STATE_FAULT:
            case STATE_STOPPED:      stColor = COLOR_STATUS_ERR;  break;
            case STATE_COMPLETE:     stColor = COLOR_STATUS_OK;   break;
            default:                 stColor = COLOR_TEXT_PRIMARY; break;
        }
        _tft->setTextSize(1);
        _tft->setTextColor(stColor, COLOR_BG);
        _tft->setCursor(166, 63);
        _tft->print(getStateName(status.state));
        _lastState = status.state;

        // Update ramp info
        if (status.state == STATE_STEP5_RAMP) {
            clearValueArea(10, 195, 300, 12);
            _tft->setTextSize(1);
            _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
            _tft->setCursor(10, 195);
            snprintf(buf, sizeof(buf), "RAMP: -1.0 C/min  FINAL: %.0f C",
                     RAMP_FINAL_TARGET);
            _tft->print(buf);
        }
    }

    // --- PWM bars ---
    if (fabsf(status.bottomPWM - _lastBottomPWM) > 0.5f) {
        drawBar(30, 84, 210, 12, status.bottomPWM, COLOR_BAR_FILL);
        clearValueArea(250, 84, 60, 12);
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
        _tft->setCursor(250, 86);
        snprintf(buf, sizeof(buf), "%.0f%%", status.bottomPWM);
        _tft->print(buf);
        _lastBottomPWM = status.bottomPWM;
    }

    if (fabsf(status.middlePWM - _lastMiddlePWM) > 0.5f) {
        drawBar(30, 100, 210, 12, status.middlePWM, COLOR_BAR_FILL);
        clearValueArea(250, 100, 60, 12);
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
        _tft->setCursor(250, 102);
        snprintf(buf, sizeof(buf), "%.0f%%", status.middlePWM);
        _tft->print(buf);
        _lastMiddlePWM = status.middlePWM;
    }

    if (fabsf(status.topPWM - _lastTopPWM) > 0.5f) {
        drawBar(30, 116, 210, 12, status.topPWM, COLOR_BAR_FILL);
        clearValueArea(250, 116, 60, 12);
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
        _tft->setCursor(250, 118);
        snprintf(buf, sizeof(buf), "%.0f%%", status.topPWM);
        _tft->print(buf);
        _lastTopPWM = status.topPWM;
    }

    // --- PID output ---
    if (fabsf(status.pidOutput - _lastPidOutput) > 0.5f) {
        clearValueArea(30, 138, 80, 12);
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
        _tft->setCursor(30, 138);
        snprintf(buf, sizeof(buf), "%.1f%%", status.pidOutput);
        _tft->print(buf);
        _lastPidOutput = status.pidOutput;
    }

    // --- Setpoint ---
    if (fabsf(status.currentSetpoint - _lastSetpoint) > 0.05f) {
        clearValueArea(195, 138, 100, 12);
        formatTemp(status.currentSetpoint, buf, sizeof(buf));
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEMP_TARGET, COLOR_BG);
        _tft->setCursor(195, 138);
        _tft->print(buf);
        _lastSetpoint = status.currentSetpoint;
    }

    // --- Timers ---
    unsigned long elapsed = 0;
    unsigned long remaining = 0;

    if (status.state == STATE_STEP_HOLD && status.holdDurationMs > 0) {
        elapsed = status.holdElapsedMs;
        remaining = (status.holdDurationMs > elapsed)
                    ? (status.holdDurationMs - elapsed) : 0;
    } else if (status.state == STATE_STEP5_RAMP && status.rampStartTime > 0) {
        elapsed = millis() - status.rampStartTime;
        remaining = 0;  // Ramp doesn't have a fixed remaining time
    } else if (status.state == STATE_STEP_APPROACH && status.stepStartTime > 0) {
        elapsed = millis() - status.stepStartTime;
    }

    if ((elapsed / 1000UL) != (_lastHoldElapsed / 1000UL) ||
        status.holdDurationMs != _lastHoldDuration) {
        // Elapsed
        clearValueArea(10, 172, 140, 18);
        formatTime(elapsed, buf, sizeof(buf));
        _tft->setTextSize(2);
        _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
        _tft->setCursor(10, 172);
        _tft->print(buf);

        // Remaining
        clearValueArea(170, 172, 140, 18);
        if (remaining > 0) {
            formatTime(remaining, buf, sizeof(buf));
            _tft->setCursor(170, 172);
            _tft->print(buf);
        } else if (status.state == STATE_STEP5_RAMP) {
            _tft->setCursor(170, 172);
            _tft->setTextSize(1);
            _tft->setTextColor(COLOR_TEXT_DIM, COLOR_BG);
            _tft->print("RAMP ACTIVE");
        }

        _lastHoldElapsed = elapsed;
        _lastHoldDuration = status.holdDurationMs;
    }

    // --- Ramp lag warning ---
    if (status.rampLag != _lastRampLag && status.state == STATE_STEP5_RAMP) {
        clearValueArea(200, 195, 110, 12);
        if (status.rampLag) {
            _tft->setTextSize(1);
            _tft->setTextColor(COLOR_STATUS_WARN, COLOR_BG);
            _tft->setCursor(200, 195);
            snprintf(buf, sizeof(buf), "RAMP LAG %.1fC", status.rampLagAmount);
            _tft->print(buf);
        }
        _lastRampLag = status.rampLag;
    }
}

// ============================================================
//  MENU SCREEN
// ============================================================

void DisplayManager::drawMenuScreen(const SystemStatus& status) {
    drawHeader();
    drawDivider(22);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 28);
    _tft->print("MAIN MENU");

    drawDivider(38);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int y = 44 + i * 24;
        bool selected = (i == status.menuSelection);

        if (selected) {
            _tft->fillRect(0, y - 2, 320, 20, COLOR_MENU_SEL_BG);
            _tft->setTextColor(COLOR_TEMP_ACTUAL);
            _tft->setCursor(10, y);
            _tft->print("> ");
        } else {
            _tft->setTextColor(COLOR_TEXT_PRIMARY);
            _tft->setCursor(10, y);
            _tft->print("  ");
        }

        _tft->setTextSize(2);
        _tft->print(MENU_LABELS[i]);
    }

    drawDivider(210);
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 225);
    _tft->print("A/B=NAV  D=SELECT  C=BACK");
}

// ============================================================
//  PROGRAM SCREEN — Recipe step overview
// ============================================================

void DisplayManager::drawProgramScreen(const Recipe& recipe) {
    drawHeader();
    drawDivider(22);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 28);
    _tft->print("RECIPE PROGRAM");

    drawDivider(38);

    char buf[32];

    for (int i = 0; i < 5; i++) {
        int y = 44 + i * 28;

        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_SECONDARY);
        _tft->setCursor(10, y);
        snprintf(buf, sizeof(buf), "STEP %d:", i + 1);
        _tft->print(buf);

        _tft->setTextColor(COLOR_TEMP_ACTUAL);
        _tft->setCursor(70, y);

        if (i < 4) {
            snprintf(buf, sizeof(buf), "%.0f C  %d min",
                     recipe.steps[i].targetTemp, recipe.steps[i].holdTimeMin);
            if (recipe.steps[i].isTempSelectable) {
                _tft->setTextColor(COLOR_TEMP_TARGET);  // Highlight editable
            }
        } else {
            snprintf(buf, sizeof(buf), "RAMP %.1f C/min -> %.0f C",
                     recipe.rampRate, recipe.rampFinalTemp);
        }
        _tft->print(buf);

        // Edit indicator for editable steps
        if (i < 4) {
            _tft->setTextColor(COLOR_TEXT_DIM);
            _tft->setCursor(280, y);
            _tft->print("[D]");
        }
    }

    drawDivider(190);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 198);
    _tft->print("A/B=SELECT STEP  D=EDIT  C=BACK");
    _tft->setCursor(10, 212);
    _tft->print("#=SAVE TO MEMORY");

    drawDivider(225);
}

// ============================================================
//  STEP EDIT SCREEN
// ============================================================

void DisplayManager::drawStepEditScreen(const SystemStatus& status,
                                         const Recipe& recipe) {
    drawHeader();
    drawDivider(22);

    uint8_t step = status.editStep;  // 0-based index
    char buf[40];

    snprintf(buf, sizeof(buf), "EDIT STEP %d", step + 1);
    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_TEXT_PRIMARY);
    _tft->setCursor(10, 30);
    _tft->print(buf);

    drawDivider(52);

    const StepConfig& sc = recipe.steps[step];

    // Temperature selection (if applicable)
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 60);
    _tft->print("TARGET TEMPERATURE:");

    _tft->setTextSize(2);
    if (sc.isTempSelectable) {
        _tft->setTextColor(COLOR_TEMP_TARGET);
        _tft->setCursor(10, 75);
        formatTemp(sc.targetTemp, buf, sizeof(buf));
        _tft->print(buf);

        // Show options
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_DIM);
        _tft->setCursor(10, 100);
        _tft->print("SELECT:");

        for (int i = 0; i < sc.tempOptionCount; i++) {
            _tft->setCursor(10 + i * 90, 114);
            _tft->setTextColor(COLOR_TEXT_PRIMARY);
            snprintf(buf, sizeof(buf), "%d=%.0fC", i + 1, sc.tempOptions[i]);
            _tft->print(buf);
        }
    } else {
        _tft->setTextColor(COLOR_TEMP_ACTUAL);
        _tft->setCursor(10, 75);
        formatTemp(sc.targetTemp, buf, sizeof(buf));
        _tft->print(buf);

        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT_DIM);
        _tft->setCursor(10, 100);
        _tft->print("(FIXED - NOT EDITABLE)");
    }

    drawDivider(130);

    // Hold time entry
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 138);
    _tft->print("HOLD TIME (MINUTES):");

    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_TEMP_ACTUAL);
    _tft->setCursor(10, 155);
    snprintf(buf, sizeof(buf), "%d min", sc.holdTimeMin);
    _tft->print(buf);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 180);
    _tft->print("PRESS D, THEN TYPE MINUTES, THEN #");

    drawDivider(200);
    _tft->setCursor(10, 210);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->print("D=EDIT TIME  #=CONFIRM  C=BACK");
}

// ============================================================
//  CONFIRMATION SCREENS
// ============================================================

void DisplayManager::drawConfirmStartScreen() {
    drawHeader();
    drawDivider(22);

    drawCenteredText(60, "READY TO START?", COLOR_TEXT_PRIMARY, 2);

    drawCenteredText(110, "Press D to START", COLOR_STATUS_OK, 2);
    drawCenteredText(145, "Press C to CANCEL", COLOR_STATUS_ERR, 2);

    drawDivider(200);
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    drawCenteredText(215, "Ensure heatsink and fans are ready", COLOR_TEXT_DIM, 1);
}

void DisplayManager::drawConfirmStopScreen() {
    drawHeader();
    drawDivider(22);

    drawCenteredText(60, "EMERGENCY STOP?", COLOR_STATUS_ERR, 2);
    drawCenteredText(95, "All Peltiers will be shut off", COLOR_TEXT_SECONDARY, 1);

    drawCenteredText(130, "Press D to STOP", COLOR_STATUS_ERR, 2);
    drawCenteredText(160, "Press C to CANCEL", COLOR_STATUS_OK, 2);
}

// ============================================================
//  PID TUNING SCREEN
// ============================================================

void DisplayManager::drawPIDScreen() {
    drawHeader();
    drawDivider(22);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 28);
    _tft->print("PID TUNING");

    drawDivider(38);

    // Labels
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 48);  _tft->print("Kp:");
    _tft->setCursor(10, 68);  _tft->print("Ki:");
    _tft->setCursor(10, 88);  _tft->print("Kd:");

    drawDivider(108);

    _tft->setCursor(10, 118); _tft->print("TEMP:");
    _tft->setCursor(10, 138); _tft->print("SETPOINT:");
    _tft->setCursor(10, 158); _tft->print("OUTPUT:");

    drawDivider(178);

    _tft->setCursor(10, 188); _tft->print("P-TERM:");
    _tft->setCursor(110, 188); _tft->print("I-TERM:");
    _tft->setCursor(220, 188); _tft->print("D-TERM:");

    drawDivider(210);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 225);
    _tft->print("C=BACK  #=SAVE");
}

void DisplayManager::updatePIDScreen(float kp, float ki, float kd,
                                      float temp, float setpoint, float output,
                                      float pTerm, float iTerm, float dTerm) {
    char buf[16];

    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_TEMP_ACTUAL, COLOR_BG);

    clearValueArea(40, 44, 120, 18);
    _tft->setCursor(40, 46); snprintf(buf, sizeof(buf), "%.2f", kp); _tft->print(buf);

    clearValueArea(40, 64, 120, 18);
    _tft->setCursor(40, 66); snprintf(buf, sizeof(buf), "%.3f", ki); _tft->print(buf);

    clearValueArea(40, 84, 120, 18);
    _tft->setCursor(40, 86); snprintf(buf, sizeof(buf), "%.2f", kd); _tft->print(buf);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);

    clearValueArea(80, 118, 80, 10);
    _tft->setCursor(80, 118); formatTemp(temp, buf, sizeof(buf)); _tft->print(buf);

    clearValueArea(80, 138, 80, 10);
    _tft->setCursor(80, 138); formatTemp(setpoint, buf, sizeof(buf)); _tft->print(buf);

    clearValueArea(80, 158, 80, 10);
    _tft->setCursor(80, 158); snprintf(buf, sizeof(buf), "%.1f%%", output); _tft->print(buf);

    _tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);

    clearValueArea(10, 200, 100, 10);
    _tft->setCursor(10, 200); snprintf(buf, sizeof(buf), "%.2f", pTerm); _tft->print(buf);

    clearValueArea(110, 200, 100, 10);
    _tft->setCursor(110, 200); snprintf(buf, sizeof(buf), "%.2f", iTerm); _tft->print(buf);

    clearValueArea(220, 200, 100, 10);
    _tft->setCursor(220, 200); snprintf(buf, sizeof(buf), "%.2f", dTerm); _tft->print(buf);
}

// ============================================================
//  CALIBRATION SCREEN
// ============================================================

void DisplayManager::drawCalibrationScreen(const SystemStatus& status) {
    drawHeader();
    drawDivider(22);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 28);
    _tft->print("TEMPERATURE CALIBRATION");

    drawDivider(38);

    char buf[20];

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 50);
    _tft->print("CURRENT SENSOR READING:");

    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_TEMP_ACTUAL);
    _tft->setCursor(10, 68);
    formatTemp(status.filteredTemp, buf, sizeof(buf));
    _tft->print(buf);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 100);
    _tft->print("CALIBRATION OFFSET:");

    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_TEMP_TARGET);
    _tft->setCursor(10, 118);
    snprintf(buf, sizeof(buf), "%+.2f C", 0.0f);  // Will be updated
    _tft->print(buf);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 150);
    _tft->print("ENTER OFFSET VALUE + # TO SAVE");
    _tft->setCursor(10, 165);
    _tft->print("USE * FOR NEGATIVE / DECIMAL");

    drawDivider(185);
    _tft->setTextColor(COLOR_STATUS_WARN);
    _tft->setCursor(10, 195);
    snprintf(buf, sizeof(buf), "MAX RECOMMENDED: +/-%.1f C", TEMP_CAL_OFFSET_MAX);
    _tft->print(buf);

    drawDivider(210);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 225);
    _tft->print("C=BACK  #=SAVE");
}

// ============================================================
//  TEST SCREEN
// ============================================================

void DisplayManager::drawTestScreen() {
    drawHeader();
    drawDivider(22);

    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_STATUS_WARN);
    _tft->setCursor(10, 30);
    _tft->print("TEST MODE");

    drawDivider(52);

    const char* testItems[] = {
        "1: Read Temperature",
        "2: Test Display",
        "3: Test Keypad",
        "4: Test DFPlayer",
        "5: Test Bottom Peltier",
        "6: Test Middle Peltier",
        "7: Test Top Peltier"
    };

    _tft->setTextSize(1);
    for (int i = 0; i < 7; i++) {
        _tft->setTextColor((i >= 4) ? COLOR_STATUS_WARN : COLOR_TEXT_PRIMARY);
        _tft->setCursor(10, 60 + i * 18);
        _tft->print(testItems[i]);
    }

    drawDivider(195);
    _tft->setTextColor(COLOR_STATUS_ERR);
    _tft->setTextSize(1);
    _tft->setCursor(10, 200);
    _tft->print("CAUTION: Peltier tests apply real power!");

    drawDivider(215);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 225);
    _tft->print("1-7=SELECT  C=BACK");
}

void DisplayManager::drawTestComponentScreen(const SystemStatus& status) {
    drawHeader();
    drawDivider(22);

    char buf[40];

    const char* stageNames[] = {"BOTTOM", "MIDDLE", "TOP"};

    snprintf(buf, sizeof(buf), "TEST %s PELTIER", stageNames[status.testStage]);
    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_STATUS_WARN);
    _tft->setCursor(10, 30);
    _tft->print(buf);

    drawDivider(52);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 62);
    _tft->print("PWM OUTPUT:");

    _tft->setTextSize(3);
    _tft->setTextColor(COLOR_TEMP_ACTUAL);
    _tft->setCursor(10, 80);
    snprintf(buf, sizeof(buf), "%.0f%%", status.testPWM);
    _tft->print(buf);

    drawBar(10, 120, 300, 16, status.testPWM, COLOR_BAR_FILL);

    drawDivider(145);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 155);
    _tft->print("TEMPERATURE:");

    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_TEMP_ACTUAL);
    _tft->setCursor(10, 170);
    formatTemp(status.filteredTemp, buf, sizeof(buf));
    _tft->print(buf);

    drawDivider(195);

    _tft->setTextSize(1);
    _tft->setTextColor(status.testActive ? COLOR_STATUS_ERR : COLOR_STATUS_OK);
    _tft->setCursor(10, 205);
    _tft->print(status.testActive ? "*** PELTIER ACTIVE ***" : "PELTIER OFF");

    drawDivider(220);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 228);
    _tft->print("#=START  C=STOP  A/B=PWM+/-");
}

// ============================================================
//  ABOUT SCREEN
// ============================================================

void DisplayManager::drawAboutScreen() {
    drawHeader();
    drawDivider(22);

    drawCenteredText(40, "TULIR", COLOR_TEMP_ACTUAL, 3);
    drawCenteredText(70, "3-Stage Peltier Controller", COLOR_TEXT_SECONDARY, 1);

    drawDivider(85);

    char buf[40];
    snprintf(buf, sizeof(buf), "Firmware: v%s", FW_VERSION_STR);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_PRIMARY);
    _tft->setCursor(10, 95);   _tft->print(buf);
    _tft->setCursor(10, 112);  _tft->print("MCU: ESP32-S3");
    _tft->setCursor(10, 129);  _tft->print("Display: ILI9341 320x240");
    _tft->setCursor(10, 146);  _tft->print("Sensor: SHT3x (Temp+RH)");
    _tft->setCursor(10, 163);  _tft->print("Audio: DFPlayer Mini");
    _tft->setCursor(10, 180);  _tft->print("Drivers: 3x BTS7960");

    drawDivider(196);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 206);
    _tft->print("Peltier cascade: BOT/MID/TOP");
    _tft->setCursor(10, 220);
    _tft->print("C=BACK");
}

// ============================================================
//  FAULT / COMPLETE / STOPPED SCREENS
// ============================================================

void DisplayManager::drawFaultScreen(const SystemStatus& status) {
    _tft->fillScreen(0x4000);  // Dark red background

    drawCenteredText(30, "!!! FAULT !!!", COLOR_STATUS_ERR, 3);

    drawCenteredText(80, getErrorName(status.errorCode), COLOR_TEXT_PRIMARY, 2);

    char buf[20];
    formatTemp(status.filteredTemp, buf, sizeof(buf));

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 120);
    _tft->print("Temperature: ");
    _tft->setTextColor(COLOR_TEXT_PRIMARY);
    _tft->print(buf);

    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 140);
    _tft->print("All Peltier outputs: OFF");

    _tft->setCursor(10, 158);
    _tft->print("Fans: ON (hardwired to PSU)");

    drawCenteredText(190, "Press D to acknowledge fault", COLOR_STATUS_WARN, 1);
    drawCenteredText(210, "System will NOT resume automatically",
                     COLOR_TEXT_DIM, 1);
}

void DisplayManager::drawCompleteScreen(const SystemStatus& status) {
    _tft->fillScreen(COLOR_BG);
    drawHeader();
    drawDivider(22);

    drawCenteredText(50, "PROCESS COMPLETE", COLOR_STATUS_OK, 2);

    char buf[20];
    formatTemp(status.filteredTemp, buf, sizeof(buf));

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(10, 90);
    _tft->print("Final Temperature: ");
    _tft->setTextColor(COLOR_TEMP_ACTUAL);
    _tft->print(buf);

    if (status.processStartTime > 0) {
        unsigned long totalTime = millis() - status.processStartTime;
        formatTime(totalTime, buf, sizeof(buf));
        _tft->setTextColor(COLOR_TEXT_SECONDARY);
        _tft->setCursor(10, 110);
        _tft->print("Total Process Time: ");
        _tft->setTextColor(COLOR_TEXT_PRIMARY);
        _tft->print(buf);
    }

    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 140);
    _tft->print("Peltiers: OFF");
    _tft->setCursor(10, 155);
    _tft->print("Fans: ON (hardwired to PSU)");

    drawCenteredText(200, "Press D to return to HOME", COLOR_TEXT_SECONDARY, 1);
}

void DisplayManager::drawStoppedScreen() {
    _tft->fillScreen(0x4000);  // Dark red

    drawCenteredText(50, "EMERGENCY STOP", COLOR_STATUS_ERR, 3);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_PRIMARY);
    _tft->setCursor(10, 100);
    _tft->print("All Peltier outputs: OFF");
    _tft->setCursor(10, 118);
    _tft->print("Fans: ON (hardwired to PSU)");

    drawCenteredText(160, "System will NOT resume automatically",
                     COLOR_STATUS_WARN, 1);
    drawCenteredText(185, "Press D to acknowledge", COLOR_TEXT_SECONDARY, 1);
    drawCenteredText(200, "and return to IDLE", COLOR_TEXT_SECONDARY, 1);
}

// ============================================================
//  MANUAL PWM SCREEN (Protected)
// ============================================================

void DisplayManager::drawManualPWMScreen(const SystemStatus& status) {
    drawHeader();
    drawDivider(22);

    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_STATUS_WARN);
    _tft->setCursor(10, 30);
    _tft->print("MANUAL PWM");

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_STATUS_ERR);
    _tft->setCursor(10, 52);
    _tft->print("CAUTION: Direct Peltier control");

    drawDivider(66);

    char buf[30];
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);

    _tft->setCursor(10, 75);
    snprintf(buf, sizeof(buf), "BOTTOM: %.0f%%", status.bottomPWM);
    _tft->print(buf);
    drawBar(120, 73, 180, 12, status.bottomPWM, COLOR_BAR_FILL);

    _tft->setCursor(10, 95);
    snprintf(buf, sizeof(buf), "MIDDLE: %.0f%%", status.middlePWM);
    _tft->print(buf);
    drawBar(120, 93, 180, 12, status.middlePWM, COLOR_BAR_FILL);

    _tft->setCursor(10, 115);
    snprintf(buf, sizeof(buf), "TOP:    %.0f%%", status.topPWM);
    _tft->print(buf);
    drawBar(120, 113, 180, 12, status.topPWM, COLOR_BAR_FILL);

    drawDivider(135);

    _tft->setCursor(10, 145);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->print("TEMP: ");
    formatTemp(status.filteredTemp, buf, sizeof(buf));
    _tft->setTextColor(COLOR_TEMP_ACTUAL);
    _tft->print(buf);

    drawDivider(165);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(10, 175);
    _tft->print("Safety limits enforced:");
    snprintf(buf, sizeof(buf), "BOT<%.0f%% MID<%.0f%% TOP<%.0f%%",
             BOTTOM_MAX_PWM_PCT, MIDDLE_MAX_PWM_PCT, TOP_MAX_PWM_PCT);
    _tft->setCursor(10, 190);
    _tft->print(buf);

    drawDivider(210);
    _tft->setCursor(10, 225);
    _tft->print("C=STOP/BACK");
}

// ============================================================
//  NUMERIC ENTRY OVERLAY
// ============================================================

void DisplayManager::drawNumericEntry(const char* prompt, const char* value,
                                       int minVal, int maxVal) {
    // Draw overlay box in the center of screen
    int bx = 40, by = 80, bw = 240, bh = 80;
    _tft->fillRect(bx, by, bw, bh, COLOR_HEADER_BG);
    _tft->drawRect(bx, by, bw, bh, COLOR_DIVIDER);
    _tft->drawRect(bx + 1, by + 1, bw - 2, bh - 2, COLOR_DIVIDER);

    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_SECONDARY);
    _tft->setCursor(bx + 10, by + 8);
    _tft->print(prompt);

    // Value display
    _tft->setTextSize(3);
    _tft->setTextColor(COLOR_TEMP_ACTUAL);
    _tft->setCursor(bx + 10, by + 28);
    if (strlen(value) > 0) {
        _tft->print(value);
    }
    _tft->print("_");  // Cursor

    // Range hint
    char rangeStr[30];
    snprintf(rangeStr, sizeof(rangeStr), "Range: %d - %d", minVal, maxVal);
    _tft->setTextSize(1);
    _tft->setTextColor(COLOR_TEXT_DIM);
    _tft->setCursor(bx + 10, by + 62);
    _tft->print(rangeStr);
}

// ============================================================
//  MESSAGE OVERLAY
// ============================================================

void DisplayManager::showMessage(const char* msg, uint16_t color) {
    int bx = 20, by = 95, bw = 280, bh = 44;
    _tft->fillRect(bx, by, bw, bh, COLOR_HEADER_BG);
    _tft->drawRect(bx, by, bw, bh, color);

    _tft->setTextSize(1);
    _tft->setTextColor(color, COLOR_HEADER_BG);
    int textLen = strlen(msg) * 6;
    int cx = bx + (bw - textLen) / 2;
    _tft->setCursor(cx, by + 18);
    _tft->print(msg);

    _messageExpiry = millis() + 2500;
}

// ============================================================
//  DRAWING HELPERS
// ============================================================

void DisplayManager::drawHeader() {
    _tft->fillRect(0, 0, 320, 20, COLOR_HEADER_BG);
    _tft->drawFastHLine(0, 20, 320, COLOR_TEMP_ACTUAL);
    _tft->setTextSize(2);
    _tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_HEADER_BG);
    _tft->setCursor(6, 3);
    _tft->print("TULIR");
}

void DisplayManager::drawDivider(int y) {
    _tft->drawFastHLine(0, y, 320, COLOR_DIVIDER);
}

void DisplayManager::drawBar(int x, int y, int w, int h,
                              float percent, uint16_t fillColor) {
    percent = CLAMP(percent, 0.0f, 100.0f);

    // Background
    _tft->fillRect(x, y, w, h, COLOR_BAR_BG);

    // Fill
    int fillW = (int)((percent / 100.0f) * w);
    if (fillW > 0) {
        _tft->fillRect(x, y, fillW, h, fillColor);
    }

    // Border
    _tft->drawRect(x, y, w, h, COLOR_DIVIDER);
}

void DisplayManager::drawKeyValue(int x, int y, const char* label,
                                   const char* value,
                                   uint16_t labelColor, uint16_t valueColor,
                                   uint8_t textSize) {
    _tft->setTextSize(textSize);
    _tft->setTextColor(labelColor, COLOR_BG);
    _tft->setCursor(x, y);
    _tft->print(label);
    _tft->setTextColor(valueColor, COLOR_BG);
    _tft->print(value);
}

void DisplayManager::drawCenteredText(int y, const char* text,
                                       uint16_t color, uint8_t size) {
    _tft->setTextSize(size);
    _tft->setTextColor(color, COLOR_BG);
    int textW = strlen(text) * 6 * size;
    int cx = (320 - textW) / 2;
    if (cx < 0) cx = 0;
    _tft->setCursor(cx, y);
    _tft->print(text);
}

void DisplayManager::clearValueArea(int x, int y, int w, int h) {
    _tft->fillRect(x, y, w, h, COLOR_BG);
}

Adafruit_ILI9341* DisplayManager::getTFT() {
    return _tft;
}

static bool readDisplayIDOnce(Adafruit_ILI9341* tft) {
    // Read the ILI9341's driver-version/driver-ID bytes (RDDID, 0x04).
    // A healthy panel reports a fixed, non-trivial pair here; a panel
    // that's been silently hardware-reset (e.g. by an RST-line glitch
    // from nearby electrical noise) responds with all-zero or all-0xFF —
    // the same signature used during initial bring-up diagnostics.
    uint8_t id2 = tft->readcommand8(0x04, 2);
    uint8_t id3 = tft->readcommand8(0x04, 3);

    if (id2 == 0x00 && id3 == 0x00) return false;
    if (id2 == 0xFF && id3 == 0xFF) return false;
    return true;
}

bool DisplayManager::isAlive() {
    if (!_tft) return false;

    // The ID readback itself travels over the same noisy SPI/MISO lines
    // that caused the original problem, so a single bad read can be the
    // *check* glitching, not the display. A genuinely reset panel reads
    // bad consistently; a flaky read on an otherwise-fine panel usually
    // doesn't repeat immediately after. Require two failed reads in a
    // row (with a short gap) before believing it, so a one-off noise
    // hit on the check doesn't trigger an unnecessary re-init.
    if (readDisplayIDOnce(_tft)) return true;

    delay(5);
    return readDisplayIDOnce(_tft);
}

// ============================================================
//  FORMAT HELPERS
// ============================================================

void DisplayManager::formatTime(unsigned long ms, char* buf, size_t len) {
    unsigned long totalSec = ms / 1000;
    unsigned long hours = totalSec / 3600;
    unsigned long mins  = (totalSec % 3600) / 60;
    unsigned long secs  = totalSec % 60;

    if (hours > 0) {
        snprintf(buf, len, "%lu:%02lu:%02lu", hours, mins, secs);
    } else {
        snprintf(buf, len, "%02lu:%02lu", mins, secs);
    }
}

void DisplayManager::formatTemp(float temp, char* buf, size_t len) {
    snprintf(buf, len, "%.1f C", temp);
}
