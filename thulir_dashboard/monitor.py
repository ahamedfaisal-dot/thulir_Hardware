"""
Run monitor: turns the ESP32 telemetry stream into dashboard state.

The firmware (v1.0.0) only POSTs telemetry; it has no remote-command channel.
So the run lifecycle (start / step change / complete / abort) is *derived* from
the device's reported state — the dashboard never claims to have started or
stopped hardware.  `request_command()` is the single place a future command
channel would plug in; today it always reports that the command was not sent.
"""

import json
import os
import threading
import time
from collections import deque
from datetime import datetime

import ml
import protocols
import storage

RUNNING_STATES = {"APPROACHING", "HOLDING", "TRANSITIONING", "RAMPING"}
READY_LIKE = {"BOOT", "IDLE", "PROGRAMMING", "READY", "TEST MODE", "UNKNOWN"}
NO_ERROR = {None, "", "NONE"}

COOLING_WINDOW_S = 120.0
COOLING_MIN_SPAN_S = 60.0
COOLING_MIN_POINTS = 8

CMD_TIMEOUT_S = 15.0          # device must acknowledge within this time
CONTROL_PIN = os.environ.get("THULIR_CONTROL_PIN") or None   # optional PIN for START
NOTE_NO_CHANNEL = ("The device is not reporting remote control (firmware without remote "
                   "control, or WEB_REMOTE_CONTROL_ENABLED is false). Use the THULIR keypad.")
NOTE_NO_PAUSE = "The device firmware has no pause/resume. Use ABORT, or the keypad."


def _f(v):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return None
    return x if x == x else None


def _b(v):
    return v is True or v == "true"


def cooling_rate(hist):
    """°C/min from timestamped (monotonic_s, temp) points; None if not enough data."""
    pts = list(hist)
    if len(pts) < COOLING_MIN_POINTS or pts[-1][0] - pts[0][0] < COOLING_MIN_SPAN_S:
        return None
    n = len(pts)
    tm = sum(p[0] for p in pts) / n
    vm = sum(p[1] for p in pts) / n
    den = sum((p[0] - tm) ** 2 for p in pts)
    if den < 1e-9:
        return None
    slope = sum((p[0] - tm) * (p[1] - vm) for p in pts) / den   # °C per second
    return round(slope * 60.0, 2)


def tolerance_status(dev, tol, in_hold):
    if tol is None:
        return "not_configured"
    if dev is None:
        return "unavailable"
    if not in_hold:
        return "approaching_target"
    a = abs(dev)
    if a > tol:
        return "outside"
    if a >= 0.8 * tol:
        return "near_limit"
    return "within"


