#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ==========================================
// 1. การตั้งค่าระบบเครือข่ายและ Broker
// ==========================================
const char* mqtt_server = "192.168.1.5";        // เปลี่ยนเองตามจริง
const int mqtt_port     = 1883;
const char* mqtt_user   = "YOUR_MQTT_USERNAME"; // ถ้าไม่มี username ให้ใส่ "" ได้
const char* mqtt_pass   = "YOUR_MQTT_PASSWORD"; // ถ้าไม่มี password ให้ใส่ "" ได้

// ==========================================
// 2. การตั้งค่าโครงสร้าง MQTT Topic
// ==========================================
#define SITE_ID   "siteA"
#define ZONE_ID   "zone1"
#define BOARD_ID  "device01"

const char* TOPIC_TELEMETRY = "smartfarm/" SITE_ID "/" ZONE_ID "/" BOARD_ID "/telemetry";
const char* TOPIC_STATUS    = "smartfarm/" SITE_ID "/" ZONE_ID "/" BOARD_ID "/status";
const char* TOPIC_CMD       = "smartfarm/" SITE_ID "/" ZONE_ID "/" BOARD_ID "/cmd";
const char* TOPIC_ACK       = "smartfarm/" SITE_ID "/" ZONE_ID "/" BOARD_ID "/ack";

// ==========================================
// 3. การกำหนดขา GPIO ให้ถูกต้องตามจริง
// ==========================================
// DHT22 ต่อที่ D4
#define DHTPIN        D4
#define DHTTYPE       DHT22

// ปุ่มเข้าโหมด WiFi Config ต่อที่ D3
#define CONFIG_BUTTON D3

// Relay ต่อที่ D6
// ไม่ใช้ D1 เพราะ D1 เป็น SCL ของ OLED I2C
#define RELAY_PIN     D6

// ==========================================
// 4. ประกาศอ็อบเจกต์เซ็นเซอร์และจอ OLED
// ==========================================
DHT dht(DHTPIN, DHTTYPE);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiManager wm;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
int pumpStatus = 0;

// ตัวแปรเก็บค่าล่าสุดไว้แสดงผล
float currentTemp = 0.0;

// ==========================================
// 5. ฟังก์ชันช่วยแสดงผล OLED
// ==========================================
void showOLEDMessage(
  const char* l1,
  const char* l2 = "",
  const char* l3 = "",
  const char* l4 = ""
) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);
  display.print(l1);

  display.setCursor(0, 16);
  display.print(l2);

  display.setCursor(0, 32);
  display.print(l3);

  display.setCursor(0, 48);
  display.print(l4);

  display.display();
}

// ==========================================
// 6. ฟังก์ชันอัปเดตหน้าจอ OLED
// ==========================================
void updateLCD(const char* mqttStateOverride = nullptr) {
  char line0[32];
  char line1[32];
  char line2[32];
  char line3[32];

  const char* wifiText = (WiFi.status() == WL_CONNECTED) ? "OK" : "ERR";
  const char* mqttText = mqttStateOverride ? mqttStateOverride : (client.connected() ? "OK" : "ERR");

  snprintf(line0, sizeof(line0), "WiFi:%s MQTT:%s", wifiText, mqttText);
  snprintf(line1, sizeof(line1), "Temp: %5.1f C", currentTemp);

  if (WiFi.status() == WL_CONNECTED) {
    snprintf(line2, sizeof(line2), "RSSI:%4ld", WiFi.RSSI());
  } else {
    snprintf(line2, sizeof(line2), "RSSI:----");
  }

  snprintf(line3, sizeof(line3), "Pump:%s Device:RUN", (pumpStatus == 1) ? "ON" : "OFF");

  showOLEDMessage(line0, line1, line2, line3);
}

// ==========================================
// 7. ฟังก์ชันตรวจสอบการกดปุ่ม WiFi Config
// ==========================================
void checkConfigButton() {
  if (digitalRead(CONFIG_BUTTON) == LOW) {
    delay(50); // debounce

    if (digitalRead(CONFIG_BUTTON) == LOW) {
      Serial.println("!!! WiFiManager Config Portal !!!");

      showOLEDMessage(
        "WIFI SETUP MODE",
        "AP: SmartFarmCfg",
        "Open from phone",
        ""
      );

      // รอจนกว่าจะปล่อยปุ่ม
      while (digitalRead(CONFIG_BUTTON) == LOW) {
        delay(10);
      }

      // เปิด AP สำหรับตั้งค่า WiFi ผ่านมือถือ
      bool ok = wm.startConfigPortal("SmartFarmCfg");

      if (ok) {
        showOLEDMessage("WiFi Saved OK", "Reconnecting...", "", "");
        delay(1200);
      } else {
        showOLEDMessage("Setup Timeout", "Using old WiFi", "", "");
        delay(1200);
      }

      updateLCD();
    }
  }
}

// ==========================================
// 8. ฟังก์ชัน Callback รับคำสั่งควบคุม Pump ผ่าน MQTT
// ==========================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  if (doc.containsKey("pump")) {
    pumpStatus = doc["pump"];

    if (pumpStatus == 1) {
      digitalWrite(RELAY_PIN, HIGH);
      client.publish(TOPIC_ACK, "{\"pump\":1,\"status\":\"success\"}");
      Serial.println("Pump ON");
    } else if (pumpStatus == 0) {
      digitalWrite(RELAY_PIN, LOW);
      client.publish(TOPIC_ACK, "{\"pump\":0,\"status\":\"success\"}");
      Serial.println("Pump OFF");
    }

    // อัปเดตสถานะปั๊มทันทีเมื่อรับคำสั่ง
    updateLCD();
  }
}

