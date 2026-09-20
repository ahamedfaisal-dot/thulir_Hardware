/*
 * ============================================================
 *  TULIR — 3-Stage Cascaded Peltier Temperature Controller
 *  thulir_final.ino — Main Sketch
 * ============================================================
 *
 *  PROCESS FLOW:
 *    POWER ON → SELF TEST → IDLE
 *    → USER PROGRAMS RECIPE → RECIPE CONFIRMED → READY
 *    → START → SENSOR CHECK → STEP 1 (25°C, hold)
 *    → STEP 2 (0/15/25°C, hold)
 *    → STEP 3 (4°C, hold)
 *    → STEP 4 (0/25°C, hold)
 *    → STEP 5 (ramp −1°C/min to −20°C)
 *    → PROCESS COMPLETE → PELTIERS OFF → IDLE
 *
 *  STATE MACHINE:
 *    STATE_BOOT           — Hardware initialization & self-test
 *    STATE_IDLE           — Waiting for user, home screen
 *    STATE_PROGRAMMING    — Editing recipe via keypad
 *    STATE_READY          — Recipe confirmed, awaiting START
 *    STATE_STEP_APPROACH  — Cooling/heating to step target
 *    STATE_STEP_HOLD      — Target reached, holding for timer
 *    STATE_STEP_TRANSITION— Moving to next step
 *    STATE_STEP5_RAMP     — Ramping −1°C/min to −20°C
 *    STATE_COMPLETE       — Process finished
 *    STATE_STOPPED        — Emergency stop
 *    STATE_FAULT          — Fault detected
 *    STATE_TEST_MODE      — Component testing
 *
 *  POWER FAILURE BEHAVIOR:
 *    On reboot, all outputs start at 0%. The system does NOT
 *    automatically resume a partially completed recipe. The user
 *    must manually restart.
 *
 * ============================================================
 */

#include "Config.h"
#include "PIDController.h"
#include "PeltierControl.h"
#include "TemperatureManager.h"
#include "RecipeManager.h"
#include "KeypadManager.h"
#include "DisplayManager.h"
#include "AudioManager.h"
#include "SafetyManager.h"

// ============================================================
//  TEST MODE
//    0 = full firmware (normal operation)
//    2 = Adafruit_ILI9341 display-only test (splash + RGB flash +
//        ID read). Kept for future hardware debugging — this is the
//        driver library DisplayManager now uses full-time, since
//        TFT_eSPI never got a response from this panel on this board.
// ============================================================
#define TEST_MODE 0

#if TEST_MODE == 2
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#endif

// ============================================================
//  GLOBAL OBJECTS
// ============================================================

PIDController     pid;
PeltierControl    peltier;
TemperatureManager tempMgr;
RecipeManager     recipeMgr;
KeypadManager     keypadMgr;
DisplayManager    displayMgr;
AudioManager      audioMgr;
SafetyManager     safetyMgr;

SystemStatus      sysStatus;

// ============================================================
//  NON-BLOCKING TIMERS
// ============================================================

unsigned long lastDisplayUpdate = 0;
unsigned long lastPIDUpdate     = 0;
unsigned long lastSerialDebug   = 0;
unsigned long lastSafetyCheck   = 0;
unsigned long lastRampUpdate    = 0;

// Event-driven display watchdog:
//   displayCheckPending = true once, shortly after a step starts.
//   displayCheckAt      = millis() value at which to fire the one-shot check.
//   The periodic isAlive() poll was removed — it itself caused white-screen
//   flashes every 4 seconds due to ILI9341 SPI register reads mid-display.
bool          displayCheckPending = false;
unsigned long displayCheckAt      = 0;

// Unused — kept for backward compat with #define references in comments only
#define DISPLAY_HEALTH_CHECK_INTERVAL_MS  4000   // retained for reference
#define DISPLAY_HEALTH_FAIL_THRESHOLD     1       // immediate reinit on failure

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================

void handleKeypress(char key);
void handleHomeKey(char key);
void handleMenuKey(char key);
void handleProgramKey(char key);
void handleStepEditKey(char key);
void handleConfirmStartKey(char key);
void handleConfirmStopKey(char key);
void handlePIDScreenKey(char key);
void handleCalibrationKey(char key);
void handleTestKey(char key);
void handleTestComponentKey(char key);
void handleAboutKey(char key);
void handleFaultKey(char key);
void handleCompleteKey(char key);
void handleStoppedKey(char key);
void handleManualPWMKey(char key);

void startProcess();
void stopProcess();
void advanceStep();
void enterApproachState();
void enterHoldState();
void enterRampState();
void completeProcess();

void updateApproachState();
void updateHoldState();
void updateRampState();

void serialDebugPrint();
void showScreen(ScreenID screen);

// ============================================================
//  HELPER — Temperature-scaled feedforward
// ============================================================
//  Wraps PIDController::computeFeedforward(). Defined here so it
//  is usable by enterApproachState, enterRampState, and updateRampState.
//  Formula: FF% = (25 − setpoint) / (25 − (−27)) × 100
//  Based on measured hardware: 12V/6.1V/2.45V → −27°C at 100% output.
inline float computeFF(float setpoint) {
    return PIDController::computeFeedforward(setpoint);
}

// ============================================================
//  SETUP
// ============================================================

#if TEST_MODE == 2
// ============================================================
//  ADAFRUIT_ILI9341 DISPLAY TEST — mirrors sih2026_input exactly
//  Same pins (CS=10, DC=9, RST=14, MOSI=11, SCK=12, MISO=13),
//  different driver library, to isolate library vs hardware.
// ============================================================
#define TEST2_MOSI 11
#define TEST2_SCK  12
#define TEST2_MISO 13
#define TEST2_CS   10
#define TEST2_DC    9
#define TEST2_RST  14

SPIClass tftSPI(FSPI);
Adafruit_ILI9341 adaTft(&tftSPI, TEST2_DC, TEST2_CS, TEST2_RST);

