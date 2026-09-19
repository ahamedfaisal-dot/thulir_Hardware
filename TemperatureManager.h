/*
 * ============================================================
 *  TULIR — Temperature Manager
 *  TemperatureManager.h
 * ============================================================
 *  Manages the cold-side SHT3x (I2C temperature + humidity) sensor:
 *    - Fixed-interval read (I2C transaction is fast, ~15ms)
 *    - Moving-average filter on temperature
 *    - Sensor validation, jump rejection, and fault detection
 *    - Calibration offset
 *  Plus an optional hot-side DS18B20 (1-Wire), unchanged from before,
 *  gated by HOT_SIDE_SENSOR_ENABLED in Config.h.
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Config.h"

class TemperatureManager {
public:
    TemperatureManager();

    // Initialize sensor(s)
    bool begin();

    // Non-blocking-at-scale update — call every loop iteration.
    // Internally self-paces to SHT3X_READ_INTERVAL_MS; each actual I2C
    // read is a short (~15ms) transaction, not a long blocking wait.
    void update();

    // Get latest raw reading (°C, with calibration offset applied)
    float getRawTemp() const;

    // Get filtered temperature (°C, for PID use)
    float getFilteredTemp() const;

    // Is the cold-side sensor reading valid?
    bool isSensorValid() const;

    // Cold-side humidity (display-only — not used by PID/safety)
    float getHumidity() const;
    bool  isHumidityValid() const;

    // Hot-side sensor (optional, DS18B20, unchanged)
    float getHotSideTemp() const;
    bool  isHotSideValid() const;

    // Calibration
    void  setCalibrationOffset(float offset);
    float getCalibrationOffset() const;

    // Number of sensors found (1 if SHT3x responded at begin(), else 0)
    uint8_t getSensorCount() const;

private:
    Adafruit_SHT31 _sht;
    unsigned long  _lastReadTime;

    // Optional hot-side sensor (unchanged 1-Wire/DS18B20 async logic)
    OneWire*           _hotOneWire;
    DallasTemperature* _hotSensors;
    enum HotReadState { HOT_READ_IDLE, HOT_READ_REQUESTED, HOT_READ_READY };
    HotReadState  _hotReadState;
    unsigned long _hotLastRequestTime;

    // Filter (temperature only — humidity is display-only, unfiltered)
    float    _filterBuffer[TEMP_FILTER_SAMPLES];
    uint8_t  _filterIndex;
    uint8_t  _filterCount;  // Samples collected (up to TEMP_FILTER_SAMPLES)
    float    _filteredTemp;

    // Current readings
    float _rawTemp;
    float _calOffset;
    bool  _sensorValid;
    uint8_t _sensorCount;
    uint8_t _consecutiveErrors;

    float _humidity;
    bool  _humidityValid;

    // Hot-side
    float _hotSideTemp;
    bool  _hotSideValid;

    // Internal helpers
    bool  validateReading(float temp) const;
    bool  validateHumidity(float rh) const;
    float computeFilteredTemp();
};
