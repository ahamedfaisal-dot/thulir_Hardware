/*
 * ============================================================
 *  TULIR — Recipe Manager Implementation
 *  RecipeManager.cpp
 * ============================================================
 */

#include "RecipeManager.h"

RecipeManager::RecipeManager() {
    initializeDefaults();
}

void RecipeManager::begin() {
    _prefs.begin(NVS_NAMESPACE, false);  // Read-write mode

    // Check if NVS has been initialized before
    bool initialized = _prefs.getBool(NVS_KEY_INITIALIZED, false);

    if (initialized) {
        loadFromNVS();
        Serial.println("[RECIPE] Loaded recipe from NVS");

        // Sanity-check the loaded recipe immediately rather than only at
        // startProcess() time — a corrupted NVS blob should be caught at
        // boot, not silently carried around until the user tries to start.
        if (!validateRecipe()) {
            Serial.println("[RECIPE] Loaded recipe FAILED validation — "
                            "resetting to factory defaults");
            initializeDefaults();
            saveToNVS();
        }
    } else {
        initializeDefaults();
        saveToNVS();
        Serial.println("[RECIPE] First boot — defaults saved to NVS");
    }

    _prefs.end();
}

const Recipe& RecipeManager::getRecipe() const {
    return _recipe;
}

Recipe& RecipeManager::getRecipeForEdit() {
    return _recipe;
}

bool RecipeManager::setStep2Temp(float temp) {
    // Valid options: 0, 15, 25
    if (temp == 0.0f || temp == 15.0f || temp == 25.0f) {
        _recipe.steps[1].targetTemp = temp;
        return true;
    }
    Serial.printf("[RECIPE] Invalid Step 2 temp: %.1f (must be 0/15/25)\n", temp);
    return false;
}

bool RecipeManager::setStep4Temp(float temp) {
    // Valid options: 0, 25
    if (temp == 0.0f || temp == 25.0f) {
        _recipe.steps[3].targetTemp = temp;
        return true;
    }
    Serial.printf("[RECIPE] Invalid Step 4 temp: %.1f (must be 0/25)\n", temp);
    return false;
}

bool RecipeManager::setStepHoldTime(uint8_t step, uint16_t minutes) {
    if (step < 1 || step > 4) return false;
    if (minutes < 1 || minutes > 999) {
        Serial.printf("[RECIPE] Invalid hold time: %d min (1–999)\n", minutes);
        return false;
    }
    _recipe.steps[step - 1].holdTimeMin = minutes;
    return true;
}

bool RecipeManager::validateRecipe() const {
    // Validate each step
    for (int i = 0; i < 4; i++) {
        if (_recipe.steps[i].holdTimeMin == 0) {
            Serial.printf("[RECIPE] Step %d has zero hold time\n", i + 1);
            return false;
        }
        if (_recipe.steps[i].targetTemp < -30.0f ||
            _recipe.steps[i].targetTemp > 50.0f) {
            Serial.printf("[RECIPE] Step %d has out-of-range target: %.1f\n",
                          i + 1, _recipe.steps[i].targetTemp);
            return false;
        }
    }

    // Validate Step 2 target
    float s2t = _recipe.steps[1].targetTemp;
    if (s2t != 0.0f && s2t != 15.0f && s2t != 25.0f) {
        Serial.println("[RECIPE] Step 2 target must be 0/15/25");
        return false;
    }

    // Validate Step 4 target
    float s4t = _recipe.steps[3].targetTemp;
    if (s4t != 0.0f && s4t != 25.0f) {
        Serial.println("[RECIPE] Step 4 target must be 0/25");
        return false;
    }

    // Validate ramp
    if (_recipe.rampRate >= 0.0f) {
        Serial.println("[RECIPE] Ramp rate must be negative (cooling)");
        return false;
    }

    return true;
}

void RecipeManager::saveToNVS() {
    _prefs.begin(NVS_NAMESPACE, false);

    _prefs.putUShort(NVS_KEY_S1_TIME, _recipe.steps[0].holdTimeMin);
    _prefs.putFloat(NVS_KEY_S2_TEMP,  _recipe.steps[1].targetTemp);
    _prefs.putUShort(NVS_KEY_S2_TIME, _recipe.steps[1].holdTimeMin);
    _prefs.putUShort(NVS_KEY_S3_TIME, _recipe.steps[2].holdTimeMin);
    _prefs.putFloat(NVS_KEY_S4_TEMP,  _recipe.steps[3].targetTemp);
    _prefs.putUShort(NVS_KEY_S4_TIME, _recipe.steps[3].holdTimeMin);

    _prefs.putBool(NVS_KEY_INITIALIZED, true);

    _prefs.end();
    Serial.println("[RECIPE] Saved to NVS");
}

void RecipeManager::loadFromNVS() {
    initializeDefaults();  // Start with defaults, then overlay NVS values

    _prefs.begin(NVS_NAMESPACE, true);  // Read-only

    _recipe.steps[0].holdTimeMin = _prefs.getUShort(NVS_KEY_S1_TIME, DEFAULT_STEP1_TIME);
    _recipe.steps[1].targetTemp  = _prefs.getFloat(NVS_KEY_S2_TEMP,  DEFAULT_STEP2_TEMP);
    _recipe.steps[1].holdTimeMin = _prefs.getUShort(NVS_KEY_S2_TIME, DEFAULT_STEP2_TIME);
    _recipe.steps[2].holdTimeMin = _prefs.getUShort(NVS_KEY_S3_TIME, DEFAULT_STEP3_TIME);
    _recipe.steps[3].targetTemp  = _prefs.getFloat(NVS_KEY_S4_TEMP,  DEFAULT_STEP4_TEMP);
    _recipe.steps[3].holdTimeMin = _prefs.getUShort(NVS_KEY_S4_TIME, DEFAULT_STEP4_TIME);

    _prefs.end();
}

