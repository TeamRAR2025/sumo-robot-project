#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "jsqr_gz.h"

#if !defined(CAMERA_MODEL_XIAO_ESP32S3)
#error "CAMERA_MODEL_XIAO_ESP32S3 must be defined in platformio.ini"
#endif

// XIAO ESP32-S3 Sense camera pins.
// Source: Seeed Studio XIAO ESP32S3 Sense camera example.
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39
#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

static constexpr bool USE_SOFT_AP = true;
static const char *AP_SSID = "SumoVision";
static const char *AP_PASSWORD = "sumo1234";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

// Used only when USE_SOFT_AP is false.
static const char *STA_SSID = "CHANGE_ME";
static const char *STA_PASSWORD = "CHANGE_ME";

// Wired UART link to the motor ESP32.
static constexpr uint32_t MOTOR_UART_BAUD = 115200;
static constexpr int MOTOR_UART_TX_PIN = D6; // XIAO D6 / GPIO43 / TX -> motor ESP32 RX
static constexpr int MOTOR_UART_RX_PIN = D7; // XIAO D7 / GPIO44 / RX <- motor ESP32 TX
static constexpr uint16_t HEARTBEAT_INTERVAL_MS = 500;
static constexpr uint16_t HTTP_PORT = 80;
static constexpr uint16_t STREAM_PORT = 81;

// Open-loop tuning constants. Replace with measured values.
static constexpr float TURN_MS_PER_DEG = 8.0f;
static constexpr float DRIVE_MS_PER_CM = 35.0f;
static constexpr uint16_t DEFAULT_SPEED = 150;
static constexpr uint16_t TURN_SETTLE_MS = 150;
static constexpr uint16_t MAX_TURN_MS = 1600;
static constexpr uint16_t MAX_DRIVE_MS = 3000;

static WebServer web(HTTP_PORT);
static httpd_handle_t streamHttpd = nullptr;

enum class MotionPhase {
  IDLE,
  TURNING,
  TURN_SETTLE,
  DRIVING,
  DONE
};

struct MotionPlan {
  MotionPhase phase = MotionPhase::IDLE;
  int turnDir = 0; // -1 left, +1 right, 0 no turn
  uint16_t turnMs = 0;
  uint16_t driveMs = 0;
  uint16_t speed = DEFAULT_SPEED;
  unsigned long deadlineMs = 0;
  String lastError;
};

static MotionPlan plan;
static String lastMotorCommand = "s";
static uint32_t motorSequence = 0;
static unsigned long uartTxCount = 0;
static String lastMotorMessage = "";
static String lastMotorAck = "";
static String uartRxLine = "";
static unsigned long lastHeartbeatMs = 0;
static unsigned long clickCount = 0;
static String lastQrId = "";
static unsigned long qrCount = 0;
static bool lastQrForwarded = false;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SumoBot Click-And-Go</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #101114;
      --panel: #1a1d22;
      --panel-2: #222731;
      --text: #f2f5f8;
      --muted: #98a2b3;
      --line: #343b46;
      --accent: #31c48d;
      --danger: #f05252;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    main {
      width: min(1120px, 100%);
      margin: 0 auto;
      padding: 16px;
      display: grid;
      gap: 12px;
    }
    header, section {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }
    h1, h2, p { margin: 0; }
    h1 { font-size: 20px; }
    h2 { font-size: 15px; margin-bottom: 8px; }
    .muted { color: var(--muted); font-size: 13px; }
    .layout {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 310px;
      gap: 12px;
      align-items: start;
    }
    .video-shell {
      position: relative;
      overflow: hidden;
      border-radius: 8px;
      background: #050608;
      border: 1px solid var(--line);
    }
    #stream {
      display: block;
      width: 100%;
      min-height: 240px;
      object-fit: contain;
      cursor: crosshair;
      user-select: none;
    }
    .crosshair {
      position: absolute;
      width: 24px;
      height: 24px;
      border: 2px solid var(--accent);
      border-radius: 999px;
      transform: translate(-50%, -50%);
      pointer-events: none;
      display: none;
    }
    .crosshair::before, .crosshair::after {
      content: "";
      position: absolute;
      background: var(--accent);
    }
    .crosshair::before {
      width: 34px;
      height: 2px;
      left: -7px;
      top: 9px;
    }
    .crosshair::after {
      width: 2px;
      height: 34px;
      left: 9px;
      top: -7px;
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }
    .metric {
      background: var(--panel-2);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 9px;
      min-height: 62px;
    }
    .metric span {
      display: block;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 4px;
    }
    .metric strong {
      font-size: 16px;
      overflow-wrap: anywhere;
    }
    label {
      display: grid;
      gap: 5px;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 8px;
    }
    input {
      width: 100%;
      border: 1px solid var(--line);
      border-radius: 6px;
      background: #11151b;
      color: var(--text);
      padding: 9px;
      font-size: 14px;
    }
    input[type="checkbox"] {
      width: auto;
      margin: 0;
    }
    button {
      border: 0;
      border-radius: 8px;
      min-height: 42px;
      padding: 0 12px;
      font-weight: 700;
      color: #06100c;
      background: var(--accent);
      cursor: pointer;
    }
    button.stop {
      background: var(--danger);
      color: #fff;
      width: 100%;
      margin-top: 8px;
    }
    pre {
      white-space: pre-wrap;
      overflow-wrap: anywhere;
      background: #0b0d10;
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 9px;
      color: var(--muted);
      min-height: 72px;
      margin: 0;
      font-size: 12px;
    }
    @media (max-width: 820px) {
      .layout { grid-template-columns: 1fr; }
      header { align-items: flex-start; flex-direction: column; }
    }
  </style>
