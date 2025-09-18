# Smart Voting Machine — Build & Submission Plan
_Generated: 2025-09-09 12:11 UTC_  
_Target submission: **2025-09-20**_

<img width="1024" height="1024" alt="68918e5a-d9e4-47bc-b84b-6905ee7cbfe0" src="https://github.com/user-attachments/assets/285cf26f-5464-4b6c-8e04-4eb9cabc802f" />
<img width="1024" height="1536" alt="image" src="https://github.com/user-attachments/assets/0c43f27e-117a-4d8f-9d1c-6e4fdb67ed25" />
<img width="1536" height="1024" alt="image" src="https://github.com/user-attachments/assets/fb87457e-8740-4691-8fa9-ac08b64edf1d" />
<img width="1536" height="1024" alt="image" src="https://github.com/user-attachments/assets/64329679-1804-41ba-8371-6eb1f14512b0" />
<img width="1536" height="1024" alt="image" src="https://github.com/user-attachments/assets/d9ebafcd-c2aa-4cac-a54d-dbf8ce5fa589" />
<img width="1024" height="1024" alt="image" src="https://github.com/user-attachments/assets/13f9167c-8568-4157-bc8f-b9128e0dd0b1" />







## 1) Goal & Success Criteria
- ส่งเดโม **ทำงานครบฟังก์ชัน**: ลงทะเบียน → โหวต → พิมพ์/ตัด (หรือตรา) → ตกกล่อง → นับ → ขึ้นสกอร์บอร์ดเรียลไทม์
- **ความปลอดภัย**: token ใช้ครั้งเดียว, ยืนยันตกกล่องด้วย IR (และถ้ามี Loadcell), ลิงก์ **TX-only** ไป ESP32/ODROID
- **แดชบอร์ดเรียลไทม์** บนคอมหรือ ODROID (Wi‑Fi MQTT) พร้อม CSV/SQLite แบบ append‑only
- เอกสาร+วิดีโอสาธิต 60–90 วินาที และภาพวงจร/ผังเดินสาย

