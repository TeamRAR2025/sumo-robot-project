# Motor ESP32 Integration Guide

This document defines what the second ESP32 must expose so the XIAO ESP32-S3 Sense can control the robot over HTTP.

## System Roles

```text
Browser
  -> opens XIAO UI
  -> clicks camera image
  -> sees QR result

XIAO ESP32-S3 Sense
  -> creates Wi-Fi AP
  -> serves camera stream and web UI
  -> converts click into timed motion command
  -> forwards HTTP commands to motor ESP32
  -> optionally forwards detected QR ID

Motor ESP32
  -> connects to XIAO Wi-Fi AP
  -> receives HTTP commands
  -> drives motor driver pins
  -> optionally receives QR ID
```

## Required Network Setup

Default XIAO access point:

```text
SSID: SumoVision
Password: sumo1234
XIAO IP: 192.168.4.1
```

The motor ESP32 should connect as a Wi-Fi station and use this static IP:

```text
Motor ESP32 IP: 192.168.4.2
Gateway: 192.168.4.1
Subnet: 255.255.255.0
```

The XIAO firmware currently sends requests to:

```cpp
static const char *MOTOR_BASE_URL = "http://192.168.4.2";
```

If the motor ESP32 uses another IP, update `MOTOR_BASE_URL` in:

```text
xiao_click_go/src/main.cpp
```

## Required Endpoint: Movement Command

The motor ESP32 must expose:

```text
GET /cmd?c=<command>&s=<speed>
```

### Command Codes

| Code | Meaning | Required Behavior |
| --- | --- | --- |
| `f` | Forward | Both wheels drive forward |
| `b` | Backward | Both wheels drive backward |
| `l` | Left | Turn left in place or with left-turn strategy |
| `r` | Right | Turn right in place or with right-turn strategy |
| `s` | Stop | Stop both motors immediately |

### Speed

`s` is an integer from `0` to `255`.

Example:

```text
GET /cmd?c=f&s=150
```

Motor ESP32 should clamp invalid speed values to a safe range or reject the request with HTTP `400`.

### Expected Success Response

Response can be plain text or JSON. XIAO only requires an HTTP `2xx` status code.

Recommended plain text:

```text
OK:f
```

Recommended JSON:

```json
{
  "status": "ok",
  "command": "f",
  "speed": 150
}
```

### Expected Error Responses

Missing command:

```text
HTTP 400
```

```json
{
  "status": "error",
  "error": "Missing command code"
}
```

Unknown command:

```text
HTTP 400
```

```json
{
  "status": "error",
  "error": "Unknown command code"
}
```

## Optional Endpoint: QR ID

If the robot needs to react to QR IDs, the motor ESP32 should expose:

```text
GET /qr?id=<qr_text>
```

Example:

```text
GET /qr?id=A1
```

The XIAO calls this only when the browser UI checkbox is enabled:

```text
Forward QR ID to motor ESP32
```

If QR reaction is not needed yet, do not implement `/qr`; keep forwarding disabled in the XIAO UI.

### Expected QR Success Response

Plain text:

```text
OK:A1
```

or JSON:

```json
{
  "status": "ok",
  "id": "A1"
}
```

## Recommended Endpoint: Health Check

This is not required by XIAO yet, but it is useful for debugging:

```text
GET /health
```

Recommended response:

```json
{
  "status": "ok",
  "wifi_connected": true,
  "ip": "192.168.4.2",
  "last_command": "s",
  "last_speed": 0,
  "last_qr_id": "A1"
}
```

## XIAO Command Timing

The XIAO does not send a single high-level `drive_to` command to the motor ESP32.

Instead, it sends simple timed commands:

```text
/cmd?c=r&s=150
wait turn_ms
/cmd?c=s&s=0
wait short settle time
/cmd?c=f&s=150
wait drive_ms
/cmd?c=s&s=0
```

This means the motor ESP32 should immediately apply each command and keep that motor state until the next command arrives.

Do not add long delays inside the motor ESP32 HTTP handlers. The XIAO is already responsible for timing.

## Safety Requirements

The motor ESP32 should implement these safety rules:

1. `s` must stop motors immediately.
2. Unknown commands must not move the robot.
3. Missing parameters must not move the robot.
4. If no command is received for a timeout period, stop motors.
5. Clamp speed to `0..255`.

Recommended watchdog:

```text
If last command age > 1500 ms, stop motors.
```

This prevents the robot from continuing to drive if Wi-Fi drops.

## Minimal Arduino-Style Motor ESP32 Skeleton

This is a protocol skeleton. Replace pin numbers and motor functions with your actual driver wiring.