</head>
<body>
<main>
  <header>
    <div>
      <h1>SumoBot Click-And-Go</h1>
      <p class="muted">Click on the camera image to send an open-loop turn + drive command.</p>
    </div>
    <button id="refresh" type="button">Refresh status</button>
  </header>

  <div class="layout">
    <section>
      <h2>Front camera</h2>
      <div class="video-shell" id="video-shell">
        <img id="stream" alt="Camera stream">
        <div class="crosshair" id="crosshair"></div>
      </div>
    </section>

    <section>
      <h2>Command</h2>
      <label>
        Speed
        <input id="speed" type="number" min="0" max="255" value="150">
      </label>
      <div class="grid">
        <div class="metric"><span>Pixel</span><strong id="pixel">-</strong></div>
        <div class="metric"><span>Ground</span><strong id="ground">not calibrated</strong></div>
        <div class="metric"><span>Angle</span><strong id="angle">-</strong></div>
        <div class="metric"><span>Distance</span><strong id="distance">-</strong></div>
      </div>
      <button class="stop" id="stop" type="button">STOP</button>
    </section>
  </div>

  <section>
    <h2>QR scanner</h2>
    <div class="grid">
      <div class="metric"><span>Decoder</span><strong id="qr-decoder">starting</strong></div>
      <div class="metric"><span>Last QR</span><strong id="qr-result">-</strong></div>
      <div class="metric"><span>Sent</span><strong id="qr-sent">-</strong></div>
      <div class="metric"><span>Status</span><strong id="qr-status">idle</strong></div>
    </div>
    <label>
      <input id="qr-forward" type="checkbox">
      Forward QR ID to motor ESP32
    </label>
  </section>

  <section>
    <h2>Status</h2>
    <pre id="status">Loading...</pre>
  </section>
</main>

<script src="/jsQR.js"></script>
<script>
const stream = document.getElementById("stream");
const shell = document.getElementById("video-shell");
const crosshair = document.getElementById("crosshair");
const speedInput = document.getElementById("speed");
const pixel = document.getElementById("pixel");
const ground = document.getElementById("ground");
const angle = document.getElementById("angle");
const distance = document.getElementById("distance");
const statusBox = document.getElementById("status");
const qrDecoder = document.getElementById("qr-decoder");
const qrResult = document.getElementById("qr-result");
const qrSent = document.getElementById("qr-sent");
const qrStatus = document.getElementById("qr-status");
const qrForward = document.getElementById("qr-forward");

