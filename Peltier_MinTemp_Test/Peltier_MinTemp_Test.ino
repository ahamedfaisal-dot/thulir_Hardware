/*
 * ============================================================
 *  Peltier_MinTemp_Test.ino  —  "Can it reach -20 °C?"
 * ============================================================
 *  Standalone test. NO PID, NO recipe, NO WiFi. It drives the three
 *  Peltier stages at a fixed power (same ratios as the main firmware)
 *  and logs the cold-side DS18B20 temperature so you can see how low
 *  the hardware really goes and how fast.
 *
 *  Pins (same as Config.h, with Bottom/Middle swapped):
 *    Bottom RPWM  GPIO8     Middle RPWM  GPIO4     Top RPWM  GPIO42
 *    DS18B20 DATA GPIO5  (4.7k pull-up to 3.3V)
 *
 *  Serial monitor: 115200, line ending "Newline".
 *  Commands (type then Enter):
 *    0..100   set master power in %   (e.g. 100 = full power)
 *    s        stop (0 %)
 *  Power ramps smoothly (10 %/s) so the supplies are not shocked.
 *
 *  Power: Bottom BTS7960 on the 12 V supply, Middle on a 6.1 V buck,
 *  Top on a 2.45 V buck (set the bucks BEFORE connecting the modules).
 *  At 100 % master each stage is fully ON, so the Peltier voltages are
 *  just the rail voltages (measure them with a meter!).
 *
 *  SAFETY: output is cut to 0 % if the sensor is lost, or if the
 *  temperature goes below -25 °C. Watch the hot side / heatsink
 *  yourself — this sketch has no hot-side sensor.
 * ============================================================
 */
#include <OneWire.h>
#include <DallasTemperature.h>

// ---- Pins ----
#define BOTTOM_RPWM  8
#define MIDDLE_RPWM  4
#define TOP_RPWM     42
#define ONE_WIRE_PIN 5

// ---- Power ratios (fraction of master %) — match Config.h ----
// Each BTS7960 is now powered from its OWN rail (Bottom 12 V supply,
// Middle buck set to 6.1 V, Top buck set to 2.45 V), so every stage runs
// at 100 % duty on its rail at full power -> all ratios 1.0.
const float RATIO_BOTTOM = 1.0f;
const float RATIO_MIDDLE = 1.0f;
const float RATIO_TOP    = 1.0f;

// ---- Stage caps (%) — no extra derating needed, the rail sets the voltage ----
const float MAX_BOTTOM = 100.0f;
const float MAX_MIDDLE = 100.0f;
const float MAX_TOP    = 100.0f;

// ---- PWM ----
#define PWM_FREQ  5000
#define PWM_BITS  10
#define PWM_MAX   ((1 << PWM_BITS) - 1)

// ---- Safety ----
const float CUTOFF_LOW_C = -25.0f;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

float targetMaster  = 0.0f;   // requested master %
float currentMaster = 0.0f;   // ramped master %
float tempC         = NAN;
float minTemp       = 1000.0f;
float startTemp     = NAN;
unsigned long startMs = 0;
bool  running = false;

static void writeStage(uint8_t ch, float pct, float cap) {
    if (pct < 0) pct = 0;
    if (pct > cap) pct = cap;
    ledcWrite(ch, (uint32_t)((pct / 100.0f) * PWM_MAX));
}

static void applyMaster(float m) {
    writeStage(0, m * RATIO_BOTTOM, MAX_BOTTOM);
    writeStage(1, m * RATIO_MIDDLE, MAX_MIDDLE);
    writeStage(2, m * RATIO_TOP,    MAX_TOP);
}

static void allOff() {
    targetMaster = currentMaster = 0;
    applyMaster(0);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    ledcSetup(0, PWM_FREQ, PWM_BITS); ledcAttachPin(BOTTOM_RPWM, 0);
    ledcSetup(1, PWM_FREQ, PWM_BITS); ledcAttachPin(MIDDLE_RPWM, 1);
    ledcSetup(2, PWM_FREQ, PWM_BITS); ledcAttachPin(TOP_RPWM,    2);
    allOff();

    sensors.begin();
    sensors.setResolution(11);
    sensors.setWaitForConversion(true);   // simple + safe for a test
    Serial.printf("\nPeltier min-temp test. DS18B20 devices: %d\n", sensors.getDeviceCount());
    Serial.println("Type 100 + Enter for full power, 's' to stop.");
}

void loop() {
    // ---- Serial commands ----
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.equalsIgnoreCase("s")) {
            allOff();
            running = false;
            Serial.println(">> STOPPED (0 %)");
        } else if (line.length() > 0) {
            float v = line.toFloat();
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            targetMaster = v;
            if (v > 0 && !running) {
                running = true;
                startMs = millis();
                startTemp = tempC;
                minTemp = 1000.0f;
            }
            Serial.printf(">> Master target = %.0f %%\n", targetMaster);
        }
    }

    // ---- Sensor (every ~1 s; conversion blocks ~375 ms, fine for a test) ----
    static unsigned long lastRead = 0;
    if (millis() - lastRead >= 1000) {
        lastRead = millis();
        sensors.requestTemperatures();
        float t = sensors.getTempCByIndex(0);

        if (t == DEVICE_DISCONNECTED_C || t <= -126.0f || t == 85.0f) {
            Serial.println("!! SENSOR LOST -> outputs OFF");
            allOff();
            running = false;
        } else {
            tempC = t;
            if (t < minTemp) minTemp = t;
            if (t <= CUTOFF_LOW_C) {
                Serial.printf("!! %.2f C below cutoff %.1f -> outputs OFF\n", t, CUTOFF_LOW_C);
                allOff();
                running = false;
            }
        }
    }

    // ---- Smooth ramp of master power (10 %/s) ----
    static unsigned long lastRamp = 0;
    unsigned long now = millis();
    if (now - lastRamp >= 100) {
        lastRamp = now;
        float step = 1.0f;                    // 1 % per 100 ms = 10 %/s
        if (currentMaster < targetMaster) currentMaster = min(currentMaster + step, targetMaster);
        else if (currentMaster > targetMaster) currentMaster = max(currentMaster - step, targetMaster);
        applyMaster(currentMaster);
    }

    // ---- Log every 2 s ----
    static unsigned long lastLog = 0;
    if (now - lastLog >= 2000) {
        lastLog = now;
        float mins = running ? (now - startMs) / 60000.0f : 0.0f;
        Serial.printf("t=%5.1f min | Temp=%7.2f C | Min=%7.2f C | master=%3.0f%% "
                      "BOT=%3.0f%% MID=%3.0f%% TOP=%3.0f%%\n",
                      mins, tempC, (minTemp < 999 ? minTemp : NAN), currentMaster,
                      min(currentMaster * RATIO_BOTTOM, MAX_BOTTOM),
                      min(currentMaster * RATIO_MIDDLE, MAX_MIDDLE),
                      min(currentMaster * RATIO_TOP,    MAX_TOP));
    }
}
