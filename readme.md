# คู่มือโปรแกรม SmartFarm ESP32 (WiFiManager + MQTT + OLED + DHT22)

เอกสารนี้อธิบายภาพรวมการทำงานของโปรแกรม และการทำงานของแต่ละฟังก์ชันในไฟล์ src/main.cpp อย่างละเอียด

## 1) ภาพรวมระบบ

โปรแกรมนี้ทำงานบนบอร์ด ESP32 โดยมีหน้าที่หลักดังนี้

1. เชื่อมต่อ WiFi ผ่าน WiFiManager
2. เปิดหน้า Config Portal ให้ตั้งค่าผ่านมือถือได้
3. รองรับการตั้งค่าแบบกำหนดเอง (Custom Parameter) จากมือถือ
   - mqtt_server
   - SITE_ID
   - ZONE_ID
   - BOARD_ID
4. บันทึกค่าตั้งทั้งหมดลงหน่วยความจำถาวรด้วย Preferences
5. สร้าง MQTT Topic แบบ Dynamic จาก SITE_ID / ZONE_ID / BOARD_ID
6. เชื่อมต่อ MQTT Broker แล้วรับ-ส่งข้อมูล
7. อ่านค่าจากเซนเซอร์ DHT22 และความชื้นดิน
8. แสดงผลสถานะและข้อมูลบนจอ OLED 1.3 นิ้ว (SH1106)
9. รับคำสั่งควบคุมปั๊มน้ำผ่าน MQTT และส่ง ACK กลับ


## 2) อุปกรณ์และไลบรารีที่ใช้

### ฮาร์ดแวร์หลัก

- ESP32
- DHT22 (อุณหภูมิ/ความชื้นอากาศ)
- Soil Moisture Sensor (อนาล็อก)
- Relay Module สำหรับปั๊มน้ำ
- ปุ่มกดสำหรับเข้าโหมดตั้งค่า WiFi
- OLED SH1106 (I2C)

### ไลบรารีหลัก

- WiFi.h
- WiFiManager.h
- PubSubClient.h
- DHT.h
- ArduinoJson.h
- Preferences.h
- Wire.h
- Adafruit_GFX.h
- Adafruit_SH110X.h


## 3) โครงสร้างการตั้งค่าในโปรแกรม

### 3.1 ค่าคงที่ระบบ

- mqtt_port ใช้พอร์ต 1883
- mqtt_user / mqtt_pass คือบัญชีสำหรับ MQTT Broker

### 3.2 ค่าที่ตั้งได้จากมือถือ

ตัวแปรเหล่านี้เป็น String และสามารถแก้ได้จากหน้า Config Portal

- mqttServer
- siteId
- zoneId
- boardId

### 3.3 Topic ที่สร้างแบบ Dynamic

โปรแกรมจะประกอบหัวข้อ MQTT ใหม่ทุกครั้งเมื่อค่าพารามิเตอร์เปลี่ยน

- topicTelemetry = smartfarm/siteId/zoneId/boardId/telemetry
- topicStatus = smartfarm/siteId/zoneId/boardId/status
- topicCmd = smartfarm/siteId/zoneId/boardId/cmd
- topicAck = smartfarm/siteId/zoneId/boardId/ack


## 4) ลำดับการทำงานหลักของระบบ

1. เริ่มต้น Serial, โหลดค่าคอนฟิกเดิมจาก Preferences
2. กำหนดโหมดขา GPIO ของรีเลย์และปุ่ม
3. เริ่มต้นจอ OLED (พร้อมสแกน I2C และลองที่อยู่ 0x3C/0x3D)
4. เริ่มต้น DHT22
5. เริ่มต้น WiFiManager + AutoConnect
6. ตั้งค่า MQTT server จากค่าที่ผู้ใช้กำหนด
7. วนลูปหลัก
   - ตรวจปุ่มเข้าหน้า WiFi Config
   - ตรวจและเชื่อม MQTT ใหม่ถ้าหลุด
   - อ่านเซนเซอร์ทุก 15 วินาที
   - อัปเดตจอ OLED
   - ส่ง Telemetry และ Status ไป MQTT


## 5) อธิบายตัวแปรสำคัญ

- lastMsg: เวลาอ้างอิงสำหรับส่งข้อมูลทุก 15 วินาที
- pumpStatus: สถานะรีเลย์ปั๊ม (0 = ปิด, 1 = เปิด)
- currentTemp / currentHum / currentSoil: ค่าปัจจุบันที่ใช้แสดงผลและส่งขึ้น MQTT
- shouldSaveConfig: ธงจาก WiFiManager เพื่อบอกว่าต้องบันทึกคอนฟิกใหม่
- mqttServerBuf / siteIdBuf / zoneIdBuf / boardIdBuf: บัฟเฟอร์สำหรับเชื่อมกับ WiFiManagerParameter


## 6) คำอธิบายแต่ละฟังก์ชัน (ละเอียด)

