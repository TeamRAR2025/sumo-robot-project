# Motor ESP32 UART Integration Guide

This document defines the wired protocol between the XIAO ESP32-S3 Sense and the second ESP32 that controls the motors.

The recommended architecture is:

```text
Browser <--Wi-Fi--> XIAO ESP32-S3 Sense <--UART wires--> Motor ESP32
```

Wi-Fi is used only where it is useful: browser UI, camera stream, QR display, and click-and-go interaction. The robot-internal control link is a short wired UART connection.

## System Roles

```text
Browser
  -> opens XIAO UI
  -> clicks camera image
  -> sees QR result

XIAO ESP32-S3 Sense
  -> creates Wi-Fi AP
  -> serves camera stream and web UI
  -> converts click into timed movement
  -> sends UART JSON-lines to motor ESP32
  -> sends QR IDs over UART when forwarding is enabled

Motor ESP32
  -> receives UART JSON-lines
  -> drives motor driver pins
  -> sends UART JSON acknowledgements
  -> stops motors if UART link goes silent
```

## Wiring

Default XIAO firmware pins:

```text
XIAO D6 / GPIO43 / TX -> Motor ESP32 RX
XIAO D7 / GPIO44 / RX <- Motor ESP32 TX
XIAO GND              -> Motor ESP32 GND
```

Default motor skeleton pins:

```text
Motor ESP32 GPIO16 / RX2 <- XIAO D6 / TX
Motor ESP32 GPIO17 / TX2 -> XIAO D7 / RX
```

Both boards use 3.3 V UART logic. Do not use 5 V level shifting.

If you choose different motor ESP32 pins, update:

```text
motor_esp32_uart/src/main.cpp
```

```cpp
static constexpr int MOTOR_UART_RX_PIN = 16;
static constexpr int MOTOR_UART_TX_PIN = 17;
```

If you choose different XIAO pins, update:

```text
xiao_click_go/src/main.cpp
```

```cpp
static constexpr int MOTOR_UART_TX_PIN = D6;
static constexpr int MOTOR_UART_RX_PIN = D7;
```

## UART Settings

```text
Baud: 115200
Data: 8 bits
Parity: none
Stop: 1 bit
Message format: UTF-8 JSON followed by newline
```

Every message is one line:

```text
{"seq":1,"type":"cmd","cmd":"f","speed":150}\n
```

## Messages From XIAO To Motor ESP32

### Movement Command

```json
{"seq":1,"type":"cmd","cmd":"f","speed":150}
```

Fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `seq` | integer | Monotonic message ID |
| `type` | string | Always `cmd` for movement |
| `cmd` | string | Movement command |
| `speed` | integer | PWM-style speed, `0..255` |

Command codes:

| Code | Meaning | Required Behavior |
| --- | --- | --- |
| `f` | Forward | Both wheels drive forward |
| `b` | Backward | Both wheels drive backward |
| `l` | Left | Turn left in place or with your chosen turn strategy |
| `r` | Right | Turn right in place or with your chosen turn strategy |
| `s` | Stop | Stop both motors immediately |

### QR ID

XIAO sends this only when the browser UI checkbox is enabled:

```text
Forward QR ID to motor ESP32
```

Message:

```json
{"seq":2,"type":"qr","id":"A1"}
```

The motor ESP32 may store this value, use it as a target ID, or ignore it. Do not move motors directly from a QR message unless the team intentionally wants QR-triggered behavior.

### Heartbeat

XIAO sends heartbeat messages about every 500 ms:

```json
{"seq":3,"type":"heartbeat","ms":123456}
```

The motor ESP32 should use any valid incoming message as proof that the link is alive.

## Messages From Motor ESP32 To XIAO

The motor ESP32 should acknowledge every valid line with one JSON line.

Command acknowledgement:

```json
{"seq":1,"status":"ok","type":"cmd","cmd":"f","speed":150}
```

QR acknowledgement:

```json
{"seq":2,"status":"ok","type":"qr","id":"A1"}
```

Error acknowledgement:

```json
{"seq":4,"status":"error","type":"cmd","error":"unknown command"}
```

XIAO currently displays the latest raw acknowledgement in `/api/status` as:

```json
{
  "last_motor_ack": "{\"seq\":1,\"status\":\"ok\",\"type\":\"cmd\",\"cmd\":\"f\",\"speed\":150}"
}
```

## XIAO Command Timing

XIAO does not send one high-level `drive_to` command.

It sends simple timed motor states:

```text
cmd=r
wait turn_ms
cmd=s
wait short settle time
cmd=f
wait drive_ms
cmd=s
```

The motor ESP32 must apply each command immediately and keep that motor state until the next command arrives or the UART watchdog fires.

## Safety Requirements

The motor ESP32 should implement these rules:

1. `cmd=s` must stop motors immediately.
2. Unknown commands must stop motors and return an error ACK.
3. Invalid or too-long messages must stop motors.
4. If the UART link goes silent, stop motors.
5. Clamp speed to `0..255`.
6. Start with motor outputs disabled until direction logic is verified.

