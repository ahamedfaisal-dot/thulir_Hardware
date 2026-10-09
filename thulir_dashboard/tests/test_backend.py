"""
Software tests for the CryoAxis dashboard backend.

All telemetry here is SYNTHETIC (posted through the real /api/data endpoint into
a throw-away database) -- it exercises the software only, not real hardware.
Run:  python tests/test_backend.py
"""
import io
import os
import sys
import tempfile

tmp = tempfile.mkdtemp()
os.environ["THULIR_DB"] = os.path.join(tmp, "test.db")
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import monitor as M

clock = {"t": 1000.0}
M.time.monotonic = lambda: clock["t"]          # controllable clock

import app as A  # noqa: E402
import storage  # noqa: E402
from openpyxl import load_workbook  # noqa: E402

c = A.app.test_client()
passed = 0


def check(name, cond, extra=""):
    global passed
    assert cond, f"FAIL: {name} {extra}"
    passed += 1
    print("PASS", name)


def post(state, temp, target, step=1, hum=45.0, hv=True, sv=True, **kw):
    d = {"state": state, "error": "NONE", "step": step, "filteredTemp": temp, "actualTemp": temp,
         "targetTemp": target, "setpoint": target, "humidity": hum, "humidityValid": hv,
         "sensorValid": sv, "pidOutput": 10, "bottomPWM": 5, "middlePWM": 5, "topPWM": 5,
         "rampLag": False, "rampLagAmount": 0, "elapsedMs": 0, "remainingMs": 0, "uptimeMs": 1}
    d.update(kw)
    rr = c.post("/api/data", json=d)
    assert rr.status_code == 200, rr.data[:3000]
    return c.get("/api/state").json["snapshot"]


def adv(s):
    clock["t"] += s


# -- protocol validation / duration ------------------------------------
default = c.get("/api/protocols").json[0]
check("default protocol seeded from firmware recipe", default["valid"] and default["total_duration_s"] == 7200)
bad = c.post("/api/protocols", json={"name": "", "steps": []})
check("empty protocol rejected", bad.status_code == 422 and len(bad.json["errors"]) >= 2)
bad = c.post("/api/protocols", json={"name": "x", "steps": [{"name": "a", "type": "hold", "target": 500, "hold_min": 5}]})
check("out-of-range temperature rejected", bad.status_code == 422)
bad = c.post("/api/protocols", json={"name": "x", "steps": [{"name": "a", "type": "hold", "target": 5, "hold_min": 0}]})
check("non-positive duration rejected", bad.status_code == 422)
bad = c.post("/api/protocols", json={"name": "x", "steps": [
    {"name": "a", "type": "hold", "target": 5, "hold_min": 5}, {"name": "r", "type": "ramp", "target": -5, "ramp_rate": 1}]})
check("ramp direction/sign mismatch rejected", bad.status_code == 422)
ok = c.post("/api/protocols", json={"name": "Test A", "tolerance": 0.5, "steps": [
    {"name": "hold", "type": "hold", "target": 10, "hold_min": 1},
    {"name": "ramp", "type": "ramp", "target": 0, "ramp_rate": -2}]})
check("valid protocol created", ok.status_code == 201)
pa = ok.json["protocol"]
check("duration = hold + ramp (60 s + 300 s)", pa["total_duration_s"] == 360.0, pa["total_duration_s"])

v2 = c.post(f"/api/protocols/{pa['id']}/version", json={**pa, "name": "Test A", "description": "v2"})
check("save as new version creates new row", v2.status_code == 201 and v2.json["protocol"]["version"] == 2)
orig = c.get(f"/api/protocols/{pa['id']}").json
check("original untouched after new version", orig["version"] == 1 and orig["description"] == "")

# -- status, deviation sign, cooling rate ------------------------------
s = c.get("/api/state").json["snapshot"]
check("no device -> not connected", not s["connected"] and not s["ever_seen"])
check("select protocol", c.post(f"/api/protocols/{pa['id']}/select").status_code == 200)

s = post("READY", 20.0, 20.0, step=0)
check("READY state", s["status"] == "READY" and s["connected"])
r = c.post("/api/command", json={"command": "start"})
check("no remote-control flag from device -> start honestly not sent", r.status_code == 501 and r.json["sent"] is False)
check("pause/resume unsupported by firmware", c.post("/api/command", json={"command": "pause"}).status_code == 501)

