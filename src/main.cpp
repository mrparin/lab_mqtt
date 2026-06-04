#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ==========================================
// 1. การตั้งค่าระบบเครือข่ายและ Broker
// ==========================================
const char* mqtt_server = "192.168.1.5";
const int mqtt_port    = 1883;
const char* mqtt_user  = "YOUR_MQTT_USERNAME"; 
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
#define DHTPIN        33
#define DHTTYPE       DHT22
#define SOIL_PIN      32
#define CONFIG_BUTTON 4
#define RELAY_PIN     2   // รีเลย์ปั๊มน้ำ

const int AIR_VALUE = 0;   // ค่าเมื่อแห้งสนิท (ปรับแก้ได้ตามจริง)
const int WATER_VALUE = 4095; // ค่าเมื่อแช่น้ำ (ปรับแก้ได้ตามจริง)

// ==========================================
// 4. ประกาศอ็อบเจกต์เซ็นเซอร์และจอ LCD
// ==========================================
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4); 
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
// 5. ฟังก์ชันพิมพ์โครงสร้างหน้าจอคงที่ (Static Template)
// ==========================================
void printLCDTemplate() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("WiFi:--- MQTT:---   ");
  lcd.setCursor(0, 1); lcd.print("T=-----  H=---      ");
  lcd.setCursor(0, 2); lcd.print("Soil:---% RSSI:---- ");
  lcd.setCursor(0, 3); lcd.print("Pump:--- Device:RUN ");
}

// ==========================================
// 6. ฟังก์ชันอัปเดตเฉพาะตัวเลข (หน้าจอนิ่ง ไม่กระพริบ ไร้อักษรค้าง)
// ==========================================
void updateLCD() {
  char buf[8];

  // แสดงสถานะเครือข่าย
  lcd.setCursor(5, 0);
  if (WiFi.status() == WL_CONNECTED) lcd.print("OK ");
  else                               lcd.print("ERR");

  lcd.setCursor(14, 0);
  if (client.connected()) lcd.print("OK ");
  else                    lcd.print("ERR");

  // แสดงค่าเซ็นเซอร์
  lcd.setCursor(2, 1);
  dtostrf(currentTemp, 5, 1, buf);  // เช่น " 25.3"
  lcd.print(buf);

  lcd.setCursor(11, 1);
  snprintf(buf, sizeof(buf), "%3d", (int)currentHum);
  lcd.print(buf);

  lcd.setCursor(5, 2);
  snprintf(buf, sizeof(buf), "%3d", (int)currentSoil);
  lcd.print(buf);

  // แสดง RSSI และสถานะปั๊ม
  lcd.setCursor(15, 2);
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "%4ld", WiFi.RSSI());
    lcd.print(buf);
  } else {
    lcd.print("----");
  }

  lcd.setCursor(5, 3);
  if (pumpStatus == 1) lcd.print("ON ");
  else                 lcd.print("OFF");
}

// ==========================================
// 7. ฟังก์ชันตรวจสอบการกดปุ่ม WiFi Config
// ==========================================
void checkConfigButton() {
  if (digitalRead(CONFIG_BUTTON) == LOW) {
    delay(50); // Debounce
    if (digitalRead(CONFIG_BUTTON) == LOW) {
      Serial.println("!!! WiFiManager Config Portal !!!");
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("====================");
      lcd.setCursor(0, 1); lcd.print("  WIFI SETUP MODE ");
      lcd.setCursor(0, 2); lcd.print(" AP: SmartFarmCfg ");
      lcd.setCursor(0, 3); lcd.print("====================");
      
      while(digitalRead(CONFIG_BUTTON) == LOW); // รอจนกว่าจะปล่อยปุ่ม

      // เปิด AP สำหรับตั้งค่า WiFi ผ่านมือถือ
      bool ok = wm.startConfigPortal("SmartFarmCfg");
      if (ok) {
        lcd.clear();
        lcd.setCursor(0, 1); lcd.print("WiFi Saved OK");
        lcd.setCursor(0, 2); lcd.print("Reconnecting...");
        delay(1200);
      } else {
        lcd.clear();
        lcd.setCursor(0, 1); lcd.print("Setup Timeout");
        lcd.setCursor(0, 2); lcd.print("Using old WiFi");
        delay(1200);
      }
      
      // เมื่อหลุดโหมดคอนฟิก ให้วาดโครงสร้างหน้าจอกลับมาใหม่ทันที
      printLCDTemplate();
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
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("WiFi AutoConnect");

  bool connected = wm.autoConnect("SmartFarmCfg");
  
  if (connected && WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print("IP Address:");
    lcd.setCursor(0, 2); lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi Setup Failed");
    lcd.setCursor(0, 1); lcd.print("Restart device...");
    delay(2000);
    ESP.restart();
  }
  
  // วาดแม่แบบหน้าจอเตรียมไว้หลังจบขั้นตอนการเชื่อมต่อ WiFi
  printLCDTemplate();
}

void reconnect() {
  while (!client.connected()) {
    checkConfigButton(); 
    
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32RealClient-" + String(random(0, 0xffff), HEX);
    const char* willTopic = TOPIC_STATUS;
    int willQoS = 1;
    bool willRetain = true;
    const char* willMessage = "{\"online\":false,\"msg\":\"unexpected_disconnection\"}";

    // แจ้งเตือนหน้าจอว่ากำลังเชื่อมต่อในช่อง MQTT เดิม
    lcd.setCursor(14, 0); lcd.print("TRY");

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
      
      lcd.setCursor(14, 0); lcd.print("ERR");
      delay(5000);
    }
  }
}

// ==========================================
// 10. ส่วนเริ่มต้นโปรแกรมและ Loop การทำงาน
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 
  pinMode(CONFIG_BUTTON, INPUT_PULLUP); 
  
  lcd.init();
  lcd.backlight();
  
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
      lcd.setCursor(7, 1); lcd.print("DHT22 ERR!! ");
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