class Monitor:
    def __init__(self, emit):
        self._emit = emit
        self.lock = threading.RLock()
        self.last = {}                 # last raw payload
        self.last_rx = None            # monotonic
        self.last_rx_iso = None
        self.ever_seen = False
        self.temp_hist = deque()
        self.hum_ts = None             # monotonic of last valid humidity reading
        self.hum_iso = None
        self.prev_state = None
        self.run = None                # active run dict
        self.finished = None           # last finished run summary (while device in terminal state)
        self.last_run_id = storage.last_run_id()
        self.alerts = {}               # key -> active alert dict
        self.selected = None
        self.refresh_selected()
        self._last_conn = None
        self.pending = None            # command waiting for the device's acknowledgement
        self._cmd_seq = 0
        self.last_uptime = None
        self.remote_ctl = False

    # ── selection ──────────────────────────────────────────────
    def refresh_selected(self):
        pid = storage.get_setting("selected_protocol_id")
        p = storage.get_protocol(pid) if pid else None
        self.selected = protocols.public(p) if p else None

    def active_protocol_id(self):
        return self.run["proto"]["id"] if self.run and self.run.get("proto") else None

    # ── helpers ────────────────────────────────────────────────
    def stale_after(self):
        return float(storage.get_setting("thresholds").get("stale_after_s", 10))

    def connected(self, now=None):
        if not self.ever_seen:
            return False
        now = now if now is not None else time.monotonic()
        return (now - self.last_rx) <= self.stale_after()

    def elapsed(self, now=None):
        r = self.run
        if not r:
            return None
        now = now if now is not None else time.monotonic()
        e = now - r["start_mono"] - r["paused_accum"]
        if r["paused_since"] is not None:
            e -= now - r["paused_since"]
        return max(0.0, e)

    def _event(self, etype, subsystem, severity, message, speak=None, **extra):
        r = self.run
        det = {"speak": speak} if speak else {}
        det.update(extra)
        ev = storage.add_event(r["id"] if r else None, etype, subsystem, severity, message,
                               elapsed_s=round(self.elapsed(), 1) if r else None,
                               details=json.dumps(det) if det else None)
        self._emit("event", ev)
        return ev

    # ── alerts ─────────────────────────────────────────────────
    def _set_alert(self, key, active, severity="warning", subsystem="System", message="", speak=None):
        cur = self.alerts.get(key)
        if active and cur is None:
            aid = storage.raise_alert(key, self.run["id"] if self.run else None,
                                      severity, subsystem, message)
            self.alerts[key] = {"id": aid, "key": key, "severity": severity,
                                "subsystem": subsystem, "message": message,
                                "raised_at": storage.now_iso(), "resolved_at": None}
            self._event("alert_raised", subsystem, severity, message, speak=speak, key=key)
        elif active and cur is not None and cur["message"] != message:
            cur["message"] = message
        elif not active and cur is not None:
            storage.resolve_alert(cur["id"])
            del self.alerts[key]
            self._event("alert_resolved", subsystem, "info", f"Resolved: {cur['message']}", key=key)

    def _evaluate_alerts(self, st, actual, dev, tol, connected):
        self._set_alert("hw_offline", False)
        sensor_ok = _b(st.get("sensorValid"))
        self._set_alert("sensor_invalid", connected and not sensor_ok, "error", "Temperature sensor",
                        "Temperature sensor reading is invalid or disconnected.")
        hum_ok = _b(st.get("humidityValid"))
        self._set_alert("humidity_unavailable", connected and not hum_ok, "warning", "Humidity sensor",
                        "Humidity sensor reading is unavailable.")
        in_hold = st.get("state") == "HOLDING" and self.run is not None
        outside = in_hold and tol is not None and dev is not None and abs(dev) > tol
        self._set_alert("temp_deviation", outside, "warning", "Temperature",
                        f"Temperature deviation {dev:+.1f} °C exceeds ±{tol:g} °C tolerance."
                        if outside else "", speak="temp_warning")
        err = st.get("error")
        dev_fault = st.get("state") in ("FAULT", "STOPPED") or err not in NO_ERROR
        self._set_alert("device_fault", connected and dev_fault, "error", "Device",
                        f"Device reports {st.get('state')}: {err}" if dev_fault else "",
                        speak="critical_error")
        self._set_alert("ramp_lag", connected and _b(st.get("rampLag")), "warning", "Temperature",
                        f"Ramp lag {_f(st.get('rampLagAmount')) or 0:.1f} °C behind setpoint.")

    # ── run lifecycle ──────────────────────────────────────────
    def _start_run(self, st):
        proto = self.selected
        base = "RUN-" + datetime.now().strftime("%Y%m%d-%H%M%S")
        rid, n = base, 1
        while storage.get_run(rid):
            n += 1
            rid = f"{base}-{n}"
        snap = None
        if proto:
            snap = {k: proto[k] for k in ("id", "family_id", "version", "name", "description",
                                          "tolerance", "steps")}
        started = storage.now_iso()
        storage.create_run(rid, snap, started)
        self.run = {"id": rid, "start_mono": time.monotonic(), "start_iso": started,
                    "paused_accum": 0.0, "paused_since": None, "proto": snap,
                    "step": None, "last_status": None}
        self.last_run_id = rid
        self.finished = None
        ml.reset()
        self._event("run_started", "Process", "info",
                    f"Run started — protocol: {snap['name'] + ' v' + str(snap['version']) if snap else 'none selected'}",
                    speak="run_started")

    def _sync_recipe(self, st):
        """The device is the source of truth for what actually runs. If its
        reported recipe (hold minutes of steps 1-4, step 2/4 targets — edited on
        the keypad) differs from the run's protocol snapshot, update the snapshot
        so the reference profile, progress and total duration match the device."""
        r = self.run
        hold = st.get("recH")
        if not r or not r.get("proto") or not isinstance(hold, list) or len(hold) != 4:
            return
        steps = r["proto"]["steps"]
        if len(steps) != 5:
            return
        want = [_f(h) for h in hold]
        t2, t4 = _f(st.get("recT2")), _f(st.get("recT4"))
        if any(h is None or h <= 0 for h in want):
            return
        changes = []
        for i in range(4):
            if steps[i].get("type") != "hold":
                return
            if steps[i].get("hold_min") != want[i]:
                changes.append(f"step {i + 1} hold {steps[i].get('hold_min'):g} → {want[i]:g} min")
        for idx, val in ((1, t2), (3, t4)):
            if val is not None and steps[idx].get("target") != val:
                changes.append(f"step {idx + 1} target {steps[idx].get('target'):g} → {val:g} °C")
        if not changes:
            return
        new = dict(r["proto"])
        new["steps"] = [dict(s) for s in steps]
        for i in range(4):
            new["steps"][i]["hold_min"] = want[i]
        if t2 is not None:
            new["steps"][1]["target"] = t2
        if t4 is not None:
            new["steps"][3]["target"] = t4
        r["proto"] = new
        storage.update_run_snapshot(r["id"], new)
        self._event("recipe_adjusted", "Process", "info",
                    "Run profile updated to the device's recipe: " + "; ".join(changes))

    def _end_run(self, status, reason):
        r = self.run
        if not r:
            return
        if r["paused_since"] is not None:
            r["paused_accum"] += time.monotonic() - r["paused_since"]
            r["paused_since"] = None
        sev = {"COMPLETED": "info", "ABORTED": "warning", "ERROR": "error"}[status]
        speak = {"COMPLETED": "completed", "ABORTED": "aborted", "ERROR": "critical_error"}[status]
        self._event({"COMPLETED": "run_completed", "ABORTED": "run_aborted", "ERROR": "run_error"}[status],
                    "Process", sev, reason, speak=speak)
        el = self.elapsed()
        total = protocols.total_duration_s(r["proto"]) if r["proto"] else None
        storage.finish_run(r["id"], status, round(r["paused_accum"], 1))
        self.finished = {"id": r["id"], "elapsed_s": el, "total_s": total, "status": status,
                         "proto": r["proto"], "start_iso": r["start_iso"]}
        # an ended run can no longer own open alerts' context; they keep their run_id
        self.run = None

    def _update_lifecycle(self, st):
        state = st.get("state")
        if self.run is None:
            if state in RUNNING_STATES:
                self._start_run(st)
            return
        if state in RUNNING_STATES:
            r = self.run
            if r["paused_since"] is not None:       # device resumed
                r["paused_accum"] += time.monotonic() - r["paused_since"]
                r["paused_since"] = None
                self._event("run_resumed", "Process", "info", "Run resumed", speak="resumed")
            return
        if state == "PAUSED":
            if self.run["paused_since"] is None:
                self.run["paused_since"] = time.monotonic()
                self._event("run_paused", "Process", "info", "Run paused", speak="paused")
            return
        if state == "COMPLETE":
            self._end_run("COMPLETED", "Run completed — device reported COMPLETE")
        elif state == "FAULT":
            self._end_run("ERROR", f"Run ended by device fault: {st.get('error')}")
        elif state == "STOPPED":
            self._end_run("ABORTED", "Run stopped on the device (STOPPED)")
        else:
            self._end_run("ABORTED", f"Run ended without completion — device state became {state}")

    # ── ingest ─────────────────────────────────────────────────
    def ingest(self, payload):
        with self.lock:
            now = time.monotonic()
            self.last_rx, self.ever_seen = now, True
            self.last_rx_iso = storage.now_iso()
            st = dict(payload)
            state = st.get("state", "UNKNOWN")
            prev = self.prev_state
            self.remote_ctl = _b(st.get("remoteCtl"))
            self._handle_ack(st)

            self._update_lifecycle(st)
            self._sync_recipe(st)

            if state == "READY" and prev != "READY":
                self._event("system_ready", "System", "info", "System ready", speak="system_ready")
            if state in READY_LIKE and self.finished and state != "COMPLETE":
                self.finished = None
            self.prev_state = state

            sensor_ok = _b(st.get("sensorValid"))
            actual = _f(st.get("filteredTemp")) if sensor_ok else None
            target = _f(st.get("setpoint")) if state == "RAMPING" else _f(st.get("targetTemp"))
            if actual is not None:
                self.temp_hist.append((now, actual))
            else:
                self.temp_hist.clear()      # a sensor gap invalidates the rate window
            while self.temp_hist and now - self.temp_hist[0][0] > COOLING_WINDOW_S:
                self.temp_hist.popleft()
            rate = cooling_rate(self.temp_hist)

            if _b(st.get("humidityValid")) and _f(st.get("humidity")) is not None:
                self.hum_ts, self.hum_iso = now, self.last_rx_iso
            humidity = _f(st.get("humidity")) if _b(st.get("humidityValid")) else None

            dev = round(actual - target, 2) if actual is not None and target is not None else None
            tol = self._tolerance()
            self._evaluate_alerts(st, actual, dev, tol, True)

            step = int(_f(st.get("step")) or 0)
            if self.run:
                r = self.run
                step_name = self._step_name(r["proto"], step)
                if step and step != r["step"]:
                    r["step"] = step
                    self._event("step_changed", "Process", "info",
                                f"Step {step}" + (f" — {step_name}" if step_name else ""),
                                speak="step_changed", step=step)
                ui = self.ui_status(state, True)
                storage.add_sample(r["id"], {
                    "ts": self.last_rx_iso, "elapsed_s": round(self.elapsed(now), 1),
                    "step": step, "step_name": step_name, "actual": actual, "target": target,
                    "deviation": dev, "cooling_rate": rate, "humidity": humidity,
                    "status": ui, "device_state": state, "setpoint": _f(st.get("setpoint")),
                    "pid_output": _f(st.get("pidOutput"))})

            st.update(ml.compute_predictions(st))
            self.last = st
            snap = self.snapshot()
        self._emit("snapshot", snap)
        return snap

    # ── command acknowledgement ────────────────────────────────
    def _finish_command(self, ok, message):
        cmd, self.pending = self.pending, None
        sev = "info" if ok else "warning"
        self._event("command_ack" if ok else "command_rejected", "Hardware command", sev,
                    f"'{cmd['name']}' " + ("acknowledged by device" if ok else "not executed") + f": {message}")
        self._emit("command_result", {"id": cmd["id"], "name": cmd["name"], "ok": ok, "message": message})

    def _handle_ack(self, st):
        up = _f(st.get("uptimeMs"))
        rebooted = up is not None and self.last_uptime is not None and up < self.last_uptime
        self.last_uptime = up
        if not self.pending:
            return
        if rebooted:
            self._finish_command(False, "device restarted before executing the command")
            return
        if int(_f(st.get("ackId")) or 0) == self.pending["id"]:
            msg = str(st.get("ackMsg") or "")
            self._finish_command(_b(st.get("ackOk")), msg or ("executed" if _b(st.get("ackOk")) else "rejected"))

    def pending_for_device(self):
        """Compact command to attach to the reply of the next telemetry POST (or None)."""
        with self.lock:
            c = self.pending
            if not c:
                return None
            out = {"id": c["id"], "name": c["name"]}
            if c.get("recipe"):
                r = c["recipe"]
                out.update({"h": r["h"], "t2": r["t2"], "t4": r["t4"]})
            return out

    # ── periodic ───────────────────────────────────────────────
    def tick(self):
        with self.lock:
            if self.pending and time.monotonic() > self.pending["expires"]:
                self._finish_command(False, f"no acknowledgement from the device within {CMD_TIMEOUT_S:g} s")
            conn = self.connected()
            if self.ever_seen and not conn:
                self._set_alert("hw_offline", True, "error", "Hardware connection",
                                f"No telemetry from the device for more than {self.stale_after():g} s.",)
            elif conn:
                self._set_alert("hw_offline", False)
            changed = conn != self._last_conn
            self._last_conn = conn
            if not (changed or self.run):
                return
            snap = self.snapshot()
        self._emit("snapshot", snap)

    # ── views ──────────────────────────────────────────────────
    def _tolerance(self):
        p = self.run["proto"] if self.run else self.selected
        return _f(p.get("tolerance")) if p else None

    @staticmethod
    def _step_name(proto, step):
        if proto and step and 1 <= step <= len(proto["steps"]):
            return proto["steps"][step - 1].get("name")
        return None

    @staticmethod
    def ui_status(state, connected):
        if state in RUNNING_STATES:
            return "RUNNING"
        if state == "PAUSED":
            return "PAUSED"
        if state == "COMPLETE":
            return "COMPLETED"
        if state in ("FAULT", "STOPPED"):
            return "ERROR"
        return "READY"

    def snapshot(self):
        now = time.monotonic()
        st = self.last
        conn = self.connected(now)
        state = st.get("state") if st else None
        status = self.ui_status(state, conn) if st else None
        sensor_ok = _b(st.get("sensorValid")) if st else False
        # stale readings are never presented as live
        actual = _f(st.get("filteredTemp")) if (st and sensor_ok and conn) else None
        # The device's target is only meaningful while a process is active (it reads 0 °C when idle)
        target = ((_f(st.get("setpoint")) if state == "RAMPING" else _f(st.get("targetTemp")))
                  if (st and conn and state in (RUNNING_STATES | {"PAUSED", "COMPLETE"})) else None)
        dev = round(actual - target, 2) if actual is not None and target is not None else None
        tol = self._tolerance()
        stale_age = round(now - self.last_rx, 1) if self.last_rx is not None else None

        hum_ok = bool(st) and _b(st.get("humidityValid")) and conn
        humidity = _f(st.get("humidity")) if hum_ok else None

        run_src = self.run
        if run_src:
            el = self.elapsed(now)
            proto = run_src["proto"]
            total = protocols.total_duration_s(proto) if proto else None
            run = {"id": run_src["id"], "active": True, "started_at": run_src["start_iso"],
                   "elapsed_s": round(el, 1), "paused": run_src["paused_since"] is not None}
        elif self.finished:
            f = self.finished
            el, total = f["elapsed_s"], f["total_s"]
            run = {"id": f["id"], "active": False, "started_at": f["start_iso"],
                   "elapsed_s": round(el, 1), "paused": False, "final_status": f["status"]}
            proto = f["proto"]
        else:
            el, total, proto, run = None, None, None, None
        if run is not None:
            run["total_s"] = round(total, 1) if total else None
            run["remaining_s"] = round(max(total - el, 0), 1) if total else None
            run["progress_pct"] = (100.0 if run.get("final_status") == "COMPLETED"
                                   else round(min(max(el / total * 100.0, 0.0), 100.0), 1)) if total else None
        show_proto = proto if (self.run or self.finished) else self.selected
        step = int(_f(st.get("step")) or 0) if st else 0

        valid_sel = bool(self.selected and self.selected["valid"])
        can_start = bool(conn and status == "READY" and valid_sel and sensor_ok
                         and state in ("IDLE", "READY"))
        channel = bool(conn and self.remote_ctl)
        busy = self.pending is not None
        compat_ok = bool(self.selected and self.selected.get("device_compatible"))
        cmds = {
            "channel": channel,
            "channel_note": ("Commands are sent to the device and only reported successful once it acknowledges them."
                             if channel else NOTE_NO_CHANNEL),
            "start": bool(can_start and channel and compat_ok and not busy),
            "pause": False,
            "resume": False,
            "abort": bool(channel and status == "RUNNING" and not busy),
            "pause_note": NOTE_NO_PAUSE,
            "pin_required": CONTROL_PIN is not None,
            "pending": ({"id": self.pending["id"], "name": self.pending["name"]} if self.pending else None),
            "start_blocked_reason": (None if compat_ok or not self.selected else
                                     "Protocol is not compatible with the device recipe: "
                                     + " ".join(self.selected.get("device_issues", []))),
        }
        return {
            "server_time": storage.now_iso(),
            "connected": conn, "ever_seen": self.ever_seen,
            "last_rx": self.last_rx_iso, "stale_age_s": stale_age,
            "simulated": False,
            "status": status, "device_state": state, "device_error": st.get("error") if st else None,
            "step": step, "step_count": len(show_proto["steps"]) if show_proto else 5,
            "step_name": self._step_name(show_proto, step),
            "protocol": ({"id": show_proto["id"], "name": show_proto["name"],
                          "version": show_proto["version"], "tolerance": show_proto.get("tolerance"),
                          "total_duration_s": protocols.total_duration_s(show_proto)}
                         if show_proto else None),
            "selected_protocol_id": self.selected["id"] if self.selected else None,
            "temperature": {
                "actual": actual, "target": target, "deviation": dev,
                "cooling_rate": cooling_rate(self.temp_hist) if conn else None,
                "tolerance": tol,
                "tolerance_status": tolerance_status(dev, tol, state == "HOLDING"),
                "sensor_valid": sensor_ok,
            },
            "humidity": {"value": humidity, "available": hum_ok,
                         "timestamp": self.hum_iso,
                         "stale": bool(self.hum_ts is not None and now - self.hum_ts > self.stale_after())},
            "run": run,
            "commands": cmds,
            "alerts": sorted(self.alerts.values(), key=lambda a: -a["id"]),
            "ml": {k: st.get(k) for k in (
                "seedViabilityScore", "lifeExtensionFactor", "lifeExtensionYears", "germRatePct",
                "tempStabilityIndex", "dewPoint", "processQualityScore", "etaToTarget", "inBandRatio")} if st else {},
            "servos_open": _b(st.get("servosOpen")) if st and "servosOpen" in st else None,
            "pidOutput": _f(st.get("pidOutput")) if st else None,
            "bottomPWM": _f(st.get("bottomPWM")) if st else None,
            "middlePWM": _f(st.get("middlePWM")) if st else None,
            "topPWM": _f(st.get("topPWM")) if st else None,
        }

    def graph_run_id(self):
        if self.run:
            return self.run["id"]
        if self.finished:
            return self.finished["id"]
        return None

    # ── commands ───────────────────────────────────────────────
    def _next_cmd_id(self):
        self._cmd_seq = max(self._cmd_seq + 1, int(time.time()))
        return self._cmd_seq

    def request_command(self, name, pin=None):
        """Validate, then queue a command for the device. Returns (http_status, body).
        202 means *queued* — success is only reported when the device acknowledges."""
        def no(code, msg):
            return code, {"ok": False, "sent": False, "message": msg}

        with self.lock:
            snap = self.snapshot()
            cm = snap["commands"]
            if not snap["connected"]:
                return no(409, "Hardware is disconnected — command not sent.")
            if name in ("pause", "resume"):
                return no(501, NOTE_NO_PAUSE)
            if not cm["channel"]:
                return no(501, NOTE_NO_CHANNEL)
            if self.pending:
                return no(409, "Another command is still waiting for the device.")
            recipe = None
            if name == "start":
                if not self.selected:
                    return no(400, "Select a protocol first.")
                if not self.selected["valid"]:
                    return no(400, "Selected protocol is invalid: " + " ".join(self.selected["errors"]))
                if not self.selected.get("device_compatible"):
                    return no(400, "Protocol cannot run on this device: " + " ".join(self.selected["device_issues"]))
                if snap["status"] == "RUNNING":
                    return no(409, "A process is already running.")
                if not snap["temperature"]["sensor_valid"]:
                    return no(409, "Temperature sensor is not valid.")
                if not cm["start"]:
                    return no(409, f"Device is not ready to start (state: {snap['device_state']}).")
                if CONTROL_PIN is not None and (pin or "") != CONTROL_PIN:
                    return no(403, "Incorrect or missing control PIN.")
                recipe = protocols.device_recipe(self.selected)
            elif name == "abort":
                if not cm["abort"]:
                    return no(409, f"Abort is not permitted in status {snap['status']}.")
            else:
                return no(400, "Unknown command.")
            cid = self._next_cmd_id()
            self.pending = {"id": cid, "name": name, "recipe": recipe,
                            "expires": time.monotonic() + CMD_TIMEOUT_S}
            self._event("command_sent", "Hardware command", "info",
                        f"'{name}' sent to device" + (f" ({self.selected['name']} v{self.selected['version']})" if recipe else "")
                        + " — waiting for acknowledgement")
            return 202, {"ok": True, "sent": True, "pending": True, "id": cid,
                         "message": f"'{name}' sent to the device — waiting for its acknowledgement."}
