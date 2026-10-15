/*
 * ============================================================
 *  THULIR — 3-Stage Cascaded Peltier Temperature Controller
 *  ServoManager.cpp — Dual Servo Control via PCA9685 (I2C)
 * ============================================================
 *
 *  Talks to the PCA9685 directly over Wire (no external library).
 *  The I2C bus (SDA=GPIO35, SCL=GPIO36) is started by TemperatureManager
 *  for the SHT3x; this class only adds a second device (0x40) to it.
 *
 *  PCA9685: 12-bit PWM (4096 ticks), 25 MHz internal oscillator.
 *    Prescale = round(25e6 / (4096 × 50 Hz)) − 1 = 121  → ≈ 50 Hz
 *    Tick     = 20000 µs / 4096 ≈ 4.88 µs
 *    duty     = pulse_us × 4096 / 20000
 *      500 µs  → 102 ticks (0°)
 *      1450 µs → 297 ticks (90°)
 *      2400 µs → 491 ticks (180°)
 *
 * ============================================================
 */

#include "ServoManager.h"
#include <Arduino.h>
#include <Wire.h>

#define PCA_MODE1      0x00
#define PCA_PRESCALE   0xFE
#define PCA_LED0_ON_L  0x06
#define PCA_PRESCALE_50HZ 121

// ---------------------------------------------------------------------------
//  Low-level PCA9685 register access
// ---------------------------------------------------------------------------
bool ServoManager::_pcaWrite(uint8_t reg, const uint8_t* data, uint8_t len) {
    Wire.beginTransmission(SERVO_PCA9685_ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) Wire.write(data[i]);
    return Wire.endTransmission() == 0;
}

// ---------------------------------------------------------------------------
//  Convert degrees → PCA9685 ticks and write one channel
// ---------------------------------------------------------------------------
void ServoManager::_writeAngle(uint8_t channel, uint8_t angle) {
    if (!_ready) return;
    if (angle > 180) angle = 180;

    uint32_t pulse_us = SERVO_MIN_PULSE_US +
        ((uint32_t)angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US)) / 180UL;
    uint16_t off = (uint16_t)((pulse_us * 4096UL) / 20000UL);

    // LEDn_ON = 0, LEDn_OFF = off  (4 bytes: ON_L, ON_H, OFF_L, OFF_H)
    uint8_t buf[4] = { 0, 0, (uint8_t)(off & 0xFF), (uint8_t)(off >> 8) };
    _pcaWrite(PCA_LED0_ON_L + 4 * channel, buf, 4);
}

// ---------------------------------------------------------------------------
//  begin() — configure PCA9685 for 50 Hz, open the lid at boot
// ---------------------------------------------------------------------------
void ServoManager::begin() {
    // Probe the board first so a missing/miswired PCA9685 is reported clearly.
    Wire.beginTransmission(SERVO_PCA9685_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.printf("[SERVO] PCA9685 NOT FOUND at 0x%02X — check SDA/SCL/VCC/GND\n",
                      SERVO_PCA9685_ADDR);
        _ready = false;
        return;
    }

    // Prescale can only be changed while the oscillator is asleep.
    uint8_t v = 0x10;                       // MODE1: SLEEP
    _pcaWrite(PCA_MODE1, &v, 1);
    v = PCA_PRESCALE_50HZ;
    _pcaWrite(PCA_PRESCALE, &v, 1);
    v = 0x20;                               // MODE1: wake, auto-increment
    _pcaWrite(PCA_MODE1, &v, 1);
    delay(5);                               // oscillator settle (>500 µs)
    v = 0xA0;                               // restart + auto-increment
    _pcaWrite(PCA_MODE1, &v, 1);

    _ready = true;
    Serial.printf("[SERVO] PCA9685 @0x%02X ready — servo1 ch%d, servo2 ch%d%s\n",
                  SERVO_PCA9685_ADDR, SERVO1_CHANNEL, SERVO2_CHANNEL,
                  SERVO2_ENABLED ? "" : " (disabled)");

    // Boot happens before the main loop runs, so finish the sequence here.
    openServos();
    while (_seq != SEQ_IDLE) {
        _service();
        delay(10);
    }
    Serial.println("[SERVO] OPEN (power-on)");
}

// ---------------------------------------------------------------------------
//  Sequenced open / close (non-blocking — advanced by _service()).
//    OPEN  : servo 1 now, servo 2 after SERVO_SEQ_DELAY_MS
//    CLOSE : servo 2 now, servo 1 after SERVO_SEQ_DELAY_MS
// ---------------------------------------------------------------------------
void ServoManager::openServos() {
    _writeAngle(SERVO1_CHANNEL, SERVO1_OPEN_ANGLE);
    _open = true;
    if (SERVO2_ENABLED) {
        _seq      = SEQ_OPEN_S2;
        _seqStart = millis();
    } else {
        _seq = SEQ_IDLE;
    }
    Serial.printf("[SERVO] OPEN: servo1=%d°, servo2=%d° in %d ms\n",
                  SERVO1_OPEN_ANGLE, SERVO2_OPEN_ANGLE, SERVO_SEQ_DELAY_MS);
}

void ServoManager::closeServos() {
    _open = false;
    if (SERVO2_ENABLED) {
        _writeAngle(SERVO2_CHANNEL, SERVO2_CLOSED_ANGLE);
        _seq      = SEQ_CLOSE_S1;
        _seqStart = millis();
    } else {
        _writeAngle(SERVO1_CHANNEL, SERVO1_CLOSED_ANGLE);
        _seq = SEQ_IDLE;
    }
    Serial.printf("[SERVO] CLOSE: servo2=%d°, servo1=%d° in %d ms\n",
                  SERVO2_CLOSED_ANGLE, SERVO1_CLOSED_ANGLE, SERVO_SEQ_DELAY_MS);
}

// Fires the delayed second move of an open/close sequence.
void ServoManager::_service() {
    if (_seq == SEQ_IDLE) return;
    if ((millis() - _seqStart) < SERVO_SEQ_DELAY_MS) return;

    if (_seq == SEQ_OPEN_S2) {
        _writeAngle(SERVO2_CHANNEL, SERVO2_OPEN_ANGLE);
        Serial.println("[SERVO] servo 2 opened");
    } else {
        _writeAngle(SERVO1_CHANNEL, SERVO1_CLOSED_ANGLE);
        Serial.println("[SERVO] servo 1 closed");
    }
    _seq = SEQ_IDLE;
}

// ---------------------------------------------------------------------------
//  startStepOpenWindow() — open now; update() closes after SERVO_STEP_OPEN_MS
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
// ---------------------------------------------------------------------------
void ServoManager::update(SystemStatus& status) {
    _service();
    if (status.servoStepTimerActive) {
        if ((millis() - status.servoStepOpenTime) >= SERVO_STEP_OPEN_MS) {
            status.servoStepTimerActive = false;
            closeServos();
            Serial.println("[SERVO] Step-open window expired — closing");
        }
    }
    status.servosOpen = _open;
}
