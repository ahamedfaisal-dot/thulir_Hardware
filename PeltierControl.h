/*
 * ============================================================
 *  TULIR — Peltier Control (BTS7960 PWM Driver)
 *  PeltierControl.h
 * ============================================================
 *  Manages three BTS7960 H-bridge modules driving the cascaded
 *  Peltier stack in unidirectional cooling mode.
 *
 *  Each BTS7960 is wired for forward-only operation:
 *    RPWM = PWM signal (cooling power)
 *    LPWM = LOW (no reverse)
 *    R_EN = HIGH (enable forward driver)
 *    L_EN = HIGH (enable forward driver)
 *
 *  The cascade power distribution takes a single master PID
 *  output (0–100%) and distributes it to the three stages
 *  using configurable base ratios, clamped by per-stage
 *  maximum limits.
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include "Config.h"

class PeltierControl {
public:
    PeltierControl();

    // Initialize BTS7960 GPIO and LEDC PWM channels
    void begin();

    // Set power for a single stage (0–100%)
    void setPeltierPower(PeltierStage stage, float percent);

    // Apply cascade power distribution from master PID output.
    // masterPercent: 0–100% cooling demand from PID.
    // Returns applied PWM percentages via the status struct.
    void setCascadePower(float masterPercent, SystemStatus& status);

    // Immediately turn off all Peltier outputs (safety critical)
    void allPeltiersOff();

    // Set power ratios (can be adjusted at runtime or from NVS)
    void setPowerRatios(float bottom, float middle, float top);

    // Set maximum PWM limits per stage
    void setMaxLimits(float bottomMax, float middleMax, float topMax);

    // Get current PWM duty percentage for a stage
    float getCurrentPWM(PeltierStage stage) const;

    // Check if any stage is actively outputting
    bool isActive() const;

private:
    // Per-stage pin configuration
    struct StageConfig {
        uint8_t rpwmPin;
        uint8_t lpwmPin;
        uint8_t renPin;
        uint8_t lenPin;
    };

    StageConfig _stages[STAGE_COUNT];
    float       _powerRatios[STAGE_COUNT];
    float       _maxLimits[STAGE_COUNT];
    float       _currentPWM[STAGE_COUNT];  // Current applied PWM %

    void applyPWM(PeltierStage stage, float percent);
};
