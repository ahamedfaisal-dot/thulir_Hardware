/*
 * Servo_Test.ino — interactive PCA9685 servo calibration tool.
 *
 * Open Serial Monitor: 115200 baud, line ending = "Newline".
 * Type a command and press Enter.
 *
 * Wiring: PCA9685 VCC->3.3V, GND->GND, SDA->GPIO35, SCL->GPIO36,
 *         V+ -> 5-6 V servo supply (GND common), servo1->ch0, servo2->ch1.
 *
 * COMMANDS
 *   1 / 2 / 3     select servo 1 / servo 2 / BOTH
 *   <number>      move selected servo(s) to that angle (0-180), e.g. 45
 *   + / -         selected servo +5 / -5 degrees
 *   . / ,         selected servo +1 / -1 degree
 *   u <us>        raw pulse in microseconds, e.g. u 1500 (finds true end stops)
 *   o             store current angle as OPEN   for selected servo(s)
 *   c             store current angle as CLOSED for selected servo(s)
 *   go            move selected servo(s) to stored OPEN
 *   gc            move selected servo(s) to stored CLOSED
 *   t             run the real cycle: CLOSED -> OPEN 10 s -> CLOSED (x3)
 *   r             release (PWM off, servo goes limp — handy to position by hand)
 *   p             print values to paste into Config.h
 *   ?             show this help
 *
 * HOW TO CALIBRATE
 *   1. Type 1, then use + / - to find the angle where flap 1 is fully CLOSED
 *      (flat on the box, no humming). Type c.  Open the flap by hand/angle
 *      with + / - until it is wide enough, type o.
 *   2. Repeat with 2 for flap 2.
 *   3. Type t to watch the real cycle, then p and copy the result.
 *
 * If the servo hums at an end stop, back the angle off by a few degrees.
 */
#include <Wire.h>

#define SDA_PIN   35
#define SCL_PIN   36
#define PCA_ADDR  0x40

#define MIN_US    500
#define MAX_US    2400
#define OPEN_MS   10000UL   // must match SERVO_STEP_OPEN_MS in Config.h

int  curAngle[2]    = { 90, 90 };
int  openAngle[2]   = { 90, 90 };
int  closedAngle[2] = { 0, 180 };   // your measured values
int  sel = 3;                       // 1, 2 or 3 (both)
#define SEQ_DELAY_MS 800            // must match SERVO_SEQ_DELAY_MS in Config.h

static void pcaWrite(uint8_t reg, const uint8_t* d, uint8_t n) {
    Wire.beginTransmission(PCA_ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < n; i++) Wire.write(d[i]);
    Wire.endTransmission();
}

static void setPulse(uint8_t ch, uint32_t us) {
    uint16_t off = us * 4096UL / 20000UL;
    uint8_t b[4] = { 0, 0, (uint8_t)(off & 0xFF), (uint8_t)(off >> 8) };
    pcaWrite(0x06 + 4 * ch, b, 4);
}

static void release(uint8_t ch) {          // full-OFF bit: no pulses at all
    uint8_t b[4] = { 0, 0, 0, 0x10 };
    pcaWrite(0x06 + 4 * ch, b, 4);
}

static void setAngle(uint8_t ch, int angle) {
    angle = constrain(angle, 0, 180);
    curAngle[ch] = angle;
    setPulse(ch, MIN_US + (uint32_t)angle * (MAX_US - MIN_US) / 180UL);
}

static bool isSel(int ch) { return sel == 3 || sel == ch + 1; }

static void moveSel(int angle) {
    for (int ch = 0; ch < 2; ch++) if (isSel(ch)) setAngle(ch, angle);
    Serial.printf("servo1=%d  servo2=%d\n", curAngle[0], curAngle[1]);
}

static void nudge(int d) {
    for (int ch = 0; ch < 2; ch++) if (isSel(ch)) setAngle(ch, curAngle[ch] + d);
    Serial.printf("servo1=%d  servo2=%d\n", curAngle[0], curAngle[1]);
}

// Same order as the firmware: OPEN = servo1 then servo2, CLOSE = servo2 then servo1.
static void moveStored(bool open) {
    int first  = open ? 0 : 1;
    int second = open ? 1 : 0;
    if (isSel(first))  setAngle(first,  open ? openAngle[first]  : closedAngle[first]);
    if (sel == 3) delay(SEQ_DELAY_MS);
    if (isSel(second)) setAngle(second, open ? openAngle[second] : closedAngle[second]);
    Serial.printf("-> %s  servo1=%d  servo2=%d\n", open ? "OPEN" : "CLOSED",
                  curAngle[0], curAngle[1]);
}

