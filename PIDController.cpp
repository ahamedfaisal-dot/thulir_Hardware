/*
 * ============================================================
 *  TULIR — PID Controller Implementation
 *  PIDController.cpp
 * ============================================================
 *
 *  Algorithm: Positional PID + Feedforward + Output Slew Limiter
 *
 *    error(k)   = PV(k) − SP(k)          [positive = too warm → more cooling]
 *    P_term     = Kp × error(k)
 *    I_term    += Ki × error(k) × dt      [only while |error| < integralZone]
 *    D_term     = −Kd × (PV(k) − PV(k−1)) / dt   [on measurement, not error]
 *    raw        = FF + P_term + I_term + D_term
 *    output     = slew_limit(clamp(raw, 0, 100))
 *
 *  Feedforward (FF):
 *    Pre-loads the output to the baseline power needed for the current
 *    setpoint, based on measured hardware data (12V/6.1V/2.45V → −27°C):
 *      FF% = (PID_FF_AMBIENT_REF − SP) / (PID_FF_AMBIENT_REF − PID_FF_MAX_COOL) × 100
 *    This eliminates the slow ramp-from-zero that causes large overshoot.
 *    The PID corrects the residual error on top of the FF baseline.
 *
 *  Slew-rate limit:
 *    Caps how fast the output can change per second (PID_OUTPUT_RATE_LIMIT %/s).
 *    Prevents sudden swings (e.g., at step transitions) from exciting
 *    the system's thermal lag into oscillation.
 *
 *  Integral separation zone:
 *    Integral only accumulates while |error| < _integralZone. Prevents
 *    windup during long approach from far away (ambient to −20°C). The
 *    zone is different for approach (5°C, wider) and ramp (2°C, tight).
 *
 *  Derivative-on-measurement:
 *    Computes dPV/dt instead of d(error)/dt to avoid the "derivative kick"
 *    when the setpoint steps (e.g., advancing from one step to the next).
 * ============================================================
 */

#include "PIDController.h"

// ============================================================
//  Constructor
// ============================================================

PIDController::PIDController()
    : _kp(DEFAULT_KP)
    , _ki(DEFAULT_KI)
    , _kd(DEFAULT_KD)
    , _outputMin(PID_OUTPUT_MIN)
    , _outputMax(PID_OUTPUT_MAX)
    , _integralZone(APPROACH_INTEGRAL_ZONE)
    , _sampleTimeMs(PID_SAMPLE_TIME_MS)
    , _output(0.0f)
    , _pTerm(0.0f)
    , _iTerm(0.0f)
    , _dTerm(0.0f)
    , _feedforward(0.0f)
    , _rateLimit(PID_OUTPUT_RATE_LIMIT)
    , _prevRateOutput(0.0f)
    , _lastMeasurement(0.0f)
    , _lastComputeTime(0)
    , _firstCompute(true)
{
}

// ============================================================
//  Configuration
// ============================================================

void PIDController::setTunings(float kp, float ki, float kd) {
    if (kp < 0.0f || ki < 0.0f || kd < 0.0f) return;
    _kp = kp;
    _ki = ki;
    _kd = kd;
}

void PIDController::setIntegralZone(float zoneDeg) {
    if (zoneDeg < 0.0f) return;
    _integralZone = zoneDeg;
}

void PIDController::setOutputLimits(float minVal, float maxVal) {
    if (minVal >= maxVal) return;
    _outputMin = minVal;
    _outputMax = maxVal;
    _iTerm = CLAMP(_iTerm, _outputMin, _outputMax);
    _output = CLAMP(_output, _outputMin, _outputMax);
}

void PIDController::setSampleTime(uint32_t ms) {
    if (ms < 1) return;
    _sampleTimeMs = ms;
}

void PIDController::setFeedforward(float pct) {
    _feedforward = CLAMP(pct, 0.0f, _outputMax);
}

void PIDController::setRateLimit(float pctPerSec) {
    _rateLimit = (pctPerSec < 0.0f) ? 0.0f : pctPerSec;
}

// ============================================================
//  Static helper — temperature-scaled feedforward
// ============================================================
/*
 *  Hardware data (measured by user):
 *    12V (Bottom) / 6.1V (Middle) / 2.45V (Top) → −27°C at 100% output
 *    25°C ambient → 0% cooling needed (Step 1 baseline)
 *
 *  Linear interpolation:
 *    FF% = (PID_FF_AMBIENT_REF − setpoint)
 *          / (PID_FF_AMBIENT_REF − PID_FF_MAX_COOL) × 100
 *  Clamped to [0, PID_FF_MAX_PCT] (leaves headroom for PID correction).
 */
