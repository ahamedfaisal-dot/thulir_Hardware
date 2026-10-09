"""
THULIR / CryoAxis Process Monitor — Flask backend.

Receives telemetry POSTed by the ESP32 firmware (WebManager.cpp), derives the
run lifecycle from it (monitor.py), persists protocols / runs / measurements /
events / alerts / settings in SQLite (storage.py), pushes live snapshots to
browsers over WebSocket, and exports any run to Excel (exporter.py).

Seed-intelligence predictions (ml.py) are kept from the original dashboard.
"""

import os
from datetime import datetime

from flask import (Flask, jsonify, render_template, request, send_file,
                   send_from_directory)
from flask_socketio import SocketIO

import exporter
import monitor as monitor_mod
import protocols
import storage

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BG_DIR = os.path.join(BASE_DIR, "bg_image")
GUIDE_PDF = os.path.join(BASE_DIR, "static", "docs", "user_guide.pdf")
XLSX_MIME = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

storage.init()
storage.close_stale_alerts()
protocols.seed_defaults()
mon = monitor_mod.Monitor(lambda name, data: socketio.emit(name, data))


def _err(msg, code=400, **extra):
    return jsonify({"ok": False, "message": msg, **extra}), code


# ── pages / static ──────────────────────────────────────────────

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/bg_image/<path:name>")
def bg_image(name):
    return send_from_directory(BG_DIR, name)


@app.route("/userguide.pdf")
def user_guide_pdf():
    if not os.path.isfile(GUIDE_PDF):
        return _err("User guide PDF will be available soon.", 404)
    return send_file(GUIDE_PDF, as_attachment=True, download_name="CryoAxis_User_Guide.pdf")


# ── telemetry in ────────────────────────────────────────────────

@app.route("/api/data", methods=["POST"])
def receive_data():
    payload = request.get_json(force=True, silent=True)
    if not isinstance(payload, dict):
        return jsonify({"ok": False, "error": "invalid json"}), 400
    mon.ingest(payload)
    reply = {"ok": True}
    cmd = mon.pending_for_device()      # delivered until the device acknowledges it
    if cmd:
        reply["cmd"] = cmd
    return jsonify(reply)


# ── live state ──────────────────────────────────────────────────

@app.route("/api/state")
def state():
    with mon.lock:
        snap = mon.snapshot()
        rid = mon.graph_run_id() or mon.last_run_id
    return jsonify({"snapshot": snap, "graph_run_id": rid,
                    "guide_available": os.path.isfile(GUIDE_PDF),
                    "settings": {"audio": storage.get_setting("audio"),
                                 "thresholds": storage.get_setting("thresholds")}})


@app.route("/api/latest")          # legacy: raw last telemetry + predictions
def latest():
    return jsonify(mon.last or {})


@app.route("/api/predict")         # legacy
def predict():
    return jsonify(mon.snapshot()["ml"])


@app.route("/api/graph")
def graph():
    rid = request.args.get("run") or mon.graph_run_id() or mon.last_run_id
    if not rid:
        return jsonify({"run_id": None, "samples": [], "profile": [], "tolerance": None})
    run = storage.get_run(rid)
    if not run:
        return _err("Run not found.", 404)
    samples = [{"t": s["elapsed_s"], "actual": s["actual"], "target": s["target"]}
               for s in storage.run_samples(rid)]
    snap = run["protocol_snapshot"]
    prof = protocols.profile_points(snap) if snap else []
    return jsonify({"run_id": rid, "samples": samples, "profile": prof,
                    "tolerance": snap.get("tolerance") if snap else None,
                    "status": run["final_status"] or "RUNNING"})


# ── protocols ───────────────────────────────────────────────────

def _protocol_in_use(pid):
    return mon.active_protocol_id() == pid


@app.route("/api/protocols", methods=["GET"])
def protocols_list():
    return jsonify([protocols.public(p) for p in storage.list_protocols()])


