/*
 * ============================================================
 *  TULIR — PID Controller Implementation
 *  PIDController.cpp
 * ============================================================
 */

#include "PIDController.h"

PIDController::PIDController()
    : _kp(DEFAULT_KP)
    , _ki(DEFAULT_KI)
    , _kd(DEFAULT_KD)
    , _outputMin(PID_OUTPUT_MIN)
    , _outputMax(PID_OUTPUT_MAX)
    , _sampleTimeMs(PID_SAMPLE_TIME_MS)
    , _output(0.0f)
    , _pTerm(0.0f)
    , _iTerm(0.0f)
    , _dTerm(0.0f)
    , _lastMeasurement(0.0f)
    , _lastComputeTime(0)
    , _firstCompute(true)
{
}

void PIDController::setTunings(float kp, float ki, float kd) {
    if (kp < 0.0f || ki < 0.0f || kd < 0.0f) return;
    _kp = kp;
    _ki = ki;
    _kd = kd;
}

void PIDController::setOutputLimits(float minVal, float maxVal) {
    if (minVal >= maxVal) return;
    _outputMin = minVal;
    _outputMax = maxVal;
    // Clamp existing integral term to new limits
    _iTerm = CLAMP(_iTerm, _outputMin, _outputMax);
    _output = CLAMP(_output, _outputMin, _outputMax);
}

void PIDController::setSampleTime(uint32_t ms) {
    if (ms < 1) return;
    _sampleTimeMs = ms;
}

bool PIDController::compute(float setpoint, float measurement) {
    unsigned long now = millis();

    // Check if sample time has elapsed
    if (!_firstCompute && (now - _lastComputeTime) < _sampleTimeMs) {
        return false;
    }

    // Actual time step in seconds for integral/derivative
    float dt;
    if (_firstCompute) {
        dt = _sampleTimeMs / 1000.0f;
        _lastMeasurement = measurement;
        _firstCompute = false;
    } else {
        dt = (now - _lastComputeTime) / 1000.0f;
        if (dt <= 0.0f) dt = _sampleTimeMs / 1000.0f;
    }

    // Error: positive when actual > setpoint (need cooling)
    float error = measurement - setpoint;

    // --- Proportional term ---
    _pTerm = _kp * error;

    // --- Integral term with anti-windup ---
    _iTerm += _ki * error * dt;

    // Clamp integral to output limits to prevent windup
    _iTerm = CLAMP(_iTerm, _outputMin, _outputMax);

    // --- Derivative term (on measurement, not error) ---
    // Using derivative-on-measurement avoids "derivative kick"
    // when setpoint changes suddenly.
    float dMeasurement = (measurement - _lastMeasurement) / dt;
    _dTerm = _kd * dMeasurement;

    // --- Compute total output ---
    _output = _pTerm + _iTerm + _dTerm;

    // Clamp output to configured limits
    _output = CLAMP(_output, _outputMin, _outputMax);

    // Additional anti-windup: if output is saturated and error
    // is pushing further into saturation, stop integrating.
    if ((_output >= _outputMax && error > 0) ||
        (_output <= _outputMin && error < 0)) {
        // Undo the integration step to prevent further windup
        _iTerm -= _ki * error * dt;
        _iTerm = CLAMP(_iTerm, _outputMin, _outputMax);
    }

    // Store state for next iteration
    _lastMeasurement = measurement;
    _lastComputeTime = now;

    return true;
}

float PIDController::getOutput() const {
    return _output;
}

float PIDController::getPterm() const { return _pTerm; }
float PIDController::getIterm() const { return _iTerm; }
float PIDController::getDterm() const { return _dTerm; }

float PIDController::getKp() const { return _kp; }
float PIDController::getKi() const { return _ki; }
float PIDController::getKd() const { return _kd; }

void PIDController::reset() {
    _pTerm = 0.0f;
    _iTerm = 0.0f;
    _dTerm = 0.0f;
    _output = 0.0f;
    _firstCompute = true;
    _lastComputeTime = 0;
}

void PIDController::softReset(float preserveRatio) {
    // Keep a fraction of the integral to smooth transitions
    _iTerm *= preserveRatio;
    _pTerm = 0.0f;
    _dTerm = 0.0f;
    _firstCompute = true;
    _lastComputeTime = 0;
}

void PIDController::forceOutput(float value) {
    _output = CLAMP(value, _outputMin, _outputMax);
    _iTerm = _output;  // Set integral to match forced output
    _pTerm = 0.0f;
    _dTerm = 0.0f;
}