void setup() {
    Serial.begin(DEBUG_BAUD);
    delay(500);
    Serial.println();
    Serial.println("=== ADAFRUIT_ILI9341 DISPLAY TEST ===");
    Serial.printf("[TEST] MOSI=%d SCK=%d MISO=%d CS=%d DC=%d RST=%d\n",
                  TEST2_MOSI, TEST2_SCK, TEST2_MISO, TEST2_CS, TEST2_DC, TEST2_RST);

    tftSPI.begin(TEST2_SCK, TEST2_MISO, TEST2_MOSI, TEST2_CS);
    adaTft.begin();
    adaTft.setRotation(3);
    adaTft.fillScreen(ILI9341_BLACK);
    Serial.println("[TEST] adaTft.begin() returned.");

    uint8_t id1 = adaTft.readcommand8(0x04, 1);
    uint8_t id2 = adaTft.readcommand8(0x04, 2);
    uint8_t id3 = adaTft.readcommand8(0x04, 3);
    Serial.printf("[TEST] Display ID bytes (0x04): 0x%02X 0x%02X 0x%02X\n", id1, id2, id3);
    Serial.println("[TEST] ILI9341 should read approx: 0x00 0x93 0x41");
    if (id1 == 0x00 && id2 == 0x00 && id3 == 0x00) {
        Serial.println("[TEST] All zeros -> panel NOT responding even with Adafruit driver.");
    } else {
        Serial.println("[TEST] Got a non-trivial response -> panel IS responding to Adafruit driver!");
    }
}

void loop() {
    adaTft.fillScreen(ILI9341_RED);
    Serial.println("[TEST] RED");
    delay(1000);

    adaTft.fillScreen(ILI9341_GREEN);
    Serial.println("[TEST] GREEN");
    delay(1000);

    adaTft.fillScreen(ILI9341_BLUE);
    Serial.println("[TEST] BLUE");
    delay(1000);
}

#else
// ============================================================
//  FULL FIRMWARE setup()/loop() — active while TEST_MODE is 0
// ============================================================

void setup() {
    Serial.begin(DEBUG_BAUD);
    delay(500);

    Serial.println();
    Serial.println("============================================");
    Serial.println("  TULIR — 3-Stage Peltier Controller");
    Serial.printf("  Firmware v%s\n", FW_VERSION_STR);
    Serial.println("  ESP32-S3");
    Serial.println("============================================");
    Serial.println();
    Serial.println("POWER RESTORED — SYSTEM RESET");
    Serial.println("All Peltier outputs start at 0% (safe state)");
    Serial.println();

    // Initialize system status to safe defaults
    memset(&sysStatus, 0, sizeof(SystemStatus));
    sysStatus.state = STATE_BOOT;
    sysStatus.currentScreen = SCREEN_HOME;
    sysStatus.currentStep = 0;
    sysStatus.errorCode = ERROR_NONE;

    // --- Initialize peripherals ---

    // Display first (gives visual feedback during boot)
    Serial.println("[BOOT] Initializing TFT display...");
    displayMgr.begin();

    // Peltier drivers (all off by default)
    Serial.println("[BOOT] Initializing BTS7960 outputs...");
    peltier.begin();
    peltier.allPeltiersOff();  // Ensure safe state

    // Temperature sensor
    Serial.println("[BOOT] Initializing SHT3x sensor (temperature + humidity)...");
    bool sensorOK = tempMgr.begin();
    sysStatus.sensorValid = sensorOK;

    // Keypad
    Serial.println("[BOOT] Initializing keypad...");
    keypadMgr.begin();

    // Recipe (loads from NVS or creates defaults)
    Serial.println("[BOOT] Loading recipe from NVS...");
    recipeMgr.begin();

    // Load PID parameters from NVS
    float kp, ki, kd;
    recipeMgr.loadPIDParams(kp, ki, kd);
    pid.setTunings(kp, ki, kd);
    pid.setOutputLimits(PID_OUTPUT_MIN, PID_OUTPUT_MAX);
    pid.setSampleTime(PID_SAMPLE_TIME_MS);
    pid.setIntegralZone(APPROACH_INTEGRAL_ZONE);   // default to approach zone
    pid.setFeedforward(0.0f);                       // no FF until process starts
    pid.setRateLimit(PID_OUTPUT_RATE_LIMIT);
    Serial.printf("[BOOT] PID loaded: Kp=%.2f Ki=%.3f Kd=%.2f  FF=auto  Slew=%.0f%%/s\n",
                  kp, ki, kd, PID_OUTPUT_RATE_LIMIT);

    // Load calibration offset
    float calOffset = recipeMgr.loadCalibrationOffset();
    tempMgr.setCalibrationOffset(calOffset);
    Serial.printf("[BOOT] Calibration offset: %.2f °C\n", calOffset);

    // Load power ratios
    float botR, midR, topR;
    recipeMgr.loadPowerRatios(botR, midR, topR);
    peltier.setPowerRatios(botR, midR, topR);
    Serial.printf("[BOOT] Power ratios: BOT=%.0f%% MID=%.0f%% TOP=%.0f%%\n",
                  botR * 100.0f, midR * 100.0f, topR * 100.0f);

    // Audio (DFPlayer Mini)
    Serial.println("[BOOT] Initializing DFPlayer Mini...");
    audioMgr.begin();

    // Safety manager
    safetyMgr.begin();

    // --- Boot complete ---
    Serial.println();
    Serial.println("[BOOT] ==============================");
    Serial.println("[BOOT]  TULIR BOOT COMPLETE");
    Serial.printf("[BOOT]  Sensor: %s\n", sensorOK ? "OK" : "ERROR");
    Serial.printf("[BOOT]  Audio:  %s\n", audioMgr.isAvailable() ? "OK" : "N/A");
    Serial.println("[BOOT] ==============================");
    Serial.println();

    // Transition to IDLE state
    sysStatus.state = STATE_IDLE;
    sysStatus.currentScreen = SCREEN_HOME;

    // Show initial home screen
    sysStatus.filteredTemp = tempMgr.getFilteredTemp();
    sysStatus.actualTemp = tempMgr.getRawTemp();
    showScreen(SCREEN_HOME);

    // Announce system start
    audioMgr.announceSystemStart();

    // If sensor failed at boot, show warning
    if (!sensorOK) {
        displayMgr.showMessage("SENSOR NOT FOUND!", COLOR_STATUS_ERR);
        Serial.println("[BOOT] WARNING: Temperature sensor not found at boot");
    }
}

// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {
    unsigned long now = millis();

    // --- 1. Temperature sensor update (async, non-blocking) ---
    tempMgr.update();
    sysStatus.actualTemp = tempMgr.getRawTemp();
    sysStatus.filteredTemp = tempMgr.getFilteredTemp();
    sysStatus.sensorValid = tempMgr.isSensorValid();
    sysStatus.humidity = tempMgr.getHumidity();
    sysStatus.humidityValid = tempMgr.isHumidityValid();

    // --- 2. Keypad scan (non-blocking) ---
    char key = keypadMgr.update();
    if (key) {
        handleKeypress(key);
    }

    // --- 3. PID control (time-gated) ---
    if (sysStatus.state == STATE_STEP_APPROACH ||
        sysStatus.state == STATE_STEP_HOLD ||
        sysStatus.state == STATE_STEP5_RAMP) {

        if (pid.compute(sysStatus.currentSetpoint, sysStatus.filteredTemp)) {
            sysStatus.pidOutput = pid.getOutput();
            peltier.setCascadePower(sysStatus.pidOutput, sysStatus);
        }
    }

    // --- 4. State-specific updates ---
    switch (sysStatus.state) {
        case STATE_STEP_APPROACH:
            updateApproachState();
            break;

        case STATE_STEP_HOLD:
            updateHoldState();
            break;

        case STATE_STEP5_RAMP:
            updateRampState();
            break;

        default:
            break;
    }

    // --- 5. Safety checks (time-gated) ---
    if ((now - lastSafetyCheck) >= SAFETY_CHECK_INTERVAL) {
        safetyMgr.update(tempMgr, peltier, audioMgr, sysStatus);
        lastSafetyCheck = now;
    }

    // --- 6. Audio update (non-blocking) ---
    audioMgr.update();

    // --- 7. Display update (time-gated) ---
    if ((now - lastDisplayUpdate) >= DISPLAY_UPDATE_INTERVAL) {
        if (sysStatus.currentScreen == SCREEN_HOME) {
            displayMgr.updateHomeScreen(sysStatus);
        } else if (sysStatus.currentScreen == SCREEN_PID) {
            displayMgr.updatePIDScreen(
                pid.getKp(), pid.getKi(), pid.getKd(),
                sysStatus.filteredTemp, sysStatus.currentSetpoint,
                sysStatus.pidOutput,
                pid.getPterm(), pid.getIterm(), pid.getDterm()
            );
        }
        lastDisplayUpdate = now;
    }

    // --- 7b. Display watchdog (EVENT-DRIVEN — one-shot post-start check) ---
    //  The periodic isAlive() poll was removed because the SPI register read
    //  that isAlive() performs itself caused the display to flash white every
    //  4 seconds. Instead, a one-shot check fires ~4 seconds after each step
    //  transition (when the 12V system starts heavy switching — the most
    //  likely moment for a RST-line noise glitch). If that single check finds
    //  the display unresponsive it reinitialises immediately. Otherwise the
    //  watchdog is silent until the next step transition schedules a new check.
    if (displayCheckPending && (now >= displayCheckAt)) {
        displayCheckPending = false;
        if (!displayMgr.isAlive()) {
            Serial.println("[DISPLAY] Post-start check failed — reinitialising");
            displayMgr.begin(false);               // silent recovery (no splash)
            showScreen(sysStatus.currentScreen);   // redraw current screen
        } else {
            Serial.println("[DISPLAY] Post-start check OK");
        }
    }

    // --- 8. Serial debug (time-gated) ---
    #if DEBUG_ENABLED
        if ((now - lastSerialDebug) >= SERIAL_PRINT_INTERVAL) {
            serialDebugPrint();
            lastSerialDebug = now;
        }
    #endif
}

#endif // TEST_MODE

// ============================================================
//  KEYPRESS ROUTING
// ============================================================

void handleKeypress(char key) {
    // Emergency stop: C key during an active process, from ANY screen —
    // must not require navigating back to HOME first.
    if (key == KEY_BACK &&
        sysStatus.currentScreen != SCREEN_CONFIRM_STOP &&
        (sysStatus.state == STATE_STEP_APPROACH ||
         sysStatus.state == STATE_STEP_HOLD ||
         sysStatus.state == STATE_STEP_TRANSITION ||
         sysStatus.state == STATE_STEP5_RAMP)) {
        showScreen(SCREEN_CONFIRM_STOP);
        return;
    }

    switch (sysStatus.currentScreen) {
        case SCREEN_HOME:           handleHomeKey(key); break;
        case SCREEN_MENU:           handleMenuKey(key); break;
        case SCREEN_PROGRAM:        handleProgramKey(key); break;
        case SCREEN_STEP_EDIT:      handleStepEditKey(key); break;
        case SCREEN_CONFIRM_START:  handleConfirmStartKey(key); break;
        case SCREEN_CONFIRM_STOP:   handleConfirmStopKey(key); break;
        case SCREEN_PID:            handlePIDScreenKey(key); break;
        case SCREEN_CALIBRATION:    handleCalibrationKey(key); break;
        case SCREEN_TEST:           handleTestKey(key); break;
        case SCREEN_TEST_COMPONENT: handleTestComponentKey(key); break;
        case SCREEN_ABOUT:          handleAboutKey(key); break;
        case SCREEN_FAULT:          handleFaultKey(key); break;
        case SCREEN_COMPLETE:       handleCompleteKey(key); break;
        case SCREEN_STOPPED:        handleStoppedKey(key); break;
        case SCREEN_MANUAL_PWM:     handleManualPWMKey(key); break;
        default: break;
    }
}

// --- HOME SCREEN ---
void handleHomeKey(char key) {
    if (key == KEY_ENTER) {
        // Open main menu
        sysStatus.menuSelection = 0;
        showScreen(SCREEN_MENU);
    }
}

