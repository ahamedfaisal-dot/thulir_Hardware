/*
 * ============================================================
 *  TULIR — Safety Manager
 *  SafetyManager.h
 * ============================================================
 *  Continuous safety monitoring:
 *    - Cold-side temperature sensor validation
 *    - Hot-side over-temperature protection (when enabled)
 *    - Fault detection and handling
 *    - Emergency stop logic
 *    - Power-on safety checks
 *
 *  IMPORTANT:
 *  This is a SOFTWARE-only safety layer. It cannot protect
 *  against hardware failures, wiring faults, or power surges.
 *  Always use hardware protection (fuses, thermal cutoffs)
 *  as the primary safety mechanism.
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include "Config.h"

// Forward declarations to avoid circular includes
class TemperatureManager;
class PeltierControl;
class AudioManager;

class SafetyManager {
public:
    SafetyManager();

    // Initialize safety system
    void begin();

    // Periodic safety check — call every loop iteration
    // Returns true if system is safe, false if fault detected
    bool update(const TemperatureManager& tempMgr,
                PeltierControl& peltier,
                AudioManager& audio,
                SystemStatus& status);

    // Pre-start safety check (run before starting a recipe)
    bool preStartCheck(const TemperatureManager& tempMgr,
                       SystemStatus& status);

    // Trigger emergency stop
    void emergencyStop(PeltierControl& peltier,
                       AudioManager& audio,
                       SystemStatus& status);

    // Trigger a fault from outside the periodic update() check — e.g. the
    // Step 5 ramp exceeding its maximum allowed duration. Same effect as
    // an internally-detected fault: Peltiers off, state -> FAULT.
    void triggerRampTimeoutFault(PeltierControl& peltier,
                                  AudioManager& audio,
                                  SystemStatus& status);

    // Acknowledge and clear a fault (user action required)
    bool acknowledgeFault(SystemStatus& status);

    // Get current error code
    ErrorCode getErrorCode() const;

    // Has the fault been acknowledged?
    bool isFaultAcknowledged() const;

private:
    ErrorCode     _errorCode;
    bool          _faultAcknowledged;
    unsigned long _lastCheckTime;
    unsigned long _faultTime;

    void triggerFault(ErrorCode code,
                      PeltierControl& peltier,
                      AudioManager& audio,
                      SystemStatus& status);
};
