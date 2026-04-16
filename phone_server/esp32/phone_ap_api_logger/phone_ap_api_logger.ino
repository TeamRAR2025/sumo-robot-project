#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "robonet";
const char* WIFI_PASSWORD = "12345678qwerty";

IPAddress LOCAL_IP(192, 168, 43, 2);
IPAddress GATEWAY(192, 168, 43, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress DNS_SERVER(192, 168, 43, 1);

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;

WebServer server(80);

String lastCommand = "STOP";
int lastSpeed = 150;
unsigned long requestCount = 0;
unsigned long lastReconnectAttemptMs = 0;

const char* HEADERS_TO_COLLECT[] = {
  "User-Agent",
  "Content-Type",
  "Accept",
  "Host",
  "Connection"
};
const size_t HEADERS_TO_COLLECT_COUNT = sizeof(HEADERS_TO_COLLECT) / sizeof(HEADERS_TO_COLLECT[0]);

String httpMethodToString(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_DELETE:
      return "DELETE";
    case HTTP_PATCH:
      return "PATCH";
    case HTTP_OPTIONS:
      return "OPTIONS";
    case HTTP_HEAD:
      return "HEAD";
    default:
      return "UNKNOWN";
  }
}

String commandCodeToName(const String& code) {
  if (code == "f") {
    return "FORWARD";
  }
  if (code == "b") {
    return "BACKWARD";
  }
  if (code == "l") {
    return "LEFT";
  }
  if (code == "r") {
    return "RIGHT";
  }
  if (code == "s") {
    return "STOP";
  }
  return "UNKNOWN";
}

void printDivider(const char* label) {
  Serial.println();
  Serial.println("========================================");
  Serial.println(label);
  Serial.println("========================================");
}

void logRequest(const char* routeName) {
  requestCount++;

  printDivider("API REQUEST");
  Serial.printf("Count: %lu\n", requestCount);
  Serial.printf("Route: %s\n", routeName);
  Serial.printf("Method: %s\n", httpMethodToString(server.method()).c_str());
  Serial.printf("URI: %s\n", server.uri().c_str());
  Serial.printf("Client IP: %s\n", server.client().remoteIP().toString().c_str());

  if (server.args() == 0) {
    Serial.println("Args: none");
  } else {
    Serial.println("Args:");
    for (int i = 0; i < server.args(); i++) {
      Serial.printf("  - %s = %s\n", server.argName(i).c_str(), server.arg(i).c_str());
    }
  }

  if (server.hasArg("plain")) {
    Serial.printf("Body: %s\n", server.arg("plain").c_str());
  }

  if (server.headers() == 0) {
    Serial.println("Headers: none");
  } else {
    Serial.println("Headers:");
    for (int i = 0; i < server.headers(); i++) {
      Serial.printf("  - %s: %s\n", server.headerName(i).c_str(), server.header(i).c_str());
    }
  }
}

void printNetworkInfo() {
  printDivider("NETWORK STATUS");
  Serial.printf("Connected to SSID: %s\n", WIFI_SSID);
  Serial.printf("ESP32 IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("Subnet: %s\n", WiFi.subnetMask().toString().c_str());
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
}

bool connectToPhoneAp() {
  printDivider("WIFI CONNECT");
  Serial.printf("Connecting to AP '%s'\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, DNS_SERVER)) {
    Serial.println("Warning: failed to apply static IP configuration.");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    printNetworkInfo();
    return true;
  }

  Serial.println("Failed to connect to the phone AP within timeout.");
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
  Serial.println("Wi-Fi disconnected. Retrying AP connection...");
  WiFi.disconnect();
  connectToPhoneAp();
}

void handleRoot() {
  logRequest("/");

  String response = "{";
  response += "\"status\":\"ok\",";
  response += "\"message\":\"ESP32 API logger is running\",";
  response += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  response += "\"last_command\":\"" + lastCommand + "\",";
  response += "\"last_speed\":" + String(lastSpeed);
  response += "}";

  server.send(200, "application/json", response);
}

void handleHealth() {
  logRequest("/health");

  String response = "{";
  response += "\"status\":\"ok\",";
  response += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  response += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  response += "\"request_count\":" + String(requestCount);
  response += "}";

  server.send(200, "application/json", response);
}

void handleCommand() {
  logRequest("/cmd");

  if (!server.hasArg("c")) {
    Serial.println("Command rejected: missing 'c' query parameter.");
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Missing command code 'c'.\"}");
    return;
  }

  String commandCode = server.arg("c");
  commandCode.toLowerCase();
  const String mappedCommand = commandCodeToName(commandCode);
  const int speed = server.hasArg("s") ? server.arg("s").toInt() : 150;

  if (mappedCommand == "UNKNOWN") {
    Serial.printf("Command rejected: unknown code '%s'\n", commandCode.c_str());
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"Unknown command code.\"}");
    return;
  }

  lastCommand = mappedCommand;
  lastSpeed = speed;

  Serial.printf("Mapped command: %s\n", lastCommand.c_str());
  Serial.printf("Speed: %d\n", lastSpeed);
  Serial.println("Motor action: not connected yet, request logged only.");

  server.send(200, "text/plain", String("OK:") + commandCode);
}

void handleNotFound() {
  logRequest("NOT_FOUND");
  Serial.println("Request rejected: route not found.");
  server.send(404, "application/json", "{\"status\":\"error\",\"error\":\"Route not found.\"}");
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/cmd", HTTP_GET, handleCommand);
  server.onNotFound(handleNotFound);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  server.collectHeaders(HEADERS_TO_COLLECT, HEADERS_TO_COLLECT_COUNT);

  connectToPhoneAp();
  setupRoutes();
  server.begin();

  printDivider("SERVER READY");
  Serial.println("Listening on port 80");
  Serial.println("Expected command format: /cmd?c=f&s=150");
}

void loop() {
  ensureWiFiConnection();
  server.handleClient();
}
