/*
 * ============================================================
 *  TULIR — 3-Stage Cascaded Peltier Temperature Controller
 *  ServoManager.cpp — Dual Servo Motor Control Implementation
 * ============================================================
 *
 *  Uses ESP32 LEDC hardware PWM directly (no external Servo library).
 *
 *  Servo pulse width calculation:
 *    Period     = 1/50 Hz = 20,000 µs
 *    Resolution = 16 bits → 65536 ticks per period
 *    Tick width = 20000 µs / 65536 ≈ 0.3052 µs per tick
 *
 *    0°   → 500 µs  → duty = 500  / 20000 × 65536 ≈  1638
 *    90°  → 1450 µs → duty = 1450 / 20000 × 65536 ≈  4751
 *    180° → 2400 µs → duty = 2400 / 20000 × 65536 ≈  7864
 *
 *    General formula:
 *      pulse_us = 500 + (angle / 180.0) × 1900
 *      duty     = pulse_us / 20000.0 × 65536
 *
 * ============================================================
 */

#include "ServoManager.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Private helper — convert degrees to LEDC duty and write to channel
// ---------------------------------------------------------------------------
void ServoManager::_writeAngle(uint8_t channel, uint8_t angle) {
    // Clamp angle to 0–180°
    if (angle > 180) angle = 180;

    // Map angle → pulse width in microseconds
    // 0° → 500 µs,  180° → 2400 µs  (standard hobby servo range)
    uint32_t pulse_us = 500UL + ((uint32_t)angle * 1900UL) / 180UL;

    // Map pulse width → 16-bit LEDC duty (20 ms period = 20000 µs)
    uint32_t duty = (pulse_us * 65536UL) / 20000UL;

    ledcWrite(channel, duty);
}

// ---------------------------------------------------------------------------
//  begin() — attach LEDC channels to GPIO pins, open servos at boot
// ---------------------------------------------------------------------------
void ServoManager::begin() {
    // Configure LEDC channels for servo PWM
    ledcSetup(SERVO1_LEDC_CHANNEL, SERVO_LEDC_FREQ, SERVO_LEDC_RES);
    ledcSetup(SERVO2_LEDC_CHANNEL, SERVO_LEDC_FREQ, SERVO_LEDC_RES);

    ledcAttachPin(SERVO1_PIN, SERVO1_LEDC_CHANNEL);
    ledcAttachPin(SERVO2_PIN, SERVO2_LEDC_CHANNEL);

    Serial.printf("[SERVO] Servo 1 → GPIO %d (LEDC ch %d)\n",
                  SERVO1_PIN, SERVO1_LEDC_CHANNEL);
    Serial.printf("[SERVO] Servo 2 → GPIO %d (LEDC ch %d)\n",
                  SERVO2_PIN, SERVO2_LEDC_CHANNEL);

    // Open both servos on power-on
    openServos();
    Serial.println("[SERVO] Both servos OPEN (power-on)");
}

// ---------------------------------------------------------------------------
//  openServos() — move both servos to the configured open angle
// ---------------------------------------------------------------------------
void ServoManager::openServos() {
    _writeAngle(SERVO1_LEDC_CHANNEL, SERVO_OPEN_ANGLE);
    _writeAngle(SERVO2_LEDC_CHANNEL, SERVO_OPEN_ANGLE);
    _open = true;
    Serial.printf("[SERVO] OPEN (%d°)\n", SERVO_OPEN_ANGLE);
}

// ---------------------------------------------------------------------------
//  closeServos() — move both servos to the configured closed angle
// ---------------------------------------------------------------------------
void ServoManager::closeServos() {
    _writeAngle(SERVO1_LEDC_CHANNEL, SERVO_CLOSED_ANGLE);
    _writeAngle(SERVO2_LEDC_CHANNEL, SERVO_CLOSED_ANGLE);
    _open = false;
    Serial.printf("[SERVO] CLOSED (%d°)\n", SERVO_CLOSED_ANGLE);
}

// ---------------------------------------------------------------------------
//  startStepOpenWindow() — open servos now; they will auto-close after
//  SERVO_STEP_OPEN_MS milliseconds (managed by update()).
// ---------------------------------------------------------------------------
void ServoManager::startStepOpenWindow(SystemStatus& status) {
    openServos();
    status.servoStepTimerActive = true;
    status.servoStepOpenTime    = millis();
    Serial.printf("[SERVO] Step-open window started (%d s)\n",
                  SERVO_STEP_OPEN_MS / 1000);
}

// ---------------------------------------------------------------------------
//  update() — call from main loop every iteration.
//             Closes servos automatically when the step-open window expires.
// ---------------------------------------------------------------------------
void ServoManager::update(SystemStatus& status) {
    if (status.servoStepTimerActive) {
        if ((millis() - status.servoStepOpenTime) >= SERVO_STEP_OPEN_MS) {
            status.servoStepTimerActive = false;
            closeServos();
            Serial.println("[SERVO] Step-open window expired — closing");
        }
    }
    // Keep sysStatus in sync with the hardware state
    status.servosOpen = _open;
}