s = post("APPROACHING", 12.5, 10.0, step=1)
check("RUNNING + run started from device state", s["status"] == "RUNNING" and s["run"]["active"])
check("deviation = actual - target (+2.5)", s["temperature"]["deviation"] == 2.5)
rid = s["run"]["id"]
adv(2)
s = post("APPROACHING", 9.7, 10.0, step=1)
check("negative deviation sign", s["temperature"]["deviation"] == -0.3)
check("cooling rate unavailable with little data", s["temperature"]["cooling_rate"] is None)

for i in range(70):
    adv(2)
    s = post("HOLDING", 9.7 - (i + 1) * (2 / 60.0), 10.0, step=1)
check("cooling rate ~ -1.0 C/min", s["temperature"]["cooling_rate"] is not None
      and abs(s["temperature"]["cooling_rate"] + 1.0) < 0.1, s["temperature"]["cooling_rate"])
check("progress within 0..100", 0 <= s["run"]["progress_pct"] <= 100)
check("progress clamped at 100 when run overshoots plan", s["run"]["progress_pct"] <= 100.0)
adv(2)
s = post("HOLDING", 8.0, 8.0, step=1)
check("elapsed ~ wall time since start", s["run"]["elapsed_s"] > 80)
check("tolerance status within", s["temperature"]["tolerance_status"] == "within")

adv(2)
s = post("HOLDING", 8.0, 8.0, step=1, hv=False, hum=0)
check("humidity unavailable not shown as 0", s["humidity"]["value"] is None and not s["humidity"]["available"])
check("humidity alert raised", any(a["key"] == "humidity_unavailable" for a in s["alerts"]))
adv(2)
s = post("HOLDING", 8.0, 8.0, step=1)
check("humidity alert resolved when sensor returns", not any(a["key"] == "humidity_unavailable" for a in s["alerts"]))

adv(2)
s = post("HOLDING", 9.0, 8.0, step=1)
check("deviation alert raised", any(a["key"] == "temp_deviation" for a in s["alerts"]))
adv(2)
s = post("HOLDING", 8.1, 8.0, step=1)
check("deviation alert cleared", not any(a["key"] == "temp_deviation" for a in s["alerts"]))

adv(2)
s = post("HOLDING", 8.0, 8.0, step=1, sv=False)
check("invalid sensor -> actual unavailable + alert",
      s["temperature"]["actual"] is None and any(a["key"] == "sensor_invalid" for a in s["alerts"]))
adv(2)
s = post("HOLDING", 8.0, 8.0, step=1)

adv(2)
s = post("TRANSITIONING", 8.0, 0.0, step=2)
evs = c.get("/api/events?limit=300").json
check("step change logged once per change",
      sum(1 for e in evs if e["type"] == "step_changed" and e["run_id"] == rid) == 2)

# -- pause / resume timing (device-reported PAUSED) ---------------------
adv(2)
s = post("HOLDING", 8.0, 8.0, step=2)
e1 = s["run"]["elapsed_s"]
adv(2)
s = post("PAUSED", 8.0, 8.0, step=2)
adv(30)
s = c.get("/api/state").json["snapshot"]
check("status PAUSED", s["status"] == "PAUSED")
adv(2)
s = post("PAUSED", 8.0, 8.0, step=2)
check("elapsed frozen while paused", abs(s["run"]["elapsed_s"] - (e1 + 2)) < 0.5, (s["run"]["elapsed_s"], e1))
check("resume reported as unsupported, nothing sent",
      c.post("/api/command", json={"command": "resume"}).json["sent"] is False)
adv(2)
s = post("HOLDING", 8.0, 8.0, step=2)
check("paused time excluded from elapsed", abs(s["run"]["elapsed_s"] - (e1 + 2)) < 0.5, (s["run"]["elapsed_s"], e1))
adv(2)
s = post("HOLDING", 8.0, 8.0, step=2)
check("timer counts again after resume", abs(s["run"]["elapsed_s"] - (e1 + 4)) < 0.5, (s["run"]["elapsed_s"], e1))

