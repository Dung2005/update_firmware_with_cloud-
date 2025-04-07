#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

const char* wifiSSID = "Wokwi-GUEST";
const char* wifiPass = "";
const char* mqttServer = "139.59.97.249";
const int mqttPort = 1883;
const char* mqttUser = "837juwphwadq48ctdue9"; // Access token TB
const char* mqttPass = "";

WiFiClient espClient;
PubSubClient client(espClient);
HTTPClient http;

// attribute: gửi thông tin cấu hình trạng thái cố định và telemetry: gửi dữ liệu cảm biến thay đổi theo thời gian
const char* attributeTopic = "v1/devices/me/attributes";
const char* requestAttr = "v1/devices/me/attributes/request/1"; // tbi -> server

String currentVersion = "1.0.0"; // Version hiện tại của firmware
bool wifiConnected = false;
bool mqttConnected = false;

bool connectToWiFi(const char *ssid, const char *password) {
  Serial.println("Đang kết nối WiFi...");
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startTime > 15000) {
      Serial.println("\nKhông thể kết nối WiFi");
      return false;
    }
  }

  Serial.println("\nWiFi đã kết nối!");
  return true;
}

bool connectToMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  Serial.print("Đang kết nối MQTT...");

  if (client.connect("ESP32Client", mqttUser, mqttPass)) {
    Serial.println("MQTT kết nối thành công!");
    mqttConnected = true;
    client.subscribe(attributeTopic); // đăng ký chủ đề nhận dữ liệu từ server
    return true;
  } else {
    Serial.print("MQTT error, rc= ");
    Serial.println(client.state());
    mqttConnected = false;
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  connectToWiFi(wifiSSID, wifiPass);
  connectToMQTT();
  if (mqttConnected) {
    requestSharedAttributes();  // Gửi yêu cầu lấy shared attributes
  }
}

void loop() {
  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();

  if (mqttConnected) {
    requestSharedAttributes();
  }
  delay(30000); // Gửi yêu cầu lấy shared attributes mỗi 30 giây
}

// Gửi yêu cầu lấy shared attributes từ ThingsBoard
void requestSharedAttributes() {
  String payload = "{\"sharedKeys\":\"targetVersion,firmwareUrl\"}";
  client.publish(requestAttr, payload.c_str());
}

// Callback xử lý khi nhận được attribute
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Nhận topic: ");
  Serial.println(topic);

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message received: ");
  Serial.println(message);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("Lỗi giải mã JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Lấy shared attribute từ ThingsBoard và lưu vào các biến
  const char* newVersion = doc["shared"]["targetVersion"];
  const char* url = doc["shared"]["firmwareUrl"];

  if (newVersion && url) {
    Serial.printf("Current: %s | New: %s\n", currentVersion.c_str(), newVersion);

    if (String(newVersion) > currentVersion) {
      Serial.println("Có firmware mới! Tiến hành cập nhật...");
      otaUpdate(url);
    } else {
      Serial.println("Đã là phiên bản mới nhất!");
    }
  } else {
    Serial.println("Không tìm thấy version hoặc URL trong message!");
  }
}

void otaUpdate(const char* firmwareUrl) {
  Serial.printf("Tải firmware từ: %s\n", firmwareUrl);
  http.begin(firmwareUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int len = http.getSize();
    if (Update.begin(len)) {
      WiFiClient* stream = http.getStreamPtr();
      size_t written = Update.writeStream(*stream);

      if (written == len && Update.end() && Update.isFinished()) {
        Serial.println("Cập nhật thành công. Khởi động lại...");
        ESP.restart();  // Khởi động lại ESP32 để chạy firmware mới
      } else {
        Serial.println("Cập nhật thất bại.");
      }
    } else {
      Serial.println("Không đủ bộ nhớ!");
    }
  } else {
    Serial.printf("Tải lỗi: %d\n", httpCode);
  }

  http.end();
}
