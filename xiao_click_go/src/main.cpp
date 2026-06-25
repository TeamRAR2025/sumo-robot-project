#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
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

static const char *AP_SSID = "SumoVision";
static const char *AP_PASSWORD = "sumo1234";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

static const char *QR_READER_BASE_URL = "http://192.168.4.2";
static const char *QR_READER_STREAM_URL = "http://192.168.4.2:81/stream";

// M5Stack Unit Color / TCS3472-style sensor on the FrontCam board.
static constexpr uint8_t COLOR_SDA_PIN = D4;
static constexpr uint8_t COLOR_SCL_PIN = D5;
static constexpr uint8_t COLOR_I2C_ADDRESS = 0x29;
static constexpr uint16_t COLOR_DARK_CLEAR_THRESHOLD = 600;
static constexpr unsigned long COLOR_READ_INTERVAL_MS = 300;
static constexpr unsigned long COLOR_RETRY_INTERVAL_MS = 5000;

static constexpr uint16_t HTTP_PORT = 80;
static constexpr uint16_t STREAM_PORT = 81;

static WebServer web(HTTP_PORT);
static httpd_handle_t streamHttpd = nullptr;

struct ColorReading {
  bool online = false;
  uint16_t clear = 0;
  uint16_t red = 0;
  uint16_t green = 0;
  uint16_t blue = 0;
  uint8_t redNorm = 0;
  uint8_t greenNorm = 0;
  uint8_t blueNorm = 0;
  bool dark = false;
  String dominant = "unknown";
  String error;
  unsigned long readCount = 0;
  unsigned long lastReadMs = 0;
};

static ColorReading colorReading;
static String lastQrId = "";
static unsigned long qrCount = 0;

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
    .camera-stream {
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
      <p class="muted">Click the front camera image. Motor commands are sent to the QR reader board over HTTP.</p>
    </div>
    <button id="refresh" type="button">Refresh status</button>
  </header>

  <div class="layout">
    <section>
      <h2>Front camera</h2>
      <div class="video-shell" id="video-shell">
        <img id="stream" class="camera-stream" alt="Front camera stream">
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

  <div class="layout">
    <section>
      <h2>QR reader camera</h2>
      <div class="video-shell">
        <img id="qr-stream" class="camera-stream" alt="QR reader camera stream">
      </div>
      <p class="muted">Expected stream: http://192.168.4.2:81/stream</p>
    </section>

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
        Send QR ID to QR reader board
      </label>
    </section>
  </div>

  <section>
    <h2>Sensors</h2>
    <div class="grid">
      <div class="metric"><span>Color</span><strong id="color-status">-</strong></div>
      <div class="metric"><span>Obstacle distance</span><strong id="distance-status">-</strong></div>
    </div>
  </section>

  <section>
    <h2>Status</h2>
    <pre id="status">Loading...</pre>
  </section>
</main>

<script src="/jsQR.js"></script>
<script>
const ROBOT_BASE_URL = "http://192.168.4.2";
const ROBOT_STREAM_URL = "http://192.168.4.2:81/stream";

const stream = document.getElementById("stream");
const qrStream = document.getElementById("qr-stream");
const shell = document.getElementById("video-shell");
const crosshair = document.getElementById("crosshair");
const speedInput = document.getElementById("speed");
const pixel = document.getElementById("pixel");
const ground = document.getElementById("ground");
const angle = document.getElementById("angle");
const distance = document.getElementById("distance");
const statusBox = document.getElementById("status");
const colorStatus = document.getElementById("color-status");
const distanceStatus = document.getElementById("distance-status");
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
qrStream.crossOrigin = "anonymous";
qrStream.src = ROBOT_STREAM_URL;

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

async function fetchJson(url, options = {}) {
  const response = await fetch(url, options);
  if (!response.ok) {
    throw new Error(`${url} returned ${response.status}`);
  }
  return response.json();
}

async function refreshStatus() {
  const front = await fetchJson("/api/status");
  let robot = null;
  try {
    robot = await fetchJson(`${ROBOT_BASE_URL}/api/status`);
  } catch (error) {
    robot = { status: "offline", error: error.message };
  }

  const color = front.color || {};
  colorStatus.textContent = color.online
    ? `${color.dominant} c=${color.clear}`
    : `offline ${color.error || ""}`;

  const distanceReading = robot && robot.distance ? robot.distance : {};
  distanceStatus.textContent = distanceReading.online
    ? `${distanceReading.mm.toFixed(0)} mm`
    : `offline ${distanceReading.error || ""}`;

  statusBox.textContent = JSON.stringify({ front, robot }, null, 2);
}

async function sendStop() {
  const data = await fetchJson(`${ROBOT_BASE_URL}/api/stop`, { method: "POST" });
  statusBox.textContent = JSON.stringify(data, null, 2);
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

  const params = new URLSearchParams({ id: value });
  const front = await fetchJson(`/api/qr?${params.toString()}`, { method: "POST" });
  let robot = null;

  if (qrForward.checked) {
    robot = await fetchJson(`${ROBOT_BASE_URL}/api/qr?${params.toString()}`, { method: "POST" });
  }

  qrSent.textContent = qrForward.checked ? "robot" : "local";
  statusBox.textContent = JSON.stringify({ front, robot }, null, 2);
}

async function scanQrFrame() {
  if (qrMode === "none") {
    return;
  }
  if (!qrStream.naturalWidth || !qrStream.naturalHeight) {
    qrStatus.textContent = "waiting for QR camera";
    return;
  }

  try {
    qrCanvas.width = qrStream.naturalWidth;
    qrCanvas.height = qrStream.naturalHeight;
    qrContext.drawImage(qrStream, 0, 0, qrCanvas.width, qrCanvas.height);

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

  try {
    const data = await fetchJson(`${ROBOT_BASE_URL}/api/click?${params.toString()}`, { method: "POST" });
    statusBox.textContent = JSON.stringify(data, null, 2);
  } catch (error) {
    statusBox.textContent = JSON.stringify({ status: "error", error: error.message }, null, 2);
  }
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

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
    } else if (c == '\n') {
      escaped += "\\n";
      continue;
    } else if (c == '\r') {
      continue;
    }
    escaped += c;
  }
  return escaped;
}

