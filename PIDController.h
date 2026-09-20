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
 *    - Temperature-scaled feedforward (pre-loads baseline output
 *      based on real hardware data: 12V/6.1V/2.45V → −27°C)
 *    - Output slew-rate limiting (prevents oscillation-inducing jumps)
 *    - Integral anti-windup (clamping)
 *    - Runtime-configurable integral zone (approach vs ramp)
 *    - Derivative-on-measurement (avoids setpoint kick)
 *    - Gain scheduling: caller switches gains for approach/ramp modes
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

    // --- Gains ---
    void setTunings(float kp, float ki, float kd);

    // --- Integral zone (°C) ---
    // Integral only accumulates while |error| < zone.
    // Default: APPROACH_INTEGRAL_ZONE. Switch to RAMP_INTEGRAL_ZONE in ramp mode.
    void setIntegralZone(float zoneDeg);

    // --- Output limits ---
    void setOutputLimits(float minVal, float maxVal);

    // --- Sample time ---
    void setSampleTime(uint32_t ms);

    // --- Feedforward ---
    // Injects a pre-computed baseline into the output.
    // Updated each step (via computeFeedforward()) and during ramp each tick.
    void setFeedforward(float pct);

    // --- Output slew-rate limit ---
    // Caps how fast the output can change per second.
    // 0.0 = disabled. Recommended: PID_OUTPUT_RATE_LIMIT (%/sec).
    void setRateLimit(float pctPerSec);

    // --- Compute ---
    // Returns true if a new output was computed (sample time elapsed).
    // setpoint: desired temperature (°C)
    // measurement: actual temperature (°C)
    bool compute(float setpoint, float measurement);

    // --- Outputs ---
    float getOutput() const;
    float getPterm() const;
    float getIterm() const;
    float getDterm() const;
    float getFeedforward() const;

    // --- Gains readback ---
    float getKp() const;
    float getKi() const;
    float getKd() const;

    // --- Reset ---
    // Hard reset: clears all state. prevRateOutput is seeded from the
    // current FF so the slew limiter doesn't fight the initial FF jump.
    void reset();

    // Soft reset: keeps a fraction of the integral for smooth transitions.
    void softReset(float preserveRatio = 0.5f);

    // Force output to a specific value (for safety override).
    void forceOutput(float value);

    // --- Static helper: compute temperature-scaled feedforward ---
    // Based on measured hardware data:
    //   12V (bottom) / 6.1V (middle) / 2.45V (top) → −27°C at 100% output
    // Formula: FF% = (PID_FF_AMBIENT_REF − setpoint)
    //                / (PID_FF_AMBIENT_REF − PID_FF_MAX_COOL) × 100
    //   clamped to [0, PID_FF_MAX_PCT].
    static float computeFeedforward(float setpoint);

private:
    float    _kp, _ki, _kd;
    float    _outputMin, _outputMax;
    float    _integralZone;          // °C — runtime-configurable
    uint32_t _sampleTimeMs;

    float    _output;
    float    _pTerm, _iTerm, _dTerm;
    float    _feedforward;           // % — current FF baseline
    float    _rateLimit;             // %/sec — 0 = disabled
    float    _prevRateOutput;        // previous output for slew calculation

    float    _lastMeasurement;       // for derivative-on-measurement
    unsigned long _lastComputeTime;
    bool     _firstCompute;
};