// --- MENU SCREEN ---
void handleMenuKey(char key) {
    if (key == KEY_UP) {
        if (sysStatus.menuSelection > 0) sysStatus.menuSelection--;
        showScreen(SCREEN_MENU);
    }
    else if (key == KEY_DOWN) {
        if (sysStatus.menuSelection < MENU_ITEM_COUNT - 1) sysStatus.menuSelection++;
        showScreen(SCREEN_MENU);
    }
    else if (key == KEY_ENTER) {
        switch ((MenuItemID)sysStatus.menuSelection) {
            case MENU_PROGRAM:
                sysStatus.editStep = 0;
                showScreen(SCREEN_PROGRAM);
                break;
            case MENU_START:
                if (sysStatus.state == STATE_IDLE || sysStatus.state == STATE_READY) {
                    showScreen(SCREEN_CONFIRM_START);
                } else {
                    displayMgr.showMessage("Cannot start: process active", COLOR_STATUS_WARN);
                }
                break;
            case MENU_STOP:
                if (sysStatus.state == STATE_STEP_APPROACH ||
                    sysStatus.state == STATE_STEP_HOLD ||
                    sysStatus.state == STATE_STEP5_RAMP) {
                    showScreen(SCREEN_CONFIRM_STOP);
                } else {
                    displayMgr.showMessage("No active process to stop", COLOR_TEXT_DIM);
                }
                break;
            case MENU_SETTINGS:
                // Future: settings submenu
                displayMgr.showMessage("Settings: Use PID/CAL screens", COLOR_TEXT_DIM);
                break;
            case MENU_PID_TUNE:
                showScreen(SCREEN_PID);
                break;
            case MENU_TEST_MODE:
                showScreen(SCREEN_TEST);
                break;
            case MENU_ABOUT:
                showScreen(SCREEN_ABOUT);
                break;
        }
    }
    else if (key == KEY_BACK) {
        showScreen(SCREEN_HOME);
    }
}

// --- PROGRAM SCREEN ---
void handleProgramKey(char key) {
    if (key == KEY_UP) {
        if (sysStatus.editStep > 0) sysStatus.editStep--;
        showScreen(SCREEN_PROGRAM);
    }
    else if (key == KEY_DOWN) {
        if (sysStatus.editStep < 3) sysStatus.editStep++;  // Steps 1-4 editable
        showScreen(SCREEN_PROGRAM);
    }
    else if (key == KEY_ENTER) {
        // Edit selected step
        showScreen(SCREEN_STEP_EDIT);
    }
    else if (key == KEY_CONFIRM) {
        // Save recipe to NVS
        recipeMgr.saveToNVS();
        displayMgr.showMessage("RECIPE SAVED!", COLOR_STATUS_OK);
        sysStatus.state = STATE_READY;
    }
    else if (key == KEY_BACK) {
        showScreen(SCREEN_MENU);
    }
}

// --- STEP EDIT SCREEN ---
void handleStepEditKey(char key) {
    uint8_t step = sysStatus.editStep;  // 0-based
    Recipe& recipe = recipeMgr.getRecipeForEdit();
    StepConfig& sc = recipe.steps[step];

    // --- Numeric input active: ALL keys go to the numeric buffer ---
    // This is checked FIRST so that once entry is started, nothing
    // intercepts digits (including A/B temp-cycle keys below).
    if (keypadMgr.getNumericInput().active) {
        if (keypadMgr.processNumericKey(key)) {
            const NumericInput& ni = keypadMgr.getNumericInput();
            if (ni.confirmed && ni.intValue >= 1 && ni.intValue <= 999) {
                sc.holdTimeMin = ni.intValue;
                Serial.printf("[EDIT] Step %d hold time set to %d min\n",
                              step + 1, ni.intValue);
                showScreen(SCREEN_STEP_EDIT);
            } else if (ni.cancelled) {
                showScreen(SCREEN_STEP_EDIT);
            }
        } else {
            const NumericInput& ni = keypadMgr.getNumericInput();
            displayMgr.drawNumericEntry("HOLD TIME (min):",
                                        ni.buffer, 1, 999);
        }
        return;
    }

    // --- Temperature cycling via A (KEY_UP) / B (KEY_DOWN) for selectable steps ---
    //  Replaces the old 1/2/3 direct-shortcut approach. Using A/B frees up all
    //  digit keys (0-9) to start hold-time numeric entry directly, so the user
    //  never needs to press D first and there is no digit-collision on any step.
    if (sc.isTempSelectable &&
        (key == KEY_UP || key == KEY_DOWN)) {
        // Find the index of the currently selected temperature option
        int8_t idx = -1;
        for (uint8_t i = 0; i < sc.tempOptionCount; i++) {
            if (fabsf(sc.targetTemp - sc.tempOptions[i]) < 0.5f) {
                idx = (int8_t)i;
                break;
            }
        }
        // If not found (custom value), snap to nearest option
        if (idx < 0) idx = 0;

        if (key == KEY_UP   && idx > 0)                    idx--;
        if (key == KEY_DOWN && idx < (int8_t)(sc.tempOptionCount - 1)) idx++;

        sc.targetTemp = sc.tempOptions[idx];
        Serial.printf("[EDIT] Step %d temp set to %.0f C (option %d)\n",
                      step + 1, sc.targetTemp, idx + 1);
        showScreen(SCREEN_STEP_EDIT);
        return;
    }

    // --- Any digit (0-9) starts numeric entry AND feeds the digit in ---
    //  User can type the hold time directly (e.g., '3','0','#' for 30 min)
    //  without needing a D-press first. D also still works as a clean start.
    if (key >= '0' && key <= '9') {
        keypadMgr.startNumericInput(false, false, 1, 999);
        keypadMgr.processNumericKey(key);            // feed the first digit
        const NumericInput& ni = keypadMgr.getNumericInput();
        displayMgr.drawNumericEntry("HOLD TIME (min):", ni.buffer, 1, 999);
        return;
    }

    // D (KEY_ENTER) — start fresh numeric entry (clears buffer first)
    if (key == KEY_ENTER) {
        keypadMgr.startNumericInput(false, false, 1, 999);
        const NumericInput& ni = keypadMgr.getNumericInput();
        displayMgr.drawNumericEntry("HOLD TIME (min):", ni.buffer, 1, 999);
        return;
    }

    if (key == KEY_BACK) {
        keypadMgr.resetNumericInput();
        showScreen(SCREEN_PROGRAM);
    }
}

