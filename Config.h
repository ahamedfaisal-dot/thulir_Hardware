/*
 * ============================================================
 *  TULIR — 3-Stage Cascaded Peltier Temperature Controller
 *  Config.h — Master Configuration
 * ============================================================
 *
 *  HARDWARE OVERVIEW
 *  -----------------
 *  Controller : ESP32-S3
 *  Peltiers   : 3× cascaded (Bottom 12A / Middle 6A / Top 6A)
 *  Drivers    : 3× BTS7960 H-bridge (unidirectional cooling)
 *  Display    : 2.8" ILI9341 SPI TFT 320×240
 *  Input      : 4×4 matrix keypad
 *  Sensor     : Waterproof DS18B20 (cold-side)
 *  Audio      : DFPlayer Mini (voice announcements)
 *  PSU        : 12 V 30 A SMPS
 *  Cooling    : 4× fans on large heatsink (hardwired to PSU)
 *
 *  POWER ARCHITECTURE
 *  ------------------
 *  12 V 30 A SMPS
 *        |
 *        +---- Fuse (15A) ---- BTS7960 #1 ---- Bottom Peltier (12V/12A)
 *        |
 *        +---- Fuse (10A) ---- BTS7960 #2 ---- Middle Peltier (12V/6A)
 *        |
 *        +---- Fuse (10A) ---- BTS7960 #3 ---- Top Peltier   (12V/6A)
 *        |
 *        +---- 4× Cooling Fans (hardwired, always-on when PSU active)
 *
 *  ESP32-S3 must be powered from a separate regulated 5V/3.3V supply.
 *  Do NOT feed 12 V into the ESP32-S3 directly.
 *  All logic grounds must share a common reference with BTS7960 GND.
 *
 *  SAFETY NOTICE
 *  -------------
 *  This firmware is NOT a substitute for electrical protection.
 *  You MUST install:
 *    - Appropriate branch fuses for each Peltier stage
 *    - Adequate gauge wiring (≥14 AWG for bottom stage)
 *    - Proper high-current terminals / connectors
 *    - Thermal interface material on Peltier stack
 *    - Adequate heatsink ventilation
 *    - Physical emergency power cutoff switch
 *
 *  The ESP32 cannot detect overcurrent without a current sensor.
 *  Software PWM limits are a secondary safeguard only.
 *
 *  Never route Peltier power (12V/12A+) through PCB signal traces.
 *  Never assume a BTS7960 module's peak rating equals safe continuous
 *  current — derate per manufacturer datasheet.
 *
 *  Test each stage independently before full cascade operation.
 *
 * ============================================================
 */

#pragma once

#include <Arduino.h>

// ============================================================
//  FIRMWARE VERSION
// ============================================================
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0
#define FW_VERSION_STR    "1.0.0"
#define FW_NAME           "TULIR"

// ============================================================
//  FEATURE FLAGS
// ============================================================
#define DEBUG_ENABLED           true    // Serial debug output
#define DEBUG_BAUD              115200
#define SERIAL_PRINT_INTERVAL   2000   // ms between debug prints

// Hot-side temperature sensor (DS18B20 on hot side of heatsink)
// Currently not installed. Set to true when you add one.
// STRONGLY RECOMMENDED for a high-power cascaded Peltier system.
#define HOT_SIDE_SENSOR_ENABLED false
#define HOT_SIDE_MAX_TEMP       75.0f  // °C — shut down Peltiers above this
#define HOT_SIDE_RECOVERY_TEMP  50.0f  // °C — allow restart below this

// ============================================================
//  GPIO PIN CONFIGURATION
// ============================================================
//
//  ESP32-S3 GPIO notes:
//    GPIO 0       : Boot strapping — avoid
//    GPIO 19, 20  : USB D−/D+ on DevKitC-1 — avoid
//    GPIO 26–32   : Do NOT exist on ESP32-S3 (internal flash/PSRAM)
//    GPIO 33–37   : Available on most S3 modules (SPI-capable)
//    GPIO 38–48   : Available on most S3 modules
//
//  If your specific board maps any of these pins differently,
//  change ONLY this section — the rest of the code uses these
//  symbolic names exclusively.
// ============================================================

