"""
Protocol model: validation, duration and target-profile helpers.

A protocol is an ordered list of steps:
  hold step : {"name", "type":"hold", "target": °C, "hold_min": minutes}
  ramp step : {"name", "type":"ramp", "target": end °C, "ramp_rate": °C/min}
              (starts from the previous step's target; duration is derived)

Hardware limits come from Config.h (valid sensor range) — no other physical
limits are invented here.
"""

import storage

TEMP_MIN = -40.0     # Config.h TEMP_SENSOR_MIN
TEMP_MAX = 85.0      # Config.h TEMP_SENSOR_MAX
MAX_STEPS = 20


def _num(v):
    if isinstance(v, bool):
        return None
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return f if f == f and abs(f) != float("inf") else None


def normalize(data):
    """Coerce an incoming dict into the stored shape (no validation)."""
    steps = []
    for i, s in enumerate(data.get("steps") or []):
        st = {"name": str(s.get("name") or "").strip(),
              "type": "ramp" if s.get("type") == "ramp" else "hold",
              "target": _num(s.get("target"))}
        if st["type"] == "hold":
            st["hold_min"] = _num(s.get("hold_min"))
        else:
            st["ramp_rate"] = _num(s.get("ramp_rate"))
        steps.append(st)
    tol = data.get("tolerance")
    return {
        "name": str(data.get("name") or "").strip(),
        "description": str(data.get("description") or "").strip(),
        "tolerance": None if tol in (None, "") else _num(tol),
        "steps": steps,
    }


def validate(p):
    """Return a list of human-readable problems (empty list == valid)."""
    errs = []
    if not p.get("name"):
        errs.append("Protocol name is required.")
    tol = p.get("tolerance")
    if p.get("tolerance") is not None and (tol is None or tol <= 0):
        errs.append("Tolerance must be a positive number of °C (or left empty).")
    steps = p.get("steps") or []
    if not steps:
        errs.append("At least one step is required.")
    if len(steps) > MAX_STEPS:
        errs.append(f"At most {MAX_STEPS} steps are supported.")
    prev = None
    for i, s in enumerate(steps, 1):
        tag = f"Step {i}"
        if not s.get("name"):
            errs.append(f"{tag}: step name is required.")
        t = s.get("target")
        if t is None:
            errs.append(f"{tag}: target temperature is not a valid number.")
        elif not (TEMP_MIN <= t <= TEMP_MAX):
            errs.append(f"{tag}: target {t:g} °C is outside the sensor range "
                        f"({TEMP_MIN:g} to {TEMP_MAX:g} °C).")
        if s.get("type") == "ramp":
            r = s.get("ramp_rate")
            if r is None or r == 0:
                errs.append(f"{tag}: ramp rate must be a non-zero number (°C/min).")
            elif prev is None:
                errs.append(f"{tag}: a ramp step cannot be the first step.")
            elif t is not None:
                delta = t - prev
                if delta == 0:
                    errs.append(f"{tag}: ramp target equals the previous temperature.")
                elif (delta < 0) != (r < 0):
                    errs.append(f"{tag}: ramp rate sign does not match the direction "
                                f"{prev:g} → {t:g} °C (cooling needs a negative rate).")
        else:
            h = s.get("hold_min")
            if h is None or h <= 0:
                errs.append(f"{tag}: duration must be a positive number of minutes.")
        if t is not None:
            prev = t
    return errs


def step_durations_min(p):
    """Duration (minutes) of every step; None when it cannot be derived."""
    out, prev = [], None
    for s in p.get("steps") or []:
        t = s.get("target")
        if s.get("type") == "ramp":
            r = s.get("ramp_rate")
            if prev is not None and t is not None and r:
                out.append(abs(t - prev) / abs(r))
            else:
                out.append(None)
        else:
            out.append(s.get("hold_min"))
        if t is not None:
            prev = t
    return out


def total_duration_s(p):
    if not p:
        return None
    d = step_durations_min(p)
    if not d or any(x is None or x <= 0 for x in d):
        return None
    return sum(d) * 60.0


