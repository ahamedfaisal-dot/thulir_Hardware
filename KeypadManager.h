/*
 * ============================================================
 *  TULIR — Keypad Manager
 *  KeypadManager.h
 * ============================================================
 *  Non-blocking 4×4 matrix keypad scanning with input state
 *  machine for numeric entry, menu navigation, and confirmation.
 *
 *  Key Map:
 *    1 2 3 A       A = UP / scroll up
 *    4 5 6 B       B = DOWN / scroll down
 *    7 8 9 C       C = BACK / CANCEL / STOP
 *    * 0 # D       D = ENTER / SELECT
 *                   # = CONFIRM entry
 *                   * = DECIMAL / NEGATIVE (context-dependent)
 *                   0-9 = numeric entry
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include <Keypad.h>
#include "Config.h"

// Numeric input state
struct NumericInput {
    char    buffer[8];     // Digit buffer (max 7 chars + null)
    uint8_t pos;           // Current position in buffer
    bool    active;        // Is numeric entry mode active?
    bool    confirmed;     // User pressed # to confirm
    bool    cancelled;     // User pressed C to cancel
    int     intValue;      // Parsed integer result
    float   floatValue;    // Parsed float result
    bool    allowNegative; // Allow negative values?
    bool    allowDecimal;  // Allow decimal point?
    int     minVal;        // Minimum allowed value
    int     maxVal;        // Maximum allowed value
};

class KeypadManager {
public:
    KeypadManager();

    // Initialize keypad GPIO
    void begin();

    // Non-blocking update — call every loop iteration.
    // Returns the pressed key, or '\0' if no key.
    char update();

    // Get the last pressed key (without consuming it)
    char getLastKey() const;

    // Start numeric input mode
    void startNumericInput(bool allowNeg = false, bool allowDec = false,
                           int minVal = 0, int maxVal = 999);

    // Get numeric input state
    const NumericInput& getNumericInput() const;

    // Process a key press during numeric input
    // Returns true if input is complete (confirmed or cancelled)
    bool processNumericKey(char key);

    // Reset numeric input
    void resetNumericInput();

    // Get display string for current numeric entry
    const char* getNumericDisplayStr() const;

private:
    Keypad*      _keypad;
    char         _lastKey;
    unsigned long _lastKeyTime;
    NumericInput _numInput;

    // Keypad configuration (static for Keypad library)
    static const byte ROWS = 4;
    static const byte COLS = 4;
    static char _keys[ROWS][COLS];
    static byte _rowPins[ROWS];
    static byte _colPins[COLS];
};
