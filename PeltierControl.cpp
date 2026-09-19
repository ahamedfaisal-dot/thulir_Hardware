/*
 * ============================================================
 *  TULIR — Peltier Control Implementation
 *  PeltierControl.cpp
 * ============================================================
 *  Uses ESP32 LEDC peripheral for hardware PWM generation.
 *
 *  Arduino-ESP32 Core 2.x API (channel-based):
 *    ledcSetup(channel, freq, resolution)
 *    ledcAttachPin(pin, channel)
 *    ledcWrite(channel, duty)
 *
 *  LEDC channel assignments:
 *    Channel 0 → Bottom Peltier RPWM
 *    Channel 1 → Middle Peltier RPWM
 *    Channel 2 → Top    Peltier RPWM
 * ============================================================
 */

#include "PeltierControl.h"

PeltierControl::PeltierControl() {
    // Initialize stage pin configurations
    // Note: MIDDLE_LEN (GPIO11) and TOP_LPWM (GPIO13) are hardwired:
    //   MIDDLE_LEN → 3.3V (always HIGH)  — GPIO11 freed for TFT MOSI
    //   TOP_LPWM   → GND  (always LOW)   — GPIO13 freed for TFT SCK
    // These freed pins are replaced with 255 (unused sentinel).
    _stages[STAGE_BOTTOM] = { BOTTOM_RPWM_PIN, BOTTOM_LPWM_PIN,
                              BOTTOM_REN_PIN,  BOTTOM_LEN_PIN };
    _stages[STAGE_MIDDLE] = { MIDDLE_RPWM_PIN, MIDDLE_LPWM_PIN,
                              MIDDLE_REN_PIN,  255 };           // LEN hardwired 3.3V
    _stages[STAGE_TOP]    = { TOP_RPWM_PIN,    255,             // LPWM hardwired GND
                              TOP_REN_PIN,     TOP_LEN_PIN };

    // Default power ratios from Config.h
    _powerRatios[STAGE_BOTTOM] = BOTTOM_POWER_RATIO;
    _powerRatios[STAGE_MIDDLE] = MIDDLE_POWER_RATIO;
    _powerRatios[STAGE_TOP]    = TOP_POWER_RATIO;

    // Default maximum limits from Config.h
    _maxLimits[STAGE_BOTTOM] = BOTTOM_MAX_PWM_PCT;
    _maxLimits[STAGE_MIDDLE] = MIDDLE_MAX_PWM_PCT;
    _maxLimits[STAGE_TOP]    = TOP_MAX_PWM_PCT;

    // No output at startup
    for (int i = 0; i < STAGE_COUNT; i++) {
        _currentPWM[i] = 0.0f;
    }
}

void PeltierControl::begin() {
    for (int i = 0; i < STAGE_COUNT; i++) {
        // Configure enable pins as outputs, drive HIGH
        // (lenPin == 255 means it is hardwired to 3.3V externally)
        pinMode(_stages[i].renPin, OUTPUT);
        digitalWrite(_stages[i].renPin, HIGH);
        if (_stages[i].lenPin != 255) {
            pinMode(_stages[i].lenPin, OUTPUT);
            digitalWrite(_stages[i].lenPin, HIGH);
        }

        // LPWM held LOW (unidirectional cooling only)
        // NEVER reverse Peltier polarity during operation.
        // (lpwmPin == 255 means it is hardwired to GND externally)
        if (_stages[i].lpwmPin != 255) {
            pinMode(_stages[i].lpwmPin, OUTPUT);
            digitalWrite(_stages[i].lpwmPin, LOW);
        }

        // Configure RPWM as LEDC PWM output
        // Arduino-ESP32 Core 2.x: channel-based API
        // Channel assignment: Bottom=0, Middle=1, Top=2
        uint8_t channel = (uint8_t)i;
        ledcSetup(channel, PWM_FREQUENCY, PWM_RESOLUTION);
        ledcAttachPin(_stages[i].rpwmPin, channel);

        // Start with zero output
        ledcWrite(channel, 0);
    }

    Serial.println("[PELTIER] BTS7960 outputs initialized (all OFF)");
    Serial.printf("[PELTIER] PWM: %d Hz, %d-bit resolution, max duty %d\n",
                  PWM_FREQUENCY, PWM_RESOLUTION, PWM_MAX_DUTY);
    Serial.printf("[PELTIER] Ratios: BOT=%.0f%% MID=%.0f%% TOP=%.0f%%\n",
                  _powerRatios[STAGE_BOTTOM] * 100.0f,
                  _powerRatios[STAGE_MIDDLE] * 100.0f,
                  _powerRatios[STAGE_TOP]    * 100.0f);
    Serial.printf("[PELTIER] Limits: BOT=%.0f%% MID=%.0f%% TOP=%.0f%%\n",
                  _maxLimits[STAGE_BOTTOM],
                  _maxLimits[STAGE_MIDDLE],
                  _maxLimits[STAGE_TOP]);
}

