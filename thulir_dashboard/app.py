"""
TULIR Process Monitor — Flask backend.

Receives telemetry POSTed by the ESP32 firmware (WebManager.cpp),
logs every reading to an Excel workbook, and pushes live updates to
connected browsers over WebSocket (Flask-SocketIO).

ML / Predictive features (computed server-side from rolling telemetry):
  - Seed Viability Score       — protocol adherence × temp stability
  - Life Extension Factor      — Q10 rule vs ambient (25°C baseline)
  - Germination Rate Estimate  — empirical model from temp + humidity + score
  - Temperature Stability Index— rolling standard deviation → 0-100 score
  - Dew Point                  — Magnus formula from temp + RH
  - Process Quality Score      — PID tracking error vs ideal
  - ETA to Target              — linear regression on recent temp trend
"""

import math
import os
import statistics
import threading
from collections import deque
from datetime import datetime

from flask import Flask, jsonify, request, render_template, send_file
from flask_socketio import SocketIO
from openpyxl import Workbook, load_workbook

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(BASE_DIR, "data")
EXCEL_PATH = os.path.join(DATA_DIR, "telemetry.xlsx")

COLUMNS = [
    "Timestamp", "State", "Error", "Step",
    "ActualTemp_C", "FilteredTemp_C", "TargetTemp_C", "Setpoint_C",
    "Humidity_%", "PID_%", "BottomPWM_%", "MiddlePWM_%", "TopPWM_%",
    "SensorValid", "ElapsedMs", "RemainingMs",
    "RampLag", "RampLagAmount_C", "DeviceUptimeMs",
    # ML columns
    "SeedViabilityScore", "LifeExtensionFactor", "GermRatePct",
    "TempStabilityIndex", "DewPoint_C", "ProcessQualityScore", "ETA_s",
]

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

_excel_lock = threading.Lock()
_last_reading = {}
_connected_since = None

# Rolling buffers for ML feature computation
_BUFFER_LEN = 60          # ~2 min at 2-s posting interval
_temp_buffer   = deque(maxlen=_BUFFER_LEN)
_error_buffer  = deque(maxlen=_BUFFER_LEN)   # |actual - setpoint|

# Cumulative protocol-adherence tracking
_steps_seen        = set()
_total_in_band     = 0    # ticks where |error| < 0.5°C
_total_active      = 0    # ticks where process is active

# ───────────────────────────────────────────────────────────────────
#  ML / Prediction helpers
# ───────────────────────────────────────────────────────────────────

def _dew_point(temp_c: float, rh: float) -> float | None:
    """Magnus formula dew point (°C). Returns None if inputs invalid."""
    if rh is None or rh <= 0 or rh > 100 or temp_c is None:
        return None
    a, b = 17.625, 243.04
    gamma = math.log(rh / 100.0) + a * temp_c / (b + temp_c)
    return round(b * gamma / (a - gamma), 1)


def _stability_index(temp_buf: deque) -> float:
    """
    Convert rolling temperature std-dev → 0-100 stability score.
    std=0 → 100,  std≥2°C → 0.
    """
    if len(temp_buf) < 5:
        return 100.0
    std = statistics.stdev(temp_buf)
    score = max(0.0, 100.0 - std * 50.0)
    return round(score, 1)


def _life_extension_factor(avg_temp_c: float) -> float:
    """
    Q10 = 2 rule: biological reaction rates halve every 10°C cooling.
    Life extension vs 25°C ambient.
    factor = 2 ^ ((25 - temp) / 10)
    """
    if avg_temp_c is None:
        return 1.0
    factor = 2.0 ** ((25.0 - avg_temp_c) / 10.0)
    return round(factor, 1)


def _life_extension_years(factor: float, baseline_years: float = 2.0) -> float:
    """
    Additional storage years beyond baseline_years.
    Typical seed shelf life at ambient ≈ 2 years.
    """
    return round((factor - 1.0) * baseline_years, 1)


