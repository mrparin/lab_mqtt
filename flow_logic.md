# Flow Logic: Function Nodes in Node-RED

เอกสารนี้อธิบายการทำงานของ Function Node ทุกตัวใน Flow ปัจจุบัน
ไฟล์อ้างอิง: flows.json

## ภาพรวมการไหลของระบบ

1. รับข้อมูล Telemetry จาก MQTT topic smartfarm/+/+/+/telemetry
2. แยกค่า temperature, humidity, soil_moisture เพื่อแสดงบน Dashboard
3. เก็บ target ล่าสุด (site/zone/board) เพื่อใช้สร้าง topic คำสั่งแบบ dynamic
4. ควบคุมรีเลย์ได้ 2 โหมด
   - Manual: ผู้ใช้กดสวิตช์ปั๊มเอง
   - Auto: ระบบตัดสินใจจากค่าความชื้นดิน
5. ส่งคำสั่งไป MQTT topic smartfarm/{site}/{zone}/{board}/cmd

---

## 1) Function Node: function 1

บทบาทหลัก:
- แยกข้อมูลจาก topic telemetry
- เก็บ target ล่าสุดลง flow context
- แตก payload ออกเป็น 3 ข้อความสำหรับ Temp/Humi/Soil

อินพุตที่คาดหวัง:
- msg.topic ตัวอย่าง smartfarm/siteA/zone1/device01/telemetry
- msg.payload เป็น object ที่มี temperature, humidity, soil_moisture

ตรรกะการทำงาน:
1. แยก msg.topic ด้วย / แล้วอ่านค่า
   - siteId = ส่วนที่ 2
   - zoneId = ส่วนที่ 3
   - boardId = ส่วนที่ 4
2. ถ้าค่าครบ จะบันทึกลง flow context
   - targetSiteId
   - targetZoneId
   - targetBoardId
3. อ่านค่าจาก msg.payload
   - temp = temperature
   - humi = humidity
   - soil = soil_moisture
4. ส่งออก 3 output ตามลำดับ
   - Output 1: topic site/zone/board/Temp, payload temp
   - Output 2: topic site/zone/board/Humi, payload humi
   - Output 3: topic site/zone/board/Soil, payload soil

ผลลัพธ์:
- Dashboard รับค่าที่แยกแล้วไปแสดง gauge/chart
- Function อื่นใช้ target ล่าสุดเพื่อสร้าง cmd topic แบบ dynamic

อธิบายโค้ดใน function 1:

```js
let topicParts = msg.topic.split("/");

let siteId  = topicParts[1];
let zoneId  = topicParts[2];
let boardId = topicParts[3];

if (siteId && zoneId && boardId) {
   flow.set("targetSiteId", siteId);
   flow.set("targetZoneId", zoneId);
   flow.set("targetBoardId", boardId);
}

let temp = msg.payload.temperature;
let humi = msg.payload.humidity;
let soil = msg.payload.soil_moisture;

return [
   {
      topic: `${siteId}/${zoneId}/${boardId}/Temp`,
      payload: temp
   },
   {
      topic: `${siteId}/${zoneId}/${boardId}/Humi`,
      payload: humi
   },
   {
      topic: `${siteId}/${zoneId}/${boardId}/Soil`,
      payload: soil
   }
];
```

คำอธิบายทีละส่วน:
1. `msg.topic.split("/")` แยก topic เป็นชิ้นๆ เพื่อดึง site/zone/board ออกมา
2. `topicParts[1..3]` คือข้อมูลระบุตำแหน่งอุปกรณ์ที่ส่ง telemetry มา
3. `flow.set(...)` เก็บค่าพวกนี้ไว้ใน context เพื่อให้ function ควบคุมปั๊มใช้ต่อได้
4. อ่าน `temperature`, `humidity`, `soil_moisture` จาก `msg.payload`
5. `return [...]` เป็นการส่งออก 3 ขา ตาม output 1, 2, 3 ของ Function Node
6. การตั้ง `topic` ในแต่ละ output ช่วยให้ debug/widget แยกแหล่งข้อมูลได้ง่าย

---

## 2) Function Node: function 2

บทบาทหลัก:
- เป็นโหนดสำรอง (backup) สำหรับแตกข้อมูล Temp/Humi/Soil

สถานะการใช้งานปัจจุบัน:
- ไม่มีสายเชื่อมต่อใช้งานจริง (wires ว่าง)

ตรรกะการทำงาน:
1. อ่านค่า Temp/Humi/Soil จาก msg.payload
2. ส่งออก 3 output เป็นข้อความแยกแต่ละค่า

หมายเหตุ:
- โหนดนี้ไม่ได้กระทบการทำงานหลักของระบบในตอนนี้

อธิบายโค้ดใน function 2:

```js
let msgTemp  = { payload: msg.payload.Temp };
let msgHumid = { payload: msg.payload.Humi };
let msgSoil  = { payload: msg.payload.Soil };

return [msgTemp, msgHumid, msgSoil];
```

