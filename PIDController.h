/*
 * ============================================================
 *  TULIR — PID Controller
 *  PIDController.h
 * ============================================================
 *  Discrete PID controller for cooling-only temperature control.
 *
 *  Error convention (reverse-acting / cooling):
 *    error = processVariable − setpoint
 *    Positive error → temperature too high → increase cooling
 *    Negative error → temperature below target → reduce/stop cooling
 *
 *  Features:
 *    - Integral anti-windup (clamping)
 *    - Derivative-on-measurement (avoids setpoint kick)
 *    - Configurable sample time
 *    - Output limits [0, 100] %
 *    - Reset for step transitions
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include "Config.h"

class PIDController {
public:
    PIDController();

    // Set PID gains
    void setTunings(float kp, float ki, float kd);

    // Set output limits (default 0–100)
    void setOutputLimits(float min, float max);

    // Set sample time in ms
    void setSampleTime(uint32_t ms);

    // Compute PID output. Call this at regular intervals.
    // Returns true if a new output was computed (sample time elapsed).
    // setpoint: desired temperature (°C)
    // measurement: actual temperature (°C)
    bool compute(float setpoint, float measurement);

    // Get the last computed output (0–100%)
    float getOutput() const;

    // Get individual PID terms for tuning display
    float getPterm() const;
    float getIterm() const;
    float getDterm() const;

    // Get current gains
    float getKp() const;
    float getKi() const;
    float getKd() const;

    // Reset PID state (call on step transitions, process start)
    void reset();

    // Soft reset: keeps integral at a proportion of current output
    // Useful for smooth step transitions
    void softReset(float preserveRatio = 0.5f);

    // Force output to a specific value (for safety override)
    void forceOutput(float value);

private:
    float _kp, _ki, _kd;
    float _outputMin, _outputMax;
    uint32_t _sampleTimeMs;

    float _output;
    float _pTerm, _iTerm, _dTerm;
    float _lastMeasurement;  // For derivative-on-measurement
    unsigned long _lastComputeTime;
    bool _firstCompute;
};
