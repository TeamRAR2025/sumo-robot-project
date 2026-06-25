#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include "esp_camera.h"
#include "esp_http_server.h"

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

static const char *WIFI_SSID = "SumoVision";
static const char *WIFI_PASSWORD = "sumo1234";
static const IPAddress LOCAL_IP(192, 168, 4, 2);
static const IPAddress GATEWAY(192, 168, 4, 1);
static const IPAddress SUBNET(255, 255, 255, 0);
static const IPAddress DNS_SERVER(192, 168, 4, 1);

// DFRobot dual motor driver pins on the QR reader board.
// Adjust these constants if the breadboard wiring changes.
static constexpr uint8_t MOTOR_DIR1_PIN = D0;
static constexpr uint8_t MOTOR_PWM1_PIN = D1;
static constexpr uint8_t MOTOR_PWM2_PIN = D2;
static constexpr uint8_t MOTOR_DIR2_PIN = D3;
static constexpr bool MOTOR1_FORWARD_HIGH = true;
static constexpr bool MOTOR2_FORWARD_HIGH = true;
static constexpr bool MOTOR_OUTPUTS_ENABLED = true;
// Leave LEDC channel 0 to the camera XCLK generator.
static constexpr uint8_t MOTOR1_PWM_CHANNEL = 4;
static constexpr uint8_t MOTOR2_PWM_CHANNEL = 5;
static constexpr uint32_t MOTOR_PWM_FREQUENCY = 20000;
static constexpr uint8_t MOTOR_PWM_RESOLUTION = 8;

// M5Stack Unit Sonic-style distance sensor on the QR reader board.
static constexpr uint8_t DISTANCE_SDA_PIN = D4;
static constexpr uint8_t DISTANCE_SCL_PIN = D5;
static constexpr uint8_t DISTANCE_I2C_ADDRESS = 0x57;
static constexpr unsigned long DISTANCE_READ_INTERVAL_MS = 350;
static constexpr unsigned long DISTANCE_MEASUREMENT_MS = 120;
static constexpr uint16_t DISTANCE_MAX_VALID_MM = 4500;
static constexpr bool DISTANCE_AUTO_STOP_ENABLED = false;
static constexpr uint16_t DISTANCE_AUTO_STOP_MM = 120;

static constexpr uint16_t HTTP_PORT = 80;
static constexpr uint16_t STREAM_PORT = 81;
static constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;

// Open-loop tuning constants. Replace with measured values after robot calibration.
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
  MANUAL,
  DONE
};

struct MotionPlan {
  MotionPhase phase = MotionPhase::IDLE;
  int turnDir = 0;
  uint16_t turnMs = 0;
  uint16_t driveMs = 0;
  uint16_t speed = DEFAULT_SPEED;
  unsigned long deadlineMs = 0;
  String lastError;
};

struct DistanceReading {
  bool online = false;
  float mm = 0.0f;
  bool inRange = false;
  String error;
  unsigned long readCount = 0;
  unsigned long lastReadMs = 0;
};

