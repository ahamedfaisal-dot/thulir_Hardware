/*
 * ============================================================
 *  TULIR — Temperature Manager Implementation
 *  TemperatureManager.cpp
 * ============================================================
 */

#include "TemperatureManager.h"

TemperatureManager::TemperatureManager()
    : _oneWire(DS18B20_PIN)
    , _sensors(&_oneWire)
    , _hotOneWire(nullptr)
    , _hotSensors(nullptr)
    , _readState(READ_IDLE)
    , _lastRequestTime(0)
    , _filterIndex(0)
    , _filterCount(0)
    , _filteredTemp(25.0f)
    , _rawTemp(25.0f)
    , _calOffset(TEMP_CALIBRATION_OFFSET)
    , _sensorValid(false)
    , _sensorCount(0)
    , _consecutiveErrors(0)
    , _hotSideTemp(0.0f)
    , _hotSideValid(false)
{
    for (int i = 0; i < TEMP_FILTER_SAMPLES; i++) {
        _filterBuffer[i] = 25.0f;
    }
}

bool TemperatureManager::begin() {
    delay(100);  // Allow 1-Wire bus pull-up to stabilize on power-up
    _sensors.begin();
    _sensorCount = _sensors.getDeviceCount();

    if (_sensorCount == 0) {
        // Retry once after 150ms stabilization
        delay(150);
        _sensors.begin();
        _sensorCount = _sensors.getDeviceCount();
    }

    if (_sensorCount == 0) {
        Serial.println("[TEMP] ERROR: No DS18B20 sensor found on cold-side bus!");
        _sensorValid = false;
        return false;
    }

    // Set to 12-bit resolution (0.0625°C steps, ~750ms conversion)
    _sensors.setResolution(12);

    // Use asynchronous mode — requestTemperatures() returns immediately
    _sensors.setWaitForConversion(false);

    Serial.printf("[TEMP] Cold-side DS18B20 initialized. Sensors found: %d\n", _sensorCount);

    // Do one synchronous read for initial value
    _sensors.setWaitForConversion(true);
    _sensors.requestTemperatures();
    float initial = _sensors.getTempCByIndex(0);
    _sensors.setWaitForConversion(false);

    if (validateReading(initial)) {
        _rawTemp = initial + _calOffset;
        _filteredTemp = _rawTemp;
        _sensorValid = true;
        // Pre-fill filter buffer
        for (int i = 0; i < TEMP_FILTER_SAMPLES; i++) {
            _filterBuffer[i] = _rawTemp;
        }
        _filterCount = TEMP_FILTER_SAMPLES;
        Serial.printf("[TEMP] Initial reading: %.2f °C (offset: %.2f)\n",
                      _rawTemp, _calOffset);
    } else {
        Serial.printf("[TEMP] WARNING: Initial reading invalid (%.2f)\n", initial);
        _sensorValid = false;
    }

    // Optional hot-side sensor
    #if HOT_SIDE_SENSOR_ENABLED
        _hotOneWire = new OneWire(DS18B20_HOT_PIN);
        _hotSensors = new DallasTemperature(_hotOneWire);
        _hotSensors->begin();
        uint8_t hotCount = _hotSensors->getDeviceCount();
        if (hotCount > 0) {
            _hotSensors->setResolution(12);
            _hotSensors->setWaitForConversion(false);
            Serial.printf("[TEMP] Hot-side DS18B20 initialized. Sensors: %d\n", hotCount);
        } else {
            Serial.println("[TEMP] WARNING: Hot-side sensor enabled but not found!");
        }
    #else
        // HOT-SIDE SENSOR NOT INSTALLED
        // It is STRONGLY RECOMMENDED to add a hot-side temperature
        // sensor for a high-power cascaded Peltier system.
        // The hot side of the bottom Peltier can exceed 80°C
        // under sustained high-power operation. Without monitoring,
        // thermal runaway is possible.
        Serial.println("[TEMP] Hot-side sensor: NOT INSTALLED (see Config.h)");
    #endif

    // Kick off the first async conversion
    _sensors.requestTemperatures();
    _lastRequestTime = millis();
    _readState = READ_REQUESTED;

    return _sensorValid;
}