const qrCanvas = document.createElement("canvas");
const qrContext = qrCanvas.getContext("2d", { willReadFrequently: true });
let qrDetector = null;
let qrMode = "none";
let lastPostedQr = "";
let lastPostedQrMs = 0;
const QR_SCAN_INTERVAL_MS = 450;
const QR_REPEAT_POST_MS = 3000;

const CALIBRATION = {
  enabled: false,
  H: [
    [1, 0, 0],
    [0, 1, 0],
    [0, 0, 1],
  ],
};

stream.crossOrigin = "anonymous";
stream.src = `http://${location.hostname}:81/stream`;

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function applyHomography(H, px, py) {
  const X = H[0][0] * px + H[0][1] * py + H[0][2];
  const Y = H[1][0] * px + H[1][1] * py + H[1][2];
  const W = H[2][0] * px + H[2][1] * py + H[2][2];
  return { xCm: X / W, yCm: Y / W };
}

function fallbackPlan(px, py, width, height) {
  const xNorm = clamp((px / width - 0.5) * 2, -1, 1);
  const yNorm = clamp(py / height, 0, 1);
  const turnDir = Math.abs(xNorm) < 0.12 ? 0 : (xNorm > 0 ? 1 : -1);
  const turnMs = Math.round(Math.abs(xNorm) * 850);
  const driveMs = Math.round(300 + (1 - yNorm) * 1400);
  return { turnDir, turnMs, driveMs };
}

async function refreshStatus() {
  const response = await fetch("/api/status");
  const data = await response.json();
  statusBox.textContent = JSON.stringify(data, null, 2);
}

async function sendStop() {
  await fetch("/api/stop", { method: "POST" });
  await refreshStatus();
}

async function initQrScanner() {
  if (window.jsQR) {
    qrMode = "jsQR";
    qrDecoder.textContent = qrMode;
    qrStatus.textContent = "scanning";
    return;
  }

  if ("BarcodeDetector" in window) {
    try {
      if (BarcodeDetector.getSupportedFormats) {
        const formats = await BarcodeDetector.getSupportedFormats();
        if (!formats.includes("qr_code")) {
          throw new Error("qr_code format is not supported");
        }
      }
      qrDetector = new BarcodeDetector({ formats: ["qr_code"] });
      qrMode = "BarcodeDetector";
      qrDecoder.textContent = qrMode;
      qrStatus.textContent = "scanning";
      return;
    } catch (error) {
      qrStatus.textContent = error.message;
    }
  }

  qrMode = "none";
  qrDecoder.textContent = "unsupported";
  qrStatus.textContent = "decoder unavailable";
}

async function decodeQrFromCanvas() {
  if (qrMode === "BarcodeDetector" && qrDetector) {
    const barcodes = await qrDetector.detect(qrCanvas);
    return barcodes.length > 0 ? barcodes[0].rawValue : "";
  }

  if (qrMode === "jsQR" && window.jsQR) {
    const imageData = qrContext.getImageData(0, 0, qrCanvas.width, qrCanvas.height);
    const code = window.jsQR(imageData.data, qrCanvas.width, qrCanvas.height);
    return code ? code.data : "";
  }

  return "";
}

async function postQrId(value) {
  const now = Date.now();
  if (value === lastPostedQr && now - lastPostedQrMs < QR_REPEAT_POST_MS) {
    return;
  }

  lastPostedQr = value;
  lastPostedQrMs = now;

  const params = new URLSearchParams({
    id: value,
    forward: qrForward.checked ? "1" : "0",
  });

  const response = await fetch(`/api/qr?${params.toString()}`, { method: "POST" });
  const data = await response.json();
  qrSent.textContent = data.forwarded ? `uart seq ${data.seq}` : "local";
  statusBox.textContent = JSON.stringify(data, null, 2);
}

async function scanQrFrame() {
  if (qrMode === "none") {
    return;
  }
  if (!stream.naturalWidth || !stream.naturalHeight) {
    qrStatus.textContent = "waiting for video";
    return;
  }

  try {
    qrCanvas.width = stream.naturalWidth;
    qrCanvas.height = stream.naturalHeight;
    qrContext.drawImage(stream, 0, 0, qrCanvas.width, qrCanvas.height);

    const value = await decodeQrFromCanvas();
    if (!value) {
      qrStatus.textContent = "scanning";
      return;
    }

    qrResult.textContent = value;
    qrStatus.textContent = "detected";
    await postQrId(value);
  } catch (error) {
    qrStatus.textContent = error.message;
  }
}