static MotionPlan plan;
static DistanceReading distanceReading;
static unsigned long requestCount = 0;
static unsigned long commandCount = 0;
static unsigned long lastReconnectAttemptMs = 0;
static String lastMotorCommand = "s";
static int lastLeftSpeed = 0;
static int lastRightSpeed = 0;
static String lastQrId = "";
static unsigned long qrCount = 0;
static bool distanceReadPending = false;
static unsigned long distanceRequestMs = 0;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>QR Reader Robot Node</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #101114;
      --panel: #1a1d22;
      --text: #f2f5f8;
      --muted: #98a2b3;
      --line: #343b46;
      --accent: #31c48d;
      --danger: #f05252;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: var(--bg);
      color: var(--text);
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    main {
      width: min(900px, 100%);
      margin: 0 auto;
      padding: 16px;
      display: grid;
      gap: 12px;
    }
    section {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
    }
    h1, h2, p { margin: 0; }
    h1 { font-size: 20px; }
    h2 { font-size: 15px; margin-bottom: 8px; }
    .muted { color: var(--muted); font-size: 13px; margin-top: 4px; }
    img {
      display: block;
      width: 100%;
      min-height: 240px;
      object-fit: contain;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #050608;
    }
    .row {
      display: grid;
      grid-template-columns: repeat(5, 1fr);
      gap: 8px;
    }
    button {
      border: 0;
      border-radius: 8px;
      min-height: 40px;
      padding: 0 10px;
      font-weight: 700;
      color: #06100c;
      background: var(--accent);
      cursor: pointer;
    }
    button.stop { background: var(--danger); color: #fff; }
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
    a { color: var(--accent); }
    @media (max-width: 620px) {
      .row { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
<main>
  <section>
    <h1>QR Reader Robot Node</h1>
    <p class="muted">This board serves the QR camera, reads the distance sensor, and drives the motor driver.</p>
  </section>
  <section>
    <h2>QR camera</h2>
    <img id="stream" alt="QR reader camera stream">
    <p class="muted">Stream: <a href="http://192.168.4.2:81/stream">http://192.168.4.2:81/stream</a></p>
  </section>
  <section>
    <h2>Manual motor test</h2>
    <div class="row">
      <button data-cmd="l">Left</button>
      <button data-cmd="f">Forward</button>
      <button class="stop" data-cmd="s">Stop</button>
      <button data-cmd="b">Back</button>
      <button data-cmd="r">Right</button>
    </div>
  </section>
  <section>
    <h2>Status</h2>
    <pre id="status">Loading...</pre>
  </section>
</main>
<script>
const statusBox = document.getElementById("status");
document.getElementById("stream").src = `http://${location.hostname}:81/stream`;

async function fetchJson(url, options = {}) {
  const response = await fetch(url, options);
  if (!response.ok) throw new Error(`${url} returned ${response.status}`);
  return response.json();
}

async function refreshStatus() {
  const data = await fetchJson("/api/status");
  statusBox.textContent = JSON.stringify(data, null, 2);
}

async function command(cmd) {
  const data = await fetchJson(`/api/command?cmd=${cmd}&speed=130`, { method: "POST" });
  statusBox.textContent = JSON.stringify(data, null, 2);
}

document.querySelectorAll("[data-cmd]").forEach((button) => {
  button.addEventListener("click", () => void command(button.dataset.cmd));
});

void refreshStatus();
window.setInterval(() => void refreshStatus(), 1500);
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
    case MotionPhase::MANUAL:
      return "manual";
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

void addCorsHeaders() {
  web.sendHeader("Access-Control-Allow-Origin", "*");
  web.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  web.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  web.sendHeader("Cache-Control", "no-store");
}

void writeMotor(uint8_t dirPin, uint8_t pwmChannel, int speed, bool forwardHigh) {
  const int clamped = constrain(speed, -255, 255);
  const bool forward = clamped >= 0;
  const uint8_t pwm = static_cast<uint8_t>(abs(clamped));
  digitalWrite(dirPin, forward == forwardHigh ? HIGH : LOW);
  ledcWrite(pwmChannel, MOTOR_OUTPUTS_ENABLED ? pwm : 0);
}

void setMotorSpeeds(int leftSpeed, int rightSpeed) {
  lastLeftSpeed = constrain(leftSpeed, -255, 255);
  lastRightSpeed = constrain(rightSpeed, -255, 255);
  writeMotor(MOTOR_DIR1_PIN, MOTOR1_PWM_CHANNEL, lastLeftSpeed, MOTOR1_FORWARD_HIGH);
  writeMotor(MOTOR_DIR2_PIN, MOTOR2_PWM_CHANNEL, lastRightSpeed, MOTOR2_FORWARD_HIGH);
}

void stopMotors() {
  lastMotorCommand = "s";
  setMotorSpeeds(0, 0);
}

void driveForward(uint16_t speed) {
  lastMotorCommand = "f";
  setMotorSpeeds(speed, speed);
}

void driveBackward(uint16_t speed) {
  lastMotorCommand = "b";
  setMotorSpeeds(-static_cast<int>(speed), -static_cast<int>(speed));
}

void turnLeft(uint16_t speed) {
  lastMotorCommand = "l";
  setMotorSpeeds(-static_cast<int>(speed), speed);
}

void turnRight(uint16_t speed) {
  lastMotorCommand = "r";
  setMotorSpeeds(speed, -static_cast<int>(speed));
}

void setupMotors() {
  pinMode(MOTOR_DIR1_PIN, OUTPUT);
  pinMode(MOTOR_DIR2_PIN, OUTPUT);
  ledcSetup(MOTOR1_PWM_CHANNEL, MOTOR_PWM_FREQUENCY, MOTOR_PWM_RESOLUTION);
  ledcSetup(MOTOR2_PWM_CHANNEL, MOTOR_PWM_FREQUENCY, MOTOR_PWM_RESOLUTION);
  ledcAttachPin(MOTOR_PWM1_PIN, MOTOR1_PWM_CHANNEL);
  ledcAttachPin(MOTOR_PWM2_PIN, MOTOR2_PWM_CHANNEL);
  stopMotors();
}

void finishDistanceRead() {
  const uint8_t received = Wire.requestFrom(DISTANCE_I2C_ADDRESS, static_cast<uint8_t>(3));
  if (received != 3) {
    distanceReading.online = false;
    distanceReading.error = "I2C read failed";
    distanceReadPending = false;
    return;
  }

  const uint32_t raw = (static_cast<uint32_t>(Wire.read()) << 16) |
                       (static_cast<uint32_t>(Wire.read()) << 8) |
                       static_cast<uint32_t>(Wire.read());
  const float distanceMm = static_cast<float>(raw) / 1000.0f;

  distanceReading.mm = distanceMm;
  distanceReading.inRange = distanceMm > 0.0f && distanceMm <= DISTANCE_MAX_VALID_MM;
  distanceReading.online = true;
  distanceReading.error = "";
  distanceReading.readCount++;
  distanceReadPending = false;

  if (DISTANCE_AUTO_STOP_ENABLED && plan.phase != MotionPhase::IDLE && plan.phase != MotionPhase::DONE &&
      distanceReading.inRange && distanceReading.mm < DISTANCE_AUTO_STOP_MM) {
    stopMotors();
    plan.phase = MotionPhase::DONE;
    plan.lastError = "distance auto-stop";
  }
}

bool updateDistanceSensor(bool force = false) {
  const unsigned long now = millis();

  if (distanceReadPending) {
    if (force || now - distanceRequestMs >= DISTANCE_MEASUREMENT_MS) {
      if (force && now - distanceRequestMs < DISTANCE_MEASUREMENT_MS) {
        delay(DISTANCE_MEASUREMENT_MS - (now - distanceRequestMs));
      }
      finishDistanceRead();
    }
    return distanceReading.online;
  }

  if (!force && now - distanceReading.lastReadMs < DISTANCE_READ_INTERVAL_MS) {
    return distanceReading.online;
  }

  Wire.beginTransmission(DISTANCE_I2C_ADDRESS);
  Wire.write(0x01);
  if (Wire.endTransmission() != 0) {
    distanceReading.online = false;
    distanceReading.error = "I2C write failed";
    return false;
  }

  distanceReading.lastReadMs = now;
  distanceRequestMs = now;
  distanceReadPending = true;

  if (force) {
    delay(DISTANCE_MEASUREMENT_MS);
    finishDistanceRead();
  }

  return distanceReading.online;
}

void cancelMotion() {
  plan.phase = MotionPhase::IDLE;
  plan.deadlineMs = 0;
  stopMotors();
}

void scheduleMotion(int turnDir, uint16_t turnMs, uint16_t driveMs, uint16_t speed) {
  cancelMotion();

  plan.turnDir = turnDir < 0 ? -1 : (turnDir > 0 ? 1 : 0);
  plan.turnMs = turnMs;
  plan.driveMs = driveMs;
  plan.speed = speed;
  plan.lastError = "";

  if (plan.turnDir != 0 && plan.turnMs > 0) {
    plan.turnDir > 0 ? turnRight(plan.speed) : turnLeft(plan.speed);
    plan.phase = MotionPhase::TURNING;
    plan.deadlineMs = millis() + plan.turnMs;
    return;
  }

  if (plan.driveMs > 0) {
    driveForward(plan.speed);
    plan.phase = MotionPhase::DRIVING;
    plan.deadlineMs = millis() + plan.driveMs;
    return;
  }

  plan.phase = MotionPhase::DONE;
}

void updateMotionPlan() {
  if (plan.phase == MotionPhase::IDLE || plan.phase == MotionPhase::DONE || plan.phase == MotionPhase::MANUAL) {
    return;
  }

  const long remaining = static_cast<long>(plan.deadlineMs - millis());
  if (remaining > 0) {
    return;
  }

  if (plan.phase == MotionPhase::TURNING) {
    stopMotors();
    plan.phase = MotionPhase::TURN_SETTLE;
    plan.deadlineMs = millis() + TURN_SETTLE_MS;
    return;
  }

  if (plan.phase == MotionPhase::TURN_SETTLE) {
    if (plan.driveMs > 0) {
      driveForward(plan.speed);
      plan.phase = MotionPhase::DRIVING;
      plan.deadlineMs = millis() + plan.driveMs;
    } else {
      plan.phase = MotionPhase::DONE;
    }
    return;
  }

  if (plan.phase == MotionPhase::DRIVING) {
    stopMotors();
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

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, DNS_SERVER)) {
    Serial.println("Warning: failed to apply static IP configuration.");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting QR reader to SSID: %s", WIFI_SSID);

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("QR reader IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    return true;
  }

  Serial.println("QR reader failed to connect to FrontCam AP.");
  return false;
}

void ensureWiFiConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastReconnectAttemptMs < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastReconnectAttemptMs = now;
  Serial.println("Wi-Fi disconnected. Reconnecting QR reader...");
  WiFi.disconnect();
  connectWiFi();
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

    vTaskDelay(pdMS_TO_TICKS(45));
  }

  return res;
}

void startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = 32770;

  const httpd_uri_t streamUri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = streamHandler,
      .user_ctx = nullptr,
  };

  if (httpd_start(&streamHttpd, &config) == ESP_OK) {
    httpd_register_uri_handler(streamHttpd, &streamUri);
    Serial.printf("QR stream ready on port %u\n", STREAM_PORT);
  } else {
    Serial.println("Failed to start QR stream server");
  }
}

