"""
ML / predictive helpers for the THULIR dashboard (moved unchanged from app.py).

Computed server-side from rolling telemetry:
  Seed Viability Score, Life Extension Factor, Germination Rate Estimate,
  Temperature Stability Index, Dew Point (Magnus), Process Quality, ETA to Target.
"""

import math
import statistics
from collections import deque

_BUFFER_LEN = 60          # ~2 min at 2-s posting interval
# Rolling buffers for ML feature computation
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




def reset():
    """Clear rolling buffers and adherence counters (called at run start)."""
    global _total_in_band, _total_active
    _temp_buffer.clear()
    _error_buffer.clear()
    _total_in_band = 0
    _total_active = 0