// --- CONFIRM START ---
void handleConfirmStartKey(char key) {
    if (key == KEY_ENTER) {
        startProcess();
    }
    else if (key == KEY_BACK) {
        showScreen(SCREEN_HOME);
    }
}

// --- CONFIRM STOP ---
void handleConfirmStopKey(char key) {
    if (key == KEY_ENTER) {
        safetyMgr.emergencyStop(peltier, audioMgr, sysStatus);
        showScreen(SCREEN_STOPPED);
    }
    else if (key == KEY_BACK) {
        showScreen(SCREEN_HOME);
    }
}

// --- PID SCREEN ---
static uint8_t pidEditField = 0;  // 0=Kp, 1=Ki, 2=Kd

void handlePIDScreenKey(char key) {
    if (keypadMgr.getNumericInput().active) {
        if (keypadMgr.processNumericKey(key)) {
            const NumericInput& ni = keypadMgr.getNumericInput();
            if (ni.confirmed) {
                float val = ni.floatValue;
                float kp = pid.getKp(), ki = pid.getKi(), kd = pid.getKd();
                switch (pidEditField) {
                    case 0: kp = val; break;
                    case 1: ki = val; break;
                    case 2: kd = val; break;
                }
                pid.setTunings(kp, ki, kd);
                Serial.printf("[PID] Updated: Kp=%.2f Ki=%.3f Kd=%.2f\n", kp, ki, kd);
            }
            showScreen(SCREEN_PID);
        } else {
            const NumericInput& ni = keypadMgr.getNumericInput();
            const char* labels[] = {"Kp:", "Ki:", "Kd:"};
            displayMgr.drawNumericEntry(labels[pidEditField], ni.buffer, 0, 999);
        }
        return;
    }

    if (key == '1') {
        pidEditField = 0;
        keypadMgr.startNumericInput(false, true, 0, 999);
        displayMgr.drawNumericEntry("Kp:", "", 0, 999);
    }
    else if (key == '2') {
        pidEditField = 1;
        keypadMgr.startNumericInput(false, true, 0, 999);
        displayMgr.drawNumericEntry("Ki:", "", 0, 999);
    }
    else if (key == '3') {
        pidEditField = 2;
        keypadMgr.startNumericInput(false, true, 0, 999);
        displayMgr.drawNumericEntry("Kd:", "", 0, 999);
    }
    else if (key == KEY_CONFIRM) {
        // Save PID params to NVS
        recipeMgr.savePIDParams(pid.getKp(), pid.getKi(), pid.getKd());
        displayMgr.showMessage("PID SAVED!", COLOR_STATUS_OK);
    }
    else if (key == KEY_BACK) {
        showScreen(SCREEN_MENU);
    }
}

// --- CALIBRATION SCREEN ---
void handleCalibrationKey(char key) {
    if (keypadMgr.getNumericInput().active) {
        if (keypadMgr.processNumericKey(key)) {
            const NumericInput& ni = keypadMgr.getNumericInput();
            if (ni.confirmed) {
                float offset = ni.floatValue;
                tempMgr.setCalibrationOffset(offset);
                recipeMgr.saveCalibrationOffset(offset);
                displayMgr.showMessage("CALIBRATION SAVED!", COLOR_STATUS_OK);
            }
            showScreen(SCREEN_CALIBRATION);
        } else {
            const NumericInput& ni = keypadMgr.getNumericInput();
            displayMgr.drawNumericEntry("OFFSET (C):", ni.buffer, -5, 5);
        }
        return;
    }

    if (key >= '0' && key <= '9' || key == '*') {
        keypadMgr.startNumericInput(true, true, -5, 5);
        if (key != '*') keypadMgr.processNumericKey(key);
        else keypadMgr.processNumericKey(key);
        const NumericInput& ni = keypadMgr.getNumericInput();
        displayMgr.drawNumericEntry("OFFSET (C):", ni.buffer, -5, 5);
    }
    else if (key == KEY_BACK) {
        showScreen(SCREEN_MENU);
    }
}

// --- TEST SCREEN ---
void handleTestKey(char key) {
    if (key == '1') {
        // Read temperature
        char msg[40];
        snprintf(msg, sizeof(msg), "TEMP: %.2f C (raw: %.2f)",
                 tempMgr.getFilteredTemp(), tempMgr.getRawTemp());
        displayMgr.showMessage(msg, COLOR_TEMP_ACTUAL);
    }
    else if (key == '2') {
        // Test display
        displayMgr.showMessage("DISPLAY TEST OK", COLOR_STATUS_OK);
    }
    else if (key == '3') {
        // Test keypad — already working if we got here
        displayMgr.showMessage("KEYPAD TEST OK", COLOR_STATUS_OK);
    }
    else if (key == '4') {
        // Test DFPlayer
        audioMgr.announceSystemStart();
        displayMgr.showMessage("PLAYING AUDIO...", COLOR_TEMP_ACTUAL);
    }
    else if (key == '5') {
        sysStatus.testStage = STAGE_BOTTOM;
        sysStatus.testPWM = 10.0f;
        sysStatus.testActive = false;
        sysStatus.state = STATE_TEST_MODE;
        showScreen(SCREEN_TEST_COMPONENT);
    }
    else if (key == '6') {
        sysStatus.testStage = STAGE_MIDDLE;
        sysStatus.testPWM = 10.0f;
        sysStatus.testActive = false;
        sysStatus.state = STATE_TEST_MODE;
        showScreen(SCREEN_TEST_COMPONENT);
    }
    else if (key == '7') {
        sysStatus.testStage = STAGE_TOP;
        sysStatus.testPWM = 10.0f;
        sysStatus.testActive = false;
        sysStatus.state = STATE_TEST_MODE;
        showScreen(SCREEN_TEST_COMPONENT);
    }
    else if (key == KEY_BACK) {
        showScreen(SCREEN_MENU);
    }
}