```cpp
#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "SumoVision";
const char* WIFI_PASSWORD = "sumo1234";

IPAddress LOCAL_IP(192, 168, 4, 2);
IPAddress GATEWAY(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 255, 0);

WebServer server(80);

String lastCommand = "s";
int lastSpeed = 0;
String lastQrId = "";
unsigned long lastCommandMs = 0;

const unsigned long COMMAND_TIMEOUT_MS = 1500;

void stopMotors() {
  // TODO: set motor PWM to 0 and brake/coast safely.
}

void driveForward(int speed) {
  // TODO: motor driver forward.
}

void driveBackward(int speed) {
  // TODO: motor driver backward.
}

void turnLeft(int speed) {
  // TODO: motor driver left turn.
}

void turnRight(int speed) {
  // TODO: motor driver right turn.
}

int clampSpeed(int speed) {
  if (speed < 0) return 0;
  if (speed > 255) return 255;
  return speed;
}

void applyCommand(const String& command, int speed) {
  if (command == "f") {
    driveForward(speed);
  } else if (command == "b") {
    driveBackward(speed);
  } else if (command == "l") {
    turnLeft(speed);
  } else if (command == "r") {
    turnRight(speed);
  } else {
    stopMotors();
  }
}

void handleCommand() {
  if (!server.hasArg("c")) {
    stopMotors();
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Missing command code\"}");
    return;
  }

  String command = server.arg("c");
  command.toLowerCase();

  if (command != "f" && command != "b" && command != "l" && command != "r" && command != "s") {
    stopMotors();
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Unknown command code\"}");
    return;
  }

  const int speed = clampSpeed(server.hasArg("s") ? server.arg("s").toInt() : 150);

  lastCommand = command;
  lastSpeed = speed;
  lastCommandMs = millis();

  applyCommand(command, speed);

  server.send(200, "application/json",
              "{\"status\":\"ok\",\"command\":\"" + command + "\",\"speed\":" + String(speed) + "}");
}

void handleQr() {
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Missing QR id\"}");
    return;
  }

  lastQrId = server.arg("id");

  // TODO: optional behavior, for example update target zone.
  // Do not move motors here unless your team intentionally wants QR-triggered motion.

  server.send(200, "application/json",
              "{\"status\":\"ok\",\"id\":\"" + lastQrId + "\"}");
}

void handleHealth() {
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"last_command\":\"" + lastCommand + "\",";
  json += "\"last_speed\":" + String(lastSpeed) + ",";
  json += "\"last_qr_id\":\"" + lastQrId + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.config(LOCAL_IP, GATEWAY, SUBNET);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }
}

void setup() {
  Serial.begin(115200);

  // TODO: pinMode setup for motor driver pins.
  stopMotors();

  connectWiFi();

  server.on("/cmd", HTTP_GET, handleCommand);
  server.on("/qr", HTTP_GET, handleQr);
  server.on("/health", HTTP_GET, handleHealth);
  server.begin();
}

void loop() {
  server.handleClient();

  if (lastCommand != "s" && millis() - lastCommandMs > COMMAND_TIMEOUT_MS) {
    lastCommand = "s";
    lastSpeed = 0;
    stopMotors();
  }
}
```

## Integration Test Sequence

### 1. Test Motor ESP32 Alone

Connect a laptop or phone to `SumoVision` and open:

```text
http://192.168.4.2/health
```

Expected:

```json
{
  "status": "ok"
}
```

Then test stop:

```text
http://192.168.4.2/cmd?c=s&s=0
```

Expected:

```text
Robot does not move and endpoint returns HTTP 200.
```

### 2. Test Each Manual Motor Command

Use low speed first:

```text
http://192.168.4.2/cmd?c=f&s=80
http://192.168.4.2/cmd?c=s&s=0
http://192.168.4.2/cmd?c=b&s=80
http://192.168.4.2/cmd?c=s&s=0
http://192.168.4.2/cmd?c=l&s=80
http://192.168.4.2/cmd?c=s&s=0
http://192.168.4.2/cmd?c=r&s=80
http://192.168.4.2/cmd?c=s&s=0
```

If a direction is inverted, fix it on the motor ESP32 side by swapping motor direction logic or wiring.

### 3. Test XIAO To Motor ESP32

Open XIAO UI:

```text
http://192.168.4.1
```

Click the camera image.

Expected XIAO behavior:

```text
XIAO sends /cmd requests to 192.168.4.2.
```

Expected motor ESP32 behavior:

```text
Robot turns/drives/stops according to received commands.
```

### 4. Test QR Forwarding

First test motor endpoint directly:

```text
http://192.168.4.2/qr?id=A1
```

Then open XIAO UI:

```text
http://192.168.4.1
```

Enable:

```text
Forward QR ID to motor ESP32
```

Show QR code `A1` to the XIAO camera.

Expected:

```text
XIAO UI: Last QR = A1
XIAO UI: Sent = yes (200)
Motor ESP32 /health: last_qr_id = A1
```

## Troubleshooting

| Symptom | Likely Cause | Fix |
| --- | --- | --- |
| XIAO UI shows motor HTTP error | Motor ESP32 is offline or wrong IP | Check Wi-Fi and static IP |
| `/health` does not open | Motor ESP32 not connected to `SumoVision` | Check SSID/password |
| Robot keeps moving after command | Missing watchdog or stop logic | Implement timeout and stop command |
| Robot direction is inverted | Motor wiring or direction logic is reversed | Fix motor ESP32 motor functions |
| QR appears in XIAO UI but not on motor ESP32 | QR forwarding disabled or `/qr` missing | Enable checkbox and implement `/qr` |
| `forward_http_code` is not 200 | Motor ESP32 rejected QR request | Check `/qr?id=...` handler |

## Final Checklist

- [ ] Motor ESP32 joins `SumoVision`
- [ ] Motor ESP32 IP is `192.168.4.2`
- [ ] `GET /health` works
- [ ] `GET /cmd?c=s&s=0` stops motors
- [ ] `GET /cmd?c=f&s=80` moves forward
- [ ] `GET /cmd?c=l&s=80` turns left
- [ ] `GET /cmd?c=r&s=80` turns right
- [ ] Watchdog stops motors after command timeout
- [ ] Optional `GET /qr?id=A1` stores or handles QR ID
- [ ] XIAO click sends commands to motor ESP32
- [ ] XIAO QR forwarding shows HTTP `200`
