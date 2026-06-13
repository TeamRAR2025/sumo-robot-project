# Motor ESP32 UART Firmware Skeleton

This firmware is the motor-side companion for `xiao_click_go`.

It receives newline-delimited JSON messages from the XIAO over UART:

```json
{"seq":1,"type":"cmd","cmd":"f","speed":150}
{"seq":2,"type":"qr","id":"A1"}
{"seq":3,"type":"heartbeat","ms":123456}
```

It sends newline-delimited JSON acknowledgements back:

```json
{"seq":1,"status":"ok","type":"cmd","cmd":"f","speed":150}
```

## Wiring

Default UART pins in this skeleton:

```text
XIAO D6 / GPIO43 / TX -> Motor ESP32 GPIO16 / RX2
XIAO D7 / GPIO44 / RX <- Motor ESP32 GPIO17 / TX2
XIAO GND              -> Motor ESP32 GND
```

Both boards use 3.3 V logic, so no level shifter is needed.

## Safety

Motor outputs are disabled by default:

```cpp
static constexpr bool MOTOR_OUTPUTS_ENABLED = false;
```

Set it to `true` only after you have filled in the real motor driver pin numbers and tested the direction logic with the robot raised off the floor.

The watchdog stops motors if the UART link goes silent:

```text
LINK_TIMEOUT_MS = 1200
```

## Build

```powershell
cd C:\Users\finmi\PycharmProjects\sumo-robot-project\motor_esp32_uart
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
..\venv\Scripts\pio.exe run
```

Upload, replacing `COMx` with the motor ESP32 port:

```powershell
..\venv\Scripts\pio.exe run --target upload --upload-port COMx
```