// --- TEST COMPONENT SCREEN ---
void handleTestComponentKey(char key) {
    if (key == KEY_UP) {
        sysStatus.testPWM += 5.0f;
        if (sysStatus.testPWM > 50.0f) sysStatus.testPWM = 50.0f;  // Safety cap
        if (sysStatus.testActive) {
            peltier.setPeltierPower(sysStatus.testStage, sysStatus.testPWM);
        }
        showScreen(SCREEN_TEST_COMPONENT);
    }
    else if (key == KEY_DOWN) {
        sysStatus.testPWM -= 5.0f;
        if (sysStatus.testPWM < 5.0f) sysStatus.testPWM = 5.0f;
        if (sysStatus.testActive) {
            peltier.setPeltierPower(sysStatus.testStage, sysStatus.testPWM);
        }
        showScreen(SCREEN_TEST_COMPONENT);
    }
    else if (key == KEY_CONFIRM) {
        // Start test — apply PWM
        sysStatus.testActive = true;
        peltier.setPeltierPower(sysStatus.testStage, sysStatus.testPWM);
        Serial.printf("[TEST] %s Peltier ON at %.0f%%\n",
                      (sysStatus.testStage == STAGE_BOTTOM) ? "BOTTOM" :
                      (sysStatus.testStage == STAGE_MIDDLE) ? "MIDDLE" : "TOP",
                      sysStatus.testPWM);
        showScreen(SCREEN_TEST_COMPONENT);
    }
    else if (key == KEY_BACK) {
        // Stop test and go back
        peltier.allPeltiersOff();
        sysStatus.testActive = false;
        sysStatus.state = STATE_IDLE;
        sysStatus.bottomPWM = 0;
        sysStatus.middlePWM = 0;
        sysStatus.topPWM = 0;
        showScreen(SCREEN_TEST);
    }
}

// --- ABOUT SCREEN ---
void handleAboutKey(char key) {
    if (key == KEY_BACK) {
        showScreen(SCREEN_MENU);
    }
}

// --- FAULT SCREEN ---
void handleFaultKey(char key) {
    if (key == KEY_ENTER) {
        if (safetyMgr.acknowledgeFault(sysStatus)) {
            sysStatus.state = STATE_IDLE;
            showScreen(SCREEN_HOME);
        } else {
            displayMgr.showMessage("Cannot clear fault yet", COLOR_STATUS_WARN);
        }
    }
}

// --- COMPLETE SCREEN ---
void handleCompleteKey(char key) {
    if (key == KEY_ENTER) {
        sysStatus.state = STATE_IDLE;
        sysStatus.currentStep = 0;
        showScreen(SCREEN_HOME);
    }
}

// --- STOPPED SCREEN ---
void handleStoppedKey(char key) {
    if (key == KEY_ENTER) {
        // Acknowledge emergency stop and return to IDLE
        // System does NOT automatically resume.
        safetyMgr.acknowledgeFault(sysStatus);
        sysStatus.state = STATE_IDLE;
        sysStatus.currentStep = 0;
        showScreen(SCREEN_HOME);
    }
}

// --- MANUAL PWM SCREEN ---
void handleManualPWMKey(char key) {
    if (key == KEY_BACK) {
        peltier.allPeltiersOff();
        sysStatus.state = STATE_IDLE;
        showScreen(SCREEN_MENU);
    }
}

// ============================================================
//  PROCESS CONTROL
// ============================================================

void startProcess() {
    Serial.println("[PROCESS] Starting process...");

    // Validate recipe
    if (!recipeMgr.validateRecipe()) {
        sysStatus.errorCode = ERROR_INVALID_RECIPE;
        displayMgr.showMessage("INVALID RECIPE!", COLOR_STATUS_ERR);
        showScreen(SCREEN_HOME);
        return;
    }

    // Pre-start safety check
    if (!safetyMgr.preStartCheck(tempMgr, sysStatus)) {
        displayMgr.showMessage("SAFETY CHECK FAILED!", COLOR_STATUS_ERR);
        Serial.printf("[PROCESS] Safety check failed: %s\n",
                      getErrorName(sysStatus.errorCode));
        showScreen(SCREEN_HOME);
        return;
    }

    // Announce system start
    audioMgr.announceSystemStart();

    // Reset PID
    pid.reset();

    // Initialize process state
    sysStatus.processStartTime = millis();
    sysStatus.currentStep = 1;
    sysStatus.pidOutput = 0.0f;

    // Fans are hardwired — no GPIO control needed.
    // If GPIO fan control were installed, turn fans ON here.

    // Enter Step 1
    enterApproachState();

    Serial.println("[PROCESS] Process started — Step 1");
}

void stopProcess() {
    safetyMgr.emergencyStop(peltier, audioMgr, sysStatus);
    showScreen(SCREEN_STOPPED);
}