String distanceJson() {
  updateDistanceSensor(true);
  String json = "{";
  json += "\"online\":" + String(distanceReading.online ? "true" : "false") + ",";
  json += "\"mm\":" + String(distanceReading.mm, 1) + ",";
  json += "\"cm\":" + String(distanceReading.mm / 10.0f, 1) + ",";
  json += "\"in_range\":" + String(distanceReading.inRange ? "true" : "false") + ",";
  json += "\"read_count\":" + String(distanceReading.readCount) + ",";
  json += "\"error\":\"" + jsonEscape(distanceReading.error) + "\"";
  json += "}";
  return json;
}

String motorJson() {
  String json = "{";
  json += "\"outputs_enabled\":" + String(MOTOR_OUTPUTS_ENABLED ? "true" : "false") + ",";
  json += "\"last_command\":\"" + jsonEscape(lastMotorCommand) + "\",";
  json += "\"left_speed\":" + String(lastLeftSpeed) + ",";
  json += "\"right_speed\":" + String(lastRightSpeed) + ",";
  json += "\"command_count\":" + String(commandCount) + ",";
  json += "\"phase\":\"" + String(phaseName(plan.phase)) + "\",";
  json += "\"turn_ms\":" + String(plan.turnMs) + ",";
  json += "\"drive_ms\":" + String(plan.driveMs) + ",";
  json += "\"speed\":" + String(plan.speed) + ",";
  json += "\"last_error\":\"" + jsonEscape(plan.lastError) + "\"";
  json += "}";
  return json;
}