stream.addEventListener("click", async (event) => {
  const rect = stream.getBoundingClientRect();
  const naturalWidth = stream.naturalWidth || rect.width;
  const naturalHeight = stream.naturalHeight || rect.height;
  const px = (event.clientX - rect.left) * naturalWidth / rect.width;
  const py = (event.clientY - rect.top) * naturalHeight / rect.height;

  crosshair.style.left = `${event.clientX - shell.getBoundingClientRect().left}px`;
  crosshair.style.top = `${event.clientY - shell.getBoundingClientRect().top}px`;
  crosshair.style.display = "block";

  pixel.textContent = `${px.toFixed(0)}, ${py.toFixed(0)}`;

  const params = new URLSearchParams({
    px: px.toFixed(2),
    py: py.toFixed(2),
    w: naturalWidth.toFixed(0),
    h: naturalHeight.toFixed(0),
    speed: speedInput.value || "150",
  });

  if (CALIBRATION.enabled) {
    const point = applyHomography(CALIBRATION.H, px, py);
    const angleDeg = Math.atan2(point.yCm, point.xCm) * 180 / Math.PI;
    const distanceCm = Math.hypot(point.xCm, point.yCm);
    ground.textContent = `${point.xCm.toFixed(1)} cm, ${point.yCm.toFixed(1)} cm`;
    angle.textContent = `${angleDeg.toFixed(1)} deg`;
    distance.textContent = `${distanceCm.toFixed(1)} cm`;
    params.set("angle", angleDeg.toFixed(2));
    params.set("distance", distanceCm.toFixed(2));
  } else {
    const plan = fallbackPlan(px, py, naturalWidth, naturalHeight);
    ground.textContent = "fallback";
    angle.textContent = `${plan.turnDir > 0 ? "right" : plan.turnDir < 0 ? "left" : "straight"} ${plan.turnMs} ms`;
    distance.textContent = `forward ${plan.driveMs} ms`;
    params.set("turn_dir", String(plan.turnDir));
    params.set("turn_ms", String(plan.turnMs));
    params.set("drive_ms", String(plan.driveMs));
  }

  const response = await fetch(`/api/click?${params.toString()}`);
  const data = await response.json();
  statusBox.textContent = JSON.stringify(data, null, 2);
});

document.getElementById("stop").addEventListener("click", () => void sendStop());
document.getElementById("refresh").addEventListener("click", () => void refreshStatus());

void initQrScanner();
void refreshStatus();
window.setInterval(() => void scanQrFrame(), QR_SCAN_INTERVAL_MS);
window.setInterval(() => void refreshStatus(), 5000);
</script>
</body>
</html>
)rawliteral";

const char *phaseName(MotionPhase phase) {
  switch (phase) {
    case MotionPhase::IDLE:
      return "idle";
    case MotionPhase::TURNING:
      return "turning";
    case MotionPhase::TURN_SETTLE:
      return "turn_settle";
    case MotionPhase::DRIVING:
      return "driving";
    case MotionPhase::DONE:
      return "done";
  }
  return "unknown";
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }
  return escaped;
}

