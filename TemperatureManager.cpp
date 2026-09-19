/*
 * ============================================================
 *  TULIR — Temperature Manager Implementation
 *  TemperatureManager.cpp
 * ============================================================
 */

#include "TemperatureManager.h"

TemperatureManager::TemperatureManager()
    : _sht()
    , _lastReadTime(0)
    , _hotOneWire(nullptr)
    , _hotSensors(nullptr)
    , _hotReadState(HOT_READ_IDLE)
    , _hotLastRequestTime(0)
    , _filterIndex(0)
    , _filterCount(0)
    , _filteredTemp(25.0f)
    , _rawTemp(25.0f)
    , _calOffset(TEMP_CALIBRATION_OFFSET)
    , _sensorValid(false)
    , _sensorCount(0)
    , _consecutiveErrors(0)
    , _humidity(50.0f)
    , _humidityValid(false)
    , _hotSideTemp(0.0f)
    , _hotSideValid(false)
{
    for (int i = 0; i < TEMP_FILTER_SAMPLES; i++) {
        _filterBuffer[i] = 25.0f;
    }
}

bool TemperatureManager::begin() {
    Wire.begin(SHT3X_SDA_PIN, SHT3X_SCL_PIN);

    if (!_sht.begin(SHT3X_I2C_ADDR)) {
        Serial.println("[TEMP] ERROR: SHT3x not found on I2C bus!");
        _sensorCount = 0;
        _sensorValid = false;
        return false;
    }
    _sensorCount = 1;

    // One initial read for a sane starting value
    float initialTemp, initialHum;
    if (_sht.readBoth(&initialTemp, &initialHum) &&
        validateReading(initialTemp) && validateHumidity(initialHum)) {
        _rawTemp = initialTemp + _calOffset;
        _filteredTemp = _rawTemp;
        _sensorValid = true;
        _humidity = initialHum;
        _humidityValid = true;
        for (int i = 0; i < TEMP_FILTER_SAMPLES; i++) {
            _filterBuffer[i] = _rawTemp;
        }
        _filterCount = TEMP_FILTER_SAMPLES;
        Serial.printf("[TEMP] SHT3x initialized. Initial: %.2f °C, %.1f%% RH (offset: %.2f)\n",
                      _rawTemp, _humidity, _calOffset);
    } else {
        Serial.println("[TEMP] WARNING: SHT3x initial reading invalid");
        _sensorValid = false;
    }

    // Optional hot-side sensor (unchanged 1-Wire DS18B20)
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

    _lastReadTime = millis();

    return _sensorValid;
}

void TemperatureManager::update() {
    unsigned long now = millis();

    // --- Cold-side SHT3x: fixed-interval read (fast I2C transaction) ---
    if ((now - _lastReadTime) >= SHT3X_READ_INTERVAL_MS) {
        _lastReadTime = now;

        float reading, humReading;
        bool ok = _sht.readBoth(&reading, &humReading);

        // Reject an implausible jump vs. the last accepted reading
        // (e.g. an I2C glitch) — but only once we have a prior reading.
        bool jumpRejected = false;
        if (ok && validateReading(reading) && _filterCount > 0) {
            float calibratedCheck = reading + _calOffset;
            if (fabsf(calibratedCheck - _rawTemp) > TEMP_INVALID_THRESH) {
                jumpRejected = true;
                Serial.printf("[TEMP] Rejected implausible jump: %.2f -> %.2f "
                              "(delta %.2f > %.2f max/sample)\n",
                              _rawTemp, calibratedCheck,
                              fabsf(calibratedCheck - _rawTemp), TEMP_INVALID_THRESH);
            }
        }

        if (ok && validateReading(reading) && !jumpRejected) {
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
            Serial.printf("[TEMP] Invalid/failed SHT3x reading (errors: %d)\n",
                          _consecutiveErrors);

            // After 5 consecutive errors, declare sensor fault
            if (_consecutiveErrors >= 5) {
                _sensorValid = false;
                Serial.println("[TEMP] SENSOR FAULT — too many consecutive errors");
            }
            // Keep using the last valid filtered value
        }

        // Humidity is display-only — validated but not jump-filtered or
        // gated on the temperature path above.
        if (ok && validateHumidity(humReading)) {
            _humidity = humReading;
            _humidityValid = true;
        } else {
            _humidityValid = false;
        }
    }

    // --- Optional hot-side DS18B20 (unchanged async 1-Wire logic) ---
    #if HOT_SIDE_SENSOR_ENABLED
        if (_hotSensors) {
            switch (_hotReadState) {
                case HOT_READ_IDLE:
                    _hotSensors->requestTemperatures();
                    _hotLastRequestTime = now;
                    _hotReadState = HOT_READ_REQUESTED;
                    break;

                case HOT_READ_REQUESTED:
                    if ((now - _hotLastRequestTime) >= TEMP_CONVERSION_MS) {
                        _hotReadState = HOT_READ_READY;
                    }
                    break;

                case HOT_READ_READY: {
                    float hotReading = _hotSensors->getTempCByIndex(0);
                    if (validateReading(hotReading)) {
                        _hotSideTemp = hotReading;
                        _hotSideValid = true;
                    } else {
                        static uint8_t hotErrors = 0;
                        hotErrors++;
                        if (hotErrors >= 3) {
                            _hotSideValid = false;
                        }
                    }
                    _hotReadState = HOT_READ_IDLE;
                    break;
                }
            }
        }
    #endif
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

float TemperatureManager::getHumidity() const {
    return _humidity;
}

bool TemperatureManager::isHumidityValid() const {
    return _humidityValid;
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
    if (isnan(temp)) return false;            // Failed I2C/1-Wire transaction

    // DS18B20 disconnect/power-on-reset codes (only ever produced by the
    // optional hot-side sensor now, but harmless to also check here)
    if (temp <= TEMP_ERROR_VALUE + 1.0f) return false;   // −127°C
    if (temp == 85.0f) return false;                      // Power-on reset value

    // Range check
    if (temp < TEMP_SENSOR_MIN || temp > TEMP_SENSOR_MAX) return false;

    return true;
}

bool TemperatureManager::validateHumidity(float rh) const {
    if (isnan(rh)) return false;
    if (rh < HUMIDITY_SENSOR_MIN || rh > HUMIDITY_SENSOR_MAX) return false;
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