คำอธิบายทีละส่วน:
1. สร้าง message object ใหม่ 3 ตัว เพื่อแยกข้อมูลให้เป็นคนละ output
2. แต่ละตัวมีเฉพาะ `payload` ที่ต้องการส่งต่อ
3. `return [A, B, C]` หมายถึง output1=A, output2=B, output3=C
4. ตอนนี้ function นี้เป็นโหนดสำรอง ยังไม่มีสายเชื่อมใช้งาน

---

## 3) Function Node: build mode cmd

บทบาทหลัก:
- รับค่าจากสวิตช์ Auto Mode บน Dashboard
- แปลงเป็นคำสั่งโหมดสำหรับ ESP32

อินพุตที่คาดหวัง:
- msg.payload เป็น boolean จาก ui_switch
  - true = Auto
  - false = Manual

ตรรกะการทำงาน:
1. อ่าน target ล่าสุดจาก flow context
   - targetSiteId, targetZoneId, targetBoardId
   - ถ้าไม่มี ให้ใช้ค่า default siteA/zone1/device01
2. แปลง boolean เป็นข้อความโหมด
   - true -> auto
   - false -> manual
3. บันทึกโหมดลง flow context
   - controlMode = auto หรือ manual
4. สร้าง topic คำสั่งแบบ dynamic
   - smartfarm/{site}/{zone}/{board}/cmd
5. สร้าง payload คำสั่งโหมด
   - { mode: "auto" } หรือ { mode: "manual" }

ผลลัพธ์:
- ส่งคำสั่งเปลี่ยนโหมดไป ESP32 ผ่าน mqtt out

อธิบายโค้ดใน build mode cmd:

```js
const siteId = flow.get("targetSiteId") || "siteA";
const zoneId = flow.get("targetZoneId") || "zone1";
const boardId = flow.get("targetBoardId") || "device01";

const mode = msg.payload ? "auto" : "manual";
flow.set("controlMode", mode);

msg.topic = `smartfarm/${siteId}/${zoneId}/${boardId}/cmd`;
msg.payload = { mode: mode };
return msg;
```

คำอธิบายทีละส่วน:
1. `flow.get(...)` อ่าน target ล่าสุดที่เก็บไว้จาก telemetry
2. `|| "siteA" ...` คือ fallback กรณียังไม่เคยรับ telemetry
3. `msg.payload ? "auto" : "manual"` แปลงค่าบูลีนจากสวิตช์เป็นข้อความโหมด
4. `flow.set("controlMode", mode)` เก็บโหมดปัจจุบันให้ function อื่นใช้ตัดสินใจ
5. ตั้ง `msg.topic` เป็น topic คำสั่งของอุปกรณ์เป้าหมาย
6. ตั้ง `msg.payload` เป็น JSON ที่ ESP32 อ่านค่า `mode`
7. `return msg` ส่งต่อไป mqtt out เพื่อ publish จริง

---

## 4) Function Node: build pump cmd

บทบาทหลัก:
- รับค่าจากสวิตช์ Pump Relay เพื่อสั่งเปิด/ปิดปั๊มในโหมด Manual เท่านั้น

อินพุตที่คาดหวัง:
- msg.payload เป็น boolean จาก ui_switch
  - true = เปิดปั๊ม
  - false = ปิดปั๊ม

ตรรกะการทำงาน:
1. อ่าน controlMode จาก flow context
2. ถ้า mode ไม่ใช่ manual ให้ return null ทันที
   - ป้องกันการสั่งชนกับโหมด Auto
3. อ่าน target ล่าสุดจาก flow context
   - ถ้าไม่มี ใช้ default siteA/zone1/device01
4. สร้าง topic คำสั่งแบบ dynamic
   - smartfarm/{site}/{zone}/{board}/cmd
5. สร้าง payload คำสั่งแบบชัดเจน
   - { mode: "manual", pump: 1 } เมื่อเปิด
   - { mode: "manual", pump: 0 } เมื่อปิด

ผลลัพธ์:
- คำสั่งเปิด/ปิดรีเลย์ถูกส่งเฉพาะตอนอยู่ Manual

อธิบายโค้ดใน build pump cmd:

```js
const mode = flow.get("controlMode") || "manual";
if (mode !== "manual") {
   return null;
}

const siteId = flow.get("targetSiteId") || "siteA";
const zoneId = flow.get("targetZoneId") || "zone1";
const boardId = flow.get("targetBoardId") || "device01";

msg.topic = `smartfarm/${siteId}/${zoneId}/${boardId}/cmd`;
msg.payload = { mode: "manual", pump: msg.payload ? 1 : 0 };
return msg;
```

คำอธิบายทีละส่วน:
1. อ่านโหมดจาก `controlMode` ก่อนทุกครั้ง
2. ถ้าไม่ใช่ `manual` ให้ `return null` เพื่อไม่ส่งคำสั่งชนกับ Auto
3. อ่าน target ล่าสุดและ fallback เหมือน function อื่น
4. สร้าง topic cmd แบบ dynamic
5. แปลงค่าจากสวิตช์เป็น `pump: 1` หรือ `pump: 0`
6. ใส่ `mode: "manual"` ไปด้วย เพื่อให้ปลายทางตีความชัดเจน

