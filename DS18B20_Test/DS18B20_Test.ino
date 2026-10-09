/*
 * ============================================================
 *  DS18B20_Test.ino
 * ============================================================
 *  Standalone sketch to verify a waterproof DS18B20 sensor
 *  (e.g. the optional hot-side sensor for THULIR, DS18B20_HOT_PIN)
 *  BEFORE wiring it into the main thulir_final firmware.
 *
 *  Wiring (DS18B20, 3-wire waterproof probe):
 *    Red    -> 3.3V
 *    Black  -> GND
 *    Yellow -> DATA -> DS18B20_PIN below, with a 4.7kOhm pull-up
 *              resistor from DATA to 3.3V
 *
 *  Default pin is GPIO37 -- moved here from GPIO43 (THULIR's original
 *  DS18B20_HOT_PIN) after GPIO43 hung the chip when touched by OneWire
 *  on this board (GPIO43/44 are the ESP32-S3's default UART0 pins,
 *  which the boot ROM also drives). GPIO37 is genuinely free: not a
 *  strapping pin, not used anywhere else in this project, and
 *  confirmed present on this board's actual header.
 *  Change DS18B20_PIN below if testing on a different GPIO/board.
 *
 *  Libraries required (same ones the main firmware uses):
 *    - OneWire        by Paul Stoffregen
 *    - DallasTemperature by Miles Burton
 *
 *  What this sketch checks:
 *    - Device detection on the 1-Wire bus (and how many found)
 *    - Each device's 64-bit ROM address (useful if you have
 *      multiple DS18B20s on one bus and need to tell them apart)
 *    - Live temperature readings every second
 *    - Disconnect / invalid-reading detection (-127C, +85C)
 * ============================================================
 */

#include <OneWire.h>
#include <DallasTemperature.h>

#define DS18B20_PIN   02  // Change to test a different GPIO
#define READ_INTERVAL_MS 1000

// Deliberately NOT global objects. OneWire's constructor touches the
// GPIO immediately, and global objects construct before setup() even
// runs (before Serial.begin(), before any print). If touching this
// specific pin that early hangs the chip, a global object would hang
// silently before anything could ever be printed -- constructing these
// AFTER a few confirmed prints in setup() isolates that possibility.
OneWire* oneWirePtr = nullptr;
DallasTemperature* sensorsPtr = nullptr;

uint8_t deviceCount = 0;

void printAddress(DeviceAddress addr) {
    for (uint8_t i = 0; i < 8; i++) {
        if (addr[i] < 16) Serial.print("0");
        Serial.print(addr[i], HEX);
        if (i < 7) Serial.print(":");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("============================================");
    Serial.println("  DS18B20 Waterproof Sensor Test");
    Serial.printf("  Data pin: GPIO%d\n", DS18B20_PIN);
    Serial.println("============================================");
    Serial.println("[DS18B20] Reached this point BEFORE touching the OneWire pin.");
    Serial.println("[DS18B20] Now constructing OneWire/DallasTemperature objects...");
    delay(200);

    oneWirePtr = new OneWire(DS18B20_PIN);
    sensorsPtr = new DallasTemperature(oneWirePtr);

    Serial.println("[DS18B20] Objects constructed OK -- pin touch did not hang.");

    sensorsPtr->begin();
    deviceCount = sensorsPtr->getDeviceCount();

    Serial.printf("[DS18B20] Devices found on bus: %d\n", deviceCount);

    if (deviceCount == 0) {
        Serial.println("[DS18B20] ERROR: No sensor detected!");
        Serial.println("[DS18B20] Check: DATA wire, 4.7k pull-up to 3.3V,");
        Serial.println("[DS18B20]        power (3.3V/GND), and GPIO number above.");
    } else {
        sensorsPtr->setResolution(12);   // 12-bit, 0.0625C steps, ~750ms conversion
        Serial.println("[DS18B20] Resolution set to 12-bit.");

        for (uint8_t i = 0; i < deviceCount; i++) {
            DeviceAddress addr;
            if (sensorsPtr->getAddress(addr, i)) {
                Serial.printf("[DS18B20] Device %d address: ", i);
                printAddress(addr);
                Serial.println();
            }
        }
    }

    Serial.println();
}

void loop() {
    static unsigned long lastRead = 0;
    unsigned long now = millis();

    if (now - lastRead < READ_INTERVAL_MS) return;
    lastRead = now;

    if (deviceCount == 0) {
        // Re-check periodically in case the sensor gets connected
        // while this sketch is already running.
        sensorsPtr->begin();
        deviceCount = sensorsPtr->getDeviceCount();
        if (deviceCount > 0) {
            Serial.println("[DS18B20] Sensor detected! Restart sketch to see its address.");
            sensorsPtr->setResolution(12);
        } else {
            Serial.println("[DS18B20] Still no sensor found...");
        }
        return;
    }

    sensorsPtr->requestTemperatures();

    for (uint8_t i = 0; i < deviceCount; i++) {
        float tempC = sensorsPtr->getTempCByIndex(i);

        Serial.printf("[DS18B20] Device %d: ", i);

        if (tempC == DEVICE_DISCONNECTED_C) {
            Serial.println("DISCONNECTED (no response)");
        } else if (tempC <= -126.0f) {
            Serial.println("ERROR -127C (bus/CRC error or bad connection)");
        } else if (tempC == 85.0f) {
            Serial.println("WARNING 85C (power-on reset value -- sensor may not be converting properly)");
        } else {
            Serial.printf("%.2f C  (%.2f F)\n", tempC, tempC * 9.0f / 5.0f + 32.0f);
        }
    }
}
