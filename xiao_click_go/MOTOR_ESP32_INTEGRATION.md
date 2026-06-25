# Two-XIAO HTTP Integration Guide

This document describes the current robot wiring and firmware split.

There is no separate motor ESP32 in the current architecture. The motor driver is connected directly to the `QR Reader XIAO`, and the two XIAO boards do not communicate over UART.

## System Architecture

```text
Browser <--Wi-Fi--> FrontCam XIAO ESP32-S3 Sense
Browser <--Wi-Fi--> QR Reader XIAO ESP32-S3 Sense

FrontCam XIAO:
  - creates SumoVision Wi-Fi
  - serves browser UI
  - serves front camera stream
  - reads color sensor

QR Reader XIAO:
  - joins SumoVision as 192.168.4.2
  - serves QR camera stream
  - receives HTTP movement commands
  - drives motor driver pins directly
  - reads distance sensor
```

The browser is the bridge between the two boards:

```text
click on front stream -> browser computes movement plan -> POST http://192.168.4.2/api/click
QR stream frame -> browser jsQR decode -> optional POST http://192.168.4.2/api/qr
```

## Wi-Fi Layout

FrontCam access point:

```text
SSID: SumoVision
Password: sumo1234
FrontCam IP: 192.168.4.1
FrontCam stream: http://192.168.4.1:81/stream
```

QR Reader station:

```text
QR Reader IP: 192.168.4.2
Gateway: 192.168.4.1
QR stream: http://192.168.4.2:81/stream
Robot API: http://192.168.4.2/api/status
```

The laptop or phone must be connected to `SumoVision`.

## Wiring Summary

### FrontCam XIAO

```text
XIAO ESP32-S3 Sense camera
Color sensor:
  SDA -> D4 / GPIO5
  SCL -> D5 / GPIO6
  VCC -> 3.3 V or module-supported VCC
  GND -> GND
```

Default color sensor address:

```text
0x29
```

### QR Reader XIAO

```text
XIAO ESP32-S3 Sense camera
Distance sensor:
  SDA -> D4 / GPIO5
  SCL -> D5 / GPIO6
  VCC -> module-supported VCC
  GND -> GND
```

Default distance sensor address:

```text
0x57
```

Default motor driver pins:

```text
DIR1 -> D0 / GPIO1
PWM1 -> D1 / GPIO2
PWM2 -> D2 / GPIO3
DIR2 -> D3 / GPIO4
GND  -> common GND
VCC  -> logic supply
VM   -> motor supply from step-down converter
```

If motor direction is reversed, change these constants in `qr_reader_xiao/src/main.cpp`:

```cpp
MOTOR1_FORWARD_HIGH
MOTOR2_FORWARD_HIGH
```

If the breadboard wiring changes, update:

```cpp
MOTOR_DIR1_PIN
MOTOR_PWM1_PIN
MOTOR_PWM2_PIN
MOTOR_DIR2_PIN
DISTANCE_SDA_PIN
DISTANCE_SCL_PIN
COLOR_SDA_PIN
COLOR_SCL_PIN
```

## HTTP API

### Movement From Browser To QR Reader

Click-and-go request:

```text
POST http://192.168.4.2/api/click?turn_dir=1&turn_ms=400&drive_ms=900&speed=150
```

Calibrated request:

```text
POST http://192.168.4.2/api/click?angle=25.0&distance=40.0&speed=150
```

The QR Reader turns these into timed motor states:

```text
turn left/right
stop briefly
drive forward
stop
```

Manual motor command:

```text
POST http://192.168.4.2/api/command?cmd=f&speed=130
```

Command codes:

| Code | Meaning |
| --- | --- |
| `f` | forward |
| `b` | backward |
| `l` | turn left |
| `r` | turn right |
| `s` | stop |

Emergency stop:

```text
POST http://192.168.4.2/api/stop
```

### QR ID

The browser stores every detected QR ID locally on FrontCam:

```text
POST http://192.168.4.1/api/qr?id=A1
```

If the UI checkbox is enabled, the browser also sends it to the robot-side board:

```text
POST http://192.168.4.2/api/qr?id=A1
```

The QR Reader stores the latest ID in `/api/status`. It does not automatically move from a QR message.

### Sensors

FrontCam color sensor:

```text
GET http://192.168.4.1/api/color
```

QR Reader distance sensor:

```text
GET http://192.168.4.2/api/distance
```

Combined status:

```text
GET http://192.168.4.1/api/status
GET http://192.168.4.2/api/status
```

## Integration Test Sequence

### 1. Flash FrontCam XIAO

```powershell
cd C:\Users\finmi\PycharmProjects\sumo-robot-project\xiao_click_go
..\venv\Scripts\pio.exe run --target upload --upload-port COMx
```

Expected serial output:

```text
Starting FrontCam XIAO
FrontCam AP SSID: SumoVision
FrontCam AP IP: 192.168.4.1
FrontCam UI ready on port 80
Front camera stream ready on port 81
```

Open:

```text
http://192.168.4.1
http://192.168.4.1/api/color
```

### 2. Flash QR Reader XIAO

```powershell
cd C:\Users\finmi\PycharmProjects\sumo-robot-project\qr_reader_xiao
..\venv\Scripts\pio.exe run --target upload --upload-port COMx
```

Expected serial output:

```text
Starting QR Reader Robot XIAO
Connecting QR reader to SSID: SumoVision
QR reader IP: 192.168.4.2
QR reader robot API ready on port 80
QR stream ready on port 81
```

Open:

```text
http://192.168.4.2
http://192.168.4.2/api/status
http://192.168.4.2/api/distance
http://192.168.4.2:81/stream
```

### 3. Test Motors Safely

Lift the robot so wheels cannot drive away.

Open:

```text
http://192.168.4.2
```

Use the manual motor buttons. If direction is wrong, change `MOTOR1_FORWARD_HIGH` or `MOTOR2_FORWARD_HIGH`.

### 4. Test Click-And-Go

Open:

```text
http://192.168.4.1
```

Click the front camera image.

Expected:

- FrontCam UI shows the clicked pixel and fallback or calibrated movement plan.
- Browser sends `/api/click` to `192.168.4.2`.
- QR Reader `/api/status` shows `motor.phase`, `turn_ms`, `drive_ms`, and `last_command`.
- Motors turn, stop briefly, drive forward, then stop.

### 5. Test QR

Open FrontCam UI:

```text
http://192.168.4.1
```

Show QR code `A1` to the QR reader camera.

Expected:

- `Last QR` updates in the FrontCam UI.
- If the checkbox is enabled, `http://192.168.4.2/api/status` shows `"last_qr_id":"A1"`.

## Troubleshooting

| Symptom | Likely Cause | Fix |
| --- | --- | --- |
| QR Reader does not appear at `192.168.4.2` | FrontCam AP is off, wrong SSID/password, or QR Reader booted first and has not reconnected yet | Start FrontCam first, then reset QR Reader |
| QR stream loads but QR decode fails | QR too small, blurred, bad lighting, or QR stream not visible in browser canvas | Move QR closer, improve lighting, check browser console |
| Clicks do not move robot | QR Reader is offline or `/api/click` blocked | Check `http://192.168.4.2/api/status` from the same browser |
| Motors do not move from manual buttons | Motor power missing, driver wiring wrong, or wrong pins | Check VM motor supply, GND, and pin constants |
| Robot moves backward when asked forward | Motor direction polarity is inverted | Change `MOTOR1_FORWARD_HIGH` or `MOTOR2_FORWARD_HIGH` |
| Distance reads offline | Wrong I2C pins, power, address, or sensor model | Check D4/D5 wiring and sensor address |
| Color reads offline | Wrong I2C pins, power, address, or sensor model | Check FrontCam D4/D5 wiring and sensor address |

## Final Checklist

- [ ] FrontCam XIAO UI loads at `http://192.168.4.1`
- [ ] Front camera stream works at `http://192.168.4.1:81/stream`
- [ ] Color sensor responds at `http://192.168.4.1/api/color`
- [ ] QR Reader joins `SumoVision` as `192.168.4.2`
- [ ] QR camera stream works at `http://192.168.4.2:81/stream`
- [ ] Distance sensor responds at `http://192.168.4.2/api/distance`
- [ ] Manual motor buttons work with robot lifted
- [ ] STOP button stops motors
- [ ] FrontCam UI shows both camera streams
- [ ] Click on front stream sends `/api/click` to QR Reader
- [ ] QR detection updates UI
- [ ] Optional QR forwarding updates QR Reader `/api/status`
- [ ] Robot still needs field calibration for real movement accuracy