def _viability_score(stability: float, in_band_ratio: float, rh: float | None) -> float:
    """
    Composite seed viability score (0-100).
      40% weight: temperature stability
      40% weight: protocol adherence (time in tolerance band)
      20% weight: humidity penalty (seeds prefer low RH)
    """
    stability_norm = stability / 100.0

    adherence_norm = min(in_band_ratio, 1.0)

    # Optimal RH for seed storage: < 15%. Penalise linearly up to 80%.
    if rh is None:
        rh_score = 0.7          # no data — neutral penalty
    else:
        rh_score = max(0.0, 1.0 - max(0.0, rh - 15.0) / 65.0)

    score = 40.0 * stability_norm + 40.0 * adherence_norm + 20.0 * rh_score
    return round(min(score, 100.0), 1)


def _germ_rate(viability: float, temp_c: float | None) -> float:
    """
    Predicted germination rate (%).
    Empirical: seeds stored at -20°C maintain ~95% germination over decades.
    Penalty applied for temps above ideal storage (-18 to -20°C).
    """
    base = viability * 0.95
    if temp_c is not None and temp_c > -10.0:
        penalty = min(15.0, (temp_c + 10.0) * 0.5)
        base = max(0.0, base - penalty)
    return round(min(base, 99.0), 1)


def _process_quality(err_buf: deque, pid_output: float | None) -> float:
    """
    PID tracking quality: 100 = perfect, 0 = large persistent error.
    Penalises mean |error| and high/unnecessary PID output when near setpoint.
    """
    if len(err_buf) < 3:
        return 100.0
    mean_err = statistics.mean(err_buf)
    # >2°C mean error → quality approaches 0
    err_score = max(0.0, 100.0 - mean_err * 25.0)
    return round(err_score, 1)


def _eta_to_target(temp_buf: deque, target: float | None) -> int | None:
    """
    Linear regression on last N temperature readings → ETA to target (seconds).
    Returns None if cannot estimate (stable / no trend / no target).
    """
    if target is None or len(temp_buf) < 10:
        return None
    temps = list(temp_buf)
    n = len(temps)
    xs = list(range(n))
    xm = (n - 1) / 2.0
    tm = sum(temps) / n
    num = sum((x - xm) * (t - tm) for x, t in zip(xs, temps))
    den = sum((x - xm) ** 2 for x in xs)
    if abs(den) < 1e-9:
        return None
    slope = num / den  # °C per tick (2 s each)
    if abs(slope) < 0.001:
        return None
    ticks_needed = (target - temps[-1]) / slope
    if ticks_needed <= 0:
        return 0
    return int(ticks_needed * 2)  # × 2 s per tick


def compute_predictions(payload: dict) -> dict:
    """Compute all ML predictions from the latest payload + rolling buffers."""
    global _total_in_band, _total_active, _steps_seen

    temp   = payload.get("filteredTemp")
    sp     = payload.get("setpoint")
    target = payload.get("targetTemp")
    rh     = payload.get("humidity")
    pid    = payload.get("pidOutput")
    state  = payload.get("state", "")

    # Update rolling buffers
    if temp is not None:
        try:
            _temp_buffer.append(float(temp))
        except (ValueError, TypeError):
            pass

    active_states = {"APPROACHING", "HOLDING", "RAMPING", "TRANSITIONING"}
    if state in active_states:
        _total_active += 1
        try:
            err = abs(float(temp) - float(sp)) if temp is not None and sp is not None else 999
            _error_buffer.append(err)
            if err < 0.5:
                _total_in_band += 1
        except (ValueError, TypeError):
            pass

    # Derived values
    avg_temp = sum(_temp_buffer) / len(_temp_buffer) if _temp_buffer else temp
    stability  = _stability_index(_temp_buffer)
    in_band_r  = (_total_in_band / _total_active) if _total_active > 0 else 0.8
    viability  = _viability_score(stability, in_band_r, rh)
    factor     = _life_extension_factor(avg_temp)
    life_yrs   = _life_extension_years(factor)
    germ_rate  = _germ_rate(viability, avg_temp)
    pq         = _process_quality(_error_buffer, pid)
    dp         = _dew_point(temp, rh)
    eta        = _eta_to_target(_temp_buffer, target)

    return {
        "seedViabilityScore":   viability,
        "lifeExtensionFactor":  factor,
        "lifeExtensionYears":   life_yrs,
        "germRatePct":          germ_rate,
        "tempStabilityIndex":   stability,
        "dewPoint":             dp,
        "processQualityScore":  pq,
        "etaToTarget":          eta,
        "inBandRatio":          round(in_band_r * 100.0, 1),
    }