// --- BTS7960 #1 — BOTTOM Peltier (12V / 12A rated) ---------
#define BOTTOM_RPWM_PIN    4    // PWM signal → BTS7960 RPWM
#define BOTTOM_LPWM_PIN    5    // Held LOW  → BTS7960 LPWM
#define BOTTOM_REN_PIN     6    // Held HIGH → BTS7960 R_EN
#define BOTTOM_LEN_PIN     7    // Held HIGH → BTS7960 L_EN

// --- BTS7960 #2 — MIDDLE Peltier (12V / 6A rated) ----------
#define MIDDLE_RPWM_PIN    8
#define MIDDLE_LPWM_PIN    9
#define MIDDLE_REN_PIN     10
// MIDDLE_LEN_PIN: always HIGH — hardwire this BTS7960 pin directly to 3.3V.
// GPIO11 is reclaimed for TFT MOSI (native FSPI pin).
// #define MIDDLE_LEN_PIN  11   // ← freed; wire BTS7960 L_EN to 3.3V

// --- BTS7960 #3 — TOP Peltier (12V / 6A rated) -------------
#define TOP_RPWM_PIN       12
// TOP_LPWM_PIN: always LOW — hardwire this BTS7960 pin directly to GND.
// GPIO13 is reclaimed for TFT SCK (native FSPI CLK pin).
// #define TOP_LPWM_PIN    13   // ← freed; wire BTS7960 LPWM to GND
#define TOP_REN_PIN        14
#define TOP_LEN_PIN        15

// --- DS18B20 Temperature Sensor (cold side) -----------------
//  Wire a 4.7 kΩ pull-up from DATA to 3.3 V.
#define DS18B20_PIN        3

// --- DS18B20 Hot-side sensor (optional, future) -------------
#define DS18B20_HOT_PIN    43   // Change when installed

// --- TFT Display (2.8" ILI9341 SPI, 320×240) ---------------
//  Uses TFT_eSPI library (Bodmer). Pin config lives in User_Setup.h
//  which must be copied to the TFT_eSPI library directory.
//
//  Physical wiring:
//    TFT MOSI  → GPIO 11  (native FSPI MOSI; MIDDLE_LEN hardwired to 3.3V)
//    TFT SCK   → GPIO 13  (native FSPI CLK;  TOP_LPWM  hardwired to GND)
//    TFT MISO  → not connected (write-only display)
//    TFT CS    → GPIO 47
//    TFT DC    → GPIO 48
//    TFT RST   → GPIO 21
//    TFT LED   → 3.3V
//
//  Note: TFT_eSPI reads pin numbers from User_Setup.h in the library
//  folder, NOT from these defines. These are kept as documentation only.
#define TFT_CS_PIN         47
#define TFT_DC_PIN         48
#define TFT_RST_PIN        21

// --- 4×4 Matrix Keypad --------------------------------------
//  Layout:
//    1 2 3 A
//    4 5 6 B
//    7 8 9 C
//    * 0 # D
//
//  Rows are outputs (directly driven), columns are inputs
//  with internal pull-ups enabled by the Keypad library.
#define KP_ROW0_PIN        16
#define KP_ROW1_PIN        17
#define KP_ROW2_PIN        18
#define KP_ROW3_PIN        38   // Moved from 19 (USB on DevKitC)

#define KP_COL0_PIN        20   // Safe if USB-OTG not used
#define KP_COL1_PIN        39
#define KP_COL2_PIN        40
#define KP_COL3_PIN        41

// --- DFPlayer Mini ------------------------------------------
//  Uses hardware UART1 on ESP32-S3.
//  Connect ESP32 TX → DFPlayer RX (via 1kΩ resistor recommended)
//  Connect DFPlayer TX → ESP32 RX
#define DFPLAYER_TX_PIN    1    // ESP32 UART1 TX → DFPlayer RX
#define DFPLAYER_RX_PIN    2    // DFPlayer TX → ESP32 UART1 RX