## 2) สถาปัตยกรรม (โมดูล)
- **Node‑REG/VOTE (Arduino #1)**: LCD 20x4 (I²C/PCF8574A), Keypad, RFID RC522, Fingerprint AS608, Buzzer/ไฟ, ปุ่ม 8 ปุ่ม
- **Node‑PRINT/BOX (Arduino #2 / MEGA)**: Thermal printer + cutter (หรือ STAMP‑CAST), IR paper, IR gate, Reed (tamper), (ถ้ามี) Loadcell+HX711
- **ESP32 Wi‑Fi Bridge**: รับ UART ทางเดียวจาก MEGA ผ่าน **Optocoupler** แล้ว Publish JSON → MQTT/HTTP
- **Laptop/ODROID**: Mosquitto + Node‑RED/เว็บแอป → กราฟคะแนน/สถานะ + บันทึก CSV/SQLite

**แผนภาพ:**
- Hardware: ESP32 Wi‑Fi Bridge — Arduino MEGA → Opto → ESP32 → Wi‑Fi  
  `sandbox:/mnt/data/ESP32_WiFiBridge_Hardware.png`
- Network flow: ESP32 → MQTT → Laptop/ODROID Dashboard  
  `sandbox:/mnt/data/ESP32_WiFiBridge_Network.png`

## 3) ข้อความสื่อสาร (Protocol)
- `#1 → #2` (สั่งงานโหวต): `CAST,choice,token,ts,nonce,crc`
- `#2 → ESP32 → MQTT` (ล็อกผล): `{ id, choice, ts, hash, drop, fw }` → topics: `svm/log`, `svm/tally`, `svm/status`
- `#2 → #1` (ตอบกลับ): `ACK|ERROR:...` (เฉพาะภายในเครื่อง)

## 4) ผังพอร์ต (แนะนำ)
- **#1 REG/VOTE**: I²C(LCD+PCF8574A), SPI(RC522), UART2(AS608), GPIO(Buzzer/ไฟ/8 ปุ่ม), UART1 → #2
- **#2 PRINT/BOX**: UART1(Printer), GPIO(IR paper/gate, Reed, Cutter MOSFET), HX711(ถ้ามี), UART2 → ESP32 ผ่าน Opto
- **ESP32**: RX2(GPIO16) รับจาก Opto, Wi‑Fi STA/AP, MQTT publish

## 5) แผนทดสอบ (Test Cases)
- **TC1 Self‑test**: เปิดเครื่อง → ไฟสถานีเขียว, LCD พร้อม
- **TC2 REG**: แตะ RFID + สแกนนิ้วผ่าน → ขึ้นเมนูโหวต
- **TC3 VOTE**: เลือก → ยืนยัน → `CAST` ถูกส่งไป #2
- **TC4 PRINT/STAMP**: สลิป/ตราออกที่หน้าต่าง 7 วินาที → ตัด/ตก
- **TC5 DROP**: IR‑gate = trigger (และน้ำหนักเพิ่ม) → นับ 1 ใบ
- **TC6 MQTT**: ESP32 ส่ง JSON → แดชบอร์ดอัปเดตใน ≤ 0.5s
- **TC7 Tamper**: เปิดฝากล่อง → แดชบอร์ดขึ้นเตือน
- **TC8 Network Failover**: MQTT ล่ม → เก็บบัฟเฟอร์ใน ESP32/MEGA, กลับมาแล้วค่อย flush

## 6) เดโมสคริปต์ (60–90 วินาที)
1) เปิดหน่วย (ไฟเขียว) → แตะบัตร + นิ้ว → LCD เข้าหน้าโหวต  
2) กดเลือก/ยืนยัน → เห็นสลิป/ตราที่หน้าต่าง → ใบถูกตัดและตก  
3) กล่องนับขึ้น + จอคอมอัปเดตแท่งคะแนนเรียลไทม์  
4) เปิดฝากล่องโชว์เตือน tamper  
5) ปิดหน่วย → แสดงรายงานสรุป (CSV/QR)