// ==========================================
// 9. ฟังก์ชันเชื่อมต่อ WiFi
// ==========================================
void setup_wifi() {
  delay(10);

  showOLEDMessage("WiFi AutoConnect", "Connecting...", "", "");

  bool connected = wm.autoConnect("SmartFarmCfg");

  if (connected && WiFi.status() == WL_CONNECTED) {
    char ipLine[32];

    snprintf(
      ipLine,
      sizeof(ipLine),
      "IP: %s",
      WiFi.localIP().toString().c_str()
    );

    showOLEDMessage("WiFi Connected!", ipLine, "", "");
    delay(2000);
  } else {
    showOLEDMessage("WiFi Setup Failed", "Restart device...", "", "");
    delay(2000);
    ESP.restart();
  }

  updateLCD();
}

// ==========================================
// 10. ฟังก์ชันเชื่อมต่อ MQTT Broker
// ==========================================
void reconnect() {
  while (!client.connected()) {
    checkConfigButton();

    Serial.print("Attempting MQTT connection...");

    String clientId = "ESP8266Client-" + String(random(0, 0xffff), HEX);

    const char* willTopic = TOPIC_STATUS;
    int willQoS = 1;
    bool willRetain = true;
    const char* willMessage = "{\"online\":false,\"msg\":\"unexpected_disconnection\"}";

    updateLCD("TRY");

    bool mqttConnected;

    // ถ้าไม่ได้ใช้ username/password ให้แก้ mqtt_user และ mqtt_pass เป็น ""
    mqttConnected = client.connect(
      clientId.c_str(),
      mqtt_user,
      mqtt_pass,
      willTopic,
      willQoS,
      willRetain,
      willMessage
    );

    if (mqttConnected) {
      Serial.println("connected");

      long rssi = WiFi.RSSI();

      String birthPayload = "{\"online\":true,\"rssi\":" +
                            String(rssi) +
                            ",\"msg\":\"hardware_ready\"}";

      client.publish(TOPIC_STATUS, birthPayload.c_str(), true);
      client.subscribe(TOPIC_CMD);

      updateLCD();
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());

      updateLCD("ERR");
      delay(5000);
    }
  }
}

// ==========================================
// 11. ฟังก์ชันเริ่มต้น OLED
// ==========================================
bool initOLED() {
  // ESP8266 NodeMCU I2C:
  // SDA = D2
  // SCL = D1
  Wire.begin(D2, D1);

  Serial.println("Scanning I2C...");

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);

    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");

      if (addr < 16) {
        Serial.print("0");
      }

      Serial.println(addr, HEX);
    }
  }

  if (display.begin(0x3C, true)) {
    Serial.println("OLED init OK at 0x3C");
    return true;
  }

  if (display.begin(0x3D, true)) {
    Serial.println("OLED init OK at 0x3D");
    return true;
  }

  return false;
}

// ==========================================
// 12. ส่วนเริ่มต้นโปรแกรม
// ==========================================
void setup() {
  Serial.begin(115200);

  // ตั้งค่า Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // ตั้งค่าปุ่ม WiFi Config
  pinMode(CONFIG_BUTTON, INPUT_PULLUP);

  // เริ่มต้น OLED
  if (!initOLED()) {
    Serial.println("SH110X init failed");

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.display();

  // เริ่มต้น DHT22
  dht.begin();

  // เชื่อมต่อ WiFi
  setup_wifi();

  // ตั้งค่า MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// ==========================================
// 13. Loop การทำงานหลัก
// ==========================================
void loop() {
  checkConfigButton();

  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  unsigned long now = millis();

  // อ่านค่าและส่งข้อมูลทุก 15 วินาที
  if (now - lastMsg > 15000) {
    lastMsg = now;

    // อ่านค่าอุณหภูมิจาก DHT22
    float t = dht.readTemperature();

    // ตรวจสอบว่าสามารถอ่านค่า DHT22 ได้หรือไม่
    if (isnan(t)) {
      Serial.println("Error: DHT22 Temperature Fail");
      showOLEDMessage("Sensor Error", "DHT22 TEMP ERR!!", "", "");
      return;
    }

    // อัปเดตค่าลงตัวแปร Global
    currentTemp = t;

    // แสดงผลบน OLED
    updateLCD();

    // แสดงผลทาง Serial Monitor
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.println(" C");

    // ------------------------------------------
    // ส่งข้อมูล Telemetry เฉพาะอุณหภูมิ
    // Topic:
    // smartfarm/siteA/zone1/device01/telemetry
    // ------------------------------------------
    StaticJsonDocument<100> telDoc;
    telDoc["temperature"] = serialized(String(t, 2));

    char telBuffer[100];
    serializeJson(telDoc, telBuffer);

    client.publish(TOPIC_TELEMETRY, telBuffer);

    Serial.print("Publish telemetry: ");
    Serial.println(telBuffer);

    // ------------------------------------------
    // ส่งข้อมูล Status
    // Topic:
    // smartfarm/siteA/zone1/device01/status
    // ------------------------------------------
    long rssi = WiFi.RSSI();

    StaticJsonDocument<150> statDoc;
    statDoc["online"] = true;
    statDoc["rssi"] = rssi;
    statDoc["battery_v"] = 4.15; // ตอนนี้เป็นค่าคงที่ ยังไม่ได้อ่านจากวงจรจริง

    char statBuffer[150];
    serializeJson(statDoc, statBuffer);

    client.publish(TOPIC_STATUS, statBuffer, true);

    Serial.print("Publish status: ");
    Serial.println(statBuffer);
  }
}