void addCorsHeaders() {
  web.sendHeader("Access-Control-Allow-Origin", "*");
  web.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  web.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  web.sendHeader("Cache-Control", "no-store");
}

bool colorWrite8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(COLOR_I2C_ADDRESS);
  Wire.write(0x80 | reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool colorSensorPresent() {
  Wire.beginTransmission(COLOR_I2C_ADDRESS);
  return Wire.endTransmission() == 0;
}

bool colorRead8(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(COLOR_I2C_ADDRESS);
  Wire.write(0x80 | reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(COLOR_I2C_ADDRESS, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool colorRead16(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(COLOR_I2C_ADDRESS);
  Wire.write(0x80 | reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(COLOR_I2C_ADDRESS, static_cast<uint8_t>(2)) != 2) {
    return false;
  }
  const uint8_t low = Wire.read();
  const uint8_t high = Wire.read();
  value = static_cast<uint16_t>(low | (high << 8));
  return true;
}

bool initColorSensor() {
  Wire.begin(COLOR_SDA_PIN, COLOR_SCL_PIN, 100000U);
  delay(20);

  if (!colorSensorPresent()) {
    colorReading.online = false;
    colorReading.error = "TCS3472 not found";
    return false;
  }

  uint8_t id = 0;
  if (!colorRead8(0x12, id)) {
    colorReading.online = false;
    colorReading.error = "TCS3472 not found";
    return false;
  }

  // 50 ms integration time, 4x gain, power on + RGBC ADC enabled.
  colorWrite8(0x01, 0xEB);
  colorWrite8(0x0F, 0x01);
  colorWrite8(0x00, 0x01);
  delay(3);
  colorWrite8(0x00, 0x03);

  colorReading.online = true;
  colorReading.error = "";
  Serial.printf("Color sensor ready on I2C 0x%02X, ID=0x%02X\n", COLOR_I2C_ADDRESS, id);
  return true;
}

void updateColorReading(bool force = false) {
  const unsigned long now = millis();
  const unsigned long interval = colorReading.online ? COLOR_READ_INTERVAL_MS : COLOR_RETRY_INTERVAL_MS;
  if (!force && now - colorReading.lastReadMs < interval) {
    return;
  }

  colorReading.lastReadMs = now;

  if (!colorSensorPresent()) {
    colorReading.online = false;
    colorReading.error = "TCS3472 not found";
    return;
  }

  uint16_t clear = 0;
  uint16_t red = 0;
  uint16_t green = 0;
  uint16_t blue = 0;

  if (!colorRead16(0x14, clear) || !colorRead16(0x16, red) || !colorRead16(0x18, green) ||
      !colorRead16(0x1A, blue)) {
    colorReading.online = false;
    colorReading.error = "read failed";
    return;
  }

  colorReading.online = true;
  colorReading.error = "";
  colorReading.clear = clear;
  colorReading.red = red;
  colorReading.green = green;
  colorReading.blue = blue;
  colorReading.dark = clear < COLOR_DARK_CLEAR_THRESHOLD;
  colorReading.readCount++;

  if (clear > 0) {
    colorReading.redNorm = static_cast<uint8_t>(min(255UL, (static_cast<unsigned long>(red) * 255UL) / clear));
    colorReading.greenNorm = static_cast<uint8_t>(min(255UL, (static_cast<unsigned long>(green) * 255UL) / clear));
    colorReading.blueNorm = static_cast<uint8_t>(min(255UL, (static_cast<unsigned long>(blue) * 255UL) / clear));
  } else {
    colorReading.redNorm = 0;
    colorReading.greenNorm = 0;
    colorReading.blueNorm = 0;
  }

  if (colorReading.dark) {
    colorReading.dominant = "dark";
  } else if (red >= green && red >= blue) {
    colorReading.dominant = "red";
  } else if (green >= red && green >= blue) {
    colorReading.dominant = "green";
  } else {
    colorReading.dominant = "blue";
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
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  const bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.printf("FrontCam AP started: %s\n", apStarted ? "true" : "false");
  Serial.printf("FrontCam AP SSID: %s\n", AP_SSID);
  Serial.printf("FrontCam AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("FrontCam AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
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
    Serial.printf("Front camera stream ready on port %u\n", STREAM_PORT);
  } else {
    Serial.println("Failed to start stream server");
  }
}

String colorJson() {
  updateColorReading(true);
  String json = "{";
  json += "\"online\":" + String(colorReading.online ? "true" : "false") + ",";
  json += "\"clear\":" + String(colorReading.clear) + ",";
  json += "\"red\":" + String(colorReading.red) + ",";
  json += "\"green\":" + String(colorReading.green) + ",";
  json += "\"blue\":" + String(colorReading.blue) + ",";
  json += "\"red_norm\":" + String(colorReading.redNorm) + ",";
  json += "\"green_norm\":" + String(colorReading.greenNorm) + ",";
  json += "\"blue_norm\":" + String(colorReading.blueNorm) + ",";
  json += "\"dominant\":\"" + jsonEscape(colorReading.dominant) + "\",";
  json += "\"dark\":" + String(colorReading.dark ? "true" : "false") + ",";
  json += "\"read_count\":" + String(colorReading.readCount) + ",";
  json += "\"error\":\"" + jsonEscape(colorReading.error) + "\"";
  json += "}";
  return json;
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
  addCorsHeaders();
  const IPAddress ip = WiFi.softAPIP();
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"role\":\"front_cam\",";
  json += "\"ip\":\"" + ip.toString() + "\",";
  json += "\"stream_url\":\"http://" + ip.toString() + ":" + String(STREAM_PORT) + "/stream\",";
  json += "\"qr_reader_base_url\":\"" + String(QR_READER_BASE_URL) + "\",";
  json += "\"qr_reader_stream_url\":\"" + String(QR_READER_STREAM_URL) + "\",";
  json += "\"color\":" + colorJson() + ",";
  json += "\"last_qr_id\":\"" + jsonEscape(lastQrId) + "\",";
  json += "\"qr_count\":" + String(qrCount);
  json += "}";
  web.send(200, "application/json", json);
}

void handleColor() {
  addCorsHeaders();
  web.send(200, "application/json", colorJson());
}

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    addCorsHeaders();
    web.send(503, "application/json", "{\"status\":\"error\",\"error\":\"Camera capture failed\"}");
    return;
  }

  web.sendHeader("Access-Control-Allow-Origin", "*");
  web.sendHeader("Cache-Control", "no-store");
  web.setContentLength(fb->len);
  web.send(200, "image/jpeg", "");
  WiFiClient client = web.client();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleQr() {
  addCorsHeaders();
  if (!web.hasArg("id")) {
    web.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Missing QR id\"}");
    return;
  }

  lastQrId = web.arg("id");
  qrCount++;

  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"role\":\"front_cam\",";
  json += "\"id\":\"" + jsonEscape(lastQrId) + "\",";
  json += "\"qr_count\":" + String(qrCount);
  json += "}";
  web.send(200, "application/json", json);
}

void handleOptions() {
  addCorsHeaders();
  web.send(204);
}

void handleNotFound() {
  if (web.method() == HTTP_OPTIONS) {
    handleOptions();
    return;
  }

  addCorsHeaders();
  web.send(404, "application/json", "{\"status\":\"error\",\"error\":\"not found\"}");
}

void startWebRoutes() {
  web.on("/", HTTP_GET, handleRoot);
  web.on("/jsQR.js", HTTP_GET, handleJsQr);
  web.on("/api/status", HTTP_GET, handleStatus);
  web.on("/api/status", HTTP_OPTIONS, handleOptions);
  web.on("/api/color", HTTP_GET, handleColor);
  web.on("/api/color", HTTP_OPTIONS, handleOptions);
  web.on("/api/qr", HTTP_POST, handleQr);
  web.on("/api/qr", HTTP_GET, handleQr);
  web.on("/api/qr", HTTP_OPTIONS, handleOptions);
  web.on("/capture", HTTP_GET, handleCapture);
  web.onNotFound(handleNotFound);
  web.begin();
  Serial.printf("FrontCam UI ready on port %u\n", HTTP_PORT);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(800);

  Serial.println();
  Serial.println("Starting FrontCam XIAO");

  if (!initCamera()) {
    Serial.println("Camera failed; UI will not be useful until this is fixed.");
  }

  initColorSensor();
  startWiFi();
  startWebRoutes();
  startStreamServer();
}

void loop() {
  web.handleClient();
  updateColorReading();
  delay(2);
}