void enterApproachState() {
    const Recipe& recipe = recipeMgr.getRecipe();
    uint8_t stepIdx = sysStatus.currentStep - 1;  // 0-based

    sysStatus.state = STATE_STEP_APPROACH;
    sysStatus.targetTemp = recipe.steps[stepIdx].targetTemp;
    sysStatus.currentSetpoint = sysStatus.targetTemp;
    sysStatus.holdDurationMs = (unsigned long)recipe.steps[stepIdx].holdTimeMin * 60000UL;
    sysStatus.targetReached = false;
    sysStatus.targetConfirmed = false;
    sysStatus.targetReachedTime = 0;
    sysStatus.holdStartTime = 0;
    sysStatus.holdElapsedMs = 0;
    sysStatus.holdTimerRunning = false;
    sysStatus.stepStartTime = millis();

    // --- PID: approach gain set + temperature-scaled feedforward ---
    // Switch to APPROACH gains (optimised for reaching a fixed setpoint).
    // Load NVS-stored Kp/Ki/Kd as the user-tuned APPROACH gains; the
    // APPROACH_K* compile-time constants are the boot defaults only.
    pid.setTunings(APPROACH_KP, APPROACH_KI, APPROACH_KD);
    pid.setIntegralZone(APPROACH_INTEGRAL_ZONE);
    // Feedforward: pre-load baseline output based on measured hardware data.
    // Eliminates the slow ramp-from-zero and reduces overshoot.
    float ff = computeFF(sysStatus.targetTemp);
    pid.setFeedforward(ff);
    pid.setRateLimit(PID_OUTPUT_RATE_LIMIT);
    // Soft-reset PID for smooth transition; slew limiter starts from current output.
    pid.softReset(0.3f);

    Serial.printf("[STEP %d] Approaching %.1f °C  FF=%.1f%%  Gains: Kp=%.1f Ki=%.2f Kd=%.1f\n",
                  sysStatus.currentStep, sysStatus.targetTemp,
                  ff, APPROACH_KP, APPROACH_KI, APPROACH_KD);

    // Announce step
    audioMgr.announceStepStart(sysStatus.currentStep);

    // Schedule a one-shot display health check 4 seconds from now.
    // The 12V system begins heavy switching as soon as a step starts;
    // that's the highest-risk window for an RST noise glitch.
    displayCheckPending = true;
    displayCheckAt = millis() + 4000UL;

    showScreen(SCREEN_HOME);
}

void enterHoldState() {
    sysStatus.state = STATE_STEP_HOLD;
    sysStatus.holdStartTime = millis();
    sysStatus.holdElapsedMs = 0;
    sysStatus.holdTimerRunning = true;

    Serial.printf("[STEP %d] Target confirmed — holding for %lu ms (%.1f min)\n",
                  sysStatus.currentStep,
                  sysStatus.holdDurationMs,
                  sysStatus.holdDurationMs / 60000.0f);
}

void enterRampState() {
    sysStatus.state = STATE_STEP5_RAMP;
    sysStatus.rampStartTemp = sysStatus.filteredTemp;
    sysStatus.rampStartTime = millis();
    sysStatus.rampLag = false;
    sysStatus.rampLagAmount = 0.0f;
    sysStatus.currentStep = 5;

    // Set initial setpoint to current temperature
    sysStatus.currentSetpoint = sysStatus.rampStartTemp;
    sysStatus.targetTemp = RAMP_FINAL_TARGET;

    // --- PID: ramp gain set + feedforward for current temperature ---
    // Higher Kp for tracking a moving setpoint; minimal Ki to avoid windup
    // while the setpoint continuously moves away.
    pid.setTunings(RAMP_KP, RAMP_KI, RAMP_KD);
    pid.setIntegralZone(RAMP_INTEGRAL_ZONE);
    float ff = computeFF(sysStatus.rampStartTemp);
    pid.setFeedforward(ff);
    pid.setRateLimit(PID_OUTPUT_RATE_LIMIT);
    pid.softReset(0.5f);

    Serial.printf("[STEP 5] RAMP from %.1f°C at %.1f°C/min to %.1f°C  FF=%.1f%%  "
                  "Gains: Kp=%.1f Ki=%.2f Kd=%.1f\n",
                  sysStatus.rampStartTemp, RAMP_RATE_DEFAULT, RAMP_FINAL_TARGET,
                  ff, RAMP_KP, RAMP_KI, RAMP_KD);

    // Announce
    audioMgr.announceStepStart(5);
    audioMgr.announceRampStarted();

    // Schedule one-shot display check — ramp mode also stresses the 12V supply.
    displayCheckPending = true;
    displayCheckAt = millis() + 4000UL;

    showScreen(SCREEN_HOME);
}

void advanceStep() {
    uint8_t nextStep = sysStatus.currentStep + 1;

    Serial.printf("[PROCESS] Step %d complete → advancing to Step %d\n",
                  sysStatus.currentStep, nextStep);

    // Announce step complete (we reuse step start audio for simplicity)
    // In a fuller system, you'd have separate "Step X complete" audio files.

    if (nextStep == 5) {
        // Step 5 is special — ramp mode
        sysStatus.currentStep = 5;
        sysStatus.state = STATE_STEP_TRANSITION;
        enterRampState();
    } else if (nextStep <= 4) {
        sysStatus.currentStep = nextStep;
        sysStatus.state = STATE_STEP_TRANSITION;
        enterApproachState();
    } else {
        // Should not reach here (Step 5 completes via ramp logic)
        completeProcess();
    }
}

void completeProcess() {
    Serial.println("[PROCESS] ========== PROCESS COMPLETE ==========");

    peltier.allPeltiersOff();
    sysStatus.state = STATE_COMPLETE;
    sysStatus.pidOutput = 0.0f;
    sysStatus.bottomPWM = 0.0f;
    sysStatus.middlePWM = 0.0f;
    sysStatus.topPWM = 0.0f;

    // Fans continue running (hardwired to PSU).
    // With GPIO fan control, you'd start a cooldown timer here.

    audioMgr.announceProcessComplete();
    showScreen(SCREEN_COMPLETE);
}

// ============================================================
//  STATE UPDATE FUNCTIONS
// ============================================================

void updateApproachState() {
    float error = fabsf(sysStatus.filteredTemp - sysStatus.targetTemp);

    if (error <= TARGET_TOLERANCE) {
        // Temperature is within tolerance band
        if (!sysStatus.targetReached) {
            sysStatus.targetReached = true;
            sysStatus.targetReachedTime = millis();
            Serial.printf("[STEP %d] Temperature entered tolerance band (%.1f°C)\n",
                          sysStatus.currentStep, sysStatus.filteredTemp);
        }

        // Check confirmation time
        if (sysStatus.targetReached &&
            (millis() - sysStatus.targetReachedTime) >= TARGET_CONFIRM_TIME_MS) {
            sysStatus.targetConfirmed = true;
            Serial.printf("[STEP %d] Target CONFIRMED after %d sec in band\n",
                          sysStatus.currentStep, TARGET_CONFIRM_TIME_MS / 1000);
            enterHoldState();
        }
    } else {
        // Temperature left tolerance band — reset confirmation timer
        if (sysStatus.targetReached) {
            Serial.printf("[STEP %d] Left tolerance band (%.1f°C, target %.1f°C)\n",
                          sysStatus.currentStep, sysStatus.filteredTemp,
                          sysStatus.targetTemp);
        }
        sysStatus.targetReached = false;
        sysStatus.targetReachedTime = 0;
    }
}

