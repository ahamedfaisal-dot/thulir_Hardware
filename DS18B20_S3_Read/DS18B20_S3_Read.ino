/*
 * DS18B20 temperature reading on ESP32-S3, DATA on GPIO5.
 * Verified hardware: raw 1-Wire test printed SENSOR PRESENT on this pin.
 *
 * Wiring: Red -> 3.3V, Black -> GND, Yellow (DATA) -> GPIO5,
 *         4.7k resistor between DATA and 3.3V.
 * Libraries: OneWire (Paul Stoffregen), DallasTemperature (Miles Burton).
 * Serial monitor: 115200.
 */
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_PIN 5

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nDS18B20 reading on ESP32-S3, GPIO5");

    sensors.begin();
    Serial.printf("Devices found: %d\n", sensors.getDeviceCount());
}

void loop() {
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);

    if (t == DEVICE_DISCONNECTED_C) {
        Serial.println("Sensor not detected - check wiring / 4.7k pull-up");
    } else {
        Serial.printf("Temperature: %.2f C  (%.2f F)\n", t, t * 9.0f / 5.0f + 32.0f);
    }
    delay(1000);
}
