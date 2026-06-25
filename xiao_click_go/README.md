# FrontCam XIAO Firmware

This PlatformIO firmware turns one Seeed Studio XIAO ESP32-S3 Sense into the front-camera, browser UI, and color-sensor board.

Current architecture:

```text
Browser -> FrontCam UI at 192.168.4.1
Browser -> Front camera stream at 192.168.4.1:81/stream
Browser -> QR reader stream at 192.168.4.2:81/stream
Browser -> QR Reader motor API at 192.168.4.2/api/*
FrontCam XIAO -> color sensor over I2C
QR Reader XIAO -> motor driver + distance sensor
```

There is no UART link and no separate motor ESP32 in the current wiring.

## Default Network

FrontCam creates the robot Wi-Fi access point:

```text
SSID: SumoVision
Password: sumo1234
FrontCam IP: 192.168.4.1
```

Open the UI from a laptop or phone connected to `SumoVision`:

```text
http://192.168.4.1
```

The front camera MJPEG stream is:

```text
http://192.168.4.1:81/stream
```

The UI expects the QR Reader XIAO at:

```text
http://192.168.4.2
http://192.168.4.2:81/stream
```

## Connected Hardware

FrontCam board:

```text
XIAO ESP32-S3 Sense camera
Color sensor on I2C
SDA: D4 / GPIO5
SCL: D5 / GPIO6
Default color sensor address: 0x29
```

The color sensor code is written for M5Stack Unit Color / TCS3472-style modules.

## PlatformIO Commands

From this directory:

```powershell
pio run
pio run --target upload
pio device monitor
```

If using the repository virtual environment:

```powershell
..\venv\Scripts\pio.exe run
```

## Click-And-Go Flow

1. Browser opens `http://192.168.4.1`.
2. FrontCam serves the page and front camera stream.
3. Browser click is converted into pixel coordinates.
4. If calibration is enabled, browser applies the homography matrix.
5. Browser sends the movement plan to the QR Reader XIAO:

```text
POST http://192.168.4.2/api/click?... 
```

Without calibration, the browser uses fallback logic:

- left/right image position controls turn direction and turn duration;
- vertical image position controls forward duration;
- center click mostly drives forward.

After camera calibration, enable `CALIBRATION.enabled = true` in the page script and replace the `H` matrix with the measured homography.

## QR Scanner

The browser UI scans QR codes from the second XIAO camera stream.

Current flow:

```text
QR Reader MJPEG stream -> browser canvas -> jsQR decode -> UI result
```

If the `Send QR ID to QR reader board` checkbox is enabled, the browser also sends the detected ID to:

```text
POST http://192.168.4.2/api/qr?id=A1
```

FrontCam also stores the latest local QR result at:

```text
POST http://192.168.4.1/api/qr?id=A1
GET  http://192.168.4.1/api/status
```

Vendored QR decoder:

```text
Library: jsQR 1.4.0
Served as: /jsQR.js
Generated header: include/jsqr_gz.h
License: third_party/jsQR-LICENSE.txt
```

## Useful Endpoints

```text
GET  /                 browser UI
GET  /api/status       FrontCam status, QR reader URL, color sensor reading
GET  /api/color        color sensor reading
POST /api/qr?id=A1     store latest QR ID locally
GET  /capture          single JPEG frame
GET  :81/stream        front camera MJPEG stream
```

## Calibration TODO

Detailed calibration instructions are in [`CALIBRATION.md`](./CALIBRATION.md).

Measure:

- forward speed in `cm/s` at the chosen speed;
- turn speed in `deg/s`;
- homography matrix from front-camera pixel coordinates to ground coordinates.

Then update these constants in `../qr_reader_xiao/src/main.cpp`:

```cpp
TURN_MS_PER_DEG
DRIVE_MS_PER_CM
```

and update the `CALIBRATION.H` matrix in this firmware's HTML script.