// ============================================================
//  PWM CONFIGURATION
// ============================================================
//  BTS7960 supports up to ~25 kHz PWM.
//  5 kHz is a good balance: inaudible, efficient switching.
#define PWM_FREQUENCY      5000     // Hz
#define PWM_RESOLUTION     10       // bits → 0–1023 duty range
#define PWM_MAX_DUTY       ((1 << PWM_RESOLUTION) - 1)  // 1023

// ============================================================
//  PELTIER POWER RATIOS & LIMITS
// ============================================================
//  These BASE RATIOS define how the master PID output (0–100%)
//  is distributed across the three cascaded Peltier stages.
//
//  Experimentally determined for ≈ −27°C at full power:
//    Bottom: 12.0 V  → 100%
//    Middle:  6.1 V  →  51%
//    Top:     2.45 V →  20%
//
//  Example: PID demands 60% cooling
//    Bottom = 100% × 60% = 60.0%
//    Middle =  51% × 60% = 30.6%
//    Top    =  20% × 60% = 12.0%

#define BOTTOM_POWER_RATIO  1.00f   // 100%
#define MIDDLE_POWER_RATIO  0.51f   //  51%
#define TOP_POWER_RATIO     0.20f   //  20%

//  Maximum PWM duty-cycle limits (absolute safety caps).
//  These prevent any stage from exceeding its thermal budget
//  even if PID demands 100%.
#define BOTTOM_MAX_PWM_PCT  100.0f  // % of full duty
#define MIDDLE_MAX_PWM_PCT   60.0f
#define TOP_MAX_PWM_PCT      30.0f

//  Minimum PWM threshold — below this, output is forced to 0.
//  Prevents ineffective dribble current that just heats wires.
#define MIN_PWM_THRESHOLD    0.0f   // % — set >0 if needed

// ============================================================
//  PID CONTROLLER DEFAULTS
// ============================================================
//  *** THESE ARE CONSERVATIVE PLACEHOLDERS ***
//  You MUST tune them experimentally on your hardware.
//
//  Start with proportional-only (Ki=0, Kd=0), increase Kp
//  until you see slight oscillation, then add Ki for zero
//  steady-state error, and Kd for damping.
//
//  PID computes: output = Kp·e + Ki·∫e·dt + Kd·de/dt
//  where e = actualTemp − setpoint (positive = too warm)
//  output = 0–100 (% cooling demand)

#define DEFAULT_KP           15.0f
#define DEFAULT_KI            0.5f
#define DEFAULT_KD            5.0f

#define PID_SAMPLE_TIME_MS    500   // PID compute interval
#define PID_OUTPUT_MIN        0.0f
#define PID_OUTPUT_MAX      100.0f

// ============================================================
//  TEMPERATURE SETTINGS
// ============================================================
#define TEMP_READ_INTERVAL_MS    200    // DS18B20 read request cycle
#define TEMP_CONVERSION_MS       750    // 12-bit conversion time
#define TEMP_FILTER_SAMPLES        5    // Moving-average window

#define TEMP_SENSOR_MIN        -40.0f   // Valid range low
#define TEMP_SENSOR_MAX         85.0f   // Valid range high
#define TEMP_ERROR_VALUE      -127.0f   // DS18B20 disconnect code
#define TEMP_INVALID_THRESH     0.5f    // Reject jump > this per sample

#define TEMP_CALIBRATION_OFFSET 0.0f    // Default cal offset
#define TEMP_CAL_OFFSET_MAX     5.0f    // Warn if |offset| exceeds this

// ============================================================
//  STEP CONTROL / PROFILE SETTINGS
// ============================================================
//  Target tolerance: temperature must be within ±TOLERANCE of
//  setpoint to be considered "at target".
#define TARGET_TOLERANCE        0.5f    // °C

//  Confirmation time: temperature must remain within tolerance
//  band for this duration before the hold timer starts.
#define TARGET_CONFIRM_TIME_MS  30000   // 30 seconds