# -- hardware disconnect -------------------------------------------------
adv(30)
s = c.get("/api/state").json["snapshot"]
check("stale telemetry -> disconnected, values unavailable", not s["connected"] and s["temperature"]["actual"] is None)
A.mon.tick()
check("hw_offline alert raised", any(a["key"] == "hw_offline" for a in A.mon.snapshot()["alerts"]))
r = c.post("/api/command", json={"command": "abort"})
check("command with hardware disconnected reports failure", r.status_code == 409 and not r.json["sent"])
s = post("HOLDING", 8.0, 8.0, step=2)
check("reconnect clears offline alert", not any(a["key"] == "hw_offline" for a in s["alerts"]))

check("cannot delete protocol in active run", c.delete(f"/api/protocols/{pa['id']}").status_code == 409)
check("cannot edit protocol in active run", c.put(f"/api/protocols/{pa['id']}", json=pa).status_code == 409)
check("cannot switch protocol during run", c.post(f"/api/protocols/{default['id']}/select").status_code == 409)

adv(2)
s = post("COMPLETE", 0.0, 0.0, step=5)
check("COMPLETED from device state", s["status"] == "COMPLETED" and s["run"]["final_status"] == "COMPLETED")
check("progress 100% on completion", s["run"]["progress_pct"] == 100.0)
runs = c.get("/api/runs").json
check("run persisted with final status", runs[0]["id"] == rid and runs[0]["final_status"] == "COMPLETED")

adv(5)
s = post("IDLE", 20.0, 20.0, step=0)
adv(2)
s = post("APPROACHING", 19, 10.0, step=1)
adv(2)
s = post("STOPPED", 19, 10.0, step=1, error="EMERGENCY STOP")
check("STOPPED -> run ABORTED (never COMPLETED), status ERROR",
      s["status"] == "ERROR" and c.get("/api/runs").json[0]["final_status"] == "ABORTED")
check("device fault alert", any(a["key"] == "device_fault" for a in s["alerts"]))

# -- Excel export ---------------------------------------------------------
resp = c.get(f"/api/runs/{rid}/export")
check("excel export 200 xlsx", resp.status_code == 200 and "spreadsheetml" in resp.mimetype)
wb = load_workbook(io.BytesIO(resp.data))
ws = wb["Run Data"]
hdr = [x.value for x in ws[1]]
check("excel columns per spec", hdr[0] == "Run ID" and hdr[6] == "Actual Temperature (°C)"
      and hdr[-1] == "Event Details" and len(hdr) == 14)
check("excel has measurement + event rows",
      ws.max_row > 40 and any(r[12].value == "step_changed" for r in ws.iter_rows(min_row=2)))
check("excel has no pipetting data", not any("pipet" in str(v.value).lower() for r in ws.iter_rows() for v in r))
check("export of unknown run -> 404", c.get("/api/runs/NOPE/export").status_code == 404)


# -- remote command channel (synthetic firmware behaviour) ------------------
import protocols as P

def api_cmd(name, **kw):
    r = c.post("/api/command", json={"command": name, **kw})
    return r.status_code, r.json

adv(5)
s = post("IDLE", 20.0, 20.0, step=0, remoteCtl=True)
check("device reports remote control -> start allowed in snapshot (protocol incompatible so blocked)",
      s["commands"]["channel"] and not s["commands"]["start"])
sc, b = api_cmd("start")
check("incompatible protocol cannot be started", sc == 400 and "cannot run" in b["message"], b)
check("2-step protocol flagged not device-compatible",
      not c.get(f"/api/protocols/{pa['id']}").json["device_compatible"])
check("default recipe is device-compatible", c.get(f"/api/protocols/{default['id']}").json["device_compatible"])
check("select default protocol", c.post(f"/api/protocols/{default['id']}/select").status_code == 200)

M.CONTROL_PIN = "1234"
sc, b = api_cmd("start")
check("start needs PIN when configured", sc == 403)
sc, b = api_cmd("start", pin="1234")
check("start queued (202), not yet success", sc == 202 and b["pending"] and b["id"] > 0)
cid = b["id"]
reply = c.post("/api/data", json={"state": "IDLE", "step": 0, "filteredTemp": 20, "targetTemp": 20, "setpoint": 20,
                                  "sensorValid": True, "humidityValid": True, "humidity": 40, "remoteCtl": True,
                                  "uptimeMs": 1}).json
check("command delivered in telemetry reply with recipe",
      reply["cmd"]["id"] == cid and reply["cmd"]["name"] == "start" and reply["cmd"]["h"] == [30, 30, 20, 20]
      and reply["cmd"]["t2"] == 15.0 and reply["cmd"]["t4"] == 0.0, reply)
