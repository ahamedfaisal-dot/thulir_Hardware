# THULIR — 3-Stage Cascaded Peltier Temperature Controller

Production-quality firmware for ESP32-S3 driving three cascaded Peltier modules via BTS7960 H-bridge drivers with PID control, TFT HMI, keypad programming, voice announcements, dual servo motors, and comprehensive safety. Cold-side sensing is an I2C SHT3x (temperature + humidity).A companion Python Flask web dashboard streams live telemetry, logs to Excel, and runs on-device ML predictions for seed viability and storage life extension.

---

## Table of Contents

1. [System Architecture](#system-architecture)
2. [Wiring Tables](#wiring-tables)
3. [Power Wiring](#power-wiring)
4. [GPIO Pin Map](#gpio-pin-map)
5. [Required Libraries](#required-libraries)
6. [File Structure](#file-structure)
7. [Process Flow](#process-flow)
8. [PID Tuning Guide](#pid-tuning-guide)
9. [Commissioning Procedure](#commissioning-procedure)
10. [Troubleshooting](#troubleshooting)
11. [Safety Notices](#safety-notices)
12. [Web Dashboard &amp; Seed Intelligence](#web-dashboard--seed-intelligence)

---

## System Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                         ESP32-S3                                   │
│                                                                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ PID      │  │ Temp     │  │ Recipe   │  │ Safety   │          │
│  │Controller│  │ Manager  │  │ Manager  │  │ Manager  │          │
│  └─────┬────┘  └─────┬────┘  └──────────┘  └─────┬────┘          │
│        │              │                           │                │
│  ┌─────▼────────────────────────────────────────────────┐         │
│  │              Main State Machine                       │         │
│  │   BOOT → IDLE → APPROACH → HOLD → RAMP → COMPLETE   │         │
│  └─────┬────────────────────────────────┬────────┬──────┘         │
│        │                                │        │                 │
│  ┌─────▼────┐  ┌──────────┐  ┌────────▼──┐  ┌──▼──────┐         │
│  │ Peltier  │  │ Display  │  │ Keypad    │  │ Audio   │          │
│  │ Control  │  │ Manager  │  │ Manager   │  │ Manager │          │
│  └─────┬────┘  └─────┬────┘  └─────┬─────┘  └────┬────┘         │
└────────┼──────────────┼─────────────┼──────────────┼──────────────┘
         │              │             │              │
    ┌────▼────┐   ┌─────▼────┐  ┌────▼────┐  ┌─────▼────┐
    │ 3×      │   │ ILI9341  │  │ 4×4     │  │ DFPlayer │
    │ BTS7960 │   │ 320×240  │  │ Keypad  │  │ Mini     │
    └────┬────┘   └──────────┘  └─────────┘  └──────────┘
         │
    ┌────▼────────────────┐
    │ 3× Peltier Modules  │
    │ (Cascaded Stack)    │
    │ BOT → MID → TOP    │
    └─────────────────────┘
```

---

## Wiring Tables

> The table below is the **authoritative, verified-working** pinout as of the
> final hardware bring-up (TFT confirmed working via `Adafruit_ILI9341`).
> It matches `Config.h` exactly — if the two ever disagree, trust `Config.h`
> and update this table.

### Low-Current Logic Connections (3.3V / Signal Level)

| Component                                  | Component Pin | ESP32-S3 GPIO              | Notes                                                                                                                                                                     |
| ------------------------------------------ | ------------- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **BTS7960 #1 (Bottom)**              | RPWM          | GPIO 4                     | PWM signal                                                                                                                                                                |
|                                            | LPWM          | **GND** (hardwired)  | Always LOW — wire directly to GND; no GPIO used                                                                                                                          |
|                                            | R_EN          | **3.3V** (hardwired) | Always HIGH — wire directly to 3.3V; no GPIO used                                                                                                                        |
|                                            | L_EN          | **3.3V** (hardwired) | Always HIGH — wire directly to 3.3V; no GPIO used                                                                                                                        |
|                                            | VCC           | 3.3V                       | Logic supply                                                                                                                                                              |
|                                            | GND           | GND                        | Common ground                                                                                                                                                             |
| **BTS7960 #2 (Middle)**              | RPWM          | GPIO 8                     | PWM signal                                                                                                                                                                |
|                                            | LPWM          | **GND** (hardwired)  | Always LOW — wire directly to GND; no GPIO used                                                                                                                          |
|                                            | R_EN          | **3.3V** (hardwired) | Always HIGH — wire directly to 3.3V; no GPIO used                                                                                                                        |
|                                            | L_EN          | **3.3V** (hardwired) | Always HIGH — wire directly to 3.3V; no GPIO used                                                                                                                        |
|                                            | VCC           | 3.3V                       | Logic supply                                                                                                                                                              |
|                                            | GND           | GND                        | Common ground                                                                                                                                                             |
| **BTS7960 #3 (Top)**                 | RPWM          | **GPIO 42**          | PWM signal (moved from GPIO12 to free native FSPI SCK)                                                                                                                    |
|                                            | LPWM          | **GND** (hardwired)  | Always LOW — wire directly to GND; no GPIO used                                                                                                                          |
|                                            | R_EN          | **3.3V** (hardwired) | Always HIGH — wire directly to 3.3V; no GPIO used                                                                                                                        |
|                                            | L_EN          | **3.3V** (hardwired) | Always HIGH — wire directly to 3.3V; no GPIO used                                                                                                                        |
|                                            | VCC           | 3.3V                       | Logic supply                                                                                                                                                              |
|                                            | GND           | GND                        | Common ground                                                                                                                                                             |
| **SHT3x (Cold side, Temp+Humidity)** | VIN           | 3.3V                       | 2.2–5.5V tolerant sensor, powered at 3.3V here                                                                                                                           |
|                                            | GND           | GND                        |                                                                                                                                                                           |
|                                            | SDA           | **GPIO 35**          | GPIO3 (strapping pin, broke uploads) and GPIO33 (not broken out on this board) were tried first                                                                           |
|                                            | SCL           | **GPIO 36**          | Confirmed present and free on this board's actual pinout diagram                                                                                                          |
| **DS18B20 (Hot side, optional)**     | DATA          | GPIO 37                    | Not installed by default — set`HOT_SIDE_SENSOR_ENABLED true` in Config.h to use. Moved from GPIO43 — OneWire hung the chip on that pin (ESP32-S3's default UART0 pin) |
|                                            | VCC           | 3.3V                       |                                                                                                                                                                           |
|                                            | GND           | GND                        |                                                                                                                                                                           |
| **TFT ILI9341**                      | SCK           | **GPIO 12**          | Driven via`Adafruit_ILI9341` + explicit `SPIClass(FSPI)`                                                                                                              |
|                                            | MOSI (SDI)    | **GPIO 11**          |                                                                                                                                                                           |
|                                            | MISO (SDO)    | **GPIO 13**          | Used for panel ID readback during bring-up diagnostics                                                                                                                    |
|                                            | CS            | **GPIO 10**          | Chip select                                                                                                                                                               |
|                                            | DC            | **GPIO 9**           | Data/Command                                                                                                                                                              |
|                                            | RST           | **GPIO 14**          | Reset                                                                                                                                                                     |
|                                            | VCC           | **5V**               | This module needs 5V — 3.3V leaves the onboard regulator without enough headroom to init                                                                                 |
|                                            | GND           | GND                        |                                                                                                                                                                           |
|                                            | LED           | 3.3V                       | Backlight (always on)                                                                                                                                                     |
| **4×4 Keypad**                      | ROW0          | GPIO 16                    |                                                                                                                                                                           |
|                                            | ROW1          | GPIO 17                    |                                                                                                                                                                           |
|                                            | ROW2          | GPIO 18                    |                                                                                                                                                                           |
|                                            | ROW3          | GPIO 38                    | (Moved from 19 — USB)                                                                                                                                                    |
|                                            | COL0          | GPIO 20                    |                                                                                                                                                                           |
|                                            | COL1          | GPIO 39                    |                                                                                                                                                                           |
|                                            | COL2          | GPIO 40                    |                                                                                                                                                                           |
|                                            | COL3          | GPIO 41                    |                                                                                                                                                                           |
| **DFPlayer Mini**                    | RX            | GPIO 1 (ESP TX)            | 1kΩ series resistor recommended                                                                                                                                          |
|                                            | TX            | GPIO 2 (ESP RX)            |                                                                                                                                                                           |
|                                            | VCC           | 5V                         |                                                                                                                                                                           |
|                                            | GND           | GND                        |                                                                                                                                                                           |
| **Servo 1**                          | Signal        | GPIO 22                    | LEDC ch 3, 50 Hz PWM, 500–2400 µs pulse width                                                                                                                           |
|                                            | VCC           | **5V**               | Hobby servos (SG90/MG90S) require 5V — do**not** use 3.3V                                                                                                          |
|                                            | GND           | GND                        |                                                                                                                                                                           |
| **Servo 2**                          | Signal        | GPIO 23                    | LEDC ch 4, 50 Hz PWM, 500–2400 µs pulse width                                                                                                                           |
|                                            | VCC           | **5V**               | Shared 5V rail with Servo 1                                                                                                                                               |
|                                            | GND           | GND                        |                                                                                                                                                                           |

> **BTS7960 LPWM / R_EN / L_EN — all three hardwired (actual build):**
> All six enable/direction pins across the three BTS7960 boards are directly
> wired to fixed rails — no GPIO is used for them:
>
> - **LPWM** → GND (always LOW, enforces unidirectional cooling)
> - **R_EN** → 3.3V (always enabled)
> - **L_EN** → 3.3V (always enabled)
>   Only the three **RPWM** lines (GPIO 4, 8, 42) are controlled by the
>   ESP32. This matches `Config.h` which has no LPWM/R_EN/L_EN `#define`s
>   for these hardwired pins.

### High-Current Power Connections (12V)

| Connection                         | Wire Gauge | Fuse      | Notes                    |
| ---------------------------------- | ---------- | --------- | ------------------------ |
| SMPS 12V → BTS7960#1 B+           | ≥14 AWG   | 15A blade | Bottom Peltier (12A max) |
| BTS7960#1 M+ → Bottom Peltier +   | ≥14 AWG   | —        | Observe polarity!        |
| BTS7960#1 M− → Bottom Peltier − | ≥14 AWG   | —        |                          |
| SMPS 12V → BTS7960#2 B+           | ≥16 AWG   | 10A blade | Middle Peltier (6A max)  |
| BTS7960#2 M+ → Middle Peltier +   | ≥16 AWG   | —        |                          |
| BTS7960#2 M− → Middle Peltier − | ≥16 AWG   | —        |                          |
| SMPS 12V → BTS7960#3 B+           | ≥16 AWG   | 10A blade | Top Peltier (6A max)     |
| BTS7960#3 M+ → Top Peltier +      | ≥16 AWG   | —        |                          |
| BTS7960#3 M− → Top Peltier −    | ≥16 AWG   | —        |                          |
| SMPS 12V → 4× Fans               | ≥16 AWG   | 5A        | Hardwired, always on     |
| SMPS GND → All BTS7960 B−        | ≥12 AWG   | —        | Star ground recommended  |

---

## Power Wiring

```
12V 30A SMPS
     │
     ├──── [15A Fuse] ──── BTS7960 #1 B+/B− ──── Bottom Peltier (12V/12A)
     │
     ├──── [10A Fuse] ──── BTS7960 #2 B+/B− ──── Middle Peltier (12V/6A)
     │
     ├──── [10A Fuse] ──── BTS7960 #3 B+/B− ──── Top Peltier   (12V/6A)
     │
     ├──── [5A Fuse]  ──── 4× Cooling Fans (hardwired)
     │
     └──── 12V→5V Buck ──── ESP32-S3 5V input
            Converter        (or use USB-C for dev)

    ⚠ CRITICAL GROUND RULES:
    • All BTS7960 B− terminals connect to SMPS GND
    • All BTS7960 logic GND pins connect to ESP32 GND
    • ESP32 GND must have a common reference with SMPS GND
    • Use a star-ground topology to minimize ground loops
    • NEVER route 12V/12A through signal-level PCB traces
```

---

## GPIO Pin Map

See `Config.h` for the authoritative, editable pin definitions.

**ESP32-S3 Pins to Avoid:**

- GPIO 0: Boot strapping pin (download mode) — avoid pull-ups/downs
- GPIO 3: Boot strapping pin (JTAG signal source) — **learned the hard
  way**: this was originally used for the SHT3x's I2C SDA, and its
  external pull-up resistor altered this pin's strap state on every
  reset, breaking `esptool`'s USB download-mode handshake entirely
  ("Wrong boot mode detected (0x4)", uploads failed completely). Avoid
  any pull-up/pull-down on GPIO3.
- GPIO 19: USB D− on DevKitC-1
- GPIO 26–32: Do not exist on ESP32-S3 (internal flash)
- GPIO 33, 34: **Not broken out at all on this ESP32-S3-DevKitC-1 board**
  (confirmed from the board's own pinout diagram — these two pins simply
  don't appear on the header). This module reserves them internally for
  PSRAM. Also tried and abandoned for the SHT3x before settling on 35/36.
- GPIO 35–37: Present on this board's header and genuinely free — the
  fact that the board omits 33/34 but keeps 35-37 confirms this module
  only needs 33/34 for PSRAM. **This firmware uses GPIO 35/36 for the
  SHT3x's SDA/SCL, and GPIO 37 for the optional hot-side DS18B20.**
- GPIO 43, 44: Default UART0 TX/RX. With **USB Mode: "Hardware CDC and
  JTAG"** (as specified below), `Serial` runs over native USB instead of
  UART0, and GPIO44 works fine as a plain GPIO output (used here for
  BTS7960 #3 R_EN) — but **GPIO43 hung the chip when OneWire tried to
  use it** during hot-side sensor bring-up (see `DS18B20_Test/`), likely
  because the ESP32-S3 boot ROM itself still drives GPIO43 for its own
  UART0 boot-log output before the app takes over, regardless of the
  USB Mode setting. Avoid GPIO43 for anything timing-sensitive; the
  optional hot-side sensor now uses GPIO37 instead.
- GPIO 45: Boot strapping pin (VDD_SPI voltage select) — do not use with a
  pull-up/pull-down; pulling it high at boot can select the wrong VDD_SPI
  voltage and break flash access on some modules.
- GPIO 46: Boot strapping pin (ROM boot-log verbosity) — lower risk than
  GPIO0/3/45, but not used by this firmware (freed back up when the SHT3x
  moved to GPIO35/36).

**GPIO Summary for this firmware:**

| GPIO | Function                    | Direction |
| ---- | --------------------------- | --------- |
| 1    | DFPlayer RX (ESP TX)        | Output    |
| 2    | DFPlayer TX (ESP RX)        | Input     |
| 4    | BTS7960#1 RPWM (Bottom PWM) | Output    |
| 8    | BTS7960#2 RPWM (Middle PWM) | Output    |
| 9    | TFT DC                      | Output    |
| 10   | TFT CS                      | Output    |
| 11   | TFT MOSI                    | Output    |
| 12   | TFT SCK                     | Output    |
| 13   | TFT MISO                    | Input     |
| 14   | TFT RST                     | Output    |
| 16   | Keypad ROW0                 | Output    |
| 17   | Keypad ROW1                 | Output    |
| 18   | Keypad ROW2                 | Output    |
| 20   | Keypad COL0                 | Input     |
| 22   | Servo 1 PWM signal          | Output    |
| 23   | Servo 2 PWM signal          | Output    |
| 35   | SHT3x SDA                   | I/O       |
| 36   | SHT3x SCL                   | I/O       |
| 38   | Keypad ROW3                 | Output    |
| 39   | Keypad COL1                 | Input     |
| 40   | Keypad COL2                 | Input     |
| 41   | Keypad COL3                 | Input     |
| 42   | BTS7960#3 RPWM (Top PWM)    | Output    |
| 43   | DS18B20 hot-side (optional) | I/O       |

**Why the TFT uses GPIO 9/10/11/12/13/14:**

- These are the pins proven to actually work with this ILI9341 panel on this
  ESP32-S3 board (verified across two independent working projects, `igem`
  and `sih2026_input`, and by a register-ID readback test in this project).
- **GPIO 11 (MOSI)**: BTS7960 all enable/direction pins are hardwired —
  no GPIO needed for LPWM/R_EN/L_EN, freeing GPIO 11 for TFT MOSI.
- **GPIO 12 (SCK)**: BTS7960 #3 `TOP RPWM` was moved to GPIO 42, freeing
  GPIO 12 for TFT SCK.
- **GPIO 9, 10, 13, 14 (DC/CS/MISO/RST)**: Entirely free since all BTS7960
  enable lines are hardwired to 3.3V/GND; no GPIO pins wasted on them.

---

## Required Libraries

Install via Arduino Library Manager:

| Library                          | Author          | Version | Purpose                                                          |
| -------------------------------- | --------------- | ------- | ---------------------------------------------------------------- |
| **Adafruit_ILI9341**       | Adafruit        | ≥1.5   | TFT display driver                                               |
| **Adafruit_GFX_Library**   | Adafruit        | ≥1.11  | Graphics primitives (required by Adafruit_ILI9341)               |
| **Adafruit_SHT31_Library** | Adafruit        | ≥2.2   | Cold-side temperature + humidity sensor (I2C)                    |
| **Adafruit_BusIO**         | Adafruit        | ≥1.14  | I2C/SPI transport (dependency of Adafruit_SHT31_Library)         |
| OneWire                          | Paul Stoffregen | ≥2.3   | 1-Wire protocol — only needed for the optional hot-side DS18B20 |
| DallasTemperature                | Miles Burton    | ≥3.9   | DS18B20 driver — only needed for the optional hot-side sensor   |
| Keypad                           | Mark Stanley    | ≥3.1   | Matrix keypad                                                    |
| DFRobotDFPlayerMini              | DFRobot         | ≥1.0.5 | Audio player                                                     |

> ⚠ **Do not use TFT_eSPI for the display.** It was tried first (it's a
> capable, widely-used library) but on this specific ESP32-S3 board this
> ILI9341 panel never responded to it — confirmed by a register-ID read
> returning `0x00 0x00 0x00` on every attempt, even with correct pins,
> power, and ground. `Adafruit_ILI9341` works perfectly on the exact same
> wiring, so `DisplayManager` uses that instead. No `User_Setup.h` /
> library-folder config file is needed — pins are passed directly in
> `DisplayManager::begin()` (see `Config.h`).

**Board:** ESP32S3 Dev Module (Arduino-ESP32 Core **2.x**)

**Arduino IDE Board Settings:**

- Board: "ESP32S3 Dev Module"
- USB Mode: "Hardware CDC and JTAG" (for Serial Monitor)
- Flash Size: 8MB (or as per your module)
- Partition Scheme: "Default 4MB with spiffs"
- PSRAM: "OPI PSRAM" if your module has R8 suffix, otherwise "Disabled"

---

## File Structure

```
thulir_final/
├── thulir_final.ino      — Main sketch: setup(), loop(), state machine
│                           (also holds TEST_MODE, a build-time switch for
│                           the Adafruit_ILI9341 display-only bring-up test)
├── Config.h              — All GPIO pins, constants, defaults, tunables
├── PIDController.h/cpp   — PID algorithm (anti-windup, derivative-on-measurement)
├── PeltierControl.h/cpp  — BTS7960 driver, LEDC PWM, cascade power distribution
├── TemperatureManager.h/cpp — SHT3x temp+humidity reads (+ optional DS18B20), filtering
├── RecipeManager.h/cpp   — 5-step recipe, NVS persistence
├── KeypadManager.h/cpp   — Non-blocking scan, numeric input FSM
├── DisplayManager.h/cpp  — ILI9341 HMI screens via Adafruit_ILI9341, selective redraw
├── AudioManager.h/cpp    — DFPlayer Mini non-blocking announcements
├── SafetyManager.h/cpp   — Fault detection, emergency stop
├── ServoManager.h/cpp    — Dual servo motor control (open/close on process events)
├── WebManager.h/cpp      — WiFi telemetry (non-blocking HTTP POST)
└── README.md             — This file
```

---

## Process Flow

```
POWER ON
   ↓
SERVO OPEN (both servos open — boot sequence)
   ↓
SELF TEST (sensor, display, audio)
   ↓
IDLE (Home Screen)
   ↓
USER PROGRAMS RECIPE (D → Menu → Program)
   ↓
RECIPE SAVED (#)
   ↓
READY (Menu → Start → Confirm)
   ↓
SENSOR CHECK ──── FAIL ──→ FAULT SCREEN → SERVO OPEN
   ↓ PASS
STEP 1: 25°C
   ↓ approach → confirm (30s in band) → hold (timer)
   ↓ hold complete → SERVO OPEN 15 s → auto-close
STEP 2: 0/15/25°C
   ↓ approach → confirm → hold
   ↓ hold complete → SERVO OPEN 15 s → auto-close
STEP 3: 4°C
   ↓ approach → confirm → hold
   ↓ hold complete → SERVO OPEN 15 s → auto-close
STEP 4: 0/25°C
   ↓ approach → confirm → hold
   ↓ hold complete → SERVO OPEN 15 s → auto-close
STEP 5: RAMP −1°C/min
   ↓ continuous setpoint ramp
−20°C REACHED
   ↓
PROCESS COMPLETE
   ↓
PELTIERS OFF
   ↓
SERVO CLOSE (chamber sealed)
   ↓
IDLE

EMERGENCY STOP / FAULT → PELTIERS OFF → SERVO OPEN (safe egress)
```

---

## PID Tuning Guide

### Initial Conservative Values (Config.h defaults)

```
Kp = 15.0   — Proportional gain
Ki =  0.5   — Integral gain
Kd =  5.0   — Derivative gain
```

> ⚠ These are PLACEHOLDER values. You MUST tune them experimentally.

> **Integral separation:** the integral term only accumulates while
> `|actualTemp - setpoint|` is within `PID_INTEGRAL_ZONE_DEG` (default
> 3.0°C, in Config.h). This prevents windup from building during a long
> approach from far away (e.g. ambient temperature down to a step's
> target) — without it, output can stay pegged near-max well after
> crossing the setpoint, because a large accumulated integral bleeds off
> very slowly against a now-small error. If you still see this after
> tuning Kp/Ki/Kd, try narrowing `PID_INTEGRAL_ZONE_DEG`.

### Tuning Procedure

1. **Open the PID screen** (Menu → PID TUNE)
2. **Start with P-only control**: Set Ki=0, Kd=0
3. **Set a target** (e.g., 10°C) and start the process
4. **Increase Kp** until you see slight oscillation around the target
5. **Reduce Kp by ~30%** from the oscillation point
6. **Add Ki** slowly (start at 0.1) to eliminate steady-state error
7. **Add Kd** (start at 2.0) to damp overshoot
8. **Save to NVS** when satisfied (press # on PID screen)

### Key Observations

- **Large overshoot**: Reduce Kp, increase Kd
- **Slow approach**: Increase Kp
- **Steady-state offset**: Increase Ki (small increments!)
- **Oscillation**: Reduce Kp and Ki, increase Kd
- **Ramp lag (Step 5)**: System may need higher Kp for aggressive ramp tracking

### Tuning for Different Temperature Ranges

The Peltier stack behaves differently at different temperatures:

- Near ambient (25°C): Low power needed, PID may need lower gains
- Near 0°C: Moderate power, standard gains
- Below −10°C: High power, COP drops, may need aggressive gains

Consider tuning at your most critical operating point (likely Step 5 ramp).

---

## Commissioning Procedure

### Phase 1: Basic Hardware Verification

| # | Step          | Procedure                                    | Expected Result                                   |
| - | ------------- | -------------------------------------------- | ------------------------------------------------- |
| 1 | Test ESP32-S3 | Upload blink sketch, check Serial Monitor    | Serial output at 115200 baud                      |
| 2 | Test TFT      | Upload THULIR firmware, check for boot screen | "THULIR Initializing..." on display                |
| 3 | Test Keypad   | Press keys on HOME screen, go to Menu        | Keys register, menu navigates                     |
| 4 | Test SHT3x    | Check boot log for sensor detection          | "SHT3x initialized" with a plausible temp/RH pair |
| 5 | Test DFPlayer | Menu → Test → 4 (Test DFPlayer)            | "System starting" audio plays                     |

### Phase 2: Motor Driver Verification (No Peltier Connected)

| # | Step      | Procedure                                                     | Expected Result                                       |
| - | --------- | ------------------------------------------------------------- | ----------------------------------------------------- |
| 6 | Test Fans | Power on SMPS, check fans spin                                | 4 fans running                                        |
| 7 | BTS7960#1 | Disconnect Peltier. Test Mode → 5 (Bottom). Set 10%, press # | Measure PWM on M+/M− with oscilloscope or multimeter |
| 8 | BTS7960#2 | Test Mode → 6 (Middle). Set 10%, press #                     | PWM output on middle BTS7960                          |
| 9 | BTS7960#3 | Test Mode → 7 (Top). Set 10%, press #                        | PWM output on top BTS7960                             |

### Phase 3: Individual Peltier Testing

> ⚠ Connect ONE Peltier at a time. Use LOW PWM (10-20%).

| #  | Step           | Procedure                                     | Expected Result                             |
| -- | -------------- | --------------------------------------------- | ------------------------------------------- |
| 10 | Bottom Peltier | Connect, Test Mode → 5, 10% PWM              | Cold side gets slightly cool, hot side warm |
| 11 | Middle Peltier | Connect, Test Mode → 6, 10% PWM              | Slight cooling effect                       |
| 12 | Top Peltier    | Connect, Test Mode → 7, 10% PWM              | Slight cooling at top                       |
| 13 | Bottom only    | Run Step 1 (25°C) with only bottom connected | PID controls to 25°C                       |

### Phase 4: Cascade Testing

| #  | Step                 | Procedure                                      | Expected Result                   |
| -- | -------------------- | ---------------------------------------------- | --------------------------------- |
| 14 | Middle + Bottom      | Connect bottom and middle, test at low power   | Enhanced cooling vs. bottom alone |
| 15 | Full cascade         | Connect all three, test at low power           | Further cooling improvement       |
| 16 | Temperature response | Plot temperature vs. time at various setpoints | Characterize thermal response     |

### Phase 5: PID Tuning

| #  | Step         | Procedure                                 | Expected Result                   |
| -- | ------------ | ----------------------------------------- | --------------------------------- |
| 17 | Characterize | Run open-loop tests at various PWM levels | Understand thermal time constants |
| 18 | Tune PID     | Follow PID Tuning Guide above             | Stable temperature control        |

### Phase 6: Process Testing

| #  | Step        | Procedure                                    | Expected Result                 |
| -- | ----------- | -------------------------------------------- | ------------------------------- |
| 19 | Test Step 1 | Run with default recipe, monitor only Step 1 | Holds 25°C for programmed time |
| 20 | Test Step 2 | Allow Step 2 to run (15°C default)          | Approaches and holds 15°C      |
| 21 | Test Step 3 | Test 4°C target                             | Stable hold at 4°C             |
| 22 | Test Step 4 | Test 0°C target                             | Stable hold at 0°C             |
| 23 | Test Step 5 | Full ramp test to −20°C                    | Smooth ramp at −1°C/min       |

### Phase 7: Safety Testing

| #  | Step              | Procedure                          | Expected Result                      |
| -- | ----------------- | ---------------------------------- | ------------------------------------ |
| 24 | Emergency Stop    | Press C → D during active process | Peltiers off, EMERGENCY STOP screen  |
| 25 | Sensor disconnect | Unplug SHT3x during operation      | FAULT screen, Peltiers off           |
| 26 | Power restart     | Power cycle during operation       | System boots to IDLE, no auto-resume |

---

## Troubleshooting

### Display Issues

| Problem                                                               | Possible Cause                                                                                                                                         | Solution                                                                                                                                                                                                                                                                                                                                                                                                                       |
| --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| No backlight at all                                                   | Display VCC/LED not powered                                                                                                                            | Confirm VCC is on**5V** and LED is on 3.3V — a floating/unpowered rail gives a totally dark panel                                                                                                                                                                                                                                                                                                                       |
| Backlight on, screen white                                            | Panel powered but not initializing                                                                                                                     | Verify wiring against the table above; run the`TEST_MODE 2` bring-up test in `thulir_final.ino` and check `[TEST] Display ID bytes` in Serial — `0x00 0x00 0x00` means the panel isn't responding at all                                                                                                                                                                                                              |
| Backlight on, screen black (static, never changes)                    | Same as above — panel at its power-up default, not receiving commands                                                                                 | Same as above                                                                                                                                                                                                                                                                                                                                                                                                                  |
| PSRAM pin conflict                                                    | Using GPIO 33/34 for other signals                                                                                                                     | This board doesn't even break these out — confirmed unusable regardless. GPIO 35–37 are confirmed free and safe on this board                                                                                                                                                                                                                                                                                                |
| Garbled display                                                       | Wrong rotation                                                                                                                                         | Change`setRotation()` in `DisplayManager::begin()`                                                                                                                                                                                                                                                                                                                                                                         |
| Wrong colors                                                          | RGB vs BGR byte order                                                                                                                                  | Adafruit_ILI9341 defaults to RGB; check the panel datasheet if colors look swapped                                                                                                                                                                                                                                                                                                                                             |
| Flickering                                                            | Full redraw too often                                                                                                                                  | Increase`DISPLAY_UPDATE_INTERVAL` in Config.h                                                                                                                                                                                                                                                                                                                                                                                |
| Turns white whenever the 12V system is powered (even at 0% PWM, idle) | Noise/ground-bounce from the SMPS itself (not specifically PWM switching) glitching the RST line — a real hardware reset of the panel, not a code bug | Firmware now self-heals this automatically (see watchdog note below) within ~12-16s. For a permanent fix: strengthen the common ground bond between the ESP32 and the 12V/SMPS domain (single short, thick, direct wire — not a long/daisy-chained one), add a 100nF-1µF capacitor from RST to GND right at the display, route SPI/RST wiring away from the 12V wiring, and add bulk capacitance directly on the SMPS output |

> This project moved from `TFT_eSPI` to `Adafruit_ILI9341` after `TFT_eSPI`
> never got a response from this specific panel on this specific ESP32-S3
> board (confirmed by reading the panel's ID register and getting all
> zeros, across many verified-correct wiring/power/pin configurations).
> If a display "should" work per every check above but still doesn't,
> trying the other driver library is a legitimate next step — see
> `TEST_MODE 2` in `thulir_final.ino` for a ready-made isolated test.

> **Display watchdog:** only active while a process is actually running
> (`STATE_STEP_APPROACH`/`STEP_HOLD`/`STEP5_RAMP`/`STEP_TRANSITION`) —
> that's when the 12V system is doing real work and the display actually
> matters, so there's no reason to poll it while idle. Once a process
> starts, `thulir_final.ino`'s main loop checks the panel's health every
> 4s (`DISPLAY_HEALTH_CHECK_INTERVAL_MS`) via
> `DisplayManager::isAlive()`, which itself reads the panel's ID
> registers twice (5ms apart) before reporting a failure — since that
> readback travels over the same noisy SPI/MISO lines as everything
> else, a single bad read can be the *check* glitching, not the panel,
> and this filters that out. After 3 consecutive failed checks
> (`DISPLAY_HEALTH_FAIL_THRESHOLD`, ~12-16s), it silently calls
> `displayMgr.begin(false)` (re-init without the splash screen/delay) and
> redraws the current screen — recovering automatically from a real
> RST-glitch reset instead of needing a manual reboot, without
> over-triggering on routine noise. This is a mitigation, not a fix for
> the underlying noise —
> still do the hardware fixes above if this triggers often.

### Temperature / Humidity Sensor Issues (SHT3x)

| Problem                | Possible Cause                      | Solution                                                                                                 |
| ---------------------- | ----------------------------------- | -------------------------------------------------------------------------------------------------------- |
| "SHT3x not found"      | Bad wiring or wrong address         | Check VIN/GND/SDA/SCL; confirm`SHT3X_I2C_ADDR` matches the board (0x44 default, 0x45 if ADDR pin high) |
| Reads NaN / invalid    | I2C bus glitch or bad wiring        | Check SDA/SCL continuity and pull-ups; the code already rejects NaN reads                                |
| Humidity shows "RH:--" | Same I2C failure as above           | Fix the I2C connection — humidity uses the same read as temperature                                     |
| Noisy readings         | Electrical interference             | Keep I2C wires short and away from the 12A/6A/6A power wiring                                            |
| Sudden jump rejected   | Real glitch, or threshold too tight | See`TEMP_INVALID_THRESH` in Config.h if legitimate fast changes are being rejected                     |

### Optional Hot-Side DS18B20 Issues

| Problem            | Possible Cause                                        | Solution                                                             |
| ------------------ | ----------------------------------------------------- | -------------------------------------------------------------------- |
| "Sensor not found" | Bad wiring, or`HOT_SIDE_SENSOR_ENABLED` still false | Check DATA/VCC/GND, verify 4.7kΩ pull-up, flip the flag in Config.h |
| Reads −127°C     | Disconnected sensor                                   | Check connections                                                    |
| Reads 85°C        | Power-on reset value                                  | Sensor not initializing — check power                               |

### Peltier / BTS7960 Issues

| Problem                   | Possible Cause         | Solution                                       |
| ------------------------- | ---------------------- | ---------------------------------------------- |
| No cooling effect         | Wrong Peltier polarity | Swap + and − on Peltier (cool/hot sides flip) |
| BTS7960 overheating       | Over-current           | Check fuses, reduce PWM max limits             |
| Audible buzzing           | PWM frequency too low  | Increase PWM_FREQUENCY in Config.h             |
| Erratic PWM               | GPIO conflict          | Check pin assignments, avoid strapping pins    |
| Peltier hot on both sides | LPWM not LOW           | Verify LPWM pin is wired and held LOW          |

### DFPlayer Issues

| Problem           | Possible Cause     | Solution                                          |
| ----------------- | ------------------ | ------------------------------------------------- |
| "Not detected"    | TX/RX swapped      | ESP TX → DFPlayer RX (and vice versa)            |
| No audio          | Volume too low     | Increase DFPLAYER_VOLUME in Config.h              |
| Wrong track plays | File numbering     | Files must be /mp3/0001.mp3 through /mp3/0012.mp3 |
| Distorted audio   | Power supply noise | Add 1000µF capacitor on DFPlayer VCC             |

### Keypad Issues

| Problem              | Possible Cause    | Solution                                  |
| -------------------- | ----------------- | ----------------------------------------- |
| Keys not registering | Wrong pin mapping | Verify ROW/COL pins in Config.h           |
| Multiple keys ghost  | Missing diodes    | Use a keypad with built-in diodes         |
| Slow response        | Debounce too high | Reduce debounce time in KeypadManager.cpp |

### PID Control Issues

| Problem           | Possible Cause             | Solution                             |
| ----------------- | -------------------------- | ------------------------------------ |
| Oscillation       | Kp too high                | Reduce Kp by 30-50%                  |
| Slow approach     | Kp too low                 | Increase Kp gradually                |
| Steady offset     | Ki too low                 | Increase Ki in small steps           |
| Overshoot         | Kd too low, Ki too high    | Increase Kd, decrease Ki             |
| Ramp lag (Step 5) | Insufficient cooling power | Normal at very low temps (COP drops) |

---

## Safety Notices

### ⚠ Electrical Safety

- **Fuses are mandatory.** Without fuses, a short circuit can cause fire.
- **Use adequate wire gauge.** 12A through thin wire = fire hazard.
- **Install a physical emergency cutoff.** Software alone is not sufficient.
- **Common ground is critical.** Floating grounds cause erratic BTS7960 behavior.
- **Never power ESP32-S3 from 12V directly.** Use a regulated 5V/3.3V supply.

### ⚠ Thermal Safety

- **Hot-side monitoring is strongly recommended.** The bottom Peltier's hot side can exceed 80°C under load. Set `HOT_SIDE_SENSOR_ENABLED = true` in Config.h and add a DS18B20 to the heatsink.
- **Adequate heatsink is critical.** Without proper heat dissipation, Peltier efficiency drops and thermal runaway is possible.
- **Use thermal interface material (TIM)** between all Peltier surfaces and heatsinks.
- **Never run full cascade at 100% PWM** without verified thermal management.

### ⚠ Software Limitations

- The ESP32 **cannot detect overcurrent** without external current sensors.
- Software PWM limits are a **secondary safeguard only**.
- The firmware **does not** automatically resume after power failure.
- The firmware **does not** provide galvanic isolation or true fail-safe shutdown.

---

## DFPlayer Audio File Map

Place these MP3 files on the microSD card in the `/mp3/` directory:

```
/mp3/0001.mp3 — "Step One — Starting" (AUDIO_STEP1)
/mp3/0002.mp3 — "Step Two — Cooling" (AUDIO_STEP2)
/mp3/0003.mp3 — "Step Three — Deep Freeze" (AUDIO_STEP3)
/mp3/0004.mp3 — "Step Four — Stabilizing" (AUDIO_STEP4)
/mp3/0005.mp3 — "Warning — Temperature Dropping" (AUDIO_EMERGENCY)
/mp3/0006.mp3 — "Process Completed Successfully" (AUDIO_COMPLETE)
/mp3/0007.mp3 — "Welcome to THULIR CryoLab" (AUDIO_WELCOME)
/mp3/0008.mp3 — "Enter Step One Duration" (AUDIO_WIZARD_1)
/mp3/0009.mp3 — "Enter Step Two Duration" (AUDIO_WIZARD_2)
/mp3/0010.mp3 — "Enter Step Three Duration" (AUDIO_WIZARD_3)
/mp3/0011.mp3 — "Enter Step Four Duration" (AUDIO_WIZARD_4)
```

File numbering matches `AudioTrack` enum in `hmi_audio.h` and `Config.h`.

---

## Web Dashboard & Seed Intelligence

The `thulir_dashboard/` folder contains a Python Flask server that receives live telemetry from the ESP32, displays it in a browser, and runs ML-based seed viability predictions.

### Quick Start

```bash
cd thulir_dashboard
pip install -r requirements.txt
python app.py
# Open http://localhost:5000  (or http://<PC-LAN-IP>:5000 from any device on the same WiFi)
```

- Set `WEB_SERVER_HOST` in `Config.h` to your PC's LAN IP before flashing.
- The ESP32 POSTs telemetry every 2 s (`WEB_POST_INTERVAL_MS`). The dashboard updates in real time via WebSocket.

### Live Monitoring

- **Pages** — Home, Run Monitor, Protocols, Run History, User Guide, Settings (CryoAxis / THULIR theme).
- **Remote control** — START (sends the protocol recipe) and ABORT from the dashboard; the device acknowledges every command (see `thulir_dashboard/README.md`). Disable with `WEB_REMOTE_CONTROL_ENABLED` in `Config.h`.
- **Real-time temperature chart** — Actual vs the planned protocol profile with tolerance band.
- **Peltier PWM bars** — PID demand, Bottom, Middle, Top stage outputs updated live.
- **Process timing** — Elapsed, Remaining hold time, Device uptime.
- **Status strip** — State, Step, Error code, Sensor validity, Ramp lag, Servo status — all live.
- **ETA to Target** — Linear-regression estimate of time remaining to reach the current target temperature.
- **Auto-reconnect** — Dashboard detects ESP32 dropout (no data >10 s, configurable) and shows OFFLINE; reconnects automatically.
- **Run logging & Excel export** — Every run (measurements, events, alerts) is stored in SQLite and exportable per run.

### Seed Intelligence — ML Predictions

All predictions run server-side in pure Python (no ML frameworks needed).

| Prediction                                     | Method                                                               | Key Insight                                                    |
| ---------------------------------------------- | -------------------------------------------------------------------- | -------------------------------------------------------------- |
| **Seed Viability Score** (0–100)        | Weighted composite of temp stability + protocol adherence + humidity | Higher = better seed condition during storage                  |
| **Germination Rate Estimate** (%)        | Viability × 0.95, penalised for temps > −10°C                     | Predicts post-storage germination success                      |
| **Life Extension** (years + ×factor)    | **Q10 rule** — biological reaction rate halves per 10°C drop | At −20°C vs 25°C ambient: ~22× longer life                 |
| **Temperature Stability Index** (0–100) | Rolling standard deviation → mapped score                           | 100 = rock-steady; drops sharply with oscillation              |
| **Dew Point** (°C)                      | Magnus formula from temp + RH                                        | Flags condensation risk when dew point approaches surface temp |
| **Process Quality Score** (0–100)       | Mean absolute PID error over rolling window                          | Measures how tightly the firmware tracks the setpoint          |
| **Protocol Adherence** (%)               | Cumulative fraction of active ticks within ±0.5°C                  | Tracks long-term compliance with the temperature profile       |

- Scores are colour-coded: **green ≥ 80**, **amber 55–79**, **red < 55**.
- The **Predicted Viable Until** year is computed from baseline shelf life (2 yr at ambient) × Q10 extension factor.
- ML values are shown on the Run Monitor page and available at `/api/predict`.
- Standalone predictions available at `/api/predict` (JSON endpoint).

### Environmental Analysis

- **Condensation Risk** — LOW / MODERATE / HIGH based on dew-point margin vs surface temp.
- **Advisory notes** — Automatic warnings if humidity > 25% or temperature above optimal storage range.
- **Stability + Quality history chart** — Second Chart.js chart tracking both scores over the session.

### Servo Motor Events (reflected on dashboard)

- **OPEN** shown when both servos open (power-on, step completion, fault/stop).
- **CLOSED** shown when servos close (process complete, 15 s step-open window expired).

### API Endpoints

| Endpoint         | Method | Description                       |
| ---------------- | ------ | --------------------------------- |
| `/`            | GET    | Live dashboard UI                 |
| `/api/data`    | POST   | ESP32 telemetry receiver          |
| `/api/latest`  | GET    | Last received telemetry (JSON)    |
| `/api/predict` | GET    | Latest ML predictions only (JSON) |
| `/api/export`  | GET    | Excel export of the latest run    |
| `/api/runs`, `/api/runs/<id>/export` | GET | Run history / per-run Excel |
| `/api/protocols` | GET/POST/PUT/DELETE | Protocol management |
| `/api/command` | POST   | Queue START / ABORT for the device |

---

## License

This firmware is provided as-is for the THULIR project.
Use at your own risk. Always follow proper electrical and thermal safety practices.