//  Hold timer behavior when temperature leaves tolerance band.
//  true  = pause timer until temp returns to band
//  false = timer continues regardless
#define HOLD_TIMER_PAUSE_ON_DEVIATION  true

// --- Step 5 Ramp Settings ---
#define RAMP_RATE_DEFAULT      -1.0f    // °C per minute
#define RAMP_FINAL_TARGET      -20.0f   // °C
#define RAMP_UPDATE_INTERVAL_MS  100    // Setpoint update rate
#define RAMP_LAG_THRESHOLD      2.0f    // °C behind setpoint → warning

// ============================================================
//  DEFAULT RECIPE (hold times in minutes)
// ============================================================
#define DEFAULT_STEP1_TEMP      25.0f
#define DEFAULT_STEP1_TIME      30      // minutes

#define DEFAULT_STEP2_TEMP      15.0f   // User-selectable: 0/15/25
#define DEFAULT_STEP2_TIME      30

#define DEFAULT_STEP3_TEMP       4.0f
#define DEFAULT_STEP3_TIME      20

#define DEFAULT_STEP4_TEMP       0.0f   // User-selectable: 0/25
#define DEFAULT_STEP4_TIME      20

// Step 5 uses ramp, no hold time in the traditional sense

// ============================================================
//  TIMING INTERVALS (ms)
// ============================================================
#define DISPLAY_UPDATE_INTERVAL   500
#define KEYPAD_SCAN_INTERVAL       50
#define SAFETY_CHECK_INTERVAL     500
#define AUDIO_MIN_INTERVAL       2000   // Min gap between announcements

// ============================================================
//  FAN CONTROL
// ============================================================
//  Fans are hardwired directly to the 12V SMPS and run
//  whenever the PSU is powered. No GPIO control is needed.
//
//  If you later add a MOSFET/relay for fan control, define:
//    #define FAN_CONTROL_ENABLED  true
//    #define FAN_PIN              42
//    #define FAN_COOLDOWN_SECS    120
//
//  For now, fan management is handled externally.
#define FAN_CONTROL_ENABLED  false

// ============================================================
//  TFT DISPLAY COLORS (RGB565)
// ============================================================
//  Industrial HMI palette — clean, professional, high-contrast.
#define COLOR_BG              0x10A2  // Dark charcoal background
#define COLOR_HEADER_BG       0x2945  // Steel header bar
#define COLOR_TEXT_PRIMARY     0xFFFF  // White
#define COLOR_TEXT_SECONDARY   0xB5B6  // Light gray
#define COLOR_TEXT_DIM         0x7BEF  // Medium gray
#define COLOR_TEMP_ACTUAL      0x5DDF  // Cool steel blue
#define COLOR_TEMP_TARGET      0xFD20  // Amber
#define COLOR_STATUS_OK        0x2DC9  // Forest green
#define COLOR_STATUS_WARN      0xFCA0  // Orange
#define COLOR_STATUS_ERR       0xF800  // Red
#define COLOR_BAR_FILL         0x2B6D  // Teal
#define COLOR_BAR_BG           0x3186  // Dark gray
#define COLOR_DIVIDER          0x4A49  // Separator gray
#define COLOR_HIGHLIGHT        0x001F  // Selection blue
#define COLOR_MENU_SEL_BG      0x0010  // Dark navy selection bg

// ============================================================
//  DFPLAYER AUDIO FILE MAP
// ============================================================
//  Files must be on microSD as /mp3/0001.mp3 through /mp3/0012.mp3
//  Change these indices if your files are numbered differently.
#define AUDIO_SYSTEM_START     1
#define AUDIO_STEP1            2
#define AUDIO_STEP2            3
#define AUDIO_STEP3            4
#define AUDIO_STEP4            5
#define AUDIO_STEP5            6
#define AUDIO_PROCESS_COMPLETE 7
#define AUDIO_EMERGENCY_STOP   8
#define AUDIO_SENSOR_ERROR     9
#define AUDIO_OVER_TEMP       10
#define AUDIO_RAMP_STARTED    11
#define AUDIO_MINUS20_REACHED 12