Recommended link watchdog:

```text
If no UART message arrives for 1200 ms, stop motors.
```

This works because XIAO sends heartbeat messages while it is alive. If XIAO crashes or a wire disconnects, heartbeat stops and the motor ESP32 stops the robot.

## Provided Motor Firmware Skeleton

A safe starter firmware is included:

```text
motor_esp32_uart/
```

It provides:

- UART receive on `Serial2`
- JSON-line parsing for `cmd`, `qr`, and `heartbeat`
- JSON ACKs back to XIAO
- link watchdog
- safe disabled motor outputs by default

Build it:

```powershell
cd C:\Users\finmi\PycharmProjects\sumo-robot-project\motor_esp32_uart
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
..\venv\Scripts\pio.exe run
```

Before enabling motors, edit:

```text
motor_esp32_uart/src/main.cpp
```

Fill in:

```cpp
void stopMotors()
void driveForward(int speed)
void driveBackward(int speed)
void turnLeft(int speed)
void turnRight(int speed)
```

Then set:

```cpp
static constexpr bool MOTOR_OUTPUTS_ENABLED = true;
```

## Integration Test Sequence

### 1. Test XIAO Alone

Upload XIAO firmware and open serial monitor.

Expected:

```text
Starting XIAO Click-And-Go
Motor UART: baud=115200 TX=D6/GPIO43 RX=D7/GPIO44
XIAO AP SSID: SumoVision
```

Open:

```text
http://192.168.4.1
```

The UI should load and camera stream should appear.

### 2. Test Motor ESP32 Alone

Upload `motor_esp32_uart` and open its serial monitor.

Expected:

```text
Motor ESP32 UART firmware skeleton
UART: baud=115200 RX=GPIO16 TX=GPIO17
Motor outputs enabled: false
```

### 3. Wire The Boards

Power off before wiring:

```text
XIAO D6/TX -> Motor ESP32 GPIO16/RX2
XIAO D7/RX <- Motor ESP32 GPIO17/TX2
GND        -> GND
```

Power both boards.

### 4. Check Heartbeat

In the motor ESP32 serial monitor, you should see messages like:

```text
UART RX: {"seq":1,"type":"heartbeat","ms":123456}
UART TX: {"seq":1,"status":"ok","type":"heartbeat"}
```

In the XIAO UI `/api/status`, `last_motor_ack` should update.

### 5. Test Click-To-Command

Open:

```text
http://192.168.4.1
```

Click the camera image.

Motor ESP32 serial monitor should show a sequence similar to:

```text
UART RX: {"seq":10,"type":"cmd","cmd":"r","speed":150}
UART RX: {"seq":11,"type":"cmd","cmd":"s","speed":0}
UART RX: {"seq":12,"type":"cmd","cmd":"f","speed":150}
UART RX: {"seq":13,"type":"cmd","cmd":"s","speed":0}
```

With motor outputs disabled, this only logs. After motor functions are implemented and tested, the robot should move.

### 6. Test QR Forwarding

Open XIAO UI:

```text
http://192.168.4.1
```

Enable:

```text
Forward QR ID to motor ESP32
```

Show QR code `A1` to the camera.

Expected motor serial output:

```text
UART RX: {"seq":20,"type":"qr","id":"A1"}
UART TX: {"seq":20,"status":"ok","type":"qr","id":"A1"}
```

Expected XIAO UI:

```text
Last QR: A1
Sent: uart seq 20
```

## Troubleshooting

| Symptom | Likely Cause | Fix |
| --- | --- | --- |
| Motor ESP32 sees no heartbeat | TX/RX swapped, missing GND, wrong pins | Check wiring and pin constants |
| XIAO sees no ACK | Motor TX not connected to XIAO RX | Check XIAO D7/RX and motor TX pin |
| Garbled serial data | Baud mismatch | Use 115200 on both boards |
| Motor logs commands but robot does not move | `MOTOR_OUTPUTS_ENABLED` is false | Fill motor functions and enable outputs |
| Robot moves wrong direction | Motor wiring or direction logic inverted | Fix motor-side functions |
| Robot keeps moving after XIAO unplugged | Watchdog not stopping motors | Check `LINK_TIMEOUT_MS` and `stopMotors()` |
| QR appears in UI but not on motor ESP32 | QR forwarding checkbox is off or UART broken | Enable forwarding and check heartbeat |

## Final Checklist

- [ ] XIAO UI loads over Wi-Fi
- [ ] XIAO camera stream works
- [ ] XIAO serial monitor shows UART pins
- [ ] Motor ESP32 serial monitor starts correctly
- [ ] XIAO D6/TX is wired to motor RX
- [ ] XIAO D7/RX is wired to motor TX
- [ ] GND is shared
- [ ] Motor ESP32 receives heartbeat
- [ ] XIAO receives ACK
- [ ] Click sends `cmd` messages over UART
- [ ] QR forwarding sends `qr` messages over UART
- [ ] Motor watchdog stops motors when UART link is silent
- [ ] Motor outputs are enabled only after direction testing