### 6.1 rebuildTopics()

หน้าที่
- สร้าง Topic ทั้ง 4 แบบ Dynamic จาก siteId, zoneId, boardId

ทำเมื่อใด
- หลังโหลดค่าจาก Preferences
- หลังผู้ใช้บันทึกค่าจากหน้า Config Portal

ผลลัพธ์
- โปรแกรมจะ publish/subscribe ด้วย Topic ที่สอดคล้องกับค่าที่ผู้ใช้ตั้งล่าสุด


### 6.2 saveConfigCallback()

หน้าที่
- ถูกเรียกโดย WiFiManager เมื่อมีการกด Save ในหน้า Config
- ตั้งค่า shouldSaveConfig = true

เหตุผลที่แยกธง
- เพื่อควบคุมจุดบันทึกจริงให้อยู่ในโฟลว์ applyPortalValues


### 6.3 loadConfig()

หน้าที่
- อ่านค่าที่เคยบันทึกไว้จาก Preferences namespace ชื่อ farmcfg
- โหลดค่า mqttServer, siteId, zoneId, boardId
- คัดลอกค่าไปยังบัฟเฟอร์สำหรับ WiFiManagerParameter
- เรียก rebuildTopics()

ผลที่ได้
- ระบบคืนค่าคอนฟิกครั้งล่าสุดอัตโนมัติหลังรีบูต


### 6.4 saveConfig()

หน้าที่
- บันทึกค่าปัจจุบันของ mqttServer, siteId, zoneId, boardId ลง Preferences

ทำเมื่อใด
- เมื่อ WiFiManager แจ้งว่ามีการ Save และผ่านการ apply ค่าแล้ว


### 6.5 applyPortalValues()

หน้าที่
- ดึงค่าจากฟิลด์ Custom Parameter ใน WiFiManager
- trim ช่องว่างหน้าหลัง
- ถ้าค่าไม่ว่าง ให้นำมาแทนค่าปัจจุบัน
- เรียก rebuildTopics()
- อัปเดต client.setServer(mqttServer, mqtt_port)
- ถ้า shouldSaveConfig เป็นจริง ให้เรียก saveConfig()

จุดเด่น
- ทำให้ทั้ง MQTT server และ topic เปลี่ยนได้ทันทีจากการตั้งค่าผ่านมือถือ


### 6.6 showOLEDMessage(l1, l2, l3, l4)

หน้าที่
- วาดข้อความ 4 บรรทัดลง OLED
- เคลียร์จอ กำหนด text size/color และแสดงผลทันที

เหมาะกับ
- ข้อความสถานะระหว่างขั้นตอนสำคัญ เช่น กำลังคอนฟิก WiFi, เชื่อมต่อสำเร็จ, error sensor


### 6.7 updateLCD(mqttStateOverride)

หมายเหตุ
- ชื่อฟังก์ชันยังชื่อ updateLCD เดิมเพื่อคงโครงสร้าง แต่ปัจจุบันอัปเดตจอ OLED

หน้าที่
- สร้างข้อความ 4 บรรทัดจากสถานะจริง
  1) WiFi และ MQTT status
  2) T และ H
  3) Soil และ RSSI
  4) Pump status
- ถ้ามีพารามิเตอร์ mqttStateOverride จะใช้ค่า override (เช่น TRY, ERR)


### 6.8 checkConfigButton()

หน้าที่
- ตรวจการกดปุ่ม CONFIG_BUTTON
- ทำ debounce เบื้องต้น
- เมื่อกดค้างและปล่อย จะเปิด WiFiManager Config Portal
- หลังผู้ใช้บันทึกค่า (ok = true) จะเรียก applyPortalValues()
- อัปเดตหน้าจอหลังจบการตั้งค่า

ประโยชน์
- เปลี่ยน WiFi และค่าระบบสำคัญผ่านมือถือได้ โดยไม่ต้องแฟลชเฟิร์มแวร์ใหม่


### 6.9 callback(topic, payload, length)

หน้าที่
- เป็น callback ของ MQTT เมื่อมีข้อความเข้ามา
- แปลง payload เป็น String
- แยก JSON และอ่านคีย์ pump
- ถ้า pump = 1: เปิดรีเลย์ + ส่ง ACK success
- ถ้า pump = 0: ปิดรีเลย์ + ส่ง ACK success
- อัปเดตหน้าจอทันที

หมายเหตุ
- ACK จะส่งที่ topicAck ซึ่งเป็น topic แบบ Dynamic


### 6.10 setup_wifi()

หน้าที่
- เตรียมค่าในบัฟเฟอร์ให้ตรงกับค่าปัจจุบันก่อนเปิด portal
- ตั้ง callback และเพิ่ม custom parameter ทั้ง 4 ตัวลง WiFiManager
- เรียก wm.autoConnect("SmartFarmCfg")
- ถ้าเชื่อมต่อสำเร็จ จะ applyPortalValues() และแสดง IP
- ถ้าไม่สำเร็จ จะรีสตาร์ทบอร์ด
- อัปเดตหน้าจอสถานะหลักเมื่อจบขั้นตอน