#define DFPLAYER_VOLUME        25      // 0–30

// ============================================================
//  NVS STORAGE KEYS
// ============================================================
#define NVS_NAMESPACE          "tulir"
#define NVS_KEY_S1_TIME        "s1_time"
#define NVS_KEY_S2_TEMP        "s2_temp"
#define NVS_KEY_S2_TIME        "s2_time"
#define NVS_KEY_S3_TIME        "s3_time"
#define NVS_KEY_S4_TEMP        "s4_temp"
#define NVS_KEY_S4_TIME        "s4_time"
#define NVS_KEY_KP             "kp"
#define NVS_KEY_KI             "ki"
#define NVS_KEY_KD             "kd"
#define NVS_KEY_CAL_OFFSET     "cal_off"
#define NVS_KEY_BOTTOM_RATIO   "bot_ratio"
#define NVS_KEY_MIDDLE_RATIO   "mid_ratio"
#define NVS_KEY_TOP_RATIO      "top_ratio"
#define NVS_KEY_INITIALIZED    "init_flag"

// ============================================================
//  SYSTEM STATE ENUMERATIONS
// ============================================================

enum SystemState {
    STATE_BOOT,
    STATE_IDLE,
    STATE_PROGRAMMING,
    STATE_READY,
    STATE_STEP_APPROACH,
    STATE_STEP_HOLD,
    STATE_STEP_TRANSITION,
    STATE_STEP5_RAMP,
    STATE_COMPLETE,
    STATE_STOPPED,
    STATE_FAULT,
    STATE_TEST_MODE
};

enum ErrorCode {
    ERROR_NONE = 0,
    ERROR_TEMP_SENSOR,
    ERROR_TEMP_INVALID,
    ERROR_OVER_TEMP,
    ERROR_HOT_SIDE_OVER_TEMP,
    ERROR_DFPLAYER,
    ERROR_DISPLAY,
    ERROR_INVALID_RECIPE,
    ERROR_EMERGENCY_STOP
};

enum ScreenID {
    SCREEN_HOME,
    SCREEN_MENU,
    SCREEN_PROGRAM,
    SCREEN_STEP_EDIT,
    SCREEN_NUM_ENTRY,
    SCREEN_CONFIRM_START,
    SCREEN_CONFIRM_STOP,
    SCREEN_SETTINGS,
    SCREEN_PID,
    SCREEN_CALIBRATION,
    SCREEN_TEST,
    SCREEN_TEST_COMPONENT,
    SCREEN_ABOUT,
    SCREEN_FAULT,
    SCREEN_COMPLETE,
    SCREEN_STOPPED,
    SCREEN_MANUAL_PWM
};

enum PeltierStage {
    STAGE_BOTTOM = 0,
    STAGE_MIDDLE = 1,
    STAGE_TOP    = 2,
    STAGE_COUNT  = 3
};

// ============================================================
//  MENU ITEMS
// ============================================================
#define MENU_ITEM_COUNT  7
const char* const MENU_LABELS[MENU_ITEM_COUNT] = {
    "PROGRAM",
    "START",
    "STOP",
    "SETTINGS",
    "PID TUNE",
    "TEST MODE",
    "ABOUT"
};

enum MenuItemID {
    MENU_PROGRAM = 0,
    MENU_START,
    MENU_STOP,
    MENU_SETTINGS,
    MENU_PID_TUNE,
    MENU_TEST_MODE,
    MENU_ABOUT
};

// ============================================================
//  RECIPE STRUCTURE
// ============================================================

struct StepConfig {
    float    targetTemp;       // °C
    uint16_t holdTimeMin;      // minutes (0 for Step 5)
    bool     isTempSelectable; // Can user choose target?
    float    tempOptions[3];   // Selectable temperatures
    uint8_t  tempOptionCount;  // Number of options
};