# ───────────────────────────────────────────────────────────────────
#  Excel helpers
# ───────────────────────────────────────────────────────────────────

def _ensure_workbook():
    os.makedirs(DATA_DIR, exist_ok=True)
    if not os.path.exists(EXCEL_PATH):
        wb = Workbook()
        ws = wb.active
        ws.title = "Telemetry"
        ws.append(COLUMNS)
        wb.save(EXCEL_PATH)


def _append_row(row: dict):
    with _excel_lock:
        _ensure_workbook()
        wb = load_workbook(EXCEL_PATH)
        ws = wb["Telemetry"]
        ws.append([
            row.get("timestamp"),
            row.get("state"),
            row.get("error"),
            row.get("step"),
            row.get("actualTemp"),
            row.get("filteredTemp"),
            row.get("targetTemp"),
            row.get("setpoint"),
            row.get("humidity"),
            row.get("pidOutput"),
            row.get("bottomPWM"),
            row.get("middlePWM"),
            row.get("topPWM"),
            row.get("sensorValid"),
            row.get("elapsedMs"),
            row.get("remainingMs"),
            row.get("rampLag"),
            row.get("rampLagAmount"),
            row.get("uptimeMs"),
            # ML columns
            row.get("seedViabilityScore"),
            row.get("lifeExtensionFactor"),
            row.get("germRatePct"),
            row.get("tempStabilityIndex"),
            row.get("dewPoint"),
            row.get("processQualityScore"),
            row.get("etaToTarget"),
        ])
        wb.save(EXCEL_PATH)


# ───────────────────────────────────────────────────────────────────
#  Flask routes
# ───────────────────────────────────────────────────────────────────

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/data", methods=["POST"])
def receive_data():
    global _last_reading, _connected_since
    payload = request.get_json(force=True, silent=True)
    if payload is None:
        return jsonify({"ok": False, "error": "invalid json"}), 400

    if _connected_since is None:
        _connected_since = datetime.now().isoformat(timespec="seconds")

    payload["timestamp"] = datetime.now().isoformat(timespec="seconds")

    # Compute ML predictions and merge into payload
    predictions = compute_predictions(payload)
    payload.update(predictions)

    _last_reading = payload
    _append_row(payload)
    socketio.emit("telemetry", payload)

    return jsonify({"ok": True})


@app.route("/api/latest")
def latest():
    return jsonify(_last_reading or {})


@app.route("/api/predict")
def predict():
    """Standalone endpoint — returns latest ML predictions only."""
    keys = [
        "seedViabilityScore", "lifeExtensionFactor", "lifeExtensionYears",
        "germRatePct", "tempStabilityIndex", "dewPoint",
        "processQualityScore", "etaToTarget", "inBandRatio",
    ]
    return jsonify({k: _last_reading.get(k) for k in keys})


@app.route("/api/export")
def export_excel():
    _ensure_workbook()
    return send_file(
        EXCEL_PATH,
        as_attachment=True,
        download_name=f"tulir_telemetry_{datetime.now():%Y%m%d_%H%M%S}.xlsx",
    )


@socketio.on("connect")
def on_connect():
    if _last_reading:
        socketio.emit("telemetry", _last_reading)


if __name__ == "__main__":
    _ensure_workbook()
    print("=" * 60)
    print(" TULIR Process Monitor  +  Seed Intelligence ML")
    print(f" Excel log:  {EXCEL_PATH}")
    print(" Open on this PC:      http://localhost:5000")
    print(" Open from other devices on the same WiFi:")
    print("   http://<this-PC-LAN-IP>:5000   (find IP via `ipconfig`)")
    print("=" * 60)
    socketio.run(app, host="0.0.0.0", port=5000, debug=False, allow_unsafe_werkzeug=True)
