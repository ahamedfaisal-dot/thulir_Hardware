/*
 * ============================================================
 *  THULIR — 3-Stage Cascaded Peltier Temperature Controller
 *  Config.h — Master Configuration
 * ============================================================
 *
 *  HARDWARE OVERVIEW
 *  -----------------
 *  Controller : ESP32-S3
 *  Peltiers   : 3× cascaded (Bottom 12A / Middle 6A / Top 6A)
 *  Drivers    : 3× BTS7960 H-bridge (unidirectional cooling)
 *               RPWM only — LPWM/R_EN/L_EN all hardwired to GND/3.3V
 *  Servos     : 2× hobby servo (SG90/MG90S, 5V) on GPIO 22 and 23
 *               Open on power-on; open 15 s on each step completion;
 *               close on process complete; open on fault/stop
 *  Display    : 2.8" ILI9341 SPI TFT 320×240
 *  Input      : 4×4 matrix keypad
 *  Sensor     : SHT3x I2C temperature + humidity (cold-side)
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
#define FW_NAME           "THULIR"

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
//  WEB DASHBOARD (WiFi telemetry to Flask server)
// ============================================================
//  Set WEB_DASHBOARD_ENABLED false to fully disable WiFi/HTTP code
//  (e.g. no network available) — control loop runs identical either way.
#define WEB_DASHBOARD_ENABLED   true

#define WIFI_SSID          "SEMINAR_HALL"
#define WIFI_PASSWORD      "B109#rec"

// Set to the PC's LAN IP running the Flask server (see thulir_dashboard
// project). Find it with `ipconfig` (Windows) — look for IPv4 Address.
#define WEB_SERVER_HOST    "172.16.13.124"   // <-- CHANGE to your PC's IP
#define WEB_SERVER_PORT    5000
#define WEB_SERVER_PATH    "/api/data"

#define WEB_POST_INTERVAL_MS   2000    // How often to POST telemetry
#define WEB_WIFI_RETRY_MS      10000   // Retry WiFi connect if dropped

// Remote control from the dashboard (START with a recipe / ABORT).
// The server sends a command in the reply to a telemetry POST; the device
// executes it through the SAME paths as the keypad (startProcess() with its
// recipe + pre-start safety checks, stopProcess() emergency stop) and reports
// an acknowledgement in its next telemetry packet. Set false to make the
// device refuse every remote command (telemetry stays active).
#define WEB_REMOTE_CONTROL_ENABLED true

// Cold-side sanity ceiling — catches a cold-side reading that's gone
// implausibly high (e.g. heatsink/airflow failure feeding back into the
// cold side). Deliberately its own constant, separate from
// HOT_SIDE_MAX_TEMP, since the cold side should never legitimately be
// anywhere near a hot-side limit (normal range here is -20..+25°C).
#define COLDSIDE_SANITY_MAX_TEMP 45.0f  // °C

// ============================================================
//  GPIO PIN CONFIGURATION
// ============================================================
//
//  ESP32-S3 GPIO notes:
//    GPIO 0       : Boot strapping (download mode) — avoid pull-ups/downs
//    GPIO 3       : Boot strapping (JTAG signal source) — avoid pull-ups/
//                   downs; an external I2C pull-up here broke esptool's
//                   download-mode handshake in testing on this board
//    GPIO 19, 20  : USB D−/D+ on DevKitC-1 — avoid
//    GPIO 26–32   : Do NOT exist on ESP32-S3 (internal flash/PSRAM)
//    GPIO 33, 34  : NOT broken out on this specific ESP32-S3-DevKitC-1
//                   board at all (confirmed from the board's own pinout
//                   diagram) — this module reserves them internally for
//                   PSRAM. Do not use even if you think you've wired them.
//    GPIO 35–37   : Confirmed present on this board's header and free —
//                   used here for the SHT3x (SDA=35, SCL=36)
//    GPIO 38–44, 47, 48 : Available on most S3 modules
//    GPIO 45      : Boot strapping (VDD_SPI voltage) — avoid pull-ups
//    GPIO 46      : Boot strapping (ROM boot-log verbosity) — lower risk
//                   than GPIO0/3/45, but still best avoided if a free
//                   non-strapping pin is available
//
//  If your specific board maps any of these pins differently,
//  change ONLY this section — the rest of the code uses these
//  symbolic names exclusively.
// ============================================================

// --- BTS7960 #1 — BOTTOM Peltier (12V / 12A rated) ---------
//  RPWM: PWM control signal from ESP32 (GPIO 4)
//  LPWM: hardwired to GND — no GPIO used in actual build
//  R_EN: hardwired to 3.3V — no GPIO used in actual build
//  L_EN: hardwired to 3.3V — no GPIO used in actual build
//  NOTE: The defines below are retained for PeltierControl.cpp
//  compatibility (it initialises these pins in software as a
//  belt-and-suspenders measure). In the actual build these three
//  lines are physically wired to GND / 3.3V, not to these GPIOs.
//  NOTE: Bottom and Middle Peltiers are SWAPPED between the two BTS7960
//  modules in the actual wiring: the BOTTOM Peltier is on the module whose
//  RPWM is GPIO8, and the MIDDLE Peltier is on the module whose RPWM is GPIO4.
#define BOTTOM_RPWM_PIN    8    // PWM signal → BTS7960 RPWM (module the BOTTOM Peltier is plugged into)
#define BOTTOM_LPWM_PIN    255  // not used — LPWM hardwired to GND (255 = skip; GPIO5 now used by DS18B20)
#define BOTTOM_REN_PIN     6    // retained for code compat (pin not used — R_EN hardwired to 3.3V)
#define BOTTOM_LEN_PIN     7    // retained for code compat (pin not used — L_EN hardwired to 3.3V)

// --- BTS7960 #2 — MIDDLE Peltier (12V / 6A rated) ----------
//  RPWM: PWM control signal from ESP32 (GPIO 8)
//  LPWM: hardwired to GND — no GPIO used in actual build
//  R_EN: hardwired to 3.3V — no GPIO used in actual build
//  L_EN: hardwired to 3.3V — no GPIO used in actual build
#define MIDDLE_RPWM_PIN    4    // swapped with BOTTOM — see note above
#define MIDDLE_LPWM_PIN    47   // retained for code compat (pin not used — LPWM hardwired to GND)
#define MIDDLE_REN_PIN     48   // retained for code compat (pin not used — R_EN hardwired to 3.3V)
// MIDDLE_LEN_PIN: hardwired to 3.3V — GPIO11 freed for TFT MOSI.
// #define MIDDLE_LEN_PIN  11   // ← freed; wire BTS7960 L_EN to 3.3V

// --- BTS7960 #3 — TOP Peltier (12V / 6A rated) -------------
//  RPWM: PWM control signal from ESP32 (GPIO 42)
//  LPWM: hardwired to GND — no GPIO used in actual build
//  R_EN: hardwired to 3.3V — no GPIO used in actual build
//  L_EN: hardwired to 3.3V — no GPIO used in actual build
//  (GPIO 42 was moved from GPIO12 to free native FSPI SCK for TFT)
#define TOP_RPWM_PIN       42   // Moved from GPIO12 — freed for TFT native FSPI CLK
#define TOP_LPWM_PIN       21   // retained for code compat (pin not used — LPWM hardwired to GND)
#define TOP_REN_PIN        44   // retained for code compat (pin not used — R_EN hardwired to 3.3V)
#define TOP_LEN_PIN        255  // not used — L_EN hardwired to 3.3V (255 = skip; GPIO15 now used by DS18B20)

// --- SHT3x Temperature + Humidity Sensor (cold side) --------
//  I2C sensor — replaces the DS18B20 for cold-side sensing.
//
//  GPIO3/GPIO46 were tried first but BOTH are ESP32-S3 strapping pins
//  (GPIO3 = JTAG signal source select). The SHT3x's I2C pull-up
//  resistors altered GPIO3's strap state on every reset, breaking the
//  USB download-mode handshake esptool uses to flash ("Wrong boot mode
//  detected (0x4)").
//
//  GPIO33/34 were tried next, but this board's own silkscreen/pinout
//  (ESP32-S3-DevKitC-1) doesn't break them out at all — this specific
//  WROOM-1 module reserves them internally (Quad PSRAM data lines).
//
//  Settled on GPIO35/36: this board's header DOES expose GPIO35-37
//  (unlike 33/34), which means this module's PSRAM only needs 33/34 —
//  35-37 are genuinely free general-purpose GPIOs here. Not strapping
//  pins, not reserved by this module's PSRAM, not used elsewhere.
//    VIN → 3.3V (2.2–5.5V tolerant, but board logic is 3.3V here)
//    GND → GND
//    SDA → GPIO 35
//    SCL → GPIO 36
#define SHT3X_SDA_PIN      35
#define SHT3X_SCL_PIN      36
#define SHT3X_I2C_ADDR     0x44   // Default SHT3x address (0x45 if ADDR pin high)

// --- DS18B20 Hot-side sensor (optional, future) -------------
//  Unrelated to the cold-side swap above — still 1-Wire DS18B20 if/when
//  a hot-side sensor is installed (HOT_SIDE_SENSOR_ENABLED in Config.h).
//  Originally GPIO43, but OneWire on that pin hung the chip during
//  bring-up (GPIO43/44 are the ESP32-S3's default UART0 pins, which
//  the boot ROM also drives). Moved to GPIO37 — free, non-strapping,
//  confirmed present on this board's header, unused elsewhere here.
#define DS18B20_HOT_PIN    37   // Hot-side sensor (optional, disabled)

// --- DS18B20 Cold-side sensor (chamber temperature for PID + dashboard) ---
//  Verified on GPIO5 with the raw 1-Wire test (pin freed from BOTTOM_LPWM).
//  When COLD_SENSOR_DS18B20 is true, the PID/dashboard temperature comes
//  from this DS18B20; the SHT3x is then used for HUMIDITY ONLY.
//  Set false to go back to the SHT3x as the temperature source.
#define COLD_SENSOR_DS18B20   true
#define DS18B20_COLD_PIN      5
#define COLD_DS_RESOLUTION    11     // bits: 11 = 0.125°C, 375 ms conversion
#define COLD_DS_CONVERSION_MS 400    // wait before reading a conversion

// --- TFT Display (2.8" ILI9341 SPI, 320×240) ---------------
//  Uses Adafruit_ILI9341 + Adafruit_GFX (NOT TFT_eSPI — TFT_eSPI v2.5.43
//  on this ESP32-S3 board never got a response from the panel, confirmed
//  by a register-ID read returning 0x00 0x00 0x00 on every attempt, even
//  with correct pins/power/ground. Adafruit_ILI9341 works on the exact
//  same wiring, so DisplayManager uses that instead).
//
//  Physical wiring (matches proven-working igem / sih2026_input projects
//  on this same ESP32-S3 board — GPIO47/48/21 did NOT work reliably as
//  SPI control lines on this hardware and caused a blank/black screen):
//    TFT MOSI  → GPIO 11  (MIDDLE_LEN hardwired to 3.3V)
//    TFT SCK   → GPIO 12  (TOP_RPWM moved to GPIO42)
//    TFT MISO  → GPIO 13
//    TFT CS    → GPIO 10
//    TFT DC    → GPIO 9
//    TFT RST   → GPIO 14
//    TFT LED   → 3.3V
#define TFT_CS_PIN         10
#define TFT_DC_PIN         9
#define TFT_RST_PIN        14
#define TFT_MOSI_PIN       11
#define TFT_SCK_PIN        12
#define TFT_MISO_PIN       13

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

//  Calibrated against the MEASURED bus voltage: Bottom delivers only
//  11.43 V at 100% duty (supply/BTS7960/wiring drop), so the ratios are
//  scaled to 11.43 V instead of the nominal 12 V:
//    Middle: 6.10 V / 11.43 V = 0.534    Top: 2.45 V / 11.43 V = 0.214
#define BOTTOM_POWER_RATIO  1.00f   // 100% (11.43 V measured)
//  UPDATE: Middle and Top BTS7960 modules are now fed from buck converters
//  set to 6.1 V and 2.45 V, so each stage runs at 100 % duty on its own rail.
//  (PWM from a 12 V rail gave far more Peltier self-heating and never
//  reached -20 °C.)  If you go back to a single 12 V rail, restore
//  MIDDLE 0.534 / TOP 0.214 and the 60 / 30 % caps below.
#define MIDDLE_POWER_RATIO  1.00f   // 6.10 V rail
#define TOP_POWER_RATIO     1.00f   // 2.45 V rail

//  Maximum PWM duty-cycle limits (absolute safety caps).
//  These prevent any stage from exceeding its thermal budget
//  even if PID demands 100%.
#define BOTTOM_MAX_PWM_PCT  100.0f  // % of full duty
#define MIDDLE_MAX_PWM_PCT  100.0f   // rail is already 6.1 V
#define TOP_MAX_PWM_PCT     100.0f   // rail is already 2.45 V

//  Minimum PWM threshold — below this, output is forced to 0.
//  Prevents ineffective dribble current that just heats wires.
#define MIN_PWM_THRESHOLD    0.0f   // % — set >0 if needed

// ============================================================
//  PID CONTROLLER — ADVANCED CONFIGURATION
// ============================================================
//  Two gain sets: APPROACH (Steps 1–4, fixed setpoint) and
//  RAMP (Step 5, continuously moving setpoint at −1°C/min).
//
//  PID computes: output = FF + Kp·e + Ki·∫e·dt − Kd·(dPV/dt)
//  where e = actualTemp − setpoint (positive = need more cooling)
//  output = 0–100% cooling demand
//  FF    = temperature-scaled feedforward baseline
//
//  DEFAULT_K* are the NVS-loadable fallback gains (loaded at boot).
//  They are also the starting Approach gains until hardware-tuned.
//
//  RETUNED from the measured step response (Peltier_MinTemp_Test, buck-rail
//  setup): process gain K ≈ 0.48 °C per % power, dead time ≈ 30 s, dominant
//  time constant ≈ 4 min.  SIMC tuning for that plant gives
//    Kp ≈ 8 %/°C,  Ki ≈ Kp/τi ≈ 0.035 %/(°C·s),  Kd ≈ Kp·θ/2 ≈ 120 %/(°C/s)
//  (old Kp=20 was ~75 % of the ultimate gain → oscillation; Kd=8 did nothing).
#define DEFAULT_KP            8.0f
#define DEFAULT_KI            0.035f
#define DEFAULT_KD          120.0f

#define PID_SAMPLE_TIME_MS    500   // PID compute interval (ms)
#define PID_OUTPUT_MIN        0.0f
#define PID_OUTPUT_MAX      100.0f

// --- Approach gains (Steps 1–4: reaching and holding a fixed setpoint) ---
#define APPROACH_KP           8.0f
#define APPROACH_KI           0.035f
#define APPROACH_KD         120.0f
#define APPROACH_INTEGRAL_ZONE  5.0f   // °C — integral only inside this band

// --- Ramp gains (Step 5: tracking a moving setpoint at −1°C/min) ---
//  Higher Kp to track a moving target; lower Ki to avoid windup while
//  the setpoint keeps moving; tighter zone to stay locked on the ramp.
#define RAMP_KP              10.0f
#define RAMP_KI               0.05f
#define RAMP_KD             120.0f
#define RAMP_INTEGRAL_ZONE    3.0f    // °C

// --- Temperature-scaled feedforward ---
//  Hardware data (measured): full cascade (12V / 6.1V / 2.45V) achieves
//  −27°C from a 25°C ambient reference. Using this, feedforward is
//  computed as a linear fraction of how far the setpoint is below ambient:
//    FF% = (FF_AMBIENT_REF − setpoint) / (FF_AMBIENT_REF − FF_MAX_COOL) × 100
//  Examples:
//    setpoint =  25°C → FF ≈  0%   (Step 1, ambient — no pre-load needed)
//    setpoint =  15°C → FF ≈ 19%   (Step 2 @ 15°C)
//    setpoint =   4°C → FF ≈ 40%   (Step 3)
//    setpoint =   0°C → FF ≈ 48%   (Step 4)
//    setpoint = −10°C → FF ≈ 67%   (mid-ramp)
//    setpoint = −20°C → FF ≈ 86%   (ramp final target)
#define PID_FF_AMBIENT_REF    25.0f   // °C — reference point (no cooling needed)
#define PID_FF_MAX_COOL      -27.0f   // °C — (legacy, no longer used by the FF map)
//  Max cooling depth below the ambient reference at 100 % power. Measured:
//  ≈ −23 °C after ~10 min from ~0 °C start (≈ 48 °C below a 25 °C ambient).
//  The FF map is concave: FF = 100·(1 − sqrt(1 − ΔT/PID_FF_DELTA_MAX)).
#define PID_FF_DELTA_MAX      48.0f   // °C
#define PID_FF_MAX_PCT        92.0f   // % — cap to leave headroom for PID correction

// --- Output slew rate limit ---
//  Caps how fast the PID output can change per second. Prevents sudden
//  large output swings that cause overshoot and oscillation, especially
//  on setpoint changes between steps.
#define PID_OUTPUT_RATE_LIMIT  15.0f  // %/second (0.0 = disabled)

// ============================================================
//  TEMPERATURE / HUMIDITY SETTINGS
// ============================================================
//  Cold-side sensor is now the I2C SHT3x (temperature + humidity).
//  Its measurement is fast (a single I2C transaction, ~15ms for a
//  high-repeatability reading) compared to the DS18B20's 750ms 1-Wire
//  conversion, so it's simply read at a fixed interval rather than
//  needing a multi-stage async state machine.
#define SHT3X_READ_INTERVAL_MS    500    // How often to read the SHT3x

#define TEMP_CONVERSION_MS       750    // DS18B20 12-bit conversion time —
                                         // only used by the optional hot-side
                                         // sensor now (see DS18B20_HOT_PIN)
#define TEMP_FILTER_SAMPLES        5    // Moving-average window

#define TEMP_SENSOR_MIN        -40.0f   // Valid range low
#define TEMP_SENSOR_MAX         85.0f   // Valid range high
#define TEMP_ERROR_VALUE      -127.0f   // DS18B20 disconnect code (hot-side)
#define TEMP_INVALID_THRESH     0.5f    // Reject jump > this per sample

#define HUMIDITY_SENSOR_MIN      0.0f   // Valid RH% range low
#define HUMIDITY_SENSOR_MAX    100.0f   // Valid RH% range high

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

//  Safety backstop: worst case (Step 4 at 25°C to -20°C) is 45 minutes
//  of nominal ramp time. If the ramp is still running after this long,
//  something is physically wrong (can't reach target) — fault out
//  rather than run indefinitely at high cooling demand.
#define RAMP_MAX_DURATION_MS   (150UL * 60000UL)   // 150 minutes

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
//  SERVO MOTOR CONFIGURATION
// ============================================================
//  Two servo motors (SG90 / MG90S or equivalent 5V hobby servos)
//  are mounted on the enclosure lid / sample chamber.
//
//  Behaviour:
//    • POWER ON  → both servos OPEN immediately (boot sequence)
//    • STEP COMPLETE (Steps 1-4) → both servos OPEN for
//      SERVO_STEP_OPEN_MS milliseconds, then close again
//    • PROCESS COMPLETE (Step 5 done) → both servos CLOSE
//      and stay closed (peltiers off, chamber sealed)
//    • EMERGENCY STOP / FAULT → both servos OPEN (safe egress)
//
//  Wiring (3-wire hobby servo — signal / VCC / GND):
//    Servo 1  Signal → GPIO SERVO1_PIN   VCC → 5V   GND → GND
//    Servo 2  Signal → GPIO SERVO2_PIN   VCC → 5V   GND → GND
//
//  GPIO 22 and GPIO 23 are free general-purpose outputs on the
//  ESP32-S3-DevKitC-1 (not strapping pins, not USB, not PSRAM,
//  not used by any other subsystem in this firmware).
//  Change these if your board layout requires different pins.
#define SERVO1_PIN            22   // Servo 1 PWM signal
#define SERVO2_PIN            23   // Servo 2 PWM signal

//  Servo angle definitions (degrees, standard 0–180 range).
//  Adjust OPEN/CLOSED angles to match your physical servo
//  mounting — swap them if the direction is reversed.
#define SERVO_OPEN_ANGLE      90   // degrees — lid/door fully open
#define SERVO_CLOSED_ANGLE     0   // degrees — lid/door fully closed

//  How long the servo stays open on step completion (ms).
//  After this window, the servo closes automatically.
#define SERVO_STEP_OPEN_MS  15000  // 15 seconds

//  ESP32 LEDC channel assignments for servo PWM.
//  Must not conflict with BTS7960 LEDC channels (0/1/2 used by
//  PeltierControl).
//  IMPORTANT: LEDC channels share a hardware timer in pairs
//  (0/1, 2/3, 4/5, 6/7). Servos run 50 Hz / 16-bit, Peltiers 5 kHz / 10-bit,
//  so they must NOT share a timer. Channel 3 shares a timer with Peltier
//  channel 2 (TOP stage) and would silently reconfigure it to 50 Hz,
//  leaving the TOP stage almost unpowered. Use 4 and 5 (their own timer).
#define SERVO1_LEDC_CHANNEL    4
#define SERVO2_LEDC_CHANNEL    5
#define SERVO_LEDC_FREQ        50   // Hz (standard hobby servo)
#define SERVO_LEDC_RES         16   // bits → 0–65535 duty range

// ============================================================
//  TFT DISPLAY COLORS (RGB565)
// ============================================================
//  Industrial HMI palette — high-contrast, vivid, professional.
#define COLOR_BG               0x0944  // Deep forest green (dashboard header tone, darkened for TFT contrast) #0E2B22
#define COLOR_HEADER_BG        0x1247  // Dashboard header green #174A3A
#define COLOR_TEXT_PRIMARY     0xFFFF  // White #FFFFFF
#define COLOR_TEXT_SECONDARY   0xDF5C  // Pale green-white (dashboard card border) #DDE9E3
#define COLOR_TEXT_DIM         0x8D74  // Muted sage #8FAEA1
#define COLOR_TEMP_ACTUAL      0x4DB6  // Dashboard actual-temperature teal #4FB6B2
#define COLOR_TEMP_TARGET      0xBF3A  // Light mint (target line; dashboard dark green is unreadable on a dark TFT) #BFE6D1
#define COLOR_STATUS_OK        0x5E31  // Bright green (dashboard success #3E8B63, lightened for contrast) #5CC48A
#define COLOR_STATUS_WARN      0xDCC6  // Dashboard warning amber #D99A35
#define COLOR_STATUS_ERR       0xE32C  // Dashboard error red (lightened for contrast) #E06666
#define COLOR_BAR_FILL         0x4DB6  // Dashboard progress-bar teal #4FB6B2
#define COLOR_BAR_BG           0x1A26  // Dark green bar track #1D4437
#define COLOR_DIVIDER          0x2B0A  // Green separator line #2E6350
#define COLOR_HIGHLIGHT        0x4DB6  // Dashboard active/selected teal #4FB6B2
#define COLOR_MENU_SEL_BG      0x1247  // Dashboard header green selection bg #174A3A

// ============================================================
//  DFPLAYER AUDIO FILE MAP
// ============================================================
//  Tracks on microSD match /mp3/0001.mp3 through /mp3/0011.mp3:
//    0001 = Step 1 ("Step One — Starting")
//    0002 = Step 2 ("Step Two — Cooling")
//    0003 = Step 3 ("Step Three — Deep Freeze")
//    0004 = Step 4 ("Step Four — Stabilizing")
//    0005 = Emergency ("Warning — Temperature Dropping")
//    0006 = Process complete ("Process Completed Successfully")
//    0007 = Welcome ("Welcome to THULIR CryoLab")
//    0008 = Wizard 1 ("Enter Step One Duration")
//    0009 = Wizard 2 ("Enter Step Two Duration")
//    0010 = Wizard 3 ("Enter Step Three Duration")
//    0011 = Wizard 4 ("Enter Step Four Duration")
#include "hmi_audio.h"

// Backwards compatibility aliases
#define AUDIO_SYSTEM_START     AUDIO_WELCOME
#define AUDIO_EMERGENCY_STOP   AUDIO_EMERGENCY
#define AUDIO_SENSOR_ERROR     AUDIO_EMERGENCY
#define AUDIO_OVER_TEMP        AUDIO_EMERGENCY

#define DFPLAYER_VOLUME        25      // 0–30


// ============================================================
//  NVS STORAGE KEYS
// ============================================================
#define NVS_NAMESPACE          "thulir"
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
    ERROR_EMERGENCY_STOP,
    ERROR_RAMP_TIMEOUT
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
    // Recipe actually in effect on the device (reported to the dashboard so
    // its reference profile follows keypad edits). Filled every loop().
    uint16_t    recipeHold[4];        // hold minutes, steps 1-4
    float       recipeT2;             // step 2 target
    float       recipeT4;             // step 4 target

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

    // Cold-side humidity (SHT3x, display-only — not used by PID/safety)
    float       humidity;
    bool        humidityValid;

    // Menu state
    uint8_t     menuSelection;
    uint8_t     editStep;             // Which step is being edited (0–4)

    // Test mode
    PeltierStage testStage;
    float        testPWM;
    bool         testActive;

    // Servo state
    bool         servosOpen;          // Are both servos currently open?
    bool         servoStepTimerActive;// Is the step-open timed window running?
    unsigned long servoStepOpenTime;  // millis() when the step window started
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
        case ERROR_RAMP_TIMEOUT:      return "RAMP TIMEOUT";
        default:                      return "UNKNOWN ERROR";
    }
}