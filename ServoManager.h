/*
 * ============================================================
 *  THULIR — 3-Stage Cascaded Peltier Temperature Controller
 *  ServoManager.h — Dual Servo Motor Control
 * ============================================================
 *
 *  Drives two hobby servos through a PCA9685 I2C PWM board
 *  (channels 0 and 1) — no external library required.
 *
 *  Channel, address, angle and mirror settings are in Config.h.
 *
 *  Behaviour controlled by thulir_final.ino:
 *    begin()           — attach pins, open both servos
 *    openServos()      — servo 1 opens, then servo 2 after a delay
 *    closeServos()     — servo 2 closes, then servo 1 after a delay
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
    bool _open  = false;
    bool _ready = false;   // PCA9685 found and configured

    // Write a servo angle (0–180°) to one PCA9685 channel.
    void _writeAngle(uint8_t channel, uint8_t angle);

    // Sequenced open/close: pending delayed second move.
    enum Seq { SEQ_IDLE, SEQ_OPEN_S2, SEQ_CLOSE_S1 };
    Seq           _seq      = SEQ_IDLE;
    unsigned long _seqStart = 0;
    void _service();

    bool _pcaWrite(uint8_t reg, const uint8_t* data, uint8_t len);
};