## 7) แผนงานรายวันจนถึงวันส่ง
| วันที่ | เป้าหมายหลัก | งานย่อย | ผลลัพธ์/เกณฑ์เสร็จ |
|---|---|---|---|
| Tue 09 Sep 2025 | Inventory + วางระบบไฟ/โครง + กำหนดพอร์ต | - เช็กลิสต์ของครบ, จัดเพาเวอร์ 5V/12–24V แยก<br>- ตัดโครงกระดาษลังชั้นล่าง (PRINT/BOX)<br>- เขียน pin-map #1/#2/ESP32 | เพาเวอร์นิ่ง, โครงฐานตัดแล้ว, pin-map เสร็จ |
| Wed 10 Sep 2025 | #2 PRINT/BOX — Firmware เริ่มต้น | - ทดสอบป้อน/ตัดสลิป (หรือกลไกตรา)<br>- อ่าน IR paper/gate, Reed, (HX711)<br>- ส่งเฟรม LOG ผ่าน Serial | ตัด/ตกได้, เซ็นเซอร์อ่านได้, LOG วิ่งบน Serial |
| Thu 11 Sep 2025 | ESP32 Wi‑Fi Bridge + MQTT | - ต่อ Optocoupler จาก MEGA→ESP32 (RX‑only)<br>- ESP32: Wi‑Fi + MQTT publish → Mosquitto<br>- ทำแดชบอร์ดง่ายใน Node‑RED | เฟรมขึ้นกราฟแท่งบนคอมแบบสด |
| Fri 12 Sep 2025 | #1 REG/VOTE — UI | - LCD เมนู + Keypad (PCF8574A)<br>- RFID RC522 + AS608 (เดโมผ่าน)<br>- สร้าง token ใช้ครั้งเดียว | ลูป REG→VOTE ได้บนโต๊ะ |
| Sat 13 Sep 2025 | บูรณาการ #1 ↔ #2 | - โปรโตคอล `CAST...`<br>- วงจรปุ่ม 8 ปุ่ม (short/long)<br>- กด CAST แล้ว #2 ทำงานครบ | กดโหวตแล้วพิมพ์/ตกจริง 1 ใบ |
| Sun 14 Sep 2025 | กล่องบัตร + Tamper + นับ | - ติดตั้ง IR‑gate ที่ปากกล่อง<br>- Reed บนฝา, (Tare) HX711<br>- Node‑RED แจ้งเตือน tamper | ตกแล้วนับขึ้น + เตือนฝาเปิด |
| Mon 15 Sep 2025 | ความเสถียร + สำรองข้อมูล | - เพิ่ม CRC/Retry, บัฟเฟอร์ ESP32 ตอน Wi‑Fi หลุด<br>- บันทึก CSV/SQLite append‑only (ชั่วโมงละไฟล์สำรอง) | รันต่อเนื่อง ≥1 ชม. ไม่มีค้าง |
| Tue 16 Sep 2025 | เก็บงานฮาร์ดแวร์ + ความสวยงาม | - ติดหน้าต่างอะคริลิก VVPAT, แถบไฟสถานี<br>- จัดสาย/เลเบล, ทำโปสเตอร์ data‑diode | ดูเรียบร้อย, พร้อมถ่ายรูป |
| Wed 17 Sep 2025 | ทดสอบครบฟังก์ชัน (TC1–TC8) | - รันเทสต์ลิสต์, เก็บวิดีโอช็อตสำคัญ<br>- แก้บั๊กสุดท้าย | ทุก TC ผ่าน, คลิปเดโม OK |
| Thu 18 Sep 2025 | เอกสาร & ซ้อมพรีเซนต์ | - เขียนรายงานสั้น (โครงสร้าง,โปรโตคอล,ผลทดสอบ)<br>- สไลด์ + สคริปต์พูด 3 นาที | แพ็กไฟล์ส่ง + พร้อมพรีเซนต์ |
| Fri 19 Sep 2025 | เอกสาร & ซ้อมพรีเซนต์ | - เขียนรายงานสั้น (โครงสร้าง,โปรโตคอล,ผลทดสอบ)<br>- สไลด์ + สคริปต์พูด 3 นาที | แพ็กไฟล์ส่ง + พร้อมพรีเซนต์ |
| Sat 20 Sep 2025 | เอกสาร & ซ้อมพรีเซนต์ | - เขียนรายงานสั้น (โครงสร้าง,โปรโตคอล,ผลทดสอบ)<br>- สไลด์ + สคริปต์พูด 3 นาที | แพ็กไฟล์ส่ง + พร้อมพรีเซนต์ |

## 8) ความเสี่ยง & ทางหนีทีไล่
- เครื่องพิมพ์/คัตเตอร์งอแง → สลับเป็น **STAMP‑CAST/EMBOSS** (เซอร์โว+ตรายาง) ใช้เฟรม LOG เดิมได้
- Wi‑Fi ไม่นิ่ง → ESP32 เก็บบัฟเฟอร์ใน RAM/EEPROM แล้วค่อย publish ใหม่เมื่อเชื่อมต่อ
- ไฟตก/รีเซ็ต → ใช้ Diode‑OR + SuperCap ที่ 5V logic / ปรับโค้ดให้ resume ได้จากสถานะล่าสุด
- เซ็นเซอร์รำคาญแสง → กั้นฉาก/ท่อแสง, ใช้ modulation 38 kHz + demod IR



LINK
https://simple-circuit.com/esp8266-nodemcu-ili9341-tft-display/

## 9) เช็กลิสต์ส่งงาน
- วิดีโอเดโม 60–90 วิ + รูปฮาร์ดแวร์/ผังเดินสาย
- รายงาน (PDF/MD) สั้น ๆ + ลิงก์ซอร์สโค้ด
- ไฟล์ CSV/SQLite ตัวอย่าง + ภาพหน้าจอ Dashboard

---
