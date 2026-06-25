# sumo-robot-project

Current firmware projects:

- `xiao_click_go` - FrontCam XIAO ESP32-S3 Sense. Creates the `SumoVision` Wi-Fi AP, serves the browser UI and front camera stream, reads the color sensor, decodes QR codes in the browser, and sends click/stop/QR HTTP requests to the QR reader board.
- `qr_reader_xiao` - QR Reader XIAO ESP32-S3 Sense. Joins `SumoVision` as `192.168.4.2`, serves the QR camera stream, reads the distance sensor, and drives the motor driver directly.
- `motor_esp32_uart` - legacy prototype folder from the earlier architecture. It is not used in the current two-XIAO wiring.

Default browser entry point:

```text
http://192.168.4.1
```

Default QR reader endpoints:

```text
http://192.168.4.2
http://192.168.4.2:81/stream
http://192.168.4.2/api/status
```
