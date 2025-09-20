/*  UNO + DS3231 (RTC) + TM1638 (8-digit)
    - แสดง HHMMSS บน TM1638 (โคลอน = LED กลาง 2 ดวงกระพริบ)
    - พิมพ์เวลา Serial ทุก 1 วินาที: "YYYY-MM-DD HH:MM:SS"
    - ชดเชยเวลา +11 วินาที "ครั้งเดียว" ด้วย EEPROM flag กันบวกซ้ำ
    - รองรับตั้งเวลาผ่าน Serial: SET YYYY-MM-DD HH:MM:SS (ภายใน 15s แรกหลังบูต)

    Wiring (UNO):
      DS3231: SDA→A4, SCL→A5, VCC→5V, GND→GND (+CR2032)
      TM1638: STB→D4, CLK→D2, DIO→D3, VCC→5V, GND→GND
*/

#include <Wire.h>
#include <RTClib.h>
#include <TM1638plus.h>
#include <EEPROM.h>

#define PIN_STB 4
#define PIN_CLK 2
#define PIN_DIO 3

RTC_DS3231 rtc;
// NOTE: TM1638plus เวอร์ชันนี้ต้องส่งพารามิเตอร์ตัวที่ 4 (highfreq)
// สำหรับ UNO/AVR ใช้ false
TM1638plus tm(PIN_STB, PIN_CLK, PIN_DIO, false);

// -------- การแสดงผล --------
uint8_t  brightnessLevel = 3;     // 0..7
bool     blinkColon      = true;  // ให้โคลอนกระพริบหรือไม่
uint32_t colonToggleMS   = 500;   // กระพริบทุกกี่ ms

// ใช้ LED กลางสองดวงเป็นโคลอน (เปลี่ยน index ได้ตามบอร์ดคุณ)
const uint8_t COLON_LED_LEFT  = 3;
const uint8_t COLON_LED_RIGHT = 4;

bool     colonOn   = true;
uint32_t lastBlink = 0;

// -------- ชดเชย +11s ครั้งเดียว --------
const int  EEPROM_MARK_ADDR   = 0x42;  // ช่องเก็บ flag
const byte EEPROM_MARK_VALUE  = 0xA5;  // ค่าที่แปลว่า "ปรับแล้ว"
const int  NUDGE_SECONDS      = 11;    // ชดเชยกี่วินาที (ตามที่คุณต้องการ)

// -------- ฟังก์ชันช่วย --------
void setColonLEDs(bool on) {
  uint8_t mask = 0x00;
  if (on) {
    if (COLON_LED_LEFT  < 8) mask |= (1 << COLON_LED_LEFT);
    if (COLON_LED_RIGHT < 8) mask |= (1 << COLON_LED_RIGHT);
  }
  tm.setLEDs(mask);
}

void showHHMMSS(uint8_t hh, uint8_t mm, uint8_t ss) {
  char seg[9];
  snprintf(seg, sizeof(seg), "%02u%02u%02u  ",
           (unsigned)hh, (unsigned)mm, (unsigned)ss);
  tm.displayText(seg);
}

void printDateTimeSerial(const DateTime& dt) {
  char line[25];
  snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d",
           (int)dt.year(), (int)dt.month(), (int)dt.day(),
           (int)dt.hour(), (int)dt.minute(), (int)dt.second());
  Serial.println(line);
}

void applyNudgeSeconds(int deltaSec){
  DateTime now = rtc.now();
  rtc.adjust(now + TimeSpan(0,0,0,deltaSec));
}

// รับคำสั่ง Serial เพื่อตั้งเวลา (และเลิกใช้ __TIME__)
void trySerialSetRTC(uint32_t wait_ms = 15000) {
  Serial.println(F("Send within 15s: SET YYYY-MM-DD HH:MM:SS"));
  String cmd;
  uint32_t t0 = millis();

  while (millis() - t0 < wait_ms) {
    if (Serial.available()) {
      cmd = Serial.readStringUntil('\n');
      cmd.trim();

      if (cmd.startsWith("SET ")) {
        int y,M,d,h,m,s;
        if (sscanf(cmd.c_str(), "SET %d-%d-%d %d:%d:%d", &y,&M,&d,&h,&m,&s) == 6) {
          rtc.adjust(DateTime(y,M,d,h,m,s));
          Serial.println(F("RTC updated from Serial."));
        } else {
          Serial.println(F("Bad format. Use: SET YYYY-MM-DD HH:MM:SS"));
        }
        break;
      }
    }
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();

  // --- TM1638 ---
  tm.displayBegin();
  tm.brightness(brightnessLevel);
  tm.displayText("  READY  ");
  setColonLEDs(false);
  delay(700);

  // --- RTC ---
  if (!rtc.begin()) {
    tm.displayText(" NO RTC ");
    Serial.println(F("RTC not found!"));
    while (1) { delay(1000); }
  }

  // ถ้า RTC เคยไฟหาย ให้ตั้งจากเวลา compile (+ชดเชย boot delay) ครั้งแรกเท่านั้น
  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power -> adjusting from __TIME__ ..."));
    uint32_t bootStart = millis();
    DateTime compiled(F(__DATE__), F(__TIME__));
    uint32_t elapsed = (millis() - bootStart) / 1000;  // วินาที
    rtc.adjust(compiled + TimeSpan(elapsed));
    Serial.print(F("Adjusted from __TIME__ + "));
    Serial.print(elapsed);
    Serial.println(F("s"));
  }

  // เปิดโอกาสให้ตั้งเวลาเป๊ะผ่าน Serial ภายใน 15 วินาทีแรก
  trySerialSetRTC(15000);

  // --- ชดเชย +11 วินาที "ครั้งเดียว" ด้วย EEPROM flag ---
  byte mark = EEPROM.read(EEPROM_MARK_ADDR);
  if (mark != EEPROM_MARK_VALUE) {
    applyNudgeSeconds(NUDGE_SECONDS);
    EEPROM.update(EEPROM_MARK_ADDR, EEPROM_MARK_VALUE);
    Serial.print(F("RTC nudged +"));
    Serial.print(NUDGE_SECONDS);
    Serial.println(F("s (one-time)"));
  } else {
    Serial.println(F("Nudge already applied (EEPROM mark set)."));
  }

  // เริ่มโคลอน
  colonOn = true;
  setColonLEDs(colonOn);
  lastBlink = millis();

  Serial.println(F("Clock ready."));
}

void loop() {
  DateTime now = rtc.now();

  // แสดงเวลา HHMMSS
  showHHMMSS(now.hour(), now.minute(), now.second());

  // กระพริบโคลอนด้วย LED สองดวงกลาง
  if (blinkColon && millis() - lastBlink >= colonToggleMS) {
    colonOn = !colonOn;
    setColonLEDs(colonOn);
    lastBlink = millis();
  }

  // พิมพ์ Serial ทุก ~1s
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    printDateTimeSerial(now);
    lastPrint = millis();
  }

  delay(10);
}