void TemperatureManager::update() {
    unsigned long now = millis();

    switch (_readState) {
        case READ_IDLE:
            // Start a new conversion request
            _sensors.requestTemperatures();
            _lastRequestTime = now;
            _readState = READ_REQUESTED;

            #if HOT_SIDE_SENSOR_ENABLED
                if (_hotSensors) _hotSensors->requestTemperatures();
            #endif
            break;

        case READ_REQUESTED:
            // Wait for conversion to complete (~750ms for 12-bit)
            if ((now - _lastRequestTime) >= TEMP_CONVERSION_MS) {
                _readState = READ_READY;
            }
            break;

        case READ_READY: {
            // Read the result
            float reading = _sensors.getTempCByIndex(0);

            // Reject an implausible jump vs. the last accepted reading
            // (e.g. electrical glitch on the 1-Wire bus) — but only once
            // we actually have a prior reading to compare against.
            bool jumpRejected = false;
            if (validateReading(reading) && _filterCount > 0) {
                float calibratedCheck = reading + _calOffset;
                if (fabsf(calibratedCheck - _rawTemp) > TEMP_INVALID_THRESH) {
                    jumpRejected = true;
                    Serial.printf("[TEMP] Rejected implausible jump: %.2f -> %.2f "
                                  "(delta %.2f > %.2f max/sample)\n",
                                  _rawTemp, calibratedCheck,
                                  fabsf(calibratedCheck - _rawTemp), TEMP_INVALID_THRESH);
                }
            }

            if (validateReading(reading) && !jumpRejected) {
                // Apply calibration offset
                float calibrated = reading + _calOffset;
                _rawTemp = calibrated;

                // Add to moving-average filter
                _filterBuffer[_filterIndex] = calibrated;
                _filterIndex = (_filterIndex + 1) % TEMP_FILTER_SAMPLES;
                if (_filterCount < TEMP_FILTER_SAMPLES) _filterCount++;

                _filteredTemp = computeFilteredTemp();
                _sensorValid = true;
                _consecutiveErrors = 0;
            } else {
                _consecutiveErrors++;
                Serial.printf("[TEMP] Invalid reading: %.2f (errors: %d)\n",
                              reading, _consecutiveErrors);

                // After 5 consecutive errors, declare sensor fault
                if (_consecutiveErrors >= 5) {
                    _sensorValid = false;
                    Serial.println("[TEMP] SENSOR FAULT — too many consecutive errors");
                }
                // Keep using the last valid filtered value
            }

            // Read hot-side sensor if enabled
            #if HOT_SIDE_SENSOR_ENABLED
                if (_hotSensors) {
                    float hotReading = _hotSensors->getTempCByIndex(0);
                    if (validateReading(hotReading)) {
                        _hotSideTemp = hotReading;
                        _hotSideValid = true;
                    } else {
                        // Don't immediately invalidate — could be a transient
                        static uint8_t hotErrors = 0;
                        hotErrors++;
                        if (hotErrors >= 3) {
                            _hotSideValid = false;
                        }
                    }
                }
            #endif

            // Go back to idle to start next cycle
            _readState = READ_IDLE;
            break;
        }
    }
}

float TemperatureManager::getRawTemp() const {
    return _rawTemp;
}

float TemperatureManager::getFilteredTemp() const {
    return _filteredTemp;
}

bool TemperatureManager::isSensorValid() const {
    return _sensorValid;
}

float TemperatureManager::getHotSideTemp() const {
    return _hotSideTemp;
}

bool TemperatureManager::isHotSideValid() const {
    return _hotSideValid;
}

void TemperatureManager::setCalibrationOffset(float offset) {
    if (fabsf(offset) > TEMP_CAL_OFFSET_MAX) {
        Serial.printf("[TEMP] WARNING: Calibration offset %.2f exceeds recommended "
                      "maximum of ±%.1f °C\n", offset, TEMP_CAL_OFFSET_MAX);
    }
    _calOffset = offset;
}

float TemperatureManager::getCalibrationOffset() const {
    return _calOffset;
}

uint8_t TemperatureManager::getSensorCount() const {
    return _sensorCount;
}

// --- Private helpers ---

bool TemperatureManager::validateReading(float temp) const {
    // Detect DS18B20 disconnect (returns −127.0 or +85.0 on error)
    if (temp <= TEMP_ERROR_VALUE + 1.0f) return false;   // −127°C
    if (temp == 85.0f) return false;                      // Power-on reset value

    // Range check
    if (temp < TEMP_SENSOR_MIN || temp > TEMP_SENSOR_MAX) return false;

    return true;
}

float TemperatureManager::computeFilteredTemp() {
    if (_filterCount == 0) return _rawTemp;

    float sum = 0.0f;
    for (uint8_t i = 0; i < _filterCount; i++) {
        sum += _filterBuffer[i];
    }
    return sum / (float)_filterCount;
}