static void printConfig() {
    Serial.println("\n---- stored values ----");
    Serial.printf("servo1: OPEN=%d  CLOSED=%d\n", openAngle[0], closedAngle[0]);
    Serial.printf("servo2: OPEN=%d  CLOSED=%d\n", openAngle[1], closedAngle[1]);
    Serial.println("---- paste into Config.h ----");
    Serial.printf("#define SERVO1_OPEN_ANGLE     %d\n", openAngle[0]);
    Serial.printf("#define SERVO1_CLOSED_ANGLE   %d\n", closedAngle[0]);
    Serial.printf("#define SERVO2_OPEN_ANGLE     %d\n", openAngle[1]);
    Serial.printf("#define SERVO2_CLOSED_ANGLE   %d\n", closedAngle[1]);
    Serial.println("-----------------------------\n");
}

static void help() {
    Serial.println(
        "\nCommands: 1/2/3 select | <deg> | +/- (5) | ./, (1) | u <us> |\n"
        "          o set OPEN | c set CLOSED | go | gc | t cycle | r release | p print | ?\n");
}

static void runCycle() {
    Serial.println("Cycle x3: CLOSED -> OPEN (10 s) -> CLOSED");
    for (int i = 1; i <= 3; i++) {
        Serial.printf("[%d/3] CLOSED\n", i);
        moveStored(false);
        delay(3000);
        Serial.printf("[%d/3] OPEN for 10 s (pipette now)\n", i);
        moveStored(true);
        unsigned long t0 = millis();
        while (millis() - t0 < OPEN_MS) {
            if ((millis() - t0) % 2000 < 20) Serial.printf("  %lu s\n", (millis() - t0) / 1000);
            delay(20);
        }
    }
    moveStored(false);
    Serial.println("Cycle done (CLOSED).");
}

void setup() {
    Serial.begin(115200);
    delay(800);
    Wire.begin(SDA_PIN, SCL_PIN);

    Wire.beginTransmission(PCA_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("PCA9685 NOT FOUND at 0x40 - check SDA/SCL/VCC/GND");
        while (true) delay(1000);
    }
    uint8_t v = 0x10; pcaWrite(0x00, &v, 1);   // sleep
    v = 121;          pcaWrite(0xFE, &v, 1);   // 50 Hz
    v = 0x20;         pcaWrite(0x00, &v, 1);   // wake + auto-increment
    delay(5);
    v = 0xA0;         pcaWrite(0x00, &v, 1);   // restart

    Serial.println("PCA9685 OK. Servos start at 90 deg.");
    setAngle(0, 90);
    setAngle(1, 90);
    help();
}

void loop() {
    if (!Serial.available()) return;
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() == 0) return;

    if (s == "1" || s == "2" || s == "3") {
        sel = s.toInt();
        Serial.printf("selected: %s\n", sel == 3 ? "BOTH" : (sel == 1 ? "servo 1" : "servo 2"));
    } else if (s == "+") nudge(5);
    else if (s == "-") nudge(-5);
    else if (s == ".") nudge(1);
    else if (s == ",") nudge(-1);
    else if (s[0] == 'u' || s[0] == 'U') {
        int us = constrain(s.substring(1).toInt(), 400, 2600);
        for (int ch = 0; ch < 2; ch++) if (isSel(ch)) setPulse(ch, us);
        Serial.printf("raw pulse %d us\n", us);
    } else if (s == "o") {
        for (int ch = 0; ch < 2; ch++) if (isSel(ch)) openAngle[ch] = curAngle[ch];
        Serial.printf("OPEN saved: servo1=%d servo2=%d\n", openAngle[0], openAngle[1]);
    } else if (s == "c") {
        for (int ch = 0; ch < 2; ch++) if (isSel(ch)) closedAngle[ch] = curAngle[ch];
        Serial.printf("CLOSED saved: servo1=%d servo2=%d\n", closedAngle[0], closedAngle[1]);
    } else if (s == "go") moveStored(true);
    else if (s == "gc") moveStored(false);
    else if (s == "t") runCycle();
    else if (s == "r") {
        for (int ch = 0; ch < 2; ch++) if (isSel(ch)) release(ch);
        Serial.println("released (limp)");
    } else if (s == "p") printConfig();
    else if (s == "?") help();
    else if (isDigit(s[0])) moveSel(s.toInt());
    else Serial.println("unknown command, type ?");
}
