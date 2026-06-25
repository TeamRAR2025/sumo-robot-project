# Click-And-Go Calibration Guide

This guide explains how to calibrate the XIAO click-and-go system once the robot is physically available.

The goal is to make this chain work:

```text
camera click in browser -> pixel coordinates -> ground coordinates -> HTTP command -> QR Reader XIAO -> motor driver
```

There are two separate calibrations:

1. Motion calibration: how long the robot must turn or drive.
2. Camera calibration: how a clicked pixel maps to a point on the floor.

You can prepare the printed calibration pattern and data tables before the robot is ready, but the actual numbers must be measured with the assembled robot.

## Files To Update After Calibration

Motion constants live in:

```text
qr_reader_xiao/src/main.cpp
```

Update:

```cpp
static constexpr float TURN_MS_PER_DEG = 8.0f;
static constexpr float DRIVE_MS_PER_CM = 35.0f;
```

Camera calibration lives in the browser script embedded in the same file:

```js
const CALIBRATION = {
  enabled: false,
  H: [
    [1, 0, 0],
    [0, 1, 0],
    [0, 0, 1],
  ],
};
```

After camera calibration, set `enabled` to `true` and replace `H` with the measured homography matrix.

## Required Equipment

- Assembled robot with fixed XIAO ESP32-S3 Sense camera position
- FrontCam XIAO with color sensor
- QR Reader XIAO with motor driver and distance sensor
- Laptop or phone connected to the FrontCam `SumoVision` Wi-Fi access point
- Ruler or measuring tape
- Printed checkerboard calibration sheet
- Masking tape or marker for fixed start position
- Flat test floor with enough space for 1 m forward driving

## Link Setup Check

Before measuring anything:

1. Flash the XIAO click-and-go firmware.
2. Flash the QR Reader XIAO firmware.
3. Start FrontCam first, then reset QR Reader so it joins `SumoVision` as `192.168.4.2`.

4. Open the XIAO UI:

```text
http://192.168.4.1
```

5. Check the live stream:

```text
http://192.168.4.1:81/stream
```

6. Check a still frame:

```text
http://192.168.4.1/capture
```

7. Check QR Reader status:

```text
http://192.168.4.2/api/status
```

8. Press `STOP` in the UI and confirm the QR Reader responds:

```json
{"status":"stopped","motor":{"last_command":"s","left_speed":0,"right_speed":0}}
```

Do not continue calibration until the browser can reliably call `http://192.168.4.2/api/status` and `POST http://192.168.4.2/api/stop`.

## Part 1: Motion Calibration

The first version of click-and-go uses open-loop motion. The QR Reader XIAO applies timed motor states:

```text
turn left/right for N milliseconds
stop briefly
drive forward for M milliseconds
stop
```

Because there are no wheel encoders yet, the timing constants must be measured on the real robot.

### 1.1 Measure Turn Rate

1. Put the robot on the floor.
2. Mark the robot's starting direction with tape.
3. Send a right-turn command for a known time, for example `1000 ms`.
4. Measure the angle turned in degrees.
5. Repeat at least 5 times.
6. Do the same for left turns.

Use this formula:

```text
TURN_MS_PER_DEG = turn_time_ms / measured_angle_deg
```

Example:

```text
1000 ms / 125 deg = 8.0 ms/deg
```

Record results:

| Trial | Direction | Time (ms) | Angle (deg) | ms/deg |
| --- | --- | ---: | ---: | ---: |
| 1 | Right | 1000 |  |  |
| 2 | Right | 1000 |  |  |
| 3 | Right | 1000 |  |  |
| 4 | Left | 1000 |  |  |
| 5 | Left | 1000 |  |  |

Use the median `ms/deg` value, not the best-looking single trial.

If left and right are very different, document both values. For the current firmware, use the average as the first approximation.

### 1.2 Measure Forward Drive Rate

1. Mark a start line on the floor.
2. Put the robot's reference point on the start line.
3. Send a forward command for a known time, for example `1000 ms`.
4. Measure the distance travelled in centimeters.
5. Repeat at least 5 times.

Use this formula:

```text
DRIVE_MS_PER_CM = drive_time_ms / measured_distance_cm
```

Example:

```text
1000 ms / 28 cm = 35.7 ms/cm
```

Record results:

| Trial | Time (ms) | Distance (cm) | ms/cm |
| --- | ---: | ---: | ---: |
| 1 | 1000 |  |  |
| 2 | 1000 |  |  |
| 3 | 1000 |  |  |
| 4 | 1000 |  |  |
| 5 | 1000 |  |  |

Use the median `ms/cm` value.

### 1.3 Update Firmware Constants

Open:

```text
qr_reader_xiao/src/main.cpp
```

Update:

```cpp
static constexpr float TURN_MS_PER_DEG = 8.0f;
static constexpr float DRIVE_MS_PER_CM = 35.0f;
```

Rebuild and upload:

```powershell
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
..\venv\Scripts\pio.exe run --target upload
```

## Part 2: Camera Calibration

Camera calibration maps a click on the camera image to a floor coordinate relative to the robot.

Coordinate convention:

```text
X = forward from robot center, in cm
Y = right from robot center, in cm
Angle 0 deg = straight ahead
Positive angle = turn right
Negative angle = turn left
```

### 2.1 Print Checkerboard

Recommended pattern:

```text
8 x 10 squares
25 mm square size
7 x 9 internal corners
A4 paper
Printed at 100% scale
```

