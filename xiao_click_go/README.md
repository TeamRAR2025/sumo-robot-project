# XIAO Click-And-Go

This PlatformIO firmware turns the Seeed Studio XIAO ESP32-S3 Sense into the front-camera and browser UI module.

Architecture:

```text
browser -> XIAO web UI + camera stream -> HTTP commands -> motor ESP32
```

The motor ESP32 is expected to expose the Milestone 2 command endpoint:

```text
GET /cmd?c=f&s=150
GET /cmd?c=l&s=150
GET /cmd?c=r&s=150
GET /cmd?c=s&s=0
```

The full motor ESP32 protocol and integration checklist are documented in [`MOTOR_ESP32_INTEGRATION.md`](./MOTOR_ESP32_INTEGRATION.md).

## Default Network

By default XIAO creates its own access point:

```text
SSID: SumoVision
Password: sumo1234
XIAO IP: 192.168.4.1
Motor ESP32 URL: http://192.168.4.2
```

So the motor ESP32 should join `SumoVision` and use `192.168.4.2`, or you should edit `MOTOR_BASE_URL` in `src/main.cpp`.

Open the UI from a laptop or phone connected to `SumoVision`:

```text
http://192.168.4.1
```

The MJPEG stream is served separately at:

```text
http://192.168.4.1:81/stream
```

## PlatformIO Commands

From this directory:

```powershell
pio run
pio run --target upload
pio device monitor
```

If `pio` is not installed:

```powershell
python -m pip install platformio
```

## First Click-And-Go Test

1. Flash the XIAO firmware.
2. Flash/configure the motor ESP32 to join `SumoVision`.
3. Connect your browser device to `SumoVision`.
4. Open `http://192.168.4.1`.
5. Click the camera image.

Without calibration, the firmware uses fallback logic:

- left/right image position controls turn direction and turn duration;
- vertical image position controls forward duration;
- center click mostly drives forward.

After camera calibration, enable `CALIBRATION.enabled = true` in the page script and replace the `H` matrix with the measured homography.

## QR Scanner

The browser UI also scans QR codes from the same camera stream.

Current flow:

```text
XIAO MJPEG stream -> browser canvas -> QR decode -> POST /api/qr -> optional forward to motor ESP32
```

The implementation serves `jsQR` locally from XIAO as `/jsQR.js`, so QR scanning works offline while connected to the robot Wi-Fi. The browser `BarcodeDetector` API remains as a secondary fallback if `jsQR` is unavailable.

The UI shows:

- decoder mode
- last detected QR ID
- send status
- scanner status

XIAO stores the latest QR result in `/api/status`:

```json
{
  "last_qr_id": "A1",
  "qr_count": 3,
  "last_qr_forward_http_code": 200
}
```

Manual local QR test without camera:

```text
POST http://192.168.4.1/api/qr?id=A1&forward=0
```

If the `Forward QR ID to motor ESP32` checkbox is enabled, XIAO forwards the ID to:

```text
GET http://192.168.4.2/qr?id=A1
```

So the motor ESP32 should expose `/qr?id=...` if it needs to react to QR IDs. If the motor ESP32 does not implement this endpoint yet, leave forwarding disabled; the QR ID will still be detected and shown in the UI.

Vendored QR decoder:

```text
Library: jsQR 1.4.0
Served as: /jsQR.js
Generated header: include/jsqr_gz.h
License: third_party/jsQR-LICENSE.txt
```

## Calibration TODO

Detailed calibration instructions are in [`CALIBRATION.md`](./CALIBRATION.md).

Measure:

- forward speed in `cm/s` at the chosen speed;
- turn speed in `deg/s`;
- homography matrix from camera pixel coordinates to ground coordinates.

Then update these constants in `src/main.cpp`:

```cpp
TURN_MS_PER_DEG
DRIVE_MS_PER_CM
```

and update the `CALIBRATION.H` matrix in the HTML script.
