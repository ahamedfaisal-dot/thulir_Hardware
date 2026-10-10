/*
 * Library-free 1-Wire presence test (no OneWire / DallasTemperature).
 * Separates "hardware/pin problem" from "library problem".
 *
 * Wiring: DATA -> PIN below + 4.7k to 3.3V, VCC 3.3V, GND.
 * Serial monitor: 115200.
 */
#define PIN 5

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\nRaw 1-Wire test on GPIO%d\n", PIN);
}

void loop() {
    pinMode(PIN, INPUT);                 // release line, pull-up should hold it high
    delay(2);
    int idle = digitalRead(PIN);

    noInterrupts();
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN, LOW);              // reset pulse
    delayMicroseconds(500);
    pinMode(PIN, INPUT);                 // release
    delayMicroseconds(70);
    int presence = digitalRead(PIN);     // sensor pulls LOW if present
    interrupts();
    delayMicroseconds(500);
    int after = digitalRead(PIN);

    Serial.printf("idle=%d  presence(sample)=%d  after=%d  -> %s\n",
                  idle, presence, after,
                  (idle == 1 && presence == 0) ? "SENSOR PRESENT"
                  : (idle == 0)                ? "line stuck LOW (short / no pull-up)"
                                               : "no response (sensor/wiring)");
    delay(1000);
}
