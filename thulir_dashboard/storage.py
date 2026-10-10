"""
Persistent storage (SQLite) for the CryoAxis dashboard.

Tables: protocols, runs, samples, events, alerts, settings.
All access goes through one connection guarded by an RLock so the
Flask request threads and the background watcher can share it.
"""

import json
import os
import sqlite3
import threading
from datetime import datetime

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(BASE_DIR, "data")
DB_PATH = os.environ.get("THULIR_DB") or os.path.join(DATA_DIR, "cryoaxis.db")

SCHEMA = """
CREATE TABLE IF NOT EXISTS protocols (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    family_id INTEGER NOT NULL,
    version INTEGER NOT NULL,
    name TEXT NOT NULL,
    description TEXT DEFAULT '',
    tolerance REAL,
    steps TEXT NOT NULL,
    created_at TEXT NOT NULL,
    modified_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS runs (
    id TEXT PRIMARY KEY,
    protocol_id INTEGER,
    protocol_name TEXT,
    protocol_version INTEGER,
    protocol_snapshot TEXT,
    started_at TEXT NOT NULL,
    ended_at TEXT,
    final_status TEXT,
    paused_total_s REAL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS samples (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id TEXT NOT NULL,
    ts TEXT NOT NULL,
    elapsed_s REAL,
    step INTEGER,
    step_name TEXT,
    actual REAL,
    target REAL,
    deviation REAL,
    cooling_rate REAL,
    humidity REAL,
    status TEXT,
    device_state TEXT,
    setpoint REAL,
    pid_output REAL
);
CREATE INDEX IF NOT EXISTS idx_samples_run ON samples(run_id, id);
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id TEXT,
    ts TEXT NOT NULL,
    elapsed_s REAL,
    type TEXT NOT NULL,
    subsystem TEXT,
    severity TEXT,
    message TEXT,
    details TEXT
);
CREATE INDEX IF NOT EXISTS idx_events_run ON events(run_id, id);
CREATE TABLE IF NOT EXISTS alerts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    key TEXT NOT NULL,
    run_id TEXT,
    severity TEXT,
    subsystem TEXT,
    message TEXT,
    raised_at TEXT NOT NULL,
    resolved_at TEXT
);
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
"""

_lock = threading.RLock()
_conn = None


def now_iso():
    return datetime.now().isoformat(timespec="seconds")


def init(path=None):
    """Open (and create) the database. Safe to call more than once."""
    global _conn
    with _lock:
        if _conn is not None:
            return
        p = path or DB_PATH
        os.makedirs(os.path.dirname(p), exist_ok=True)
        _conn = sqlite3.connect(p, check_same_thread=False)
        _conn.row_factory = sqlite3.Row
        _conn.executescript(SCHEMA)
        _conn.commit()


def _q(sql, args=(), one=False):
    with _lock:
        cur = _conn.execute(sql, args)
        rows = cur.fetchall()
    return (rows[0] if rows else None) if one else rows


def _x(sql, args=()):
    with _lock:
        cur = _conn.execute(sql, args)
        _conn.commit()
        return cur.lastrowid


# ── settings ────────────────────────────────────────────────────

DEFAULT_SETTINGS = {
    "audio": {"enabled": True, "language": "en", "volume": 1.0, "rate": 1.0},
    "selected_protocol_id": None,
    "thresholds": {"stale_after_s": 10},
}


def get_setting(key):
    row = _q("SELECT value FROM settings WHERE key=?", (key,), one=True)
    if row is None:
        return json.loads(json.dumps(DEFAULT_SETTINGS.get(key)))
    try:
        val = json.loads(row["value"])
    except (ValueError, TypeError):
        return json.loads(json.dumps(DEFAULT_SETTINGS.get(key)))
    default = DEFAULT_SETTINGS.get(key)
    if isinstance(default, dict) and isinstance(val, dict):
        merged = dict(default)
        merged.update(val)
        return merged
    return val


def set_setting(key, value):
    _x("INSERT INTO settings(key,value) VALUES(?,?) "
       "ON CONFLICT(key) DO UPDATE SET value=excluded.value", (key, json.dumps(value)))


# ── protocols ───────────────────────────────────────────────────

def _proto_row(r):
    if r is None:
        return None
    d = dict(r)
    d["steps"] = json.loads(d["steps"])
    return d


def list_protocols():
    return [_proto_row(r) for r in
            _q("SELECT * FROM protocols ORDER BY modified_at DESC, id DESC")]


def get_protocol(pid):
    return _proto_row(_q("SELECT * FROM protocols WHERE id=?", (pid,), one=True))


def create_protocol(name, description, tolerance, steps):
    ts = now_iso()
    with _lock:
        cur = _conn.execute(
            "INSERT INTO protocols(family_id,version,name,description,tolerance,steps,created_at,modified_at)"
            " VALUES(0,1,?,?,?,?,?,?)",
            (name, description, tolerance, json.dumps(steps), ts, ts))
        pid = cur.lastrowid
        _conn.execute("UPDATE protocols SET family_id=? WHERE id=?", (pid, pid))
        _conn.commit()
    return pid


def update_protocol(pid, name, description, tolerance, steps):
    _x("UPDATE protocols SET name=?,description=?,tolerance=?,steps=?,modified_at=? WHERE id=?",
       (name, description, tolerance, json.dumps(steps), now_iso(), pid))


