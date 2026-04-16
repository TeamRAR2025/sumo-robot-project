# Phone Server

This module adds the Milestone 2 phone-side communication and control server to the robotics repository.
It provides the middle layer for:

`browser -> Flask server on phone -> ESP32 -> motor`

The phone is expected to run in access point mode and expose a camera stream while the Flask app serves the control UI.

## How It Fits The Repository

`phone_server/` is a self-contained module inside the existing repository. It does not restructure or replace the rest of the project. The other folders such as `analysis`, `bom`, `cad`, `diagrams`, and `docs` remain untouched.

## Folder Structure

```text
phone_server/
|-- app.py
|-- config.py
|-- requirements.txt
|-- README.md
|-- esp32/
|   |-- README.md
|   \-- phone_ap_api_logger/
|       \-- phone_ap_api_logger.ino
|-- services/
|   \-- esp32_client.py
|-- templates/
|   \-- index.html
|-- static/
|   |-- css/
|   |   \-- styles.css
|   \-- js/
|       \-- controls.js
\-- tests/
    \-- test_routes.py
```

## What This Module Does

- Serves a mobile-friendly web page at `/` titled `Sumo Robot Control`
- Embeds the phone camera stream with `<img src="{{ camera_stream_url }}" alt="Camera stream">`
- Sends movement commands from the browser to Flask and then to the ESP32 over HTTP
- Tracks the current connection status, last command, and last speed in memory
- Exposes JSON endpoints for command sending, status checks, and health checks

## Default Network Configuration

The known Milestone 2 defaults are configured in [`config.py`](./config.py):

- Phone AP IP: `192.168.43.1`
- Camera base URL: `http://192.168.43.1:8080`
- Camera stream URL: `http://192.168.43.1:8080/video`
- ESP32 base URL: `http://192.168.43.2`
- ESP32 command endpoint: `/cmd`

## Local Run On A Laptop

From the repository root:

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r phone_server\requirements.txt
python phone_server\app.py
```

The Flask server listens on:

```text
http://0.0.0.0:5000
```

Open the UI in a browser:

```text
http://127.0.0.1:5000
```

Run the tests:

```powershell
python -m pytest phone_server\tests
```

## Later Run In Termux On The Phone

```sh
pkg update
pkg install python
cd /path/to/sumo-robot-project/phone_server
pip install -r requirements.txt
python app.py
```

When the phone is running in AP mode, connect a browser client to the phone network and open:

```text
http://192.168.43.1:5000
```

The camera stream is expected at:

```text
http://192.168.43.1:8080/video
```

## ESP32 Communication

The reusable ESP32 client lives in [`services/esp32_client.py`](./services/esp32_client.py).
An example ESP32 sketch that joins the phone AP and logs incoming API requests lives in [`esp32/phone_ap_api_logger/phone_ap_api_logger.ino`](./esp32/phone_ap_api_logger/phone_ap_api_logger.ino).

Current request format:

```text
GET /cmd?c=f&s=150
```

Command mapping:

- `FORWARD -> f`
- `BACKWARD -> b`
- `LEFT -> l`
- `RIGHT -> r`
- `STOP -> s`

The request-building logic is separated from the transport call so the project can later switch to POST JSON more easily.

## API Endpoints

### `GET /`

Renders the control page with:

- camera stream
- connection status
- last command
- speed input
- Forward, Backward, Left, Right, Stop buttons

### `POST /api/command`

Request:

```json
{
  "command": "FORWARD",
  "speed": 150
}
```

Successful response example:

```json
{
  "status": "ok",
  "sent_command": "FORWARD",
  "speed": 150,
  "esp32_response": "OK:f",
  "connection_status": "Connected to ESP32"
}
```

### `GET /api/status`

Example response:

```json
{
  "status": "running",
  "phone_ap_ip": "192.168.43.1",
  "camera_stream_url": "http://192.168.43.1:8080/video",
  "esp32_base_url": "http://192.168.43.2",
  "last_command": "STOP",
  "last_speed": 150,
  "connection_status": "Idle",
  "last_error": null
}
```

### `GET /health`

Example response:

```json
{
  "status": "ok"
}
```

## Camera Stream Behavior

The UI keeps working even if the camera stream fails to load. In that case the page shows a fallback message instead of blocking robot controls.

## Known Limitations

- No authentication is included
- No persistent storage or database is used
- The latest state is only kept in memory while the Flask process is running
- The Flask app assumes the ESP32 already exposes `/cmd`
- The camera stream is embedded directly and not proxied through Flask

## Next Steps

- Add hold-to-drive or touch events for smoother robot control
- Add an ESP32 health probe endpoint and display it in `/api/status`
- Add POST JSON support for ESP32 commands if the firmware changes
- Add optional telemetry such as battery level or sensor status
