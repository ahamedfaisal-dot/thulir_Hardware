# TULIR Process Monitor (Web Dashboard)

Flask + WebSocket dashboard. ESP32 firmware POSTs telemetry here;
browser gets live updates; every reading logged to Excel.

## Setup

```bash
cd thulir_final/thulir_dashboard
pip install -r requirements.txt
python app.py
```

Server starts on port 5000, reachable from any device on same WiFi.

## Find this PC's LAN IP

Windows: `ipconfig`, look for IPv4 Address under active WiFi adapter
(e.g. `192.168.1.42`).

## Point firmware at this server

In `thulir_final/Config.h`:

```cpp
#define WEB_SERVER_HOST    "192.168.1.42"   // <-- this PC's IP
#define WEB_SERVER_PORT    5000
```

Re-flash. ESP32 must be on the same WiFi network/SSID as this PC.

## Open dashboard

- This PC: http://localhost:5000
- Phone/other PC on same WiFi: http://192.168.1.42:5000

## Data

Every reading appended to `data/telemetry.xlsx`. Download current
log anytime via the "Export Excel" button (top right of dashboard).

## Notes

- Dashboard is passive/monitor-only. Losing WiFi/server never affects
  onboard PID, safety, or Peltier control on the ESP32 — firmware
  runs identically with or without this connected.
- If port 5000 is in use, change `port=5000` in `app.py` (both the
  `socketio.run(...)` line and the printed URLs) and `WEB_SERVER_PORT`
  in Config.h to match.