struct Recipe {
    StepConfig steps[5];
    float      rampRate;       // °C/min for Step 5 (negative)
    float      rampFinalTemp;  // °C final target for Step 5
};

// ============================================================
//  SYSTEM STATUS (shared runtime state)
// ============================================================

struct SystemStatus {
    SystemState state;
    ErrorCode   errorCode;
    ScreenID    currentScreen;

    uint8_t     currentStep;          // 1–5
    float       actualTemp;           // Raw sensor reading
    float       filteredTemp;         // Filtered for PID
    float       targetTemp;           // Current step target
    float       currentSetpoint;      // Ramp: continuously changing
    float       pidOutput;            // 0–100%
    float       bottomPWM;            // Applied PWM %
    float       middlePWM;
    float       topPWM;

    // Hold timing
    bool        targetReached;        // Temp within tolerance?
    bool        targetConfirmed;      // Confirmed for CONFIRM_TIME?
    unsigned long targetReachedTime;  // When temp first entered band
    unsigned long holdStartTime;      // When hold timer started
    unsigned long holdDurationMs;     // Programmed hold duration
    unsigned long holdElapsedMs;      // Accumulated hold time
    bool        holdTimerRunning;     // Is hold timer active?

    // Ramp (Step 5)
    float       rampStartTemp;
    unsigned long rampStartTime;
    bool        rampLag;              // Actual lagging behind setpoint?
    float       rampLagAmount;        // How far behind (°C)

    // Process timing
    unsigned long processStartTime;
    unsigned long stepStartTime;

    // Sensor status
    bool        sensorValid;
    bool        hotSideSensorValid;
    float       hotSideTemp;

    // Menu state
    uint8_t     menuSelection;
    uint8_t     editStep;             // Which step is being edited (0–4)

    // Test mode
    PeltierStage testStage;
    float        testPWM;
    bool         testActive;
};

// ============================================================
//  KEYPAD KEY DEFINITIONS
// ============================================================
#define KEY_UP      'A'
#define KEY_DOWN    'B'
#define KEY_BACK    'C'
#define KEY_ENTER   'D'
#define KEY_CONFIRM '#'
#define KEY_DECIMAL '*'
// Numeric keys '0'–'9' used directly

// ============================================================
//  UTILITY MACROS
// ============================================================
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define PCT_TO_DUTY(pct)  ((uint32_t)(((pct) / 100.0f) * PWM_MAX_DUTY))

// Helper to get state name as string
inline const char* getStateName(SystemState s) {
    switch (s) {
        case STATE_BOOT:            return "BOOT";
        case STATE_IDLE:            return "IDLE";
        case STATE_PROGRAMMING:     return "PROGRAMMING";
        case STATE_READY:           return "READY";
        case STATE_STEP_APPROACH:   return "APPROACHING";
        case STATE_STEP_HOLD:       return "HOLDING";
        case STATE_STEP_TRANSITION: return "TRANSITIONING";
        case STATE_STEP5_RAMP:      return "RAMPING";
        case STATE_COMPLETE:        return "COMPLETE";
        case STATE_STOPPED:         return "STOPPED";
        case STATE_FAULT:           return "FAULT";
        case STATE_TEST_MODE:       return "TEST MODE";
        default:                    return "UNKNOWN";
    }
}

inline const char* getErrorName(ErrorCode e) {
    switch (e) {
        case ERROR_NONE:              return "NONE";
        case ERROR_TEMP_SENSOR:       return "TEMP SENSOR";
        case ERROR_TEMP_INVALID:      return "TEMP INVALID";
        case ERROR_OVER_TEMP:         return "OVER TEMP";
        case ERROR_HOT_SIDE_OVER_TEMP:return "HOT SIDE OVER TEMP";
        case ERROR_DFPLAYER:          return "DFPLAYER";
        case ERROR_DISPLAY:           return "DISPLAY";
        case ERROR_INVALID_RECIPE:    return "INVALID RECIPE";
        case ERROR_EMERGENCY_STOP:    return "EMERGENCY STOP";
        default:                      return "UNKNOWN ERROR";
    }
}