uint16_t boundedDuration(long value, uint16_t maxValue) {
  if (value < 0) {
    return 0;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return static_cast<uint16_t>(value);
}

uint16_t boundedSpeed(long value) {
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return static_cast<uint16_t>(value);
}

bool sendMotorJson(const String &json) {
  Serial1.println(json);
  lastMotorMessage = json;
  uartTxCount++;
  Serial.printf("Motor UART TX: %s\n", json.c_str());
  plan.lastError = "";
  return true;
}

uint32_t nextMotorSequence() {
  motorSequence++;
  if (motorSequence == 0) {
    motorSequence = 1;
  }
  return motorSequence;
}

bool sendMotorCommand(const String &code, uint16_t speed) {
  const uint32_t seq = nextMotorSequence();
  lastMotorCommand = code;

  String json = "{";
  json += "\"seq\":" + String(seq) + ",";
  json += "\"type\":\"cmd\",";
  json += "\"cmd\":\"" + jsonEscape(code) + "\",";
  json += "\"speed\":" + String(speed);
  json += "}";

  return sendMotorJson(json);
}

bool forwardQrToMotor(const String &qrId) {
  const uint32_t seq = nextMotorSequence();

  String json = "{";
  json += "\"seq\":" + String(seq) + ",";
  json += "\"type\":\"qr\",";
  json += "\"id\":\"" + jsonEscape(qrId) + "\"";
  json += "}";

  return sendMotorJson(json);
}

void sendHeartbeatIfDue() {
  const unsigned long now = millis();
  if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
    return;
  }

  lastHeartbeatMs = now;
  const uint32_t seq = nextMotorSequence();

  String json = "{";
  json += "\"seq\":" + String(seq) + ",";
  json += "\"type\":\"heartbeat\",";
  json += "\"ms\":" + String(now);
  json += "}";

  sendMotorJson(json);
}

void readMotorAck() {
  while (Serial1.available() > 0) {
    const char c = static_cast<char>(Serial1.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (uartRxLine.length() > 0) {
        lastMotorAck = uartRxLine;
        Serial.printf("Motor UART RX: %s\n", lastMotorAck.c_str());
        uartRxLine = "";
      }
      continue;
    }

    if (uartRxLine.length() < 240) {
      uartRxLine += c;
    } else {
      uartRxLine = "";
      plan.lastError = "Motor UART RX line too long";
    }
  }
}

void cancelMotion() {
  plan.phase = MotionPhase::IDLE;
  plan.deadlineMs = 0;
  sendMotorCommand("s", 0);
}

void scheduleMotion(int turnDir, uint16_t turnMs, uint16_t driveMs, uint16_t speed) {
  cancelMotion();

  plan.turnDir = turnDir < 0 ? -1 : (turnDir > 0 ? 1 : 0);
  plan.turnMs = turnMs;
  plan.driveMs = driveMs;
  plan.speed = speed;

  if (plan.turnDir != 0 && plan.turnMs > 0) {
    sendMotorCommand(plan.turnDir > 0 ? "r" : "l", plan.speed);
    plan.phase = MotionPhase::TURNING;
    plan.deadlineMs = millis() + plan.turnMs;
    return;
  }

  if (plan.driveMs > 0) {
    sendMotorCommand("f", plan.speed);
    plan.phase = MotionPhase::DRIVING;
    plan.deadlineMs = millis() + plan.driveMs;
    return;
  }

  plan.phase = MotionPhase::DONE;
}

void updateMotionPlan() {
  if (plan.phase == MotionPhase::IDLE || plan.phase == MotionPhase::DONE) {
    return;
  }

  const long remaining = static_cast<long>(plan.deadlineMs - millis());
  if (remaining > 0) {
    return;
  }

  if (plan.phase == MotionPhase::TURNING) {
    sendMotorCommand("s", 0);
    plan.phase = MotionPhase::TURN_SETTLE;
    plan.deadlineMs = millis() + TURN_SETTLE_MS;
    return;
  }

  if (plan.phase == MotionPhase::TURN_SETTLE) {
    if (plan.driveMs > 0) {
      sendMotorCommand("f", plan.speed);
      plan.phase = MotionPhase::DRIVING;
      plan.deadlineMs = millis() + plan.driveMs;
    } else {
      plan.phase = MotionPhase::DONE;
    }
    return;
  }

  if (plan.phase == MotionPhase::DRIVING) {
    sendMotorCommand("s", 0);
    plan.phase = MotionPhase::DONE;
  }
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = psramFound() ? 2 : 1;

  if (!psramFound()) {
    Serial.println("Warning: PSRAM not found, using DRAM framebuffer.");
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    if (sensor->id.PID == OV3660_PID) {
      sensor->set_vflip(sensor, 1);
      sensor->set_brightness(sensor, 1);
      sensor->set_saturation(sensor, -2);
    }
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
  }

  return true;
}

