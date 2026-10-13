/*
 * Servo_Test.ino — runs by itself in a loop, no serial input needed.
 *
 * The lid servo (MG996R) goes CLOSED -> waits -> OPEN -> waits -> repeat.
 *
 * Wiring: servo signal -> GPIO15
 *         servo power  -> its OWN 5-6 V supply (NOT the ESP32 5V pin)
 *         that supply's GND -> ESP32 GND (common ground)
 *
 * Change CLOSED_ANGLE / OPEN_ANGLE below to match your lid, then copy the
 * final values into SERVO_CLOSED_ANGLE / SERVO_OPEN_ANGLE in Config.h.
 * The serial monitor (115200) shows what it is doing.
 * If the lid touches something and the servo hums, unplug it and reduce the angle.
 */
#define SERVO_PIN      15
#define SERVO_CHANNEL  4
#define SERVO_FREQ     50     // Hz
#define SERVO_BITS     14     // ESP32-S3 LEDC supports at most 14 bits (16 fails!)

const int CLOSED_ANGLE = 0;      // degrees, lid closed
const int OPEN_ANGLE   = 90;     // degrees, lid open
const unsigned long HOLD_MS = 3000;   // time to wait at each position

static void writeAngle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t pulse_us = 500UL + ((uint32_t)angle * 1900UL) / 180UL;   // 500..2400 us
    uint32_t duty = (pulse_us * (1UL << SERVO_BITS)) / 20000UL;       // 20 ms period
    ledcWrite(SERVO_CHANNEL, duty);
    Serial.printf("angle %3d  (pulse %lu us)\n", angle, (unsigned long)pulse_us);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    ledcSetup(SERVO_CHANNEL, SERVO_FREQ, SERVO_BITS);
    ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);
    Serial.println("\nServo auto test on GPIO15: CLOSED <-> OPEN, repeating.");
}

void loop() {
    Serial.print("CLOSED  ");
    writeAngle(CLOSED_ANGLE);
    delay(HOLD_MS);

    Serial.print("OPEN    ");
    writeAngle(OPEN_ANGLE);
    delay(HOLD_MS);
}