---

## 5) Function Node: auto pump by soil

บทบาทหลัก:
- ตัดสินใจเปิด/ปิดปั๊มอัตโนมัติจากค่าความชื้นดิน
- ใช้ hysteresis เพื่อลดการสลับรีเลย์ถี่

อินพุตที่คาดหวัง:
- msg.payload เป็นค่าความชื้นดิน (soil) ที่มาจาก output Soil ของ function 1

ตรรกะการทำงาน:
1. อ่าน controlMode จาก flow context
2. ถ้า mode ไม่ใช่ auto ให้ return null
3. แปลง msg.payload เป็นตัวเลข
   - ถ้าไม่ใช่ตัวเลข ให้ return null
4. ใช้ threshold
   - onThreshold = 30
   - offThreshold = 45
5. อ่านสถานะปั๊มล่าสุดจาก flow context
   - autoPumpState (ถ้าไม่เคยมี ให้เริ่มที่ 0)
6. ตัดสินใจสถานะใหม่
   - soil <= 30 -> nextPump = 1
   - soil >= 45 -> nextPump = 0
   - ช่วง 31-44 -> คงสถานะเดิม
7. ถ้าสถานะไม่เปลี่ยน ให้ return null
8. ถ้าสถานะเปลี่ยน
   - บันทึก autoPumpState ใหม่
   - สร้าง topic cmd แบบ dynamic
   - สร้าง payload { mode: "auto", pump: nextPump }

ผลลัพธ์:
- ระบบส่งคำสั่งอัตโนมัติเมื่อความชื้นถึงเงื่อนไข และหลีกเลี่ยงการสั่งซ้ำไม่จำเป็น

อธิบายโค้ดใน auto pump by soil:

```js
const mode = flow.get("controlMode") || "manual";
if (mode !== "auto") {
   return null;
}

const soil = Number(msg.payload);
if (Number.isNaN(soil)) {
   return null;
}

const onThreshold = 30;
const offThreshold = 45;
let currentPump = flow.get("autoPumpState");
if (currentPump !== 0 && currentPump !== 1) {
   currentPump = 0;
}

let nextPump = currentPump;
if (soil <= onThreshold) {
   nextPump = 1;
} else if (soil >= offThreshold) {
   nextPump = 0;
}

if (nextPump === currentPump) {
   return null;
}

flow.set("autoPumpState", nextPump);

const siteId = flow.get("targetSiteId") || "siteA";
const zoneId = flow.get("targetZoneId") || "zone1";
const boardId = flow.get("targetBoardId") || "device01";

msg.topic = `smartfarm/${siteId}/${zoneId}/${boardId}/cmd`;
msg.payload = { mode: "auto", pump: nextPump };
return msg;
```

คำอธิบายทีละส่วน:
1. ทำงานเฉพาะตอน `controlMode` เป็น `auto`
2. แปลงค่าความชื้นเป็นตัวเลขก่อน ถ้าไม่ใช่ตัวเลขจะหยุดทันที
3. กำหนด threshold เปิด/ปิดแยกกัน (`30` และ `45`) เพื่อสร้าง hysteresis
4. อ่านสถานะปั๊มเดิมจาก `autoPumpState` เพื่อเทียบก่อนสั่งงาน
5. ช่วงค่ากลาง (31-44) จะไม่เปลี่ยนสถานะ เพื่อลดการสลับรีเลย์ถี่
6. ถ้าสถานะใหม่เท่าเดิม ให้ `return null` ไม่ publish ซ้ำ
7. ถ้าสถานะเปลี่ยน ค่อยบันทึก state ใหม่และสร้างคำสั่งส่งไป ESP32
8. payload ที่ส่งเป็น `{ mode: "auto", pump: 0/1 }`

---

## Context Keys ที่ใช้งานใน Flow

- targetSiteId: site ล่าสุดจาก telemetry
- targetZoneId: zone ล่าสุดจาก telemetry
- targetBoardId: board ล่าสุดจาก telemetry
- controlMode: โหมดควบคุมปัจจุบัน (auto/manual)
- autoPumpState: สถานะปั๊มล่าสุดในโหมด auto (0/1)

---

## สรุปการทำงานเชิงพฤติกรรม

- เมื่อเปิด Auto Mode
  - build mode cmd ส่ง { mode: "auto" }
  - auto pump by soil จะเริ่มพิจารณาความชื้นดินเพื่อสั่งปั๊ม
  - build pump cmd จะไม่ส่งคำสั่ง (ถูกบล็อก)

- เมื่อปิด Auto Mode (Manual)
  - build mode cmd ส่ง { mode: "manual" }
  - auto pump by soil หยุดสั่งงาน
  - ผู้ใช้ควบคุมปั๊มเองผ่าน Pump Relay switch
