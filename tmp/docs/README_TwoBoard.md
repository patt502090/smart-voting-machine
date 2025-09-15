# SmartVoting — Two-Board Best-Practice Package
Updated: 2025-09-11T04:49:51 UTC

## สถาปัตยกรรม
- **Arduino (MEGA แนะนำ)**: อ่าน RFID, ลายนิ้วมือ, ปุ่ม 8 ปุ่ม, LCD, รีด/IR/โหลดเซลล์, กลไก ฯลฯ
- **ESP32**: รับข้อมูลแบบ **TX-only** จาก Arduino ผ่าน Serial2 → แสดงผล **Dashboard Wi‑Fi** และ JSON API

## โปรโตคอล (แข็งแรง/ปลอดภัย)
- Frame: `[0x02][VER][TYPE][LEN][PAYLOAD][CRC16][0x03]`, VER=0x01, CRC16-CCITT(0x1021) คำนวนตั้งแต่ VER..PAYLOAD
- TYPE: HEARTBEAT(0x10), TALLY_SNAP(0x11), VOTE_CAST(0x12), TAMPER_EVT(0x13), BOX_DROP(0x14), ADMIN_LOG(0x1F)
- **TX-only**: ESP32 ไม่มีคำสั่งย้อนกลับ (แนะนำใส่ optocoupler)

## ไฟล์
```
SmartVoting_TwoBoard/
├─ arduino/
│  ├─ SmartVoting_Arduino.ino
│  └─ Protocol.h
├─ esp32/
│  ├─ SmartVoting_ESP32.ino
│  └─ Protocol.h
└─ docs/
   └─ wiring_notes.md (เขียนเพิ่มได้)
```

## ต่อสาย (MEGA2560)
- RC522: SS=53, RST=49, SCK=52, MOSI=51, MISO=50, VCC=3.3V
- FP(AS608): `Serial2` (RX=19, TX=18) — (ในโค้ดใช้ `Serial2.begin(57600)`)
- Uplink: **Serial1 TX=18 → ESP32 RX2=GPIO16** (ผ่าน optocoupler ได้ยิ่งดี)  *หมายเหตุ: MEGA TX1 คือ D18*
- LCD I²C: 0x27 SDA/SCL
- Buttons: D22..D29 (INPUT_PULLUP)
- Reed: D30, IR: D31/D32, Buzzer: D8

## ใช้งาน
1) เปิด `arduino/SmartVoting_Arduino.ino` ตั้ง `MULTI_MODE`, `MAX_SELECT`, และฐานข้อมูล `voters[]`
2) อัปโหลดเข้า MEGA; ต่อสาย TX1(D18) → ESP32 RX2(GPIO16)
3) เปิด `esp32/SmartVoting_ESP32.ino` ตั้ง Wi‑Fi SSID/PASS; อัปโหลดเข้า ESP32
4) เปิดเบราว์เซอร์ไป `http://IP_ESP32/` ดู Dashboard

## ขยายผล
- เติม HX711/โหลดเซลล์, WS2812, DS3231, กลไก STAMP/เฟอร์ริสวีล แล้วส่ง **BOX_DROP / TAMPER_EVT** เข้ามาตาม type
- ฝั่ง ESP32 เพิ่ม WebSocket/SSE ได้ง่าย เพื่อเรียลไทม์เต็มรูปแบบ

## หมายเหตุ
- โค้ดจัดวางแนว **FSM + โปรโตคอลแยกไฟล์ + เดบาวซ์ + RingBuffer ล็อก** เพื่อ Best-Practice
- หากใช้ UNO: ปรับพิน RC522, ใช้ SoftwareSerial สำหรับ FP, uplink ใช้ `Serial` เดียว