def new_protocol_version(pid, name, description, tolerance, steps):
    """Insert a new version row in the same family; the original is untouched."""
    base = get_protocol(pid)
    ts = now_iso()
    top = _q("SELECT MAX(version) AS v FROM protocols WHERE family_id=?",
             (base["family_id"],), one=True)["v"]
    return _x(
        "INSERT INTO protocols(family_id,version,name,description,tolerance,steps,created_at,modified_at)"
        " VALUES(?,?,?,?,?,?,?,?)",
        (base["family_id"], (top or base["version"]) + 1, name, description,
         tolerance, json.dumps(steps), ts, ts))


def delete_protocol(pid):
    _x("DELETE FROM protocols WHERE id=?", (pid,))


def protocol_count():
    return _q("SELECT COUNT(*) AS c FROM protocols", one=True)["c"]


# ── runs / samples / events ─────────────────────────────────────

def create_run(run_id, proto, started_at):
    _x("INSERT INTO runs(id,protocol_id,protocol_name,protocol_version,protocol_snapshot,started_at)"
       " VALUES(?,?,?,?,?,?)",
       (run_id, proto["id"] if proto else None,
        proto["name"] if proto else None,
        proto["version"] if proto else None,
        json.dumps(proto) if proto else None, started_at))


def update_run_snapshot(run_id, proto):
    """Replace the run's stored protocol snapshot (device recipe adjustments)."""
    _x("UPDATE runs SET protocol_snapshot=? WHERE id=?", (json.dumps(proto), run_id))


def finish_run(run_id, status, paused_total_s):
    _x("UPDATE runs SET ended_at=?, final_status=?, paused_total_s=? WHERE id=?",
       (now_iso(), status, paused_total_s, run_id))


def add_sample(run_id, s):
    _x("INSERT INTO samples(run_id,ts,elapsed_s,step,step_name,actual,target,deviation,"
       "cooling_rate,humidity,status,device_state,setpoint,pid_output) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
       (run_id, s["ts"], s["elapsed_s"], s["step"], s["step_name"], s["actual"], s["target"],
        s["deviation"], s["cooling_rate"], s["humidity"], s["status"], s["device_state"],
        s["setpoint"], s["pid_output"]))


def add_event(run_id, etype, subsystem, severity, message, elapsed_s=None, details=None):
    ts = now_iso()
    eid = _x("INSERT INTO events(run_id,ts,elapsed_s,type,subsystem,severity,message,details)"
             " VALUES(?,?,?,?,?,?,?,?)",
             (run_id, ts, elapsed_s, etype, subsystem, severity, message, details))
    return {"id": eid, "run_id": run_id, "ts": ts, "elapsed_s": elapsed_s, "type": etype,
            "subsystem": subsystem, "severity": severity, "message": message, "details": details}


def get_run(run_id):
    r = _q("SELECT * FROM runs WHERE id=?", (run_id,), one=True)
    return _run_dict(r) if r else None


def _run_dict(r):
    d = dict(r)
    d["protocol_snapshot"] = json.loads(d["protocol_snapshot"]) if d.get("protocol_snapshot") else None
    return d


def list_runs(limit=200):
    out = []
    for r in _q("SELECT * FROM runs ORDER BY started_at DESC, id DESC LIMIT ?", (limit,)):
        d = _run_dict(r)
        cnt = _q("SELECT severity, COUNT(*) AS c FROM events WHERE run_id=? AND type IN"
                 " ('alert_raised') GROUP BY severity", (d["id"],))
        d["warnings"] = sum(x["c"] for x in cnt if x["severity"] == "warning")
        d["errors"] = sum(x["c"] for x in cnt if x["severity"] == "error")
        out.append(d)
    return out


def last_run_id():
    r = _q("SELECT id FROM runs ORDER BY started_at DESC, id DESC LIMIT 1", one=True)
    return r["id"] if r else None


def run_samples(run_id):
    return [dict(r) for r in _q("SELECT * FROM samples WHERE run_id=? ORDER BY id", (run_id,))]


def run_events(run_id):
    return [dict(r) for r in _q("SELECT * FROM events WHERE run_id=? ORDER BY id", (run_id,))]


def recent_events(limit=100):
    return [dict(r) for r in _q("SELECT * FROM events ORDER BY id DESC LIMIT ?", (limit,))]


def open_runs():
    return [_run_dict(r) for r in _q("SELECT * FROM runs WHERE ended_at IS NULL")]


# ── alerts ──────────────────────────────────────────────────────

def raise_alert(key, run_id, severity, subsystem, message):
    return _x("INSERT INTO alerts(key,run_id,severity,subsystem,message,raised_at) VALUES(?,?,?,?,?,?)",
              (key, run_id, severity, subsystem, message, now_iso()))


def resolve_alert(alert_id):
    _x("UPDATE alerts SET resolved_at=? WHERE id=?", (now_iso(), alert_id))


def list_alerts(limit=100, run_id=None):
    if run_id:
        rows = _q("SELECT * FROM alerts WHERE run_id=? ORDER BY id DESC LIMIT ?", (run_id, limit))
    else:
        rows = _q("SELECT * FROM alerts ORDER BY id DESC LIMIT ?", (limit,))
    return [dict(r) for r in rows]


def close_stale_alerts():
    """After a server restart no alert can still be active — mark them resolved."""
    _x("UPDATE alerts SET resolved_at=? WHERE resolved_at IS NULL", (now_iso(),))
