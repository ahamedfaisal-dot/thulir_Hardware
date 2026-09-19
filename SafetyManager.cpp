/*
 * ============================================================
 *  TULIR — Safety Manager Implementation
 *  SafetyManager.cpp
 * ============================================================
 */

#include "SafetyManager.h"
#include "TemperatureManager.h"
#include "PeltierControl.h"
#include "AudioManager.h"

SafetyManager::SafetyManager()
    : _errorCode(ERROR_NONE)
    , _faultAcknowledged(false)
    , _lastCheckTime(0)
    , _faultTime(0)
{
}

void SafetyManager::begin() {
    _errorCode = ERROR_NONE;
    _faultAcknowledged = false;
    Serial.println("[SAFETY] Safety manager initialized");

    /*
     * HARDWARE SAFETY REMINDER:
     *
     * The ESP32 cannot detect overcurrent without external current
     * sensors. This software safety layer provides:
     *   - Temperature sensor fault detection
     *   - Software over-temperature shutdown
     *   - PWM output limit enforcement
     *
     * You MUST also implement hardware protection:
     *   - 15A fuse on bottom Peltier branch
     *   - 10A fuse on middle Peltier branch
     *   - 10A fuse on top Peltier branch
     *   - Adequate wire gauge (≥14 AWG for bottom, ≥16 AWG for mid/top)
     *   - Physical emergency power cutoff switch
     *   - Proper thermal interface on Peltier stack
     *   - Adequate heatsink and ventilation
     */
}

bool SafetyManager::update(const TemperatureManager& tempMgr,
                            PeltierControl& peltier,
                            AudioManager& audio,
                            SystemStatus& status) {
    unsigned long now = millis();

    // Rate-limit safety checks to avoid excessive overhead
    if ((now - _lastCheckTime) < SAFETY_CHECK_INTERVAL) {
        return (_errorCode == ERROR_NONE);
    }
    _lastCheckTime = now;

    // --- Check 1: Cold-side temperature sensor ---
    if (!tempMgr.isSensorValid()) {
        // Sensor has failed — this is critical during operation
        if (status.state == STATE_STEP_APPROACH ||
            status.state == STATE_STEP_HOLD ||
            status.state == STATE_STEP5_RAMP ||
            status.state == STATE_STEP_TRANSITION ||
            status.state == STATE_TEST_MODE) {
            triggerFault(ERROR_TEMP_SENSOR, peltier, audio, status);
            return false;
        }
        // In IDLE/COMPLETE states, just flag it
        status.sensorValid = false;
    } else {
        status.sensorValid = true;
    }

    // --- Check 2: Temperature reading validity ---
    float temp = tempMgr.getFilteredTemp();
    if (temp < TEMP_SENSOR_MIN || temp > TEMP_SENSOR_MAX) {
        if (peltier.isActive()) {
            triggerFault(ERROR_TEMP_INVALID, peltier, audio, status);
            return false;
        }
    }

    // --- Check 3: Cold-side over-temperature ---
    // If the cold side somehow reads very high (e.g., heatsink failure),
    // shut down to prevent thermal damage to the Peltier stack.
    if (temp > COLDSIDE_SANITY_MAX_TEMP && peltier.isActive()) {
        triggerFault(ERROR_OVER_TEMP, peltier, audio, status);
        return false;
    }

    // --- Check 4: Hot-side temperature sensor (if enabled) ---
    #if HOT_SIDE_SENSOR_ENABLED
        if (tempMgr.isHotSideValid()) {
            float hotTemp = tempMgr.getHotSideTemp();
            status.hotSideTemp = hotTemp;
            status.hotSideSensorValid = true;

            if (hotTemp > HOT_SIDE_MAX_TEMP) {
                Serial.printf("[SAFETY] HOT SIDE OVER TEMP: %.1f°C > %.1f°C limit\n",
                              hotTemp, HOT_SIDE_MAX_TEMP);
                triggerFault(ERROR_HOT_SIDE_OVER_TEMP, peltier, audio, status);
                return false;
            }
        }
    #endif

    return true;
}

