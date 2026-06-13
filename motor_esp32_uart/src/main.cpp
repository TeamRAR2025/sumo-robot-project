#include <Arduino.h>

static constexpr uint32_t MOTOR_UART_BAUD = 115200;
static constexpr int MOTOR_UART_RX_PIN = 16; // Motor ESP32 RX2 <- XIAO D6/TX
static constexpr int MOTOR_UART_TX_PIN = 17; // Motor ESP32 TX2 -> XIAO D7/RX
static constexpr unsigned long LINK_TIMEOUT_MS = 1200;

// Keep false until real motor driver pins and direction logic are filled in.
static constexpr bool MOTOR_OUTPUTS_ENABLED = false;

static String rxLine;
static String lastType = "";
static String lastCommand = "s";
static String lastQrId = "";
static int lastSpeed = 0;
static uint32_t lastSeq = 0;
static unsigned long lastPacketMs = 0;
static bool stoppedByWatchdog = false;

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

String extractJsonString(const String &json, const String &key, const String &fallback = "") {
  const String pattern = "\"" + key + "\":\"";
  int start = json.indexOf(pattern);
  if (start < 0) {
    return fallback;
  }

  start += pattern.length();
  String value = "";
  bool escaped = false;

  for (int i = start; i < json.length(); i++) {
    const char c = json[i];
    if (escaped) {
      value += c;
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      return value;
    }
    value += c;
  }

  return fallback;
}

long extractJsonInt(const String &json, const String &key, long fallback = 0) {
  const String pattern = "\"" + key + "\":";
  int start = json.indexOf(pattern);
  if (start < 0) {
    return fallback;
  }

  start += pattern.length();
  while (start < json.length() && json[start] == ' ') {
    start++;
  }

  int end = start;
  while (end < json.length() && (isDigit(json[end]) || json[end] == '-')) {
    end++;
  }

  if (end == start) {
    return fallback;
  }

  return json.substring(start, end).toInt();
}

int clampSpeed(long speed) {
  if (speed < 0) {
    return 0;
  }
  if (speed > 255) {
    return 255;
  }
  return static_cast<int>(speed);
}

void stopMotors() {
  if (!MOTOR_OUTPUTS_ENABLED) {
    Serial.println("Motor outputs disabled: stopMotors()");
    return;
  }

  // TODO: set all motor PWM outputs to 0 and brake/coast safely.
}

void driveForward(int speed) {
  if (!MOTOR_OUTPUTS_ENABLED) {
    Serial.printf("Motor outputs disabled: driveForward(%d)\n", speed);
    return;
  }

  // TODO: apply motor driver forward direction and PWM.
}

void driveBackward(int speed) {
  if (!MOTOR_OUTPUTS_ENABLED) {
    Serial.printf("Motor outputs disabled: driveBackward(%d)\n", speed);
    return;
  }

  // TODO: apply motor driver backward direction and PWM.
}

void turnLeft(int speed) {
  if (!MOTOR_OUTPUTS_ENABLED) {
    Serial.printf("Motor outputs disabled: turnLeft(%d)\n", speed);
    return;
  }

  // TODO: apply left turn strategy.
}

void turnRight(int speed) {
  if (!MOTOR_OUTPUTS_ENABLED) {
    Serial.printf("Motor outputs disabled: turnRight(%d)\n", speed);
    return;
  }

  // TODO: apply right turn strategy.
}

void applyCommand(const String &command, int speed) {
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

void sendAck(const String &status, const String &extra = "") {
  String json = "{";
  json += "\"seq\":" + String(lastSeq) + ",";
  json += "\"status\":\"" + jsonEscape(status) + "\",";
  json += "\"type\":\"" + jsonEscape(lastType) + "\"";
  if (extra.length() > 0) {
    json += ",";
    json += extra;
  }
  json += "}";

  Serial2.println(json);
  Serial.printf("UART TX: %s\n", json.c_str());
}

void handleCommandMessage(const String &line) {
  const String command = extractJsonString(line, "cmd", "s");
  const int speed = clampSpeed(extractJsonInt(line, "speed", 150));

  if (command != "f" && command != "b" && command != "l" && command != "r" && command != "s") {
    stopMotors();
    sendAck("error", "\"error\":\"unknown command\"");
    return;
  }

  lastCommand = command;
  lastSpeed = speed;
  stoppedByWatchdog = false;

  applyCommand(command, speed);
  sendAck("ok", "\"cmd\":\"" + jsonEscape(command) + "\",\"speed\":" + String(speed));
}

void handleQrMessage(const String &line) {
  lastQrId = extractJsonString(line, "id", "");
  sendAck("ok", "\"id\":\"" + jsonEscape(lastQrId) + "\"");
}

void handleHeartbeatMessage() {
  sendAck("ok");
}

void handleLine(const String &line) {
  Serial.printf("UART RX: %s\n", line.c_str());

  lastSeq = static_cast<uint32_t>(extractJsonInt(line, "seq", 0));
  lastType = extractJsonString(line, "type", "");
  lastPacketMs = millis();

  if (lastType == "cmd") {
    handleCommandMessage(line);
  } else if (lastType == "qr") {
    handleQrMessage(line);
  } else if (lastType == "heartbeat") {
    handleHeartbeatMessage();
  } else {
    stopMotors();
    sendAck("error", "\"error\":\"unknown type\"");
  }
}

void readUart() {
  while (Serial2.available() > 0) {
    const char c = static_cast<char>(Serial2.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (rxLine.length() > 0) {
        handleLine(rxLine);
        rxLine = "";
      }
      continue;
    }

    if (rxLine.length() < 240) {
      rxLine += c;
    } else {
      rxLine = "";
      stopMotors();
      sendAck("error", "\"error\":\"line too long\"");
    }
  }
}

void applyLinkWatchdog() {
  if (lastPacketMs == 0) {
    return;
  }
  if (millis() - lastPacketMs <= LINK_TIMEOUT_MS) {
    return;
  }
  if (stoppedByWatchdog) {
    return;
  }

  stoppedByWatchdog = true;
  lastCommand = "s";
  lastSpeed = 0;
  stopMotors();
  Serial.println("UART watchdog timeout: motors stopped");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(MOTOR_UART_BAUD, SERIAL_8N1, MOTOR_UART_RX_PIN, MOTOR_UART_TX_PIN);
  delay(800);

  Serial.println();
  Serial.println("Motor ESP32 UART firmware skeleton");
  Serial.printf(
      "UART: baud=%lu RX=GPIO%d TX=GPIO%d\n",
      static_cast<unsigned long>(MOTOR_UART_BAUD),
      MOTOR_UART_RX_PIN,
      MOTOR_UART_TX_PIN);
  Serial.printf("Motor outputs enabled: %s\n", MOTOR_OUTPUTS_ENABLED ? "true" : "false");

  stopMotors();
}

void loop() {
  readUart();
  applyLinkWatchdog();
  delay(2);
}