def profile_points(p):
    """Planned target profile as [(minutes, °C)] with vertical jumps at hold steps."""
    pts, t, prev = [], 0.0, None
    for s, dur in zip(p.get("steps") or [], step_durations_min(p)):
        tgt, dur = s.get("target"), dur
        if tgt is None or dur is None:
            break
        if s.get("type") == "ramp" and prev is not None:
            pts.append((round(t, 3), prev))
            t += dur
            pts.append((round(t, 3), tgt))
        else:
            pts.append((round(t, 3), tgt))
            t += dur
            pts.append((round(t, 3), tgt))
        prev = tgt
    return pts


# ── device (firmware) compatibility ─────────────────────────────
# The THULIR firmware runs a fixed 5-step recipe: four holds, then a controlled
# ramp.  Only some values can be changed on the device (RecipeManager.cpp /
# Config.h): hold minutes 1-999 for steps 1-4, step 2 target 0/15/25 °C,
# step 4 target 0/25 °C; step 1 (25 °C) and step 3 (4 °C) are fixed and
# step 5 ramps -1 °C/min to -20 °C.
DEVICE_STEP_TARGETS = {1: (25.0,), 2: (0.0, 15.0, 25.0), 3: (4.0,), 4: (0.0, 25.0)}
DEVICE_RAMP_END, DEVICE_RAMP_RATE = -20.0, -1.0


def device_compat(p):
    """List of reasons this protocol cannot be executed by the device ([] == compatible)."""
    issues = []
    steps = p.get("steps") or []
    if len(steps) != 5:
        return [f"The device runs exactly 5 steps (4 holds + 1 ramp); this protocol has {len(steps)}."]
    for i in range(4):
        s = steps[i]
        n = i + 1
        if s.get("type") != "hold":
            issues.append(f"Step {n} must be a hold step.")
            continue
        opts = DEVICE_STEP_TARGETS[n]
        if s.get("target") not in opts:
            issues.append(f"Step {n} target must be " + " / ".join(f"{o:g}" for o in opts) + " °C.")
        h = s.get("hold_min")
        if h is None or h != int(h) or not (1 <= h <= 999):
            issues.append(f"Step {n} duration must be a whole number of minutes (1-999).")
    r = steps[4]
    if r.get("type") != "ramp" or r.get("target") != DEVICE_RAMP_END or r.get("ramp_rate") != DEVICE_RAMP_RATE:
        issues.append(f"Step 5 must be a ramp to {DEVICE_RAMP_END:g} °C at {DEVICE_RAMP_RATE:g} °C/min.")
    return issues


def device_recipe(p):
    """Recipe payload for the device (only call when device_compat(p) == [])."""
    st = p["steps"]
    return {"h": [int(st[i]["hold_min"]) for i in range(4)],
            "t2": float(st[1]["target"]), "t4": float(st[3]["target"])}


def public(p):
    """Protocol dict enriched with derived fields for the UI."""
    d = dict(p)
    errs = validate(p)
    d["valid"] = not errs
    d["device_issues"] = [] if errs else device_compat(p)
    d["device_compatible"] = (not errs) and not d["device_issues"]
    d["errors"] = errs
    d["step_count"] = len(p.get("steps") or [])
    d["total_duration_s"] = total_duration_s(p) if not errs else None
    d["profile"] = profile_points(p) if not errs else []
    return d


def seed_defaults():
    """First-run protocol taken from the firmware's factory recipe (Config.h):
    25 °C/30 min, 15 °C/30 min, 4 °C/20 min, 0 °C/20 min, then ramp to -20 °C
    at -1 °C/min; target tolerance ±0.5 °C (TARGET_TOLERANCE)."""
    if storage.protocol_count():
        return
    storage.create_protocol(
        "THULIR Device Default Recipe",
        "Factory recipe stored in the device firmware (Config.h defaults). "
        "Reference profile; edit or save as a new version to match your own recipe.",
        0.5,
        [
            {"name": "Equilibrate", "type": "hold", "target": 25.0, "hold_min": 30},
            {"name": "Cooling", "type": "hold", "target": 15.0, "hold_min": 30},
            {"name": "Deep chill", "type": "hold", "target": 4.0, "hold_min": 20},
            {"name": "Stabilise", "type": "hold", "target": 0.0, "hold_min": 20},
            {"name": "Controlled ramp", "type": "ramp", "target": -20.0, "ramp_rate": -1.0},
        ],
    )