void startWiFi() {
  WiFi.setSleep(false);

  if (USE_SOFT_AP) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("XIAO AP SSID: %s\n", AP_SSID);
    Serial.printf("XIAO AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASSWORD);
  Serial.printf("Connecting to Wi-Fi SSID: %s", STA_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("XIAO STA IP: %s\n", WiFi.localIP().toString().c_str());
}

static esp_err_t streamHandler(httpd_req_t *req) {
  static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
  static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
  static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr) {
      Serial.println("Camera frame capture failed");
      return ESP_FAIL;
    }

    char partBuffer[64];
    const size_t headerLength = snprintf(partBuffer, sizeof(partBuffer), STREAM_PART, fb->len);

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, partBuffer, headerLength);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(fb->buf), fb->len);
    }

    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(35));
  }

  return res;
}

void startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = 32769;

  const httpd_uri_t streamUri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = streamHandler,
      .user_ctx = nullptr,
  };

  if (httpd_start(&streamHttpd, &config) == ESP_OK) {
    httpd_register_uri_handler(streamHttpd, &streamUri);
    Serial.printf("Stream ready on port %u\n", STREAM_PORT);
  } else {
    Serial.println("Failed to start stream server");
  }
}

void handleRoot() {
  web.send_P(200, "text/html", INDEX_HTML);
}

void handleJsQr() {
  web.sendHeader("Content-Encoding", "gzip");
  web.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  web.send_P(
      200,
      "application/javascript",
      reinterpret_cast<const char *>(JSQR_JS_GZ),
      JSQR_JS_GZ_LEN);
}

void handleStatus() {
  const IPAddress ip = USE_SOFT_AP ? WiFi.softAPIP() : WiFi.localIP();
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"ip\":\"" + ip.toString() + "\",";
  json += "\"stream_url\":\"http://" + ip.toString() + ":" + String(STREAM_PORT) + "/stream\",";
  json += "\"motor_uart_baud\":" + String(MOTOR_UART_BAUD) + ",";
  json += "\"motor_uart_tx_pin\":" + String(MOTOR_UART_TX_PIN) + ",";
  json += "\"motor_uart_rx_pin\":" + String(MOTOR_UART_RX_PIN) + ",";
  json += "\"motion_phase\":\"" + String(phaseName(plan.phase)) + "\",";
  json += "\"last_motor_command\":\"" + lastMotorCommand + "\",";
  json += "\"motor_sequence\":" + String(motorSequence) + ",";
  json += "\"uart_tx_count\":" + String(uartTxCount) + ",";
  json += "\"last_motor_message\":\"" + jsonEscape(lastMotorMessage) + "\",";
  json += "\"last_motor_ack\":\"" + jsonEscape(lastMotorAck) + "\",";
  json += "\"last_qr_id\":\"" + jsonEscape(lastQrId) + "\",";
  json += "\"qr_count\":" + String(qrCount) + ",";
  json += "\"last_qr_forwarded\":" + String(lastQrForwarded ? "true" : "false") + ",";
  json += "\"click_count\":" + String(clickCount) + ",";
  json += "\"turn_ms\":" + String(plan.turnMs) + ",";
  json += "\"drive_ms\":" + String(plan.driveMs) + ",";
  json += "\"speed\":" + String(plan.speed) + ",";
  json += "\"last_error\":\"" + jsonEscape(plan.lastError) + "\"";
  json += "}";
  web.send(200, "application/json", json);
}

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    web.send(503, "application/json", "{\"status\":\"error\",\"error\":\"Camera capture failed\"}");
    return;
  }

  web.setContentLength(fb->len);
  web.send(200, "image/jpeg", "");
  WiFiClient client = web.client();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleClick() {
  clickCount++;

  const uint16_t speed = boundedSpeed(web.arg("speed").toInt());
  int turnDir = 0;
  uint16_t turnMs = 0;
  uint16_t driveMs = 0;

  if (web.hasArg("angle") && web.hasArg("distance")) {
    const float angleDeg = web.arg("angle").toFloat();
    const float distanceCm = web.arg("distance").toFloat();
    turnDir = fabs(angleDeg) < 5.0f ? 0 : (angleDeg > 0 ? 1 : -1);
    turnMs = boundedDuration(lroundf(fabs(angleDeg) * TURN_MS_PER_DEG), MAX_TURN_MS);
    driveMs = boundedDuration(lroundf(distanceCm * DRIVE_MS_PER_CM), MAX_DRIVE_MS);
  } else {
    turnDir = web.arg("turn_dir").toInt();
    turnMs = boundedDuration(web.arg("turn_ms").toInt(), MAX_TURN_MS);
    driveMs = boundedDuration(web.arg("drive_ms").toInt(), MAX_DRIVE_MS);
  }

  scheduleMotion(turnDir, turnMs, driveMs, speed);

  String json = "{";
  json += "\"status\":\"accepted\",";
  json += "\"click_count\":" + String(clickCount) + ",";
  json += "\"turn_dir\":" + String(plan.turnDir) + ",";
  json += "\"turn_ms\":" + String(plan.turnMs) + ",";
  json += "\"drive_ms\":" + String(plan.driveMs) + ",";
  json += "\"speed\":" + String(plan.speed) + ",";
  json += "\"motion_phase\":\"" + String(phaseName(plan.phase)) + "\",";
  json += "\"last_error\":\"" + jsonEscape(plan.lastError) + "\"";
  json += "}";
  web.send(200, "application/json", json);
}

