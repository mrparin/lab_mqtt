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
const char* mqtt_server = "192.168.1.5";//เปลี่ยนเองตามจริง
const int mqtt_port    = 1883;
const char* mqtt_user  = "YOUR_MQTT_USERNAME"; //ถ้ามี
const char* mqtt_pass  = "YOUR_MQTT_PASSWORD"; 

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
// 3. การกำหนดขา GPIO
// ==========================================
#define DHTPIN        D4
#define DHTTYPE       DHT22
#define SOIL_PIN      A0
#define CONFIG_BUTTON D3
#define RELAY_PIN     D1   // รีเลย์ปั๊มน้ำ

const int AIR_VALUE = 0;   // ค่าเมื่อแห้งสนิท (ปรับแก้ได้ตามจริง)
const int WATER_VALUE = 1023; // ค่าเมื่อแช่น้ำ (ปรับแก้ได้ตามจริง)

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
float currentHum = 0.0;
float currentSoil = 0.0;

// ==========================================
// 5. ฟังก์ชันช่วยแสดงผล OLED
// ==========================================
void showOLEDMessage(const char* l1, const char* l2 = "", const char* l3 = "", const char* l4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);  display.print(l1);
  display.setCursor(0, 16); display.print(l2);
  display.setCursor(0, 32); display.print(l3);
  display.setCursor(0, 48); display.print(l4);

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
  snprintf(line1, sizeof(line1), "T=%5.1f H=%3d", currentTemp, (int)currentHum);

  if (WiFi.status() == WL_CONNECTED) {
    snprintf(line2, sizeof(line2), "Soil:%3d RSSI:%4ld", (int)currentSoil, WiFi.RSSI());
  } else {
    snprintf(line2, sizeof(line2), "Soil:%3d RSSI:----", (int)currentSoil);
  }

  snprintf(line3, sizeof(line3), "Pump:%s Device:RUN", (pumpStatus == 1) ? "ON" : "OFF");
  showOLEDMessage(line0, line1, line2, line3);
}

// ==========================================
// 7. ฟังก์ชันตรวจสอบการกดปุ่ม WiFi Config
// ==========================================
void checkConfigButton() {
  if (digitalRead(CONFIG_BUTTON) == LOW) {
    delay(50); // Debounce
    if (digitalRead(CONFIG_BUTTON) == LOW) {
      Serial.println("!!! WiFiManager Config Portal !!!");

      showOLEDMessage("WIFI SETUP MODE", "AP: SmartFarmCfg", "Open from phone", "");
      
      while(digitalRead(CONFIG_BUTTON) == LOW); // รอจนกว่าจะปล่อยปุ่ม

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
// 8. ฟังก์ชัน Callback รับคำสั่งควบคุม (CMD)
// ==========================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (!error) {
    if (doc.containsKey("pump")) {
      pumpStatus = doc["pump"];
      if (pumpStatus == 1) {
        digitalWrite(RELAY_PIN, HIGH);
        client.publish(TOPIC_ACK, "{\"pump\":1,\"status\":\"success\"}");
      } else if (pumpStatus == 0) {
        digitalWrite(RELAY_PIN, LOW);
        client.publish(TOPIC_ACK, "{\"pump\":0,\"status\":\"success\"}");
      }
      updateLCD(); // อัปเดตสถานะปั๊มทันทีเมื่อรับคำสั่ง
    }
  }
}

// ==========================================
// 9. ฟังก์ชันเชื่อมต่อ WiFi และ MQTT Broker
// ==========================================
void setup_wifi() {
  delay(10);
  showOLEDMessage("WiFi AutoConnect", "Connecting...", "", "");

  bool connected = wm.autoConnect("SmartFarmCfg");
  
  if (connected && WiFi.status() == WL_CONNECTED) {
    char ipLine[32];
    snprintf(ipLine, sizeof(ipLine), "IP: %s", WiFi.localIP().toString().c_str());
    showOLEDMessage("WiFi Connected!", ipLine, "", "");
    delay(2000);
  } else {
    showOLEDMessage("WiFi Setup Failed", "Restart device...", "", "");
    delay(2000);
    ESP.restart();
  }

  updateLCD();
}

void reconnect() {
  while (!client.connected()) {
    checkConfigButton(); 
    
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-" + String(random(0, 0xffff), HEX);
    const char* willTopic = TOPIC_STATUS;
    int willQoS = 1;
    bool willRetain = true;
    const char* willMessage = "{\"online\":false,\"msg\":\"unexpected_disconnection\"}";

    // แจ้งเตือนหน้าจอว่ากำลังเชื่อมต่อ MQTT
    updateLCD("TRY");

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass, willTopic, willQoS, willRetain, willMessage)) {
      Serial.println("connected");
      long rssi = WiFi.RSSI();
      String birthPayload = "{\"online\":true,\"rssi\":" + String(rssi) + ",\"msg\":\"hardware_ready\"}";
      client.publish(TOPIC_STATUS, birthPayload.c_str(), true);
      client.subscribe(TOPIC_CMD);
      
      // อัปเดตข้อมูลขึ้นจอทันที
      updateLCD();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());

      updateLCD("ERR");
      delay(5000);
    }
  }
}

