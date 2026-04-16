# ESP32 Phone AP Firmware

This folder contains an Arduino sketch for ESP32 that:

- connects to the phone access point `robonet`
- uses password `12345678qwerty`
- requests a static IP `192.168.43.2`
- exposes an HTTP API compatible with the Flask phone server
- prints every API request to the Serial Monitor

## Sketch

- `phone_ap_api_logger/phone_ap_api_logger.ino`

## Endpoints

- `GET /health`
- `GET /cmd?c=f&s=150`
- `GET /`

## Notes

- The sketch logs request method, URI, client IP, query args, selected headers, and command mapping.
- `/cmd` acknowledges the request with `OK:<code>` so it matches the current Flask client expectations.
- Motor control is not wired in yet; this firmware is focused on connection and API visibility in Serial output.

## Upload

1. Open the `.ino` file in Arduino IDE.
2. Select your ESP32 board.
3. Ensure the ESP32 Arduino core is installed.
4. Upload the sketch.
5. Open Serial Monitor at `115200` baud.