void handleRoot() {
  requestCount++;
  addCorsHeaders();
  web.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  requestCount++;
  addCorsHeaders();

  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"role\":\"qr_reader_robot\",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"stream_url\":\"http://" + WiFi.localIP().toString() + ":" + String(STREAM_PORT) + "/stream\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"request_count\":" + String(requestCount) + ",";
  json += "\"motor\":" + motorJson() + ",";
  json += "\"distance\":" + distanceJson() + ",";
  json += "\"last_qr_id\":\"" + jsonEscape(lastQrId) + "\",";
  json += "\"qr_count\":" + String(qrCount);
  json += "}";

  web.send(200, "application/json", json);
}

void handleDistance() {
  requestCount++;
  addCorsHeaders();
  web.send(200, "application/json", distanceJson());
}

void handleCapture() {
  requestCount++;

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

void handleCommand() {
  requestCount++;
  commandCount++;
  addCorsHeaders();

  const String cmd = web.arg("cmd");
  const uint16_t speed = boundedSpeed(web.arg("speed").length() > 0 ? web.arg("speed").toInt() : DEFAULT_SPEED);

  plan.phase = MotionPhase::MANUAL;
  plan.deadlineMs = 0;
  plan.lastError = "";

  if (cmd == "f") {
    driveForward(speed);
  } else if (cmd == "b") {
    driveBackward(speed);
  } else if (cmd == "l") {
    turnLeft(speed);
  } else if (cmd == "r") {
    turnRight(speed);
  } else if (cmd == "s") {
    cancelMotion();
  } else {
    stopMotors();
    plan.phase = MotionPhase::DONE;
    plan.lastError = "unknown command";
    web.send(400, "application/json", "{\"status\":\"error\",\"error\":\"unknown command\"}");
    return;
  }

  String json = "{";
  json += "\"status\":\"accepted\",";
  json += "\"cmd\":\"" + jsonEscape(cmd) + "\",";
  json += "\"speed\":" + String(speed) + ",";
  json += "\"motor\":" + motorJson();
  json += "}";
  web.send(200, "application/json", json);
}

void handleClick() {
  requestCount++;
  commandCount++;
  addCorsHeaders();

  const uint16_t speed = boundedSpeed(web.arg("speed").length() > 0 ? web.arg("speed").toInt() : DEFAULT_SPEED);
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
  json += "\"turn_dir\":" + String(plan.turnDir) + ",";
  json += "\"turn_ms\":" + String(plan.turnMs) + ",";
  json += "\"drive_ms\":" + String(plan.driveMs) + ",";
  json += "\"speed\":" + String(plan.speed) + ",";
  json += "\"motor\":" + motorJson() + ",";
  json += "\"distance\":" + distanceJson();
  json += "}";
  web.send(200, "application/json", json);
}

void handleQr() {
  requestCount++;
  addCorsHeaders();

  if (!web.hasArg("id")) {
    web.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Missing QR id\"}");
    return;
  }

  lastQrId = web.arg("id");
  qrCount++;

  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"role\":\"qr_reader_robot\",";
  json += "\"id\":\"" + jsonEscape(lastQrId) + "\",";
  json += "\"qr_count\":" + String(qrCount);
  json += "}";
  web.send(200, "application/json", json);
}