void updateHoldState() {
    float error = fabsf(sysStatus.filteredTemp - sysStatus.targetTemp);

    #if HOLD_TIMER_PAUSE_ON_DEVIATION
        if (error <= TARGET_TOLERANCE) {
            // In tolerance — accumulate hold time
            if (sysStatus.holdTimerRunning) {
                sysStatus.holdElapsedMs = millis() - sysStatus.holdStartTime;
            } else {
                // Resuming after deviation
                sysStatus.holdTimerRunning = true;
                // Adjust start time to preserve accumulated time
                sysStatus.holdStartTime = millis() - sysStatus.holdElapsedMs;
            }
        } else {
            // Out of tolerance — pause timer
            if (sysStatus.holdTimerRunning) {
                sysStatus.holdElapsedMs = millis() - sysStatus.holdStartTime;
                sysStatus.holdTimerRunning = false;
                Serial.printf("[STEP %d] Hold timer PAUSED (temp deviation: %.1f°C)\n",
                              sysStatus.currentStep, sysStatus.filteredTemp);
            }
        }
    #else
        // Timer runs regardless of deviation
        sysStatus.holdElapsedMs = millis() - sysStatus.holdStartTime;
    #endif

    // Check if hold duration is complete
    if (sysStatus.holdElapsedMs >= sysStatus.holdDurationMs) {
        Serial.printf("[STEP %d] Hold COMPLETE (%.1f min)\n",
                      sysStatus.currentStep,
                      sysStatus.holdDurationMs / 60000.0f);
        advanceStep();
    }
}

void updateRampState() {
    unsigned long now = millis();

    // Safety backstop: if the ramp has been running far longer than any
    // realistic scenario allows, the target is physically unreachable —
    // fault out instead of running indefinitely at high cooling demand.
    if ((now - sysStatus.rampStartTime) >= RAMP_MAX_DURATION_MS) {
        Serial.printf("[STEP 5] RAMP TIMEOUT after %lu min — target unreachable\n",
                      (now - sysStatus.rampStartTime) / 60000UL);
        safetyMgr.triggerRampTimeoutFault(peltier, audioMgr, sysStatus);
        showScreen(SCREEN_FAULT);
        return;
    }

    // Update ramp setpoint continuously
    float elapsedMin = (now - sysStatus.rampStartTime) / 60000.0f;

    // Setpoint = startTemp + rampRate × elapsed minutes
    // rampRate is negative (−1°C/min), so setpoint decreases
    float newSetpoint = sysStatus.rampStartTemp + (RAMP_RATE_DEFAULT * elapsedMin);

    // Clamp at final target
    if (newSetpoint < RAMP_FINAL_TARGET) {
        newSetpoint = RAMP_FINAL_TARGET;
    }

    sysStatus.currentSetpoint = newSetpoint;

    // Update feedforward to track the moving setpoint.
    // As temperature drops, FF increases to pre-load the additional power
    // needed — the PID then only corrects the residual lag/lead error.
    pid.setFeedforward(computeFF(newSetpoint));

    // Check ramp lag
    float lag = sysStatus.filteredTemp - sysStatus.currentSetpoint;
    sysStatus.rampLagAmount = lag;
    sysStatus.rampLag = (lag > RAMP_LAG_THRESHOLD);

    // Check if ramp is complete (setpoint reached final target)
    if (newSetpoint <= RAMP_FINAL_TARGET &&
        sysStatus.filteredTemp <= (RAMP_FINAL_TARGET + TARGET_TOLERANCE)) {
        Serial.println("[STEP 5] RAMP COMPLETE — -20°C reached!");
        audioMgr.announceMinus20Reached();
        completeProcess();
    }
}

// ============================================================
//  DISPLAY HELPER
// ============================================================

void showScreen(ScreenID screen) {
    sysStatus.currentScreen = screen;
    displayMgr.drawScreen(screen, sysStatus, recipeMgr.getRecipe());
}

// ============================================================
//  SERIAL DEBUG OUTPUT
// ============================================================

void serialDebugPrint() {
    Serial.printf("[DBG] State=%-14s Step=%d/%d  Temp=%.2f°C  SP=%.2f°C  "
                  "PID=%.1f%%(FF=%.0f P=%.1f I=%.1f D=%.1f)  "
                  "BOT=%.0f%% MID=%.0f%% TOP=%.0f%%",
                  getStateName(sysStatus.state),
                  sysStatus.currentStep, 5,
                  sysStatus.filteredTemp,
                  sysStatus.currentSetpoint,
                  sysStatus.pidOutput,
                  pid.getFeedforward(),
                  pid.getPterm(), pid.getIterm(), pid.getDterm(),
                  sysStatus.bottomPWM,
                  sysStatus.middlePWM,
                  sysStatus.topPWM);

    if (sysStatus.state == STATE_STEP_HOLD && sysStatus.holdDurationMs > 0) {
        unsigned long remain = (sysStatus.holdDurationMs > sysStatus.holdElapsedMs)
                               ? (sysStatus.holdDurationMs - sysStatus.holdElapsedMs) : 0;
        Serial.printf("  Hold=%lus/%lus",
                      sysStatus.holdElapsedMs / 1000,
                      sysStatus.holdDurationMs / 1000);
    }

    if (sysStatus.state == STATE_STEP5_RAMP) {
        Serial.printf("  Ramp=%.1f°C/min  Lag=%.1f°C%s",
                      RAMP_RATE_DEFAULT,
                      sysStatus.rampLagAmount,
                      sysStatus.rampLag ? " [LAG!]" : "");
    }

    if (sysStatus.errorCode != ERROR_NONE) {
        Serial.printf("  ERR=%s", getErrorName(sysStatus.errorCode));
    }

    Serial.println();
}
