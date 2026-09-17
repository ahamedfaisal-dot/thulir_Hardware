/*
 * ============================================================
 *  TULIR — Keypad Manager Implementation
 *  KeypadManager.cpp
 * ============================================================
 */

#include "KeypadManager.h"

// Static member initialization — keypad layout
char KeypadManager::_keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

byte KeypadManager::_rowPins[ROWS] = {
    KP_ROW0_PIN, KP_ROW1_PIN, KP_ROW2_PIN, KP_ROW3_PIN
};

byte KeypadManager::_colPins[COLS] = {
    KP_COL0_PIN, KP_COL1_PIN, KP_COL2_PIN, KP_COL3_PIN
};

KeypadManager::KeypadManager()
    : _keypad(nullptr)
    , _lastKey('\0')
    , _lastKeyTime(0)
{
    resetNumericInput();
}

void KeypadManager::begin() {
    _keypad = new Keypad(makeKeymap(_keys), _rowPins, _colPins, ROWS, COLS);

    // Set debounce and hold times
    _keypad->setDebounceTime(50);
    _keypad->setHoldTime(800);

    Serial.println("[KEYPAD] 4x4 matrix keypad initialized");
    Serial.printf("[KEYPAD] Rows: GPIO %d,%d,%d,%d  Cols: GPIO %d,%d,%d,%d\n",
                  KP_ROW0_PIN, KP_ROW1_PIN, KP_ROW2_PIN, KP_ROW3_PIN,
                  KP_COL0_PIN, KP_COL1_PIN, KP_COL2_PIN, KP_COL3_PIN);
}

char KeypadManager::update() {
    if (!_keypad) return '\0';

    char key = _keypad->getKey();

    if (key) {
        _lastKey = key;
        _lastKeyTime = millis();

        #if DEBUG_ENABLED
            Serial.printf("[KEYPAD] Key pressed: '%c'\n", key);
        #endif
    }

    return key;
}

char KeypadManager::getLastKey() const {
    return _lastKey;
}

void KeypadManager::startNumericInput(bool allowNeg, bool allowDec,
                                       int minVal, int maxVal) {
    resetNumericInput();
    _numInput.active = true;
    _numInput.allowNegative = allowNeg;
    _numInput.allowDecimal = allowDec;
    _numInput.minVal = minVal;
    _numInput.maxVal = maxVal;
}

const NumericInput& KeypadManager::getNumericInput() const {
    return _numInput;
}

bool KeypadManager::processNumericKey(char key) {
    if (!_numInput.active) return false;

    // Numeric keys 0-9: append digit
    if (key >= '0' && key <= '9') {
        if (_numInput.pos < 6) {  // Leave room for null terminator
            _numInput.buffer[_numInput.pos++] = key;
            _numInput.buffer[_numInput.pos] = '\0';
        }
        return false;
    }

    // * key: decimal point or negative sign
    if (key == '*') {
        if (_numInput.allowNegative && _numInput.pos == 0) {
            // Negative sign at start
            _numInput.buffer[_numInput.pos++] = '-';
            _numInput.buffer[_numInput.pos] = '\0';
        } else if (_numInput.allowDecimal) {
            // Decimal point (if not already present)
            bool hasDot = false;
            for (uint8_t i = 0; i < _numInput.pos; i++) {
                if (_numInput.buffer[i] == '.') { hasDot = true; break; }
            }
            if (!hasDot && _numInput.pos < 6) {
                _numInput.buffer[_numInput.pos++] = '.';
                _numInput.buffer[_numInput.pos] = '\0';
            }
        }
        return false;
    }

    // # key: confirm entry
    if (key == KEY_CONFIRM || key == KEY_ENTER) {
        if (_numInput.pos > 0) {
            _numInput.intValue = atoi(_numInput.buffer);
            _numInput.floatValue = atof(_numInput.buffer);

            // Range check
            if (_numInput.intValue < _numInput.minVal) {
                _numInput.intValue = _numInput.minVal;
            }
            if (_numInput.intValue > _numInput.maxVal) {
                _numInput.intValue = _numInput.maxVal;
            }

            _numInput.confirmed = true;
            _numInput.active = false;
            return true;
        }
        return false;
    }

    // C key: cancel / clear
    if (key == KEY_BACK) {
        if (_numInput.pos > 0) {
            // Backspace — remove last character
            _numInput.pos--;
            _numInput.buffer[_numInput.pos] = '\0';
        } else {
            // Cancel input
            _numInput.cancelled = true;
            _numInput.active = false;
            return true;
        }
        return false;
    }

    return false;
}

void KeypadManager::resetNumericInput() {
    memset(_numInput.buffer, 0, sizeof(_numInput.buffer));
    _numInput.pos = 0;
    _numInput.active = false;
    _numInput.confirmed = false;
    _numInput.cancelled = false;
    _numInput.intValue = 0;
    _numInput.floatValue = 0.0f;
    _numInput.allowNegative = false;
    _numInput.allowDecimal = false;
    _numInput.minVal = 0;
    _numInput.maxVal = 999;
}

const char* KeypadManager::getNumericDisplayStr() const {
    return _numInput.buffer;
}