void handleStop() {
  requestCount++;
  commandCount++;
  addCorsHeaders();
  cancelMotion();
  web.send(200, "application/json", "{\"status\":\"stopped\",\"motor\":{\"last_command\":\"s\",\"left_speed\":0,\"right_speed\":0}}");
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

void setupRoutes() {
  web.on("/", HTTP_GET, handleRoot);
  web.on("/", HTTP_OPTIONS, handleOptions);
  web.on("/api/status", HTTP_GET, handleStatus);
  web.on("/api/status", HTTP_OPTIONS, handleOptions);
  web.on("/api/distance", HTTP_GET, handleDistance);
  web.on("/api/distance", HTTP_OPTIONS, handleOptions);
  web.on("/api/command", HTTP_GET, handleCommand);
  web.on("/api/command", HTTP_POST, handleCommand);
  web.on("/api/command", HTTP_OPTIONS, handleOptions);
  web.on("/api/click", HTTP_GET, handleClick);
  web.on("/api/click", HTTP_POST, handleClick);
  web.on("/api/click", HTTP_OPTIONS, handleOptions);
  web.on("/api/qr", HTTP_GET, handleQr);
  web.on("/api/qr", HTTP_POST, handleQr);
  web.on("/api/qr", HTTP_OPTIONS, handleOptions);
  web.on("/api/stop", HTTP_GET, handleStop);
  web.on("/api/stop", HTTP_POST, handleStop);
  web.on("/api/stop", HTTP_OPTIONS, handleOptions);
  web.on("/capture", HTTP_GET, handleCapture);
  web.onNotFound(handleNotFound);
  web.begin();
  Serial.printf("QR reader robot API ready on port %u\n", HTTP_PORT);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(800);

  Serial.println();
  Serial.println("Starting QR Reader Robot XIAO");

  setupMotors();
  Wire.begin(DISTANCE_SDA_PIN, DISTANCE_SCL_PIN, 100000U);

  if (!initCamera()) {
    Serial.println("Camera failed; stream will not be useful until this is fixed.");
  }

  connectWiFi();
  setupRoutes();
  startStreamServer();
  updateDistanceSensor(true);

  Serial.printf(
      "Motor pins: DIR1=D0/GPIO%d PWM1=D1/GPIO%d PWM2=D2/GPIO%d DIR2=D3/GPIO%d enabled=%s\n",
      MOTOR_DIR1_PIN,
      MOTOR_PWM1_PIN,
      MOTOR_PWM2_PIN,
      MOTOR_DIR2_PIN,
      MOTOR_OUTPUTS_ENABLED ? "true" : "false");
  Serial.printf("Distance sensor I2C: SDA=D4/GPIO%d SCL=D5/GPIO%d address=0x%02X\n", DISTANCE_SDA_PIN, DISTANCE_SCL_PIN, DISTANCE_I2C_ADDRESS);
}

void loop() {
  ensureWiFiConnection();
  web.handleClient();
  updateMotionPlan();
  updateDistanceSensor();
  delay(2);
}