void handleQr() {
  if (!web.hasArg("id")) {
    web.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Missing QR id\"}");
    return;
  }

  lastQrId = web.arg("id");
  qrCount++;

  const bool shouldForward = web.arg("forward") == "1";
  bool forwarded = false;
  if (shouldForward) {
    forwarded = forwardQrToMotor(lastQrId);
  }
  lastQrForwarded = forwarded;

  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"id\":\"" + jsonEscape(lastQrId) + "\",";
  json += "\"qr_count\":" + String(qrCount) + ",";
  json += "\"forward_requested\":" + String(shouldForward ? "true" : "false") + ",";
  json += "\"forwarded\":" + String(forwarded ? "true" : "false") + ",";
  json += "\"seq\":" + String(motorSequence) + ",";
  json += "\"last_error\":\"" + jsonEscape(plan.lastError) + "\"";
  json += "}";
  web.send(200, "application/json", json);
}

void handleStop() {
  cancelMotion();
  web.send(200, "application/json", "{\"status\":\"stopped\"}");
}

void handleNotFound() {
  web.send(404, "application/json", "{\"status\":\"error\",\"error\":\"not found\"}");
}

void startWebRoutes() {
  web.on("/", HTTP_GET, handleRoot);
  web.on("/jsQR.js", HTTP_GET, handleJsQr);
  web.on("/api/status", HTTP_GET, handleStatus);
  web.on("/api/click", HTTP_GET, handleClick);
  web.on("/api/qr", HTTP_POST, handleQr);
  web.on("/api/qr", HTTP_GET, handleQr);
  web.on("/api/stop", HTTP_POST, handleStop);
  web.on("/capture", HTTP_GET, handleCapture);
  web.onNotFound(handleNotFound);
  web.begin();
  Serial.printf("Control UI ready on port %u\n", HTTP_PORT);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial1.begin(MOTOR_UART_BAUD, SERIAL_8N1, MOTOR_UART_RX_PIN, MOTOR_UART_TX_PIN);
  delay(800);

  Serial.println();
  Serial.println("Starting XIAO Click-And-Go");
  Serial.printf(
      "Motor UART: baud=%lu TX=D6/GPIO%d RX=D7/GPIO%d\n",
      static_cast<unsigned long>(MOTOR_UART_BAUD),
      MOTOR_UART_TX_PIN,
      MOTOR_UART_RX_PIN);

  if (!initCamera()) {
    Serial.println("Camera failed; UI will not be useful until this is fixed.");
  }

  startWiFi();
  startWebRoutes();
  startStreamServer();
}

void loop() {
  readMotorAck();
  sendHeartbeatIfDue();
  web.handleClient();
  updateMotionPlan();
  delay(2);
}