check("no run before device acknowledges", not c.get("/api/state").json["snapshot"]["run"])
sc, b = api_cmd("start", pin="1234")
check("second command refused while one is pending", sc == 409)
s = post("APPROACHING", 20.0, 25.0, step=1, remoteCtl=True, ackId=cid, ackOk=True, ackMsg="started")
check("ack clears pending; run now exists because device reports it", s["run"]["active"] and s["commands"]["pending"] is None)
check("reply stops carrying the command after ack",
      "cmd" not in c.post("/api/data", json={"state": "APPROACHING", "step": 1, "filteredTemp": 20, "targetTemp": 25,
                                              "setpoint": 25, "sensorValid": True, "remoteCtl": True, "uptimeMs": 1,
                                              "ackId": cid, "ackOk": True}).json)
evs = [e["type"] for e in c.get("/api/events?limit=50").json]
check("command_sent and command_ack logged", "command_sent" in evs and "command_ack" in evs)
check("pause still unsupported while running", api_cmd("pause")[0] == 501)

sc, b = api_cmd("abort")
check("abort queued", sc == 202)
aid = b["id"]
s = post("STOPPED", 18.0, 25.0, step=1, remoteCtl=True, ackId=aid, ackOk=True, ackMsg="stopped", error="EMERGENCY STOP")
check("abort acked -> run ABORTED, never COMPLETED", c.get("/api/runs").json[0]["final_status"] == "ABORTED")

adv(5); s = post("IDLE", 20.0, 20.0, step=0, remoteCtl=True)
sc, b = api_cmd("start", pin="1234")
bid = b["id"]
s = post("IDLE", 20.0, 20.0, step=0, remoteCtl=True, ackId=bid, ackOk=False, ackMsg="operator busy at keypad")
evs = c.get("/api/events?limit=10").json
check("device rejection reported, no run started",
      any(e["type"] == "command_rejected" and "operator busy" in e["message"] for e in evs) and not s["run"])

sc, b = api_cmd("start", pin="1234")
adv(20); A.mon.tick()
evs = c.get("/api/events?limit=10").json
check("unacknowledged command times out", any("no acknowledgement" in e["message"] for e in evs)
      and c.get("/api/state").json["snapshot"]["commands"]["pending"] is None)

s = post("IDLE", 20.0, 20.0, step=0, remoteCtl=True, uptimeMs=50000)
sc, b = api_cmd("start", pin="1234")
s = post("IDLE", 20.0, 20.0, step=0, remoteCtl=True, uptimeMs=300)
evs = c.get("/api/events?limit=10").json
check("device reboot cancels pending command", any("restarted" in e["message"] for e in evs))
M.CONTROL_PIN = None
s = post("IDLE", 20.0, 20.0, step=0, remoteCtl=False)
check("remote control disabled on device -> channel off", not s["commands"]["channel"] and api_cmd("start")[0] == 501)

# -- persistence across restart ---------------------------------------------
c.put("/api/settings", json={"audio": {"enabled": False, "language": "ta", "volume": 0.4, "rate": 1.3}})
conn = storage._conn
storage._conn = None
conn.close()
storage.init()
check("protocols persist after restart", len(storage.list_protocols()) == 3)
check("runs persist after restart", len(storage.list_runs()) == 3)
a = storage.get_setting("audio")
check("audio prefs persist after restart", a["language"] == "ta" and a["enabled"] is False and a["volume"] == 0.4)
check("invalid language ignored",
      c.put("/api/settings", json={"audio": {"language": "xx"}}).json["audio"]["language"] == "ta")

check("delete protocol (run finished)", c.delete(f"/api/protocols/{pa['id']}").status_code == 200)
check("history keeps protocol snapshot after delete",
      c.get(f"/api/runs/{rid}").json["protocol_snapshot"]["name"] == "Test A")

check("guide PDF absent -> 404", c.get("/userguide.pdf").status_code == 404)
check("background image served from project-relative dir", c.get("/bg_image/thulirbg.png").status_code == 200)
check("index renders", b"CryoAxis" in c.get("/").data)
check("no pipetting API exists", c.get("/api/pipetting").status_code == 404)
print(f"\nALL {passed} CHECKS PASSED")