float PIDController::computeFeedforward(float setpoint) {
    float range = PID_FF_AMBIENT_REF - PID_FF_MAX_COOL;   // = 25 − (−27) = 52
    float delta = PID_FF_AMBIENT_REF - setpoint;
    float ff = (delta / range) * 100.0f;
    return CLAMP(ff, 0.0f, PID_FF_MAX_PCT);
}

// ============================================================
//  Compute
// ============================================================

bool PIDController::compute(float setpoint, float measurement) {
    unsigned long now = millis();

    // Check if sample time has elapsed
    if (!_firstCompute && (now - _lastComputeTime) < _sampleTimeMs) {
        return false;
    }

    // Actual time step in seconds
    float dt;
    if (_firstCompute) {
        dt = _sampleTimeMs / 1000.0f;
        _lastMeasurement = measurement;
        _firstCompute = false;
    } else {
        dt = (now - _lastComputeTime) / 1000.0f;
        if (dt <= 0.0f) dt = _sampleTimeMs / 1000.0f;
    }

    // Error: positive when actual > setpoint (need more cooling)
    float error = measurement - setpoint;

    // --- Proportional term ---
    _pTerm = _kp * error;

    // --- Integral term with integral-zone anti-windup ---
    // Only accumulate while near the setpoint. This prevents the large windup
    // that builds during a long approach from ambient to a cold target, which
    // would otherwise hold the output pegged high long after crossing the target.
    if (fabsf(error) <= _integralZone) {
        _iTerm += _ki * error * dt;
        _iTerm = CLAMP(_iTerm, _outputMin - _feedforward,
                               _outputMax - _feedforward);
    }

    // --- Derivative term (on measurement, NOT on error) ---
    // Avoids "derivative kick" when the setpoint steps suddenly.
    float dMeasurement = (measurement - _lastMeasurement) / dt;
    _dTerm = _kd * dMeasurement;   // positive dM (temp rising) → more cooling

    // --- Raw output = FF + P + I + D ---
    float rawOutput = _feedforward + _pTerm + _iTerm + _dTerm;
    rawOutput = CLAMP(rawOutput, _outputMin, _outputMax);

    // --- Output slew-rate limit ---
    // Prevents sudden large output jumps that excite thermal lag into oscillation.
    if (_rateLimit > 0.0f) {
        float maxDelta = _rateLimit * dt;
        float delta = rawOutput - _prevRateOutput;
        delta = CLAMP(delta, -maxDelta, maxDelta);
        _output = CLAMP(_prevRateOutput + delta, _outputMin, _outputMax);
    } else {
        _output = rawOutput;
    }
    _prevRateOutput = _output;

    // Store state for next iteration
    _lastMeasurement = measurement;
    _lastComputeTime = now;

    return true;
}

// ============================================================
//  Getters
// ============================================================

float PIDController::getOutput()      const { return _output; }
float PIDController::getPterm()       const { return _pTerm; }
float PIDController::getIterm()       const { return _iTerm; }
float PIDController::getDterm()       const { return _dTerm; }
float PIDController::getFeedforward() const { return _feedforward; }
float PIDController::getKp()          const { return _kp; }
float PIDController::getKi()          const { return _ki; }
float PIDController::getKd()          const { return _kd; }

// ============================================================
//  Reset
// ============================================================

void PIDController::reset() {
    _pTerm = 0.0f;
    _iTerm = 0.0f;
    _dTerm = 0.0f;
    _output = _feedforward;        // Start at FF level, not zero
    _prevRateOutput = _feedforward; // Slew limiter starts from FF, not zero
    _firstCompute = true;
    _lastComputeTime = 0;
}

void PIDController::softReset(float preserveRatio) {
    // Keep a fraction of the integral for smooth inter-step transitions.
    // The slew limiter's prevOutput is also preserved so there is no
    // rate-limiter-induced glitch at the transition instant.
    _iTerm *= preserveRatio;
    _pTerm = 0.0f;
    _dTerm = 0.0f;
    _prevRateOutput = _output;     // Continue smoothly from current output
    _firstCompute = true;
    _lastComputeTime = 0;
}

void PIDController::forceOutput(float value) {
    _output = CLAMP(value, _outputMin, _outputMax);
    // Back-calculate integral so that when PID resumes it continues smoothly.
    _iTerm = _output - _feedforward;
    _iTerm = CLAMP(_iTerm, _outputMin, _outputMax);
    _pTerm = 0.0f;
    _dTerm = 0.0f;
    _prevRateOutput = _output;
}