@app.route("/api/protocols/validate", methods=["POST"])
def protocols_validate():
    p = protocols.normalize(request.get_json(force=True, silent=True) or {})
    errs = protocols.validate(p)
    return jsonify({"valid": not errs, "errors": errs,
                    "total_duration_s": None if errs else protocols.total_duration_s(p)})


@app.route("/api/protocols", methods=["POST"])
def protocols_create():
    p = protocols.normalize(request.get_json(force=True, silent=True) or {})
    errs = protocols.validate(p)
    if errs:
        return _err("Protocol is not valid.", 422, errors=errs)
    pid = storage.create_protocol(p["name"], p["description"], p["tolerance"], p["steps"])
    return jsonify({"ok": True, "protocol": protocols.public(storage.get_protocol(pid))}), 201


@app.route("/api/protocols/<int:pid>", methods=["GET"])
def protocols_get(pid):
    p = storage.get_protocol(pid)
    return jsonify(protocols.public(p)) if p else _err("Protocol not found.", 404)


@app.route("/api/protocols/<int:pid>", methods=["PUT"])
def protocols_update(pid):
    if not storage.get_protocol(pid):
        return _err("Protocol not found.", 404)
    if _protocol_in_use(pid):
        return _err("This protocol is used by the run in progress and cannot be edited now.", 409)
    p = protocols.normalize(request.get_json(force=True, silent=True) or {})
    errs = protocols.validate(p)
    if errs:
        return _err("Protocol is not valid.", 422, errors=errs)
    storage.update_protocol(pid, p["name"], p["description"], p["tolerance"], p["steps"])
    with mon.lock:
        mon.refresh_selected()
    return jsonify({"ok": True, "protocol": protocols.public(storage.get_protocol(pid))})


@app.route("/api/protocols/<int:pid>/version", methods=["POST"])
def protocols_new_version(pid):
    if not storage.get_protocol(pid):
        return _err("Protocol not found.", 404)
    p = protocols.normalize(request.get_json(force=True, silent=True) or {})
    errs = protocols.validate(p)
    if errs:
        return _err("Protocol is not valid.", 422, errors=errs)
    nid = storage.new_protocol_version(pid, p["name"], p["description"], p["tolerance"], p["steps"])
    return jsonify({"ok": True, "protocol": protocols.public(storage.get_protocol(nid))}), 201


@app.route("/api/protocols/<int:pid>", methods=["DELETE"])
def protocols_delete(pid):
    if not storage.get_protocol(pid):
        return _err("Protocol not found.", 404)
    if _protocol_in_use(pid):
        return _err("This protocol is used by the run in progress and cannot be deleted.", 409)
    storage.delete_protocol(pid)
    if storage.get_setting("selected_protocol_id") == pid:
        storage.set_setting("selected_protocol_id", None)
    with mon.lock:
        mon.refresh_selected()
    return jsonify({"ok": True})


@app.route("/api/protocols/<int:pid>/select", methods=["POST"])
def protocols_select(pid):
    p = storage.get_protocol(pid)
    if not p:
        return _err("Protocol not found.", 404)
    if mon.run:
        return _err("A run is in progress; the protocol cannot be changed until it ends.", 409)
    pub = protocols.public(p)
    if not pub["valid"]:
        return _err("Protocol is not valid.", 422, errors=pub["errors"])
    storage.set_setting("selected_protocol_id", pid)
    with mon.lock:
        mon.refresh_selected()
        snap = mon.snapshot()
    socketio.emit("snapshot", snap)
    return jsonify({"ok": True, "protocol": pub})


# ── runs / export ───────────────────────────────────────────────

@app.route("/api/runs")
def runs_list():
    return jsonify(storage.list_runs())


