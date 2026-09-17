/*
 * ============================================================
 *  TULIR — Temperature Manager
 *  TemperatureManager.h
 * ============================================================
 *  Manages DS18B20 temperature sensor(s) with:
 *    - Asynchronous (non-blocking) conversion cycle
 *    - Moving-average filter
 *    - Sensor validation and fault detection
 *    - Calibration offset
 *    - Optional hot-side sensor support
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Config.h"

class TemperatureManager {
public:
    TemperatureManager();

    // Initialize sensor(s)
    bool begin();

    // Non-blocking update — call every loop iteration.
    // Manages the request→wait→read async cycle.
    void update();

    // Get latest raw reading (°C, with calibration offset applied)
    float getRawTemp() const;

    // Get filtered temperature (°C, for PID use)
    float getFilteredTemp() const;

    // Is the cold-side sensor reading valid?
    bool isSensorValid() const;

    // Hot-side sensor (optional)
    float getHotSideTemp() const;
    bool  isHotSideValid() const;

    // Calibration
    void  setCalibrationOffset(float offset);
    float getCalibrationOffset() const;

    // Number of sensors found on bus
    uint8_t getSensorCount() const;

private:
    OneWire           _oneWire;
    DallasTemperature _sensors;

    // Optional hot-side sensor
    OneWire*           _hotOneWire;
    DallasTemperature* _hotSensors;

    // Async read state machine
    enum ReadState {
        READ_IDLE,
        READ_REQUESTED,
        READ_READY
    };
    ReadState     _readState;
    unsigned long _lastRequestTime;

    // Filter
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

    // Hot-side
    float _hotSideTemp;
    bool  _hotSideValid;

    // Internal helpers
    bool  validateReading(float temp) const;
    float computeFilteredTemp();
};
