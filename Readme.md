# Smart Voting Machine

## รายละเอียดโครงงาน
ระบบลงคะแนนเสียงอิเล็กทรอนิกส์อัจฉริยะ ใช้ ESP32, Arduino UNO R3, ODROID C4 สำหรับยืนยันตัวตน/ลงคะแนน/แสดงผล-เก็บข้อมูลแบบ real-time

## ฟีเจอร์เด่น
- ยืนยันตัวตน 2 ชั้น (RFID + Fingerprint)
- จำกัดสิทธิ์โหวต 1 คน 1 สิทธิ์
- Web Dashboard แสดงผลคะแนนแบบ real-time
- ฟังก์ชัน Admin (สมัคร/ลบ/รีเซ็ต/ดูคะแนน)
- AI Camera ปลุกระบบอัตโนมัติ

**Block Diagram: Smart Voting Machine**

- **User**  
    ⬇ แตะบัตร / วางนิ้ว / กดปุ่ม

- **1. ESP32 (Main Controller)**
    - รับข้อมูลจาก RFID, Fingerprint, Keypad, TFT Display
    - เชื่อมต่อกับ Arduino UNO R3 (UART)
    - เชื่อมต่อกับ ODROID C4 (WiFi/HTTP)
    - จัดการ EEPROM (เก็บสถานะบัตร/โหวต)

- **2. Arduino UNO R3 (UI/Keypad)**
    - รับอินพุตจาก Matrix Keypad
    - แสดงข้อความบน LCD 16x2
    - สื่อสารกับ ESP32 ด้วย UART

- **3. ODROID N2+ (Backend & Dashboard)**
    - รัน Flask/FastAPI รับข้อมูลโหวตจาก ESP32
    - บันทึกผลลงฐานข้อมูล SQLite
    - แสดง Dashboard web (real-time)
    - AI Camera ตรวจจับคน/ปลุก ESP32 (GPIO)

- **4. Peripheral Devices**
    - RFID RC522 (SPI)
    - Fingerprint AS608 (UART)
    - TFT LCD ILI9341 (SPI)
    - Ultrasonic Sensor HC-SR04 (GPIO)
    - EEPROM 24C32 (I2C)
    - Power Supply

**การเชื่อมต่อหลัก:**
- ESP32 <-> Arduino UNO R3 : UART (Serial)
- ESP32 <-> ODROID C4: WiFi (HTTP API), GPIO Wake
- ESP32 <-> Peripheral : SPI/I2C/UART/GPIO
- ODROID N2+ <-> AI Camera : USB

---

**ตัวอย่างภาพรวม (Flow):**

1. **User** เริ่มต้น ➡ แตะบัตร ➡ วางนิ้ว ➡ เลือกผู้สมัคร ➡ กดยืนยัน  
2. **ESP32** ประมวลผล ➡ ส่งผลไปยัง **ODROID N2+** ➡ บันทึกผล/อัปเดต Dashboard  
3. **Arduino Mega** รับอินพุต/แสดงผลข้อความ  
4. **ODROID N2+** ตรวจจับผู้ใช้ด้วย AI Camera ➡ ปลุก ESP32 เมื่อมีคนเข้าใกล้  
5. **Web Dashboard** แสดงผลคะแนน/กิจกรรมแบบ real-time

---

## อุปกรณ์ที่ใช้
- ESP32 DevKitC
- Arduino UNO R3
- ODROID C4
- MFRC522 RFID Module
- AS608 Fingerprint Sensor
- TFT ILI9341 2.4"
- Ultrasonic Sensor HC-SR04
- LCD 16x2 + Keypad 4x4

## ซอฟต์แวร์และไลบรารี
- PlatformIO, Arduino IDE
- Python 3.8+, FastAPI, SQLite, OpenCV
- Arduino Lib: MFRC522, Adafruit_Fingerprint, TFT_eSPI, LiquidCrystal_I2C, Keypad

## วิธีเริ่มต้นใช้งาน
1. Clone repository:  
   `git clone ...`
2. อัปโหลดโค้ด ESP32 และ Arduino ตามคู่มือ
3. รัน backend:  
   `cd backend && python app.py`
4. เข้าชม Dashboard:  
   `http://<ip-odroid>:5000/`

## วิธีใช้งาน
- แตะบัตร/วางนิ้ว → เลือกผู้สมัคร → กดยืนยัน
- Admin Mode: Hold REGISTER/DELETE/ SCORE 3s แล้วใส่ PIN
- Dashboard: ดูผลคะแนน, อัปโหลดรูป, export ข้อมูล

## ตัวอย่างหน้าจอ
<img width="2048" height="1280" alt="image" src="https://github.com/user-attachments/assets/9bb0aee9-bba9-47f1-bbdd-acbb1e38653a" />
<img width="2048" height="881" alt="image" src="https://github.com/user-attachments/assets/dfd972d3-62cc-4fe6-b27d-8e47edb4d9c2" />
<img width="2048" height="1279" alt="image" src="https://github.com/user-attachments/assets/5544ad3a-3ac1-4761-a955-280827bd3fca" />


## รายชื่อผู้จัดทำ
- นายกรวิทย์ กอหลัง  6610110007
- นายพชรพล ศุกลสกุล  6610110191
- Section 01

## เสนอ
- รศ.ดร. ทวีศักดิ์ เรืองพีระกุล
- รศ.ดร. ปัญญยศ ไชยกาฬ
- ผศ.ดร. วชรินทร์ แก้วอภิชัย

## รายวิชา
240-319 Embedded System Developer Module  
คณะวิศวกรรมศาสตร์ สาขาวิศวกรรมคอมพิวเตอร์  
มหาวิทยาลัยสงขลานครินทร์  
ภาคเรียนที่ 1 ปีการศึกษา 2568

## Reference / Datasheet
- [ESP32 Datasheet (PDF)](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [Arduino UNO R3 Datasheet (PDF)]([https://docs.arduino.cc/static/8a9afcaa7e0b785c8e5e6039c0c76e8b/ABX00003-datasheet.pdf](https://en.wikipedia.org/wiki/Arduino_Uno))
- [ODROID C4 Wiki]([https://wiki.odroid.com/odroid-n2/hardware/hardware](https://wikidevi.wi-cat.ru/ODROID-C4))
