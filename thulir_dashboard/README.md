# CryoAxis / THULIR Process Monitor

Flask + WebSocket dashboard for the THULIR cryopreservation controller.
The ESP32 firmware POSTs telemetry to `/api/data`; the dashboard derives the run
lifecycle from the device state, logs every run to SQLite, and exports runs to Excel.

## Run

```bash
cd thulir_final/thulir_dashboard
pip install -r requirements.txt
python app.py
```

Open http://localhost:5000 (or `http://<PC-LAN-IP>:5000` from another device on the same WiFi).
Point the firmware at this PC in `Config.h` (`WEB_SERVER_HOST`, `WEB_SERVER_PORT`).

## Pages
Home · Run Monitor · Protocols · Run History · User Guide · Settings

## Data (`data/cryoaxis.db`, SQLite)
Protocols (versioned), runs, per-reading measurements, events, alerts, settings.
Override the location with the `THULIR_DB` environment variable.
Export any run: Run History → Excel, or Run Monitor → Export to Excel.
(`data/telemetry.xlsx` is the old log from the previous dashboard; it is no longer written.)

## Remote control (dashboard -> device)
The server attaches a pending command to the reply of the device's telemetry POST; the firmware
runs it through the same code as the keypad and reports an acknowledgement in its next telemetry
packet (`ackId/ackOk/ackMsg`). The dashboard only says "success" once that ack arrives
(15 s timeout, cancelled if the device reboots).

- **START** sends the protocol's recipe (4 hold times + step 2/4 temperatures). The device applies it in RAM
  only (the recipe saved on the keypad is not overwritten), then runs its normal recipe validation and
  pre-start safety check. Refused if the operator is in a keypad menu, or the device is not idle.
- **ABORT** triggers the same emergency stop as the keypad (also opens the servos).
- **PAUSE / RESUME** are not supported by the firmware and stay disabled.
- Only device-compatible protocols can run: steps 1-4 are holds (step 1 = 25 °C, step 2 = 0/15/25 °C,
  step 3 = 4 °C, step 4 = 0/25 °C, whole minutes 1-999) and step 5 is the ramp to -20 °C at -1 °C/min.
- Optional safety: set the `THULIR_CONTROL_PIN` environment variable to require a PIN for START.
- Disable on the device with `WEB_REMOTE_CONTROL_ENABLED false` in `Config.h`.
- The server has no login: anyone who can open the dashboard can press ABORT/START. Keep it on a trusted LAN.

## Other limitations
- Automated pipetting is a static "Not Implemented" placeholder card only.
- User guide PDF: drop it at `static/docs/user_guide.pdf` to enable the download.
- Hardware speaker language cannot be changed from software; language setting affects browser audio only.
- Chart.js / Socket.IO load from a CDN (needs internet on the viewing device; page falls back to polling).

## Tests
```bash
python tests/test_backend.py          # software tests on synthetic telemetry, isolated temp DB
python tests/feed_synthetic.py 120    # synthetic feeder for manual UI testing only
```