void PeltierControl::setPeltierPower(PeltierStage stage, float percent) {
    if (stage >= STAGE_COUNT) return;

    // Clamp to [0, max limit] for this stage
    percent = CLAMP(percent, 0.0f, _maxLimits[stage]);

    // Apply minimum threshold — below this, force to zero
    if (percent < MIN_PWM_THRESHOLD) {
        percent = 0.0f;
    }

    applyPWM(stage, percent);
}

void PeltierControl::setCascadePower(float masterPercent, SystemStatus& status) {
    // Master demand from PID: 0–100%
    masterPercent = CLAMP(masterPercent, 0.0f, 100.0f);

    // Distribute to each stage using base power ratios
    float bottomDemand = _powerRatios[STAGE_BOTTOM] * masterPercent;
    float middleDemand = _powerRatios[STAGE_MIDDLE] * masterPercent;
    float topDemand    = _powerRatios[STAGE_TOP]    * masterPercent;

    // Apply and clamp each stage
    setPeltierPower(STAGE_BOTTOM, bottomDemand);
    setPeltierPower(STAGE_MIDDLE, middleDemand);
    setPeltierPower(STAGE_TOP,    topDemand);

    // Update status for display
    status.bottomPWM = _currentPWM[STAGE_BOTTOM];
    status.middlePWM = _currentPWM[STAGE_MIDDLE];
    status.topPWM    = _currentPWM[STAGE_TOP];
}

void PeltierControl::allPeltiersOff() {
    for (int i = 0; i < STAGE_COUNT; i++) {
        ledcWrite((uint8_t)i, 0);  // channel == stage index
        _currentPWM[i] = 0.0f;
    }
}

void PeltierControl::setPowerRatios(float bottom, float middle, float top) {
    _powerRatios[STAGE_BOTTOM] = CLAMP(bottom, 0.0f, 1.0f);
    _powerRatios[STAGE_MIDDLE] = CLAMP(middle, 0.0f, 1.0f);
    _powerRatios[STAGE_TOP]    = CLAMP(top,    0.0f, 1.0f);
}

void PeltierControl::setMaxLimits(float bottomMax, float middleMax, float topMax) {
    _maxLimits[STAGE_BOTTOM] = CLAMP(bottomMax, 0.0f, 100.0f);
    _maxLimits[STAGE_MIDDLE] = CLAMP(middleMax, 0.0f, 100.0f);
    _maxLimits[STAGE_TOP]    = CLAMP(topMax,    0.0f, 100.0f);
}

float PeltierControl::getCurrentPWM(PeltierStage stage) const {
    if (stage >= STAGE_COUNT) return 0.0f;
    return _currentPWM[stage];
}

bool PeltierControl::isActive() const {
    for (int i = 0; i < STAGE_COUNT; i++) {
        if (_currentPWM[i] > 0.0f) return true;
    }
    return false;
}

// --- Private ---

void PeltierControl::applyPWM(PeltierStage stage, float percent) {
    percent = CLAMP(percent, 0.0f, 100.0f);
    _currentPWM[stage] = percent;

    uint32_t duty = PCT_TO_DUTY(percent);
    ledcWrite((uint8_t)stage, duty);  // channel == stage index
}
