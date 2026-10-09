"""SYNTHETIC telemetry feeder for UI testing only (not hardware). Usage: python tests/feed_synthetic.py [seconds]"""
import json, sys, time, urllib.request
dur = int(sys.argv[1]) if len(sys.argv) > 1 else 120
t0 = time.time()
temp = 24.0
while time.time() - t0 < dur:
    el = time.time() - t0
    state = "IDLE" if el < 4 else ("APPROACHING" if el < 40 else "HOLDING")
    target = 25.0 if el < 60 else 15.0
    temp += (target - temp) * 0.05
    d = {"state": state, "error": "NONE", "step": 0 if el < 4 else 1, "filteredTemp": round(temp, 2), "actualTemp": round(temp, 2),
         "targetTemp": target, "setpoint": target, "humidity": 42.0, "humidityValid": True, "sensorValid": True,
         "pidOutput": 30, "bottomPWM": 20, "middlePWM": 15, "topPWM": 10, "rampLag": False, "rampLagAmount": 0,
         "elapsedMs": 0, "remainingMs": 0, "uptimeMs": int(el * 1000), "servosOpen": True}
    req = urllib.request.Request("http://localhost:5000/api/data", json.dumps(d).encode(), {"Content-Type": "application/json"})
    urllib.request.urlopen(req, timeout=3).read()
    time.sleep(2)