Important:

- Print at actual size.
- Do not use "fit to page".
- Measure one printed square with a ruler.
- Write down the real square size.

Record:

```text
Printed square size: ____ mm
```

### 2.2 Place Robot And Checkerboard

1. Put the robot on the floor.
2. Mark the robot start position and direction with tape.
3. Place the checkerboard flat on the floor in front of the robot.
4. Make sure the full checkerboard is visible in the camera image.
5. Measure the distance from the robot rotation center to the checkerboard center.

Record:

```text
Checkerboard center X0: ____ cm forward
Checkerboard center Y0: ____ cm right
```

If the checkerboard is centered on the robot's forward axis, `Y0 = 0`.

### 2.3 Capture Calibration Image

Open:

```text
http://192.168.4.1/capture
```

Save the image as:

```text
calibration_frame.jpg
```

The image is valid only if:

- all internal checkerboard corners are visible;
- the image is not motion-blurred;
- the checkerboard is flat on the ground;
- lighting is close to the real operating environment.

### 2.4 Compute Homography

The homography matrix `H` should be computed from:

- detected checkerboard pixel corners;
- known real-world checkerboard corner positions in centimeters.

The output format must be:

```js
H: [
  [h00, h01, h02],
  [h10, h11, h12],
  [h20, h21, h22],
]
```

The current browser code applies it like this:

```js
ground_x_cm = (h00 * px + h01 * py + h02) / (h20 * px + h21 * py + h22)
ground_y_cm = (h10 * px + h11 * py + h12) / (h20 * px + h21 * py + h22)
```

If using OpenCV, the expected function is:

```python
cv2.findHomography(pixel_points, floor_points, cv2.RANSAC)
```

### 2.5 Update Browser Calibration Matrix

Open:

```text
xiao_click_go/src/main.cpp
```

Find:

```js
const CALIBRATION = {
  enabled: false,
  H: [
    [1, 0, 0],
    [0, 1, 0],
    [0, 0, 1],
  ],
};
```

Change it to:

```js
const CALIBRATION = {
  enabled: true,
  H: [
    [/* measured h00 */, /* measured h01 */, /* measured h02 */],
    [/* measured h10 */, /* measured h11 */, /* measured h12 */],
    [/* measured h20 */, /* measured h21 */, /* measured h22 */],
  ],
};
```

Rebuild and upload the firmware.

## Part 3: Validation

After both calibrations are entered, test the complete click-and-go behavior.

### 3.1 Static Coordinate Test

1. Put a small object at a known floor position.
2. Example: `60 cm forward, 20 cm right`.
3. Click that object in the browser camera image.
4. Check the displayed ground coordinate.

Record:

| Target X (cm) | Target Y (cm) | Measured X (cm) | Measured Y (cm) | Error (cm) |
| ---: | ---: | ---: | ---: | ---: |
| 60 | 20 |  |  |  |
| 40 | -20 |  |  |  |
| 80 | 0 |  |  |  |

Acceptable first target:

```text
Median coordinate error <= 5 cm
```

If error is larger near image edges, avoid edge clicks or redo calibration with the checkerboard covering more of the useful camera area.

### 3.2 Driving Test

1. Put the robot at the marked start position.
2. Click a point about `40 cm` straight ahead.
3. Confirm the robot drives roughly to that point.
4. Click a point diagonally right.
5. Confirm the robot turns right first, then drives.
6. Click a point diagonally left.
7. Confirm the robot turns left first, then drives.

Record:

| Trial | Target Description | Result | Notes |
| --- | --- | --- | --- |
| 1 | 40 cm straight ahead |  |  |
| 2 | diagonal right |  |  |
| 3 | diagonal left |  |  |

### 3.3 Safety Check

Always verify:

- STOP button stops the robot immediately.
- Clicks do not create excessive movement duration.
- Robot does not drive outside the test area.
- QR Reader XIAO rejects invalid commands and stops on `cmd=s`.

## Troubleshooting

| Problem | Likely Cause | Fix |
| --- | --- | --- |
| Camera stream is blank | Camera init or power issue | Check serial monitor and camera ribbon cable |
| Clicks do nothing | QR Reader HTTP API is offline or blocked | Check `http://192.168.4.2/api/status` from the same browser |
| Robot turns the wrong way | Motor mapping inverted | Swap left/right command mapping or motor wiring |
| Robot drives too far | `DRIVE_MS_PER_CM` too high | Re-measure forward distance |
| Robot stops short | `DRIVE_MS_PER_CM` too low | Re-measure forward distance |
| Robot over-rotates | `TURN_MS_PER_DEG` too high | Re-measure turn angle |
| Robot under-rotates | `TURN_MS_PER_DEG` too low | Re-measure turn angle |
| Coordinates are wrong everywhere | Bad homography | Redo checkerboard capture and measurements |
| Coordinates are good in center but bad at edges | Lens distortion or weak calibration coverage | Click closer to image center or use a wider calibration area |

## Calibration Data Summary

Fill this section after testing:

```text
Robot:
Camera mount:
Motor driver:
QR Reader IP:
Speed used:

TURN_MS_PER_DEG:
DRIVE_MS_PER_CM:

Checkerboard square size:
Checkerboard X0:
Checkerboard Y0:

Homography H:
[
  [__, __, __],
  [__, __, __],
  [__, __, __],
]

Median static coordinate error:
Median driving endpoint error:
Known limitations:
```