bool initOLED() {
  // ESP8266 (NodeMCU) default I2C pins: SDA=D2, SCL=D1
  Wire.begin(D2, D1);

  Serial.println("Scanning I2C...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      if (addr < 16) Serial.print("0");
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
// 10. ส่วนเริ่มต้นโปรแกรมและ Loop การทำงาน
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 
  pinMode(CONFIG_BUTTON, INPUT_PULLUP); 
  
  if (!initOLED()) {
    Serial.println("SH110X init failed");
    while (true) { delay(1000); }
  }
  display.clearDisplay();
  display.display();
  
  dht.begin();
  setup_wifi(); 
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  checkConfigButton();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 15000) {
    lastMsg = now;

    // อ่านค่าฮาร์ดแวร์จริง
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    int raw_soil = analogRead(SOIL_PIN);
    float soil_moisture = map(raw_soil, AIR_VALUE, WATER_VALUE, 0, 100);
    soil_moisture = constrain(soil_moisture, 0, 100);

    // ตรวจสอบสัญญาณเซ็นเซอร์สภาพอากาศ
    if (isnan(t) || isnan(h)) {
      Serial.println("Error: DHT22 Fail");
      showOLEDMessage("Sensor Error", "DHT22 ERR!!", "", "");
      return;
    }

    // อัปเดตค่าลงตัวแปร Global
    currentTemp = t;
    currentHum = h;
    currentSoil = soil_moisture;

    // พิมพ์เฉพาะตัวเล็กลงจอภาพ (นิ่งสนิท ไร้รอยทับ)
    updateLCD();

    // ------------------------------------------
    // ส่งข้อมูล Telemetry (JSON Format)
    // ------------------------------------------
    StaticJsonDocument<200> telDoc;
    telDoc["temperature"] = serialized(String(t, 2));
    telDoc["humidity"]    = serialized(String(h, 2));
    telDoc["soil_moisture"] = serialized(String(soil_moisture, 1));

    char telBuffer[200];
    serializeJson(telDoc, telBuffer);
    client.publish(TOPIC_TELEMETRY, telBuffer);

    // ------------------------------------------
    // ส่งข้อมูล Status
    // ------------------------------------------
    long rssi = WiFi.RSSI();
    StaticJsonDocument<150> statDoc;
    statDoc["online"] = true;
    statDoc["rssi"]   = rssi;
    statDoc["battery_v"] = 4.15;

    char statBuffer[150];
    serializeJson(statDoc, statBuffer);
    client.publish(TOPIC_STATUS, statBuffer, true);
  }
}