void RecipeManager::resetToDefaults() {
    initializeDefaults();
    saveToNVS();
    Serial.println("[RECIPE] Reset to factory defaults");
}

// --- PID parameter storage ---

void RecipeManager::savePIDParams(float kp, float ki, float kd) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat(NVS_KEY_KP, kp);
    _prefs.putFloat(NVS_KEY_KI, ki);
    _prefs.putFloat(NVS_KEY_KD, kd);
    _prefs.end();
    Serial.printf("[RECIPE] PID saved: Kp=%.2f Ki=%.3f Kd=%.2f\n", kp, ki, kd);
}

void RecipeManager::loadPIDParams(float& kp, float& ki, float& kd) {
    _prefs.begin(NVS_NAMESPACE, true);
    kp = _prefs.getFloat(NVS_KEY_KP, DEFAULT_KP);
    ki = _prefs.getFloat(NVS_KEY_KI, DEFAULT_KI);
    kd = _prefs.getFloat(NVS_KEY_KD, DEFAULT_KD);
    _prefs.end();
}

void RecipeManager::saveCalibrationOffset(float offset) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat(NVS_KEY_CAL_OFFSET, offset);
    _prefs.end();
    Serial.printf("[RECIPE] Calibration offset saved: %.2f °C\n", offset);
}

float RecipeManager::loadCalibrationOffset() {
    _prefs.begin(NVS_NAMESPACE, true);
    float offset = _prefs.getFloat(NVS_KEY_CAL_OFFSET, TEMP_CALIBRATION_OFFSET);
    _prefs.end();
    return offset;
}

void RecipeManager::savePowerRatios(float bot, float mid, float top) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat(NVS_KEY_BOTTOM_RATIO, bot);
    _prefs.putFloat(NVS_KEY_MIDDLE_RATIO, mid);
    _prefs.putFloat(NVS_KEY_TOP_RATIO,    top);
    _prefs.end();
}

void RecipeManager::loadPowerRatios(float& bot, float& mid, float& top) {
    _prefs.begin(NVS_NAMESPACE, true);
    bot = _prefs.getFloat(NVS_KEY_BOTTOM_RATIO, BOTTOM_POWER_RATIO);
    mid = _prefs.getFloat(NVS_KEY_MIDDLE_RATIO, MIDDLE_POWER_RATIO);
    top = _prefs.getFloat(NVS_KEY_TOP_RATIO,    TOP_POWER_RATIO);
    _prefs.end();
}

// --- Private ---

void RecipeManager::initializeDefaults() {
    // Step 1: Fixed target 25°C
    _recipe.steps[0].targetTemp       = DEFAULT_STEP1_TEMP;
    _recipe.steps[0].holdTimeMin      = DEFAULT_STEP1_TIME;
    _recipe.steps[0].isTempSelectable = false;
    _recipe.steps[0].tempOptionCount  = 0;

    // Step 2: Selectable target (0/15/25°C)
    _recipe.steps[1].targetTemp       = DEFAULT_STEP2_TEMP;
    _recipe.steps[1].holdTimeMin      = DEFAULT_STEP2_TIME;
    _recipe.steps[1].isTempSelectable = true;
    _recipe.steps[1].tempOptions[0]   = 0.0f;
    _recipe.steps[1].tempOptions[1]   = 15.0f;
    _recipe.steps[1].tempOptions[2]   = 25.0f;
    _recipe.steps[1].tempOptionCount  = 3;

    // Step 3: Fixed target 4°C
    _recipe.steps[2].targetTemp       = DEFAULT_STEP3_TEMP;
    _recipe.steps[2].holdTimeMin      = DEFAULT_STEP3_TIME;
    _recipe.steps[2].isTempSelectable = false;
    _recipe.steps[2].tempOptionCount  = 0;

    // Step 4: Selectable target (0/25°C)
    _recipe.steps[3].targetTemp       = DEFAULT_STEP4_TEMP;
    _recipe.steps[3].holdTimeMin      = DEFAULT_STEP4_TIME;
    _recipe.steps[3].isTempSelectable = true;
    _recipe.steps[3].tempOptions[0]   = 0.0f;
    _recipe.steps[3].tempOptions[1]   = 25.0f;
    _recipe.steps[3].tempOptionCount  = 2;

    // Step 5: Ramp (no user-editable hold time)
    _recipe.steps[4].targetTemp       = RAMP_FINAL_TARGET;
    _recipe.steps[4].holdTimeMin      = 0;
    _recipe.steps[4].isTempSelectable = false;
    _recipe.steps[4].tempOptionCount  = 0;

    // Ramp parameters
    _recipe.rampRate      = RAMP_RATE_DEFAULT;
    _recipe.rampFinalTemp = RAMP_FINAL_TARGET;
}