### 6.11 reconnect()

หน้าที่
- วนเชื่อมต่อ MQTT จนสำเร็จ
- ระหว่างรอจะยังตรวจปุ่มคอนฟิกได้
- ตั้ง Last Will ที่ topicStatus
- ถ้าสำเร็จ
  - publish birth message online=true
  - subscribe topicCmd
  - อัปเดตหน้าจอ
- ถ้าไม่สำเร็จ
  - แสดง ERR
  - หน่วง 5 วินาทีแล้วลองใหม่


### 6.12 initOLED()

หน้าที่
- เริ่มต้น I2C ด้วย Wire.begin()
- สแกน I2C และพิมพ์ address ที่พบลง Serial
- ลอง init จอที่ 0x3C ก่อน
- ถ้าไม่สำเร็จลอง 0x3D

ผลลัพธ์
- return true เมื่อเริ่มจอสำเร็จ
- return false เมื่อไม่สำเร็จทั้งสอง address


### 6.13 setup()

หน้าที่
- เริ่ม Serial
- โหลด config เดิม (loadConfig)
- ตั้ง GPIO ของรีเลย์/ปุ่ม
- เริ่ม OLED (initOLED)
- เริ่ม DHT
- เริ่ม WiFi (setup_wifi)
- ตั้ง MQTT server จาก mqttServer ปัจจุบัน
- ผูก callback สำหรับข้อความ MQTT


### 6.14 loop()

หน้าที่
- ตรวจปุ่มคอนฟิก
- ตรวจสถานะ MQTT ถ้าหลุดให้ reconnect
- ทุก 15 วินาที
  - อ่าน DHT22
  - อ่านค่า soil moisture
  - validate ค่า DHT
  - อัปเดตค่าปัจจุบันและจอ OLED
  - ส่ง telemetry (temperature, humidity, soil_moisture)
  - ส่ง status (online, rssi, battery_v)


## 7) โครงสร้าง MQTT ของระบบ

### 7.1 Telemetry
- topic: topicTelemetry
- payload: temperature, humidity, soil_moisture

### 7.2 Status
- topic: topicStatus
- payload: online, rssi, battery_v

### 7.3 Command
- topic: topicCmd
- payload ตัวอย่าง: {"pump":1} หรือ {"pump":0}

### 7.4 ACK
- topic: topicAck
- payload ตัวอย่าง: {"pump":1,"status":"success"}


## 8) วิธีตั้งค่าผ่านมือถือ

1. กดปุ่ม CONFIG_BUTTON ที่บอร์ด
2. มือถือเชื่อมต่อ AP ชื่อ SmartFarmCfg
3. เปิดหน้า Config Portal ของ WiFiManager
4. กรอก
   - MQTT Server
   - SITE_ID
   - ZONE_ID
   - BOARD_ID
5. กด Save
6. บอร์ดจะ apply ค่าทันที และบันทึกลง Preferences


## 9) การแก้ปัญหาเบื้องต้น

### ปัญหา: SH110X init failed

ตรวจสอบ
1. สายไฟและ GND
2. ขา I2C (ESP32 ปกติ SDA=21, SCL=22)
3. ดู Serial ว่าพบ I2C ที่ address ใด
4. โมดูลบางตัวใช้ 0x3C หรือ 0x3D

### ปัญหา: ไม่เชื่อม MQTT

ตรวจสอบ
1. MQTT Server ในหน้า config ว่าถูกต้อง
2. user/password ถูกต้อง
3. Broker อยู่ในเครือข่ายเดียวกัน
4. ดูค่า rc จาก client.state() ใน Serial

### ปัญหา: ไม่รับคำสั่งปั๊ม

ตรวจสอบ
1. publish ไป topicCmd ที่ตรงกับ site/zone/board ปัจจุบัน
2. payload เป็น JSON ถูกต้อง เช่น {"pump":1}


## 10) ข้อควรปรับปรุงในอนาคต

1. เปลี่ยนโค้ด ArduinoJson ให้ใช้รูปแบบใหม่เพื่อลด deprecation warning
2. เพิ่ม validation ความยาว/รูปแบบของ SITE_ID, ZONE_ID, BOARD_ID
3. เพิ่ม fallback sensor error handling ให้ละเอียดขึ้น
4. เพิ่ม factory reset สำหรับล้างค่า Preferences จากปุ่มกด


## 11) สรุป

โปรแกรมนี้เป็นโครงงาน SmartFarm ที่พร้อมใช้งานจริงในภาคสนาม โดยเน้นการตั้งค่าหน้างานผ่านมือถือ ไม่ต้องแก้โค้ดบ่อย และรองรับการจัดการหลายโซน/หลายบอร์ดผ่านโครงสร้าง topic แบบ Dynamic
