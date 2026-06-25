# QR Reader Robot XIAO Firmware

This firmware turns the second Seeed Studio XIAO ESP32-S3 Sense into the robot-side board.

It handles:

- QR camera stream;
- motor driver control;
- distance sensor reading;
- HTTP API for click-and-go, stop, manual motor commands, QR IDs, and status.

It does not decode QR codes on the ESP32. The FrontCam browser UI loads this stream and decodes QR codes in JavaScript with `jsQR`.

## Role In The Robot

```text
QR Reader XIAO -> joins SumoVision Wi-Fi as 192.168.4.2
QR Reader XIAO -> serves QR camera stream
QR Reader XIAO -> drives the motor driver directly
QR Reader XIAO -> reads the distance sensor over I2C
FrontCam UI -> sends click-and-go HTTP requests to QR Reader XIAO
```

There is no separate motor ESP32 and no UART link in the current wiring.

## Default Network

The QR reader joins the FrontCam access point:

```text
SSID: SumoVision
Password: sumo1234
QR reader IP: 192.168.4.2
Gateway: 192.168.4.1
```

The stream URL expected by `xiao_click_go` is:

```text
http://192.168.4.2:81/stream
```

## Connected Hardware

Default motor driver pins:

```text
DIR1: D0 / GPIO1
PWM1: D1 / GPIO2
PWM2: D2 / GPIO3
DIR2: D3 / GPIO4
```

Default distance sensor pins:

```text
SDA: D4 / GPIO5
SCL: D5 / GPIO6
I2C address: 0x57
```

The distance sensor code is written for M5Stack Unit Sonic-style I2C modules.

If motor direction is reversed, change these constants in `src/main.cpp`:

```cpp
MOTOR1_FORWARD_HIGH
MOTOR2_FORWARD_HIGH
```

If the wiring changes, update:

```cpp
MOTOR_DIR1_PIN
MOTOR_PWM1_PIN
MOTOR_PWM2_PIN
MOTOR_DIR2_PIN
DISTANCE_SDA_PIN
DISTANCE_SCL_PIN
```

## Endpoints

```text
GET  /                         simple status page with camera preview and manual motor buttons
GET  /api/status               JSON status
GET  /api/distance             distance sensor reading
GET  /capture                  single JPEG frame
GET  :81/stream                MJPEG stream
POST /api/click?...            timed click-and-go movement plan
POST /api/command?cmd=f        manual motor command: f,b,l,r,s
POST /api/stop                 immediate stop
POST /api/qr?id=A1             store latest QR ID
```

All API and camera endpoints include CORS headers so the FrontCam UI can call them from `http://192.168.4.1`.

## Build

```powershell
cd C:\Users\finmi\PycharmProjects\sumo-robot-project\qr_reader_xiao
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
..\venv\Scripts\pio.exe run
```

Upload, replacing `COMx` with the QR reader XIAO serial port:

```powershell
..\venv\Scripts\pio.exe run --target upload --upload-port COMx
```

## Test

1. Flash and start the FrontCam XIAO first, so `SumoVision` exists.
2. Flash and start the QR Reader XIAO.
3. Connect a laptop or phone to `SumoVision`.
4. Open:

```text
http://192.168.4.2
http://192.168.4.2:81/stream
http://192.168.4.2/api/status
http://192.168.4.2/api/distance
```

5. On `http://192.168.4.2`, test manual buttons with the robot lifted from the table.
6. Open the FrontCam UI at `http://192.168.4.1`.
7. Confirm the QR reader camera appears in the QR panel.
8. Click the front camera image and confirm `/api/status` shows motor phase changes.
9. Show a QR code such as `A1` and confirm `Last QR` updates in the FrontCam UI.

## Safety Notes

- Keep the robot lifted during the first motor direction test.
- Use `POST http://192.168.4.2/api/stop` or the STOP button in the FrontCam UI to stop motors.
- `DISTANCE_AUTO_STOP_ENABLED` is currently `false`; distance is reported but does not automatically stop the robot.