bool SafetyManager::preStartCheck(const TemperatureManager& tempMgr,
                                   SystemStatus& status) {
    Serial.println("[SAFETY] Running pre-start checks...");

    // Check sensor
    if (!tempMgr.isSensorValid()) {
        Serial.println("[SAFETY] FAIL: Temperature sensor not valid");
        status.errorCode = ERROR_TEMP_SENSOR;
        return false;
    }

    // Check temperature reading is reasonable
    float temp = tempMgr.getFilteredTemp();
    if (temp < TEMP_SENSOR_MIN || temp > TEMP_SENSOR_MAX) {
        Serial.printf("[SAFETY] FAIL: Temperature reading out of range: %.1f°C\n", temp);
        status.errorCode = ERROR_TEMP_INVALID;
        return false;
    }

    // Clear any previous faults
    _errorCode = ERROR_NONE;
    _faultAcknowledged = false;
    status.errorCode = ERROR_NONE;

    Serial.printf("[SAFETY] Pre-start checks PASSED. Current temp: %.1f°C\n", temp);
    return true;
}

void SafetyManager::emergencyStop(PeltierControl& peltier,
                                   AudioManager& audio,
                                   SystemStatus& status) {
    Serial.println("[SAFETY] !!! EMERGENCY STOP !!!");

    // Immediately kill all Peltier outputs
    peltier.allPeltiersOff();

    // Fans remain ON (they are hardwired to PSU).
    // If GPIO fan control were installed, you would keep fans running here.

    status.state = STATE_STOPPED;
    status.errorCode = ERROR_EMERGENCY_STOP;
    status.pidOutput = 0.0f;
    status.bottomPWM = 0.0f;
    status.middlePWM = 0.0f;
    status.topPWM = 0.0f;

    _errorCode = ERROR_EMERGENCY_STOP;
    _faultAcknowledged = false;
    _faultTime = millis();

    // Play emergency stop audio
    audio.announceEmergencyStop();
}

void SafetyManager::triggerRampTimeoutFault(PeltierControl& peltier,
                                             AudioManager& audio,
                                             SystemStatus& status) {
    triggerFault(ERROR_RAMP_TIMEOUT, peltier, audio, status);
}

bool SafetyManager::acknowledgeFault(SystemStatus& status) {
    if (_errorCode == ERROR_NONE) return true;

    #if HOT_SIDE_SENSOR_ENABLED
        // If hot-side over-temp, only allow acknowledgement after
        // temperature drops below recovery level
        if (_errorCode == ERROR_HOT_SIDE_OVER_TEMP) {
            if (status.hotSideTemp > HOT_SIDE_RECOVERY_TEMP) {
                Serial.printf("[SAFETY] Cannot acknowledge: hot side still %.1f°C "
                              "(need < %.1f°C)\n",
                              status.hotSideTemp, HOT_SIDE_RECOVERY_TEMP);
                return false;
            }
        }
    #endif

    Serial.println("[SAFETY] Fault acknowledged by user");
    _faultAcknowledged = true;
    _errorCode = ERROR_NONE;
    status.errorCode = ERROR_NONE;
    status.state = STATE_IDLE;
    return true;
}

ErrorCode SafetyManager::getErrorCode() const {
    return _errorCode;
}

bool SafetyManager::isFaultAcknowledged() const {
    return _faultAcknowledged;
}

// --- Private ---

void SafetyManager::triggerFault(ErrorCode code,
                                  PeltierControl& peltier,
                                  AudioManager& audio,
                                  SystemStatus& status) {
    Serial.printf("[SAFETY] FAULT TRIGGERED: %s\n", getErrorName(code));

    // ALWAYS turn off Peltiers on any fault
    peltier.allPeltiersOff();

    // Fans remain ON (hardwired to PSU).
    // With GPIO fan control, you would keep fans on here for heatsink cooling.

    _errorCode = code;
    _faultAcknowledged = false;
    _faultTime = millis();

    status.state = STATE_FAULT;
    status.errorCode = code;
    status.pidOutput = 0.0f;
    status.bottomPWM = 0.0f;
    status.middlePWM = 0.0f;
    status.topPWM = 0.0f;

    // Play appropriate audio
    switch (code) {
        case ERROR_TEMP_SENSOR:
        case ERROR_TEMP_INVALID:
            audio.announceSensorError();
            break;
        case ERROR_OVER_TEMP:
        case ERROR_HOT_SIDE_OVER_TEMP:
            audio.announceOverTemp();
            break;
        default:
            break;
    }
}