@app.route("/api/runs/<run_id>")
def runs_get(run_id):
    run = storage.get_run(run_id)
    if not run:
        return _err("Run not found.", 404)
    samples = storage.run_samples(run_id)
    run["samples"] = samples
    run["events"] = storage.run_events(run_id)
    run["alerts"] = storage.list_alerts(500, run_id)
    snap = run["protocol_snapshot"]
    run["profile"] = protocols.profile_points(snap) if snap else []
    return jsonify(run)


@app.route("/api/runs/<run_id>/export")
def runs_export(run_id):
    data = exporter.build_run_workbook(run_id)
    if data is None:
        return _err("Run not found.", 404)
    import io
    return send_file(io.BytesIO(data), mimetype=XLSX_MIME, as_attachment=True,
                     download_name=f"cryoaxis_{run_id}.xlsx")


@app.route("/api/export")          # legacy button: export the current/most recent run
def export_latest():
    rid = mon.graph_run_id() or mon.last_run_id
    if not rid:
        return _err("No run data has been recorded yet.", 404)
    return runs_export(rid)


# ── alerts / events ─────────────────────────────────────────────

@app.route("/api/alerts")
def alerts_list():
    return jsonify({"active": mon.snapshot()["alerts"], "history": storage.list_alerts(100)})


@app.route("/api/events")
def events_list():
    return jsonify(storage.recent_events(int(request.args.get("limit", 100))))


# ── settings ────────────────────────────────────────────────────

@app.route("/api/settings", methods=["GET"])
def settings_get():
    return jsonify({"audio": storage.get_setting("audio"),
                    "thresholds": storage.get_setting("thresholds")})


@app.route("/api/settings", methods=["PUT"])
def settings_put():
    body = request.get_json(force=True, silent=True) or {}
    if "audio" in body:
        a = body["audio"]
        cur = storage.get_setting("audio")
        if "enabled" in a:
            cur["enabled"] = bool(a["enabled"])
        if a.get("language") in ("en", "ta", "hi"):
            cur["language"] = a["language"]
        for k, lo, hi in (("volume", 0.0, 1.0), ("rate", 0.5, 2.0)):
            if k in a:
                try:
                    cur[k] = min(hi, max(lo, float(a[k])))
                except (TypeError, ValueError):
                    return _err(f"Invalid {k}.", 422)
        storage.set_setting("audio", cur)
    if "thresholds" in body:
        t = storage.get_setting("thresholds")
        try:
            if "stale_after_s" in body["thresholds"]:
                v = float(body["thresholds"]["stale_after_s"])
                if not 3 <= v <= 300:
                    return _err("Stale-data threshold must be 3–300 s.", 422)
                t["stale_after_s"] = v
        except (TypeError, ValueError):
            return _err("Invalid threshold.", 422)
        storage.set_setting("thresholds", t)
    return settings_get()


# ── commands ────────────────────────────────────────────────────

@app.route("/api/command", methods=["POST"])
def command():
    body_in = request.get_json(force=True, silent=True) or {}
    name = body_in.get("command")
    if name not in ("start", "pause", "resume", "abort"):
        return _err("Unknown command.", 400)
    code, body = mon.request_command(name, body_in.get("pin"))
    return jsonify(body), code


@socketio.on("connect")
def on_connect():
    socketio.emit("snapshot", mon.snapshot())


def _watcher():
    while True:
        socketio.sleep(1)
        try:
            mon.tick()
        except Exception as exc:      # never let the watcher die
            print("[watcher]", exc)


socketio.start_background_task(_watcher)


if __name__ == "__main__":
    print("=" * 62)
    print(" THULIR / CryoAxis Process Monitor")
    print(f" Database:  {storage.DB_PATH}")
    print(" Open on this PC:      http://localhost:5000")
    print(" Open from other devices on the same WiFi:")
    print("   http://<this-PC-LAN-IP>:5000   (find IP via `ipconfig`)")
    print("=" * 62)
    socketio.run(app, host="0.0.0.0", port=5000, debug=False, allow_unsafe_werkzeug=True)
