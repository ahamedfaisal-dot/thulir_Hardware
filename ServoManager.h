/*
 * ============================================================
 *  THULIR — 3-Stage Cascaded Peltier Temperature Controller
 *  ServoManager.h — Dual Servo Motor Control
 * ============================================================
 *
 *  Drives two standard hobby servos (SG90 / MG90S style) using
 *  ESP32 LEDC hardware PWM — no external Servo library required.
 *
 *  Standard PWM parameters for hobby servos:
 *    Frequency : 50 Hz  (20 ms period)
 *    Pulse width: 500 µs (≈ 0°) to 2400 µs (≈ 180°)
 *
 *  Pin assignments and angle constants are in Config.h.
 *
 *  Behaviour controlled by thulir_final.ino:
 *    begin()           — attach pins, open both servos
 *    openServos()      — move both servos to SERVO_OPEN_ANGLE
 *    closeServos()     — move both servos to SERVO_CLOSED_ANGLE
 *    update(sysStatus) — call from main loop; manages the timed
 *                        step-completion open window and closes
 *                        automatically after SERVO_STEP_OPEN_MS
 *
 * ============================================================
 */

#pragma once

#include "Config.h"

class ServoManager {
public:
    // Initialise LEDC channels and open both servos.
    void begin();

    // Move both servos to the open position.
    void openServos();

    // Move both servos to the closed position.
    void closeServos();

    // Start the timed step-open window (called on each step completion).
    // Opens servos immediately; update() will close them after
    // SERVO_STEP_OPEN_MS milliseconds.
    void startStepOpenWindow(SystemStatus& status);

    // Call from the main loop — manages the timed auto-close.
    void update(SystemStatus& status);

    // Returns true if both servos are currently in the open position.
    bool isOpen() const { return _open; }

private:
    bool _open = false;

    // Write a servo angle (0–180°) to one LEDC channel.
    void _writeAngle(uint8_t channel, uint8_t angle);
};
