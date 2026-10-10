/*
 * DS18B20 temperature reading on an ESP32 dev board, DATA on GPIO5 (D5).
 *
 * Wiring: Red -> 3.3V, Black -> GND, Yellow (DATA) -> GPIO5 (D5),
 *         4.7k resistor between DATA and 3.3V.
 * Libraries: OneWire (Paul Stoffregen), DallasTemperature (Miles Burton).
 * Serial monitor: 115200.
 */
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_PIN 5   // GPIO5 = D5

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

void setup() {
    Serial.begin(115200);
    delay(500);
    sensors.begin();
    Serial.printf("DS18B20 devices found: %d\n", sensors.getDeviceCount());
}

void loop() {
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);

    if (t == DEVICE_DISCONNECTED_C) {
        Serial.println("Sensor not detected - check wiring / 4.7k pull-up");
    } else {
        Serial.printf("Temperature: %.2f C\n", t);
    }
    delay(1000);
}
