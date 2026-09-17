/*
 * ============================================================
 *  TULIR — Recipe Manager
 *  RecipeManager.h
 * ============================================================
 *  Manages the 5-step temperature profile recipe:
 *    - Default initialization
 *    - NVS persistent storage (Preferences library)
 *    - Recipe validation
 *    - PID parameter storage
 *    - Calibration offset storage
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class RecipeManager {
public:
    RecipeManager();

    // Initialize recipe — loads from NVS or creates defaults
    void begin();

    // Get the current recipe (read-only)
    const Recipe& getRecipe() const;

    // Get a mutable reference for editing
    Recipe& getRecipeForEdit();

    // Set Step 2 target (must be 0, 15, or 25)
    bool setStep2Temp(float temp);

    // Set Step 4 target (must be 0 or 25)
    bool setStep4Temp(float temp);

    // Set hold time for a step (1–4), in minutes
    bool setStepHoldTime(uint8_t step, uint16_t minutes);

    // Validate the entire recipe before starting
    bool validateRecipe() const;

    // Save recipe to NVS (call only when user confirms edits)
    void saveToNVS();

    // Load recipe from NVS (called automatically in begin())
    void loadFromNVS();

    // Reset recipe to factory defaults
    void resetToDefaults();

    // PID parameter storage
    void savePIDParams(float kp, float ki, float kd);
    void loadPIDParams(float& kp, float& ki, float& kd);

    // Calibration offset storage
    void saveCalibrationOffset(float offset);
    float loadCalibrationOffset();

    // Power ratio storage
    void savePowerRatios(float bot, float mid, float top);
    void loadPowerRatios(float& bot, float& mid, float& top);

private:
    Recipe      _recipe;
    Preferences _prefs;

    void initializeDefaults();
};
