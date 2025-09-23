#define BLYNK_TEMPLATE_ID "TMPL6G6KsJzqK"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "RUBdFFrRrLJ99YHyTgYN5rew8gfkPzaH"

#define WAKE_PIN 33

// ==== must be the very first lines ====
static constexpr int UID_HEX_MAX = 16;

struct Rec;
void readRec(int idx, Rec &r);         // tell IDE not to autogenerate wrong prototypes
void writeRec(int idx, const Rec &r);  // uses incomplete type by reference (OK)

// ==== ด้านบนไฟล์ (globals) ====
#define SAFE_PIN 0   // กดลง LOW ตอนบูตเพื่อ skip SD/TFT

bool SAFE_MODE = false;


#include "driver/rtc_io.h"  // สำหรับ rtc_gpio_get_level()
#include "esp_system.h"
#include "esp_heap_caps.h"

// ประกาศล่วงหน้าค่าคงที่ที่ struct ใช้ (ถ้าคุณมีเวอร์ชันเป็น #define อยู่แล้ว ข้ามได้)
#if 0  // DISABLE: duplicates UID_HEX_MAX (we already #define it at top)
const int      UID_HEX_MAX = 16;
#endif

// ต้อง “นิยาม” struct Rec ให้เสร็จก่อนฟังก์ชัน readRec()/writeRec()
// (forward declare เฉยๆ ไม่พอ เพราะฟังก์ชันแตะฟิลด์ใน struct)
// ==== constants (place near the top, before struct Rec) ====
#if 0  // DISABLE: duplicate struct Rec (already defined at top)
struct Rec {
  char     uid[UID_HEX_MAX];
  uint8_t  fp_id;
  uint8_t  voted;
  uint8_t  valid;
  uint8_t  reserved;
};
#endif

#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Adafruit_Fingerprint.h>


// ===== Added: Deep-sleep support =====
#include "esp_sleep.h"

// ===== [ADD TFT] Display & SD =====
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <SD.h>

#define SD_CS    13     // CS ของ SD (คุณทดสอบไว้ที่ 13 ใช้ต่อได้)
#define TFT_CS   15      // CS ของจอ (ตาม User_Setup.h ที่ตั้งไว้)

TFT_eSPI tft;            // ใช้พิน SPI/CS/DC/RST จาก User_Setup.h

// ==== [ADD] Wake-pin debug helpers (no change to existing code) ====
volatile uint32_t WAKE_edges = 0;
volatile uint32_t WAKE_lastMs = 0;

IRAM_ATTR void WAKE_isr() {
  // นับทุกครั้งที่มีขอบขึ้น/ลง
  WAKE_edges++;
  WAKE_lastMs = millis();
}

// อ่านระดับจากทั้ง digital และ RTC domain
int wake_digital() {
  return digitalRead(WAKE_PIN);
}
int wake_rtc() {
  return rtc_gpio_get_level((gpio_num_t)WAKE_PIN);
}

void dbgPrintWakePin(const char *tag) {
  Serial.print("[WAKEDBG] ");
  Serial.print(tag);
  Serial.print("  digital=");
  Serial.print(wake_digital());
  Serial.print("  rtc=");
  Serial.print(wake_rtc());
  Serial.print("  edges=");
  Serial.print(WAKE_edges);
  Serial.print("  lastMs=");
  Serial.println(WAKE_lastMs);
}

// ใช้ GPIO35 เป็นขาปลุก (ต่อมาจาก ODROID PIN_33 ผ่าน R อนุกรม ~1k)
// *GPIO35 เป็นขา RTC input ได้ ปลุกด้วย ext1 ได้

// ==== forward declarations to satisfy compile order (ADD ONLY) ====


// ===== [ADD] Ultrasonic (HC-SR04) for auto-sleep =====
const int TRIG_PIN = 4;
const int ECHO_PIN = 34;  // ต้องลดเป็น 3.3V ก่อนเข้า ESP32

// เกณฑ์ “ใกล้”
volatile float NEAR_ON_CM = 25.0;   // เข้าสถานะ NEAR เมื่อ <= 25 cm
volatile float NEAR_OFF_CM = 35.0;  // กลับ FAR เมื่อ >= 35 cm (ฮิสเทอรีส)

// รอบวัดและ timeout
const uint16_t US_INTERVAL_MS = 200;     // วัดทุก ~200ms
const unsigned long US_TIMEOUT = 25000;  // pulseIn timeout ~25ms

// ===== [ADD] counters & confirm windows for noise filtering =====
static uint8_t nearConsec = 0;
static uint8_t farConsec = 0;
static const uint8_t NEAR_CONFIRM_N = 2;  // ต้องเห็น NEAR 2 เฟรมติดถึงจะเปลี่ยนเป็น NEAR
static const uint8_t FAR_CONFIRM_N = 2;   // ต้องเห็น FAR  2 เฟรมติดถึงจะเปลี่ยนเป็น FAR

// จับเวลาเพื่อหลับ
const uint32_t NO_NEAR_SLEEP_MS = 30000;  // FAR ต่อเนื่อง 30 วินาที -> หลับ

// ตัวแปรสถานะ
static bool nearState = false;
static uint32_t lastUSms = 0;
static uint32_t lastNearSeenMs = 0;

// [ADD] Debug logging for Ultrasonic
#define DEBUG_ULTRA 1
static uint32_t lastUltraLogMs = 0;

// (ออปชัน) ถ้าจะให้หลับเองเมื่อไม่มีเหตุการณ์นาน X ms ให้เปิดใช้ 2 บรรทัดนี้ได้ภายหลัง
// #define IDLE_SLEEP_MS 60000UL   // FAR/ไม่มีงาน 60s → หลับ
// static uint32_t lastActiveMs = 0;
// inline void noteActivity(){ lastActiveMs = millis(); }

#include "esp_system.h"
#include "driver/rtc_io.h"

void printBootAndWakeInfo() {
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("Reset reason=%d (1=POWERON, 12=BROWNOUT, 5=DEEPSLEEP)\n", (int)rr);

  esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
  Serial.print("Wake cause=");
  switch (wc) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("EXT0"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("EXT1"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("TIMER"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("TOUCH"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("ULP"); break;
    case ESP_SLEEP_WAKEUP_GPIO: Serial.println("GPIO"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default: Serial.println("POWER-ON/RESET"); break;
  }

  if (wc == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t m = esp_sleep_get_ext1_wakeup_status();
    Serial.printf("EXT1 mask=0x%016llX\n", (unsigned long long)m);
    if (m) {
      Serial.print("Pins HIGH: ");
      bool first = true;
      for (int g = 0; g <= 39; ++g)
        if (m & (1ULL << g)) {
          Serial.print(first ? "" : " ,");
          Serial.print(g);
          first = false;
        }
      Serial.println();
    }
  }
}

// เข้าหลับทันที แล้วปลุกเมื่อ WAKE_PIN=HIGH จาก ODROID
void goDeepSleepNow() {
  Serial.println("-> Deep-sleep now. Waiting for ODROID wake (GPIO HIGH)...");
  delay(30);

  // ปิด I/O ที่อาจดีดกลับ
  pinMode(12, INPUT);
  pinMode(4,  INPUT);

  // เอา interrupt ของขาปลุกออกก่อน
  detachInterrupt(digitalPinToInterrupt(WAKE_PIN));

  // ตั้งค่าพินปลุกในสองโดเมนให้สะอาด
  rtc_gpio_hold_dis((gpio_num_t)WAKE_PIN);
  pinMode(WAKE_PIN, INPUT);                       // digital
  rtc_gpio_deinit((gpio_num_t)WAKE_PIN);          // RTC
  rtc_gpio_init((gpio_num_t)WAKE_PIN);
  rtc_gpio_set_direction((gpio_num_t)WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en((gpio_num_t)WAKE_PIN);
  rtc_gpio_pullup_dis((gpio_num_t)WAKE_PIN);

  // ถ้าขาปลุก HIGH อยู่แล้ว ให้ข้ามหลับ (กันเด้ง)
  if (rtc_gpio_get_level((gpio_num_t)WAKE_PIN) == 1) {
    Serial.println("[SLEEP] WAKE_PIN is HIGH already -> skip sleep");
    return;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext1_wakeup(1ULL << WAKE_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);

  esp_deep_sleep_start();
}


// ---------- Serial / UART ----------
HardwareSerial mySerial(2);      // UART2 : ใช้คุยกับบอร์ด/จออีกตัว ตามที่คุณใช้อยู่ (TX=17, RX=16 ด้านล่าง)
HardwareSerial FingerSerial(1);  // UART1 : ใช้คุยกับโมดูลลายนิ้วมือ
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerSerial);

// ---------- RFID ----------
#define SS_PIN 5
#define RST_PIN 27
MFRC522 rfid(SS_PIN, RST_PIN);

// ===== [ADD TFT] Bus guard: กัน SPI ชนกันระหว่าง TFT/SD/RFID =====
inline void spi_idle_all() {
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH); // จอ
  pinMode(SS_PIN,  OUTPUT); digitalWrite(SS_PIN,  HIGH); // RFID
  pinMode(SD_CS,   OUTPUT); digitalWrite(SD_CS,   HIGH); // SD
}

// ===== Bus lock helpers for RC522 on shared VSPI =====
inline void bus_acquire_for_rfid() {
  // ปล่อยจอ/SD ออกจากบัสก่อน (กันจอค้าง CS ต่ำ)
  // ถ้าใช้ TFT_eSPI: endWrite จะปล่อย CS ของจอ
  tft.endWrite();                // <-- ปล่อยธุรกรรมของจอ (ถ้าค้างอยู่)
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);
  pinMode(SD_CS,  OUTPUT); digitalWrite(SD_CS,  HIGH);

  // ย้ำว่า SS ของ RC522 = HIGH ก่อนเริ่ม (กันเศษเดิม)
  pinMode(SS_PIN, OUTPUT); digitalWrite(SS_PIN, HIGH);
  // ตั้งความถี่สำหรับ RC522 (<= 10MHz)
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  // เลือก RC522
  digitalWrite(SS_PIN, LOW);
}

inline void bus_release_after_rfid() {
  // ปล่อย RC522 ออกจากบัส
  digitalWrite(SS_PIN, HIGH);
  SPI.endTransaction();

  // ปล่อยทุกตัวกลับ idle
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SD_CS,  HIGH);
}


// ---------- I/O ----------
const int EEPROM_SIZE = 512;
const int buzzerPin = 12;
const int switchPin33 = 14;  // สวิตช์ Register
const int switchPin32 = 32;  // สวิตช์ Delete
// const int ledPin = 13;

// ---------- Finger UART Pins (ปรับให้ตรงบอร์ดคุณ) ----------
const int FINGER_RX = 26;  // ESP32 RX1 pin to sensor TX
const int FINGER_TX = 25;  // ESP32 TX1 pin to sensor RX

// ---------- Protocol between boards (คงรูปแบบเดิมของคุณ) ----------
/*
  mySerial.println("regis"); // โหมดลงทะเบียน
  mySerial.println("S");     // สแกนบัตรปกติ: สถานะกำลังอ่าน
  mySerial.println("W");     // บัตรผิด
  mySerial.println("OK");    // ยืนยันผ่าน (บัตร+นิ้วผ่าน)
*/

// ---------- Durable Storage Layout (EEPROM) ----------
/*
  Header (offset 0..15)
    0..3   : MAGIC 'VOTE' (0x56 0x4F 0x54 0x45)
    4      : VERSION = 1
    5..15  : reserved

  Records start at BASE = 16
  Each record = 20 bytes
    0..15 : UID_HEX (fixed 16 chars, ASCII hex, padded with 0x00)
    16    : FP_ID (0..199)  => id ในโมดูลลายนิ้วมือ
    17    : VOTED (0/1)
    18    : VALID (0xA5 = valid, 0xFF = empty)
    19    : reserved
*/
const uint32_t MAGIC = 0x564F5445UL;  // 'VOTE'
const uint8_t VERSION = 1;
const int HDR_SIZE = 16;
const int RECORD_SIZE = 20;
const int BASE = HDR_SIZE;
const uint8_t VALID_FLAG = 0xA5;
const uint8_t EMPTY_FLAG = 0xFF;
const int MAX_RECORDS = (EEPROM_SIZE - BASE) / RECORD_SIZE;  // ~= 24

// ---------- Utils ----------
// ==== constants (place near the top, before struct Rec) ====
struct Rec {
  char uid[UID_HEX_MAX];  // ไม่รับ '\0' เสมอ ให้เก็บเป็น 16 ชาร์ (ถ้าน้อยกว่าก็ 0x00 padding)
  uint8_t fp_id;
  uint8_t voted;  // 0/1
  uint8_t valid;  // VALID_FLAG หรือ EMPTY_FLAG
  uint8_t reserved;
};

void eepromWriteBytes(int addr, const uint8_t *data, int len) {
  for (int i = 0; i < len; ++i) EEPROM.write(addr + i, data[i]);
}

void eepromReadBytes(int addr, uint8_t *data, int len) {
  for (int i = 0; i < len; ++i) data[i] = EEPROM.read(addr + i);
}

void writeHeader() {
  uint8_t hdr[HDR_SIZE] = { 0 };
  hdr[0] = 'V';
  hdr[1] = 'O';
  hdr[2] = 'T';
  hdr[3] = 'E';
  hdr[4] = VERSION;
  // rest zero
  eepromWriteBytes(0, hdr, HDR_SIZE);
  EEPROM.commit();
}

bool headerOK() {
  uint8_t h[5];
  for (int i = 0; i < 5; i++) h[i] = EEPROM.read(i);
  return (h[0] == 'V' && h[1] == 'O' && h[2] == 'T' && h[3] == 'E' && h[4] == VERSION);
}

int recAddr(int idx) {
  return BASE + idx * RECORD_SIZE;
}

void readRec(int idx, Rec &r) {
  uint8_t buf[RECORD_SIZE];
  eepromReadBytes(recAddr(idx), buf, RECORD_SIZE);
  for (int i = 0; i < UID_HEX_MAX; ++i) r.uid[i] = (char)buf[i];
  r.fp_id = buf[16];
  r.voted = buf[17];
  r.valid = buf[18];
  r.reserved = buf[19];
}

void writeRec(int idx, const Rec &r) {
  uint8_t buf[RECORD_SIZE];
  for (int i = 0; i < UID_HEX_MAX; ++i) buf[i] = (uint8_t)r.uid[i];
  buf[16] = r.fp_id;
  buf[17] = r.voted;
  buf[18] = r.valid;
  buf[19] = r.reserved;
  eepromWriteBytes(recAddr(idx), buf, RECORD_SIZE);
  EEPROM.commit();
}

void clearRec(int idx) {
  Rec r{};
  for (int i = 0; i < UID_HEX_MAX; ++i) r.uid[i] = 0x00;
  r.fp_id = 0;
  r.voted = 0;
  r.valid = EMPTY_FLAG;
  r.reserved = 0;
  writeRec(idx, r);
}

int findFreeSlot() {
  for (int i = 0; i < MAX_RECORDS; ++i) {
    Rec r;
    readRec(i, r);
    if (r.valid != VALID_FLAG) return i;
  }
  return -1;
}

bool sameUID16(const char a[UID_HEX_MAX], const char b[UID_HEX_MAX]) {
  for (int i = 0; i < UID_HEX_MAX; ++i)
    if (a[i] != b[i]) return false;
  return true;
}

void uidToFixed16(const String &uidHex, char out16[UID_HEX_MAX]) {
  // ตัด/แพดให้ยาว 16 ตัวอักษร
  // (UID 4 ไบต์ => 8 ตัวอักษร, UID 7/10 ไบต์ => 14/20 ตัวอักษร → เก็บ 16 ตัวอักษรแรกพอ)
  for (int i = 0; i < UID_HEX_MAX; i++) {
    out16[i] = (i < uidHex.length()) ? uidHex.charAt(i) : 0x00;
  }
}

int findByUID(const String &uidHex) {
  char key[UID_HEX_MAX];
  uidToFixed16(uidHex, key);
  for (int i = 0; i < MAX_RECORDS; ++i) {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && sameUID16(r.uid, key)) return i;
  }
  return -1;
}

int findByFPID(uint8_t fp) {
  for (int i = 0; i < MAX_RECORDS; ++i) {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && r.fp_id == fp) return i;
  }
  return -1;
}

// สแกนนิ้วแบบเร็วเพื่อเช็กว่ามีนิ้วนี้อยู่ในฐานแล้วหรือไม่
int quickSearchFingerprint(uint32_t timeout_ms = 10000) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeout_ms) {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      delay(50);
      continue;
    }
    if (p != FINGERPRINT_OK) {
      delay(50);
      continue;
    }
    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) {
      delay(50);
      continue;
    }
    p = finger.fingerFastSearch();                    // ค้นหาในฐานของเซ็นเซอร์
    if (p == FINGERPRINT_OK) return finger.fingerID;  // พบแล้ว → คืน fp_id เดิม
    else return -1;                                   // ไม่พบ → นิ้วใหม่น่าจะยังไม่อยู่ในฐาน
  }
  return -1;  // timeout
}


bool setVotedByIndex(int idx, uint8_t v) {
  if (idx < 0 || idx >= MAX_RECORDS) return false;
  Rec r;
  readRec(idx, r);
  if (r.valid != VALID_FLAG) return false;
  r.voted = v ? 1 : 0;
  writeRec(idx, r);
  return true;
}

// ---------- Fingerprint helpers ----------
bool fingerBegin() {
  // เริ่มพอร์ตกับโมดูลลายนิ้วมือ
  FingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  finger.begin(57600);
  delay(200);
  return finger.verifyPassword();
}

int enrollFingerprint(uint8_t fp_id) {
  // ขั้นตอนย่อสไตล์ Adafruit: ขอภาพสองครั้ง, สร้างโมเดล, เก็บไว้ตำแหน่ง fp_id
  // คืน 0 = ok, อื่นๆ = code ผิดพลาด
  int p = -1;
  Serial.printf("Enroll FP id=%d : place finger\n", fp_id);

  // ภาพ 1
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) {
      delay(50);
      continue;
    }
    if (p == FINGERPRINT_PACKETRECIEVEERR) return p;
    if (p == FINGERPRINT_IMAGEFAIL) return p;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return p;

  Serial.println("Remove finger");
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(50);

  Serial.println("Place same finger again");
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) {
      delay(50);
      continue;
    }
    if (p == FINGERPRINT_PACKETRECIEVEERR) return p;
    if (p == FINGERPRINT_IMAGEFAIL) return p;
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) return p;

  p = finger.createModel();
  if (p != FINGERPRINT_OK) return p;

  p = finger.storeModel(fp_id);
  return p;  // FINGERPRINT_OK = 0x00
}

int matchFingerprint() {
  // จับภาพ → แปลง → ค้นหา เร็ว
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -1;
  return finger.fingerID;  // ตำแหน่งที่ match
}

// ---------- App Logic ----------
String readRFIDasHex() {
  // คืนเป็นตัวอักษร hex (ไม่เว้นวรรค), ตัวพิมพ์ใหญ่, ยาวเท่าจำนวน uid.size*2 (สูงสุด ~20 chars)
  String ID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) ID += "0";
    ID += String(rfid.uid.uidByte[i], HEX);
  }
  ID.toUpperCase();
  ID.replace(" ", "");
  return ID;
}

bool storeNewRecord(const String &uidHex, uint8_t fp_id) {
  int slot = findFreeSlot();
  if (slot < 0) return false;
  Rec r{};
  uidToFixed16(uidHex, r.uid);
  r.fp_id = fp_id;
  r.voted = 0;
  r.valid = VALID_FLAG;
  r.reserved = 0;
  writeRec(slot, r);
  return true;
}

bool removeByUID(const String &uidHex) {
  int idx = findByUID(uidHex);
  if (idx < 0) return false;
  Rec r;
  readRec(idx, r);
  // ลบในโมดูลลายนิ้วมือด้วย
  if (r.fp_id > 0) {
    finger.deleteModel(r.fp_id);
  }
  clearRec(idx);
  return true;
}

// ---------- High-level flows ----------
void registerCardAndFingerprint() {
  mySerial.println("regis");
  Serial.println("Registration mode... Tap a new card");

  // รอการ์ด
  while (true) {
    bool ok = false;
    bus_acquire_for_rfid();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) ok = true;
    bus_release_after_rfid();
    if (ok) break;
    delay(50);
  }
  String uidHex;
  bus_acquire_for_rfid();
  uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  bus_release_after_rfid();

  if (findByUID(uidHex) >= 0) {
    Serial.println("This card is already registered.");
    tone(buzzerPin, 1200, 150);
    return;
  }

  // === NEW: ตรวจนิ้วซ้ำก่อน Enroll ===
  Serial.println("Place finger to check duplication...");
  int existing_fp = quickSearchFingerprint(10000);
  if (existing_fp >= 0) {
    int idxExisting = findByFPID(existing_fp);
    if (idxExisting >= 0) {
      // มีนิ้วนี้อยู่ในระบบแล้ว และผูกกับบัตรเดิมอยู่ → บล็อก
      Rec rExist;
      readRec(idxExisting, rExist);
      Serial.printf("Duplicate finger detected! Already linked to another card (FP_ID=%d). Abort.\n", existing_fp);
      tone(buzzerPin, 600, 400);
      return;
    } else {
      // กรณี “เจอในเซ็นเซอร์แต่ไม่เจอใน EEPROM” (ข้อมูลค้าง) → ลบเทมเพลตทิ้งก่อน
      Serial.printf("Found stale FP template (id=%d) without EEPROM record. Deleting stale template.\n", existing_fp);
      finger.deleteModel(existing_fp);
    }
  }

  // หา fp_id ว่าง (เหมือนเดิม)
  uint8_t chosen_fp_id = 1;
  bool used[200];
  for (int i = 0; i < 200; i++) used[i] = false;
  for (int i = 0; i < MAX_RECORDS; i++) {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && r.fp_id > 0 && r.fp_id < 200) used[r.fp_id] = true;
  }
  while (chosen_fp_id < 200 && used[chosen_fp_id]) chosen_fp_id++;
  if (chosen_fp_id >= 200) {
    Serial.println("No free FP ID slot.");
    tone(buzzerPin, 800, 300);
    return;
  }

  // Enroll ตามปกติ
  Serial.printf("Enroll fingerprint for this card (UID=%s) at FP_ID=%d\n", uidHex.c_str(), chosen_fp_id);
  int p = enrollFingerprint(chosen_fp_id);
  if (p != FINGERPRINT_OK) {
    Serial.printf("Enroll failed (code=%d). Abort.\n", p);
    tone(buzzerPin, 500, 500);
    return;
  }

  if (storeNewRecord(uidHex, chosen_fp_id)) {
    Serial.println("Card+Fingerprint registered successfully.");
    mySerial.println("Card Registered!");
    tone(buzzerPin, 1600, 120);
    delay(200);
    tone(buzzerPin, 1600, 120);
  } else {
    Serial.println("EEPROM full. Cannot store new record.");
    tone(buzzerPin, 500, 500);
    finger.deleteModel(chosen_fp_id);  // roll back
  }
}


void deleteCardFlow() {
  Serial.println("Delete mode... Tap a card to delete");
  while (true) {
    bool ok = false;
    bus_acquire_for_rfid();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) ok = true;
    bus_release_after_rfid();
    if (ok) break;
    delay(50);
  }
  String uidHex;
  bus_acquire_for_rfid();
  uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  bus_release_after_rfid();

  int idx = findByUID(uidHex);
  if (idx < 0) {
    Serial.println("Card not found");
    tone(buzzerPin, 600, 300);
    return;
  }

  // โหลดเรคคอร์ดเพื่อรู้ fp_id ของเจ้าของบัตร
  Rec r;
  readRec(idx, r);

  // ✅ ขั้นตอน “ยืนยันลายนิ้วมือก่อนลบ”
  Serial.printf("Verify fingerprint to delete (expect FP_ID=%d)\n", r.fp_id);
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000) {  // รอสูงสุด 15 วินาที
    matched = matchFingerprint();
    if (matched >= 0) break;
    delay(50);
  }
  if (matched < 0 || matched != r.fp_id) {
    Serial.println("Fingerprint verify failed / timeout. Abort delete.");
    tone(buzzerPin, 600, 400);
    return;
  }

  // ลบ fingerprint template ในเซ็นเซอร์
  if (r.fp_id > 0) {
    uint8_t p = finger.deleteModel(r.fp_id);
    if (p != FINGERPRINT_OK) {
      Serial.printf("Delete template failed (code=%d). Continue to clear record.\n", p);
    }
  }

  // ลบเรคคอร์ดบัตรใน EEPROM
  clearRec(idx);
  Serial.println("Card + Fingerprint deleted");
  tone(buzzerPin, 1200, 150);
  delay(150);
  tone(buzzerPin, 1200, 150);
}


// =============== REPLACE WHOLE FUNCTION ===============
void normalScanFlow() {
  // แตะบัตร → ตรวจว่าลงทะเบียนหรือยัง → ถ้าลงทะเบียน ต้องสแกนนิ้วให้ "ตรงกับ fp_id" ของบัตรนั้น
  Serial.println("Scan card...");
  mySerial.println("S");  // แจ้งจอ/บอร์ดลูกว่าเริ่มสแกน

  // 1) รอการ์ดและอ่าน UID ให้สำเร็จจริง โดยล็อคบัสทุกครั้ง
  String uidHex;
  while (true) {
    bool ok = false;

    bus_acquire_for_rfid();  // <<< ล็อคบัส ปล่อย TFT/SD ให้ว่าง แล้วดึง SS ของ RC522 ลง
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      uidHex = readRFIDasHex();   // อ่านเป็น HEX
      rfid.PICC_HaltA();          // ปล่อยการ์ด
      rfid.PCD_StopCrypto1();     // ปิด crypto
      ok = true;
    }
    bus_release_after_rfid();     // <<< ปล่อยบัสคืน (ดันทุก CS = HIGH)

    if (ok) break;                // อ่านได้แล้วออกจากลูป
    delay(20);                    // ยังไม่ได้ → รอแป๊บกันบัสถี่เกิน
  }

  // 2) เช็คใน EEPROM ว่าการ์ดนี้ลงทะเบียนหรือยัง
  int idx = findByUID(uidHex);
  if (idx < 0) {
    Serial.println("Unknown card");
    //mySerial.println("W");
    tone(buzzerPin, 1000, 200);
    // digitalWrite(ledPin, HIGH);
    delay(200);
    // digitalWrite(ledPin, LOW);
    delay(150);
    tone(buzzerPin, 1000, 200);
    return;
  }

  Rec r;
  readRec(idx, r);

  // ถ้าเป็นระบบโหวต: บล็อกบัตรที่โหวตไปแล้ว
  if (r.voted == 1) {
    Serial.println("Already voted for this card holder.");
    mySerial.println("W");
    tone(buzzerPin, 700, 300);
    return;
  }

  // 3) ยืนยันลายนิ้วมือให้ตรง FP_ID ของบัตรนี้
  Serial.printf("Card OK. Please verify fingerprint (expect FP_ID=%d)\n", r.fp_id);
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000) {     // รอสูงสุด 15 วินาที
    matched = matchFingerprint();
    if (matched >= 0) break;
    delay(50);
  }

  if (matched < 0) {
    Serial.println("Fingerprint not matched / timeout.");
    mySerial.println("W");
    tone(buzzerPin, 600, 400);
    return;
  }

  Serial.printf("Matched fingerID=%d\n", matched);
  if (matched != r.fp_id) {
    Serial.println("Fingerprint does not belong to this card.");
    mySerial.println("W");
    tone(buzzerPin, 600, 400);
    return;
  }

  // 4) ผ่านทุกเงื่อนไข
  mySerial.println("OK");
  tone(buzzerPin, 1500, 120);
  // digitalWrite(ledPin, HIGH);
  delay(120);
  // digitalWrite(ledPin, LOW);

  // ถ้าเป็นระบบโหวต: mark voted = 1
  setVotedByIndex(idx, 1);
}
// =============== END REPLACEMENT ===============

// [ADD] วัด echo ครั้งเดียว
inline unsigned long us_read_once() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return pulseIn(ECHO_PIN, HIGH, US_TIMEOUT);  // microseconds
}

// [ADD] อ่าน 3 ครั้งเอามัธยฐาน เพื่อลดสไปค์
float measureDistanceCm() {
  unsigned long a = us_read_once();
  delayMicroseconds(150);
  unsigned long b = us_read_once();
  delayMicroseconds(150);
  unsigned long c = us_read_once();
  // sort a<=b<=c
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    auto t = b;
    b = c;
    c = t;
  }
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  unsigned long us = b;
  if (us == 0) return NAN;
  return (float)us / 58.0f;  // cm
}

// [ADD] งานหลัก Ultrasonic: อัปเดต nearState + ตัดสินใจหลับ
// ===== [REPLACE CALL INSIDE YOUR TICK] =====
void ultrasonicTickForSleep() {
  if (millis() - lastUSms < US_INTERVAL_MS) return;
  lastUSms = millis();

  float cm = measureDistanceCmRobust();  // <-- ใช้ตัว robust

  // ถ้าอ่านไม่ได้: นับ FAR ต่อ และพิมพ์ log เป็นครั้งคราว
  if (isnan(cm)) {
    farConsec = min<uint8_t>(FAR_CONFIRM_N, farConsec + 1);
    nearConsec = 0;

    if (DEBUG_ULTRA && (millis() - lastUltraLogMs >= 1000)) {
      Serial.println("[US] cm=NaN (treat FAR)");
      lastUltraLogMs = millis();
    }
  } else {
    // ตัดสินใจ newNear ด้วยฮิสเทอรีส
    bool wantNear = nearState;
    if (!nearState && cm <= NEAR_ON_CM) wantNear = true;
    if (nearState && cm >= NEAR_OFF_CM) wantNear = false;

    if (wantNear) {
      nearConsec = min<uint8_t>(NEAR_CONFIRM_N, nearConsec + 1);
      farConsec = 0;
    } else {
      farConsec = min<uint8_t>(FAR_CONFIRM_N, farConsec + 1);
      nearConsec = 0;
    }

    // เปลี่ยนสถานะเมื่อ “ยืนยัน” ครบ N เฟรม
    bool newNear = nearState;
    if (!nearState && nearConsec >= NEAR_CONFIRM_N) newNear = true;
    if (nearState && farConsec >= FAR_CONFIRM_N) newNear = false;

    // log ทุก 1s หรือเมื่อมีการสลับสถานะ
    if (DEBUG_ULTRA && (millis() - lastUltraLogMs >= 1000 || newNear != nearState)) {
      Serial.print("[US] cm=");
      Serial.printf("%.1f", cm);
      Serial.print(" near=");
      Serial.println(newNear ? 1 : 0);
      lastUltraLogMs = millis();
    }

    if (newNear != nearState) {
      mySerial.println(newNear ? "NEAR" : "FAR");  // แจ้ง ODROID ถ้าต่อ UART
      nearState = newNear;
      if (newNear) lastNearSeenMs = millis();  // รีเฟรชเวลาเมื่อเห็นคน
    } else {
      if (newNear) lastNearSeenMs = millis();  // ยังเห็นคนอยู่
    }
  }

  // ไม่มี NEAR ต่อเนื่องครบ 5s → หลับ
  if (!nearState && (millis() - lastNearSeenMs >= NO_NEAR_SLEEP_MS)) {
    Serial.println("No NEAR (valid) for 5s -> Deep-sleep");
    goDeepSleepNow();
  }
}

// ---------- Setup / Loop ----------
#include "driver/rtc_io.h"
#include "esp_system.h"

// ===== [ADD TFT] TJpg_Decoder callback วาดบล็อกลงจอ =====
static uint16_t *lineBuf = nullptr;

// ★★ แทนที่ฟังก์ชันเดิมอันนี้ทั้งก้อน ★★
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height() || x >= tft.width()) return 0;

  if (!lineBuf) lineBuf = (uint16_t*)heap_caps_malloc(w * sizeof(uint16_t), MALLOC_CAP_8BIT);
  if (!lineBuf) return 0;

  for (uint16_t row = 0; row < h; row++) {
    memcpy(lineBuf, bitmap + row * w, w * sizeof(uint16_t));
    tft.pushImage(x, y + row, w, 1, lineBuf);
  }
  return 1;
}

// ===== [ADD TFT] วาด JPG ให้ “พอดีจอและอยู่กึ่งกลาง” =====
bool drawJpgCenteredFromSD(const String& path) {
  uint16_t jpgW, jpgH;
  if (!TJpgDec.getJpgSize(&jpgW, &jpgH, path.c_str())) return false;

  uint16_t scrW = tft.width(), scrH = tft.height();
  float sx = (float)scrW / jpgW;
  float sy = (float)scrH / jpgH;
  float s  = min(sx, sy);

  // TJpgDec สเกลได้แค่ 1/2/4/8 (downscale เท่านั้น)
  uint8_t scale = 1;
  if (s <= 0.125f) scale = 8;
  else if (s <= 0.25f) scale = 4;
  else if (s <= 0.5f) scale = 2;
  else scale = 1;

  TJpgDec.setJpgScale(scale);

  uint16_t dw = jpgW / scale;
  uint16_t dh = jpgH / scale;
  int16_t  ox = (scrW > dw) ? (scrW - dw) / 2 : 0;
  int16_t  oy = (scrH > dh) ? (scrH - dh) / 2 : 0;

  // ก่อนแตะ SD ต้องปล่อย TFT CS สูง และ RFID CS สูง กันบัสชน
  spi_idle_all();
  digitalWrite(SD_CS, LOW);            // เลือก SD
  tft.endWrite();                      // เผื่อ TFT_eSPI ค้าง bus

  tft.fillScreen(TFT_BLACK);
  bool ok = TJpgDec.drawJpg(ox, oy, path.c_str());

  digitalWrite(SD_CS, HIGH);           // ปล่อย SD
  return ok;
}

// ===== [ADD TFT] ฟังก์ชันแสดงรูปตามเบอร์ผู้สมัคร =====
void showCandidateJpg(uint8_t n) {
  // รองรับทั้ง .jpg และ .JPG
  String path = "/" + String(n) + ".jpg";
  if (!SD.exists(path)) {
    String alt = "/" + String(n) + ".JPG";
    if (SD.exists(alt)) path = alt;
  }
  bool ok = drawJpgCenteredFromSD(path);
  if (!ok) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Missing or bad:", 8, 96, 2);
    tft.drawString(path, 8, 114, 2);
  }
}

// ===== [ADD TFT] หน้ารอ/เคลียร์ =====
void showIdleScreen(const char* msg = "Ready") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(msg, 10, 10, 2);
}

void setup() {
  rtc_gpio_hold_dis((gpio_num_t)WAKE_PIN);
  pinMode(WAKE_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(WAKE_PIN), WAKE_isr, CHANGE);

  Serial.begin(115200);
  Serial.setTimeout(200);
  mySerial.setTimeout(200);
  pinMode(SAFE_PIN, INPUT_PULLUP);
  SAFE_MODE = (digitalRead(SAFE_PIN) == LOW);
  Serial.println();
  Serial.println("=== BOOT ===");
  Serial.printf("SAFE_MODE=%d\n", SAFE_MODE);
  Serial.flush();

  Wire.begin();

  // UART2 สำหรับคุยบอร์ดลูกโซ่
  mySerial.begin(9600, SERIAL_8N1, 16, 17);  // RX=16, TX=17

  // ===== [ADD] เริ่ม SPI ก่อนแตะ SD/TFT/RFID =====
  SPI.begin();                              // [MOVE UP] ต้องมาก่อน SD.begin()

  // ===== [ADD] ดันทุก CS เป็น HIGH กัน SPI ชนตั้งแต่บูต =====
  spi_idle_all();                           // ตั้ง TFT_CS/SS_PIN/SD_CS เป็น OUTPUT+HIGH

  // ===== [ADD TFT] Init TFT =====
  tft.init();
  tft.setRotation(1);                       // 1 = แนวนอน 320x240
  showIdleScreen("Mounting SD...");

  // ===== [ADD TFT] ตั้ง callback JPEG =====
  TJpgDec.setCallback(tft_output);

  // ===== [ADD] Mount SD (แชร์ SPI) =====
  spi_idle_all();                           // ปล่อยทุก CS ให้ว่างก่อนแตะ SD
  digitalWrite(SD_CS, HIGH);                // ย้ำว่าปล่อย SD
  if (!SD.begin(SD_CS)) {                   // ใช้ VSPI เดิม
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD mount failed", 10, 10, 2);
    Serial.println("SD mount failed");
  } else {
    showIdleScreen("SD OK");
  }

  // ===== [ADD] Init RFID หลัง SD เพื่อกันชนบัส =====
  spi_idle_all();
  bus_acquire_for_rfid();
  rfid.PCD_Init();
  bus_release_after_rfid();

  // Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);                 // GPIO34 input-only
  digitalWrite(TRIG_PIN, LOW);
  lastNearSeenMs = millis();

  EEPROM.begin(EEPROM_SIZE);
  if (!headerOK()) {
    Serial.println("Init header...");
    writeHeader();
    for (int i = 0; i < MAX_RECORDS; i++) clearRec(i);
  }

  pinMode(buzzerPin, OUTPUT);
  pinMode(switchPin33, INPUT_PULLUP);
  pinMode(switchPin32, INPUT_PULLUP);
  // pinMode(ledPin, OUTPUT);
  // digitalWrite(ledPin, LOW);

  if (!fingerBegin()) {
    Serial.println("Fingerprint module not found. Check wiring.");
  } else {
    Serial.println("Fingerprint module ready.");
  }

  Serial.printf("MAX_RECORDS=%d, RECORD_SIZE=%d\n", MAX_RECORDS, RECORD_SIZE);

  printBootAndWakeInfo();
  pinMode(WAKE_PIN, INPUT_PULLDOWN);        // กันลอยตอนบูต

  // ==== attach edge logger & dump initial levels (คงเดิม) ====
  attachInterrupt(digitalPinToInterrupt(WAKE_PIN), WAKE_isr, CHANGE);
  dbgPrintWakePin("boot");

  lastUltraLogMs = millis();
}


void loop() {
  int switchReg = digitalRead(switchPin33);
  int switchDel = digitalRead(switchPin32);

  if (switchReg == LOW) {
    // โหมดลงทะเบียน: บัตร + ลายนิ้วมือ (คู่กัน)
    while (digitalRead(switchPin33) == LOW) delay(10);  // รอปล่อยปุ่ม
    registerCardAndFingerprint();
    delay(300);
    return;
  } else if (switchDel == LOW) {
    // โหมดลบเรคคอร์ด (บัตร) + ลบ template ในนิ้ว
    while (digitalRead(switchPin32) == LOW) delay(10);
    deleteCardFlow();
    delay(300);
    return;
  }

  // โหมดใช้งานปกติ: แตะบัตร → ต้องยืนยันนิ้วเจ้าของบัตร
  bool cardReady = false;
  bus_acquire_for_rfid();
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    cardReady = true;
  }
  bus_release_after_rfid();

  if (cardReady) {
    normalScanFlow();
  }

  // pipe ข้อความจากพอร์ตลูกโซ่ (option)
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');

    // ===== Added: Command "SLEEP!" from ODROID =====
    String t = msg;
    t.trim();
    if (t.equalsIgnoreCase("SLEEP!")) {
      mySerial.println("OK SLEEP");
      delay(30);
      goDeepSleepNow();  // เข้าหลับทันที (จะไม่กลับจากฟังก์ชันนี้)
    }

    // ===== [ADD] TFT sync from UNO: SEL:<n>, SEL:CLEAR, CF:<n> =====
    String m = msg;
    m.trim();
    if (m.startsWith("SEL:")) {
      if (m.equalsIgnoreCase("SEL:CLEAR")) {
        // ล้างกลับหน้า idle
        showIdleScreen("Ready");
      } else {
        int n = m.substring(4).toInt();   // หลัง "SEL:"
        if (n >= 0 && n <= 9) {
          showCandidateJpg((uint8_t)n);   // แสดง /n.jpg จาก SD
        } else {
          // เลขนอกช่วง → ขึ้นข้อความเตือนเบา ๆ
          showIdleScreen("Bad SEL");
        }
      }
    } else if (m.startsWith("CF:")) {
      int n = m.substring(3).toInt();     // หลัง "CF:"
      if (n >= 0 && n <= 9) {
        // ยืนยันแล้ว: แสดงรูปเดิมอีกครั้ง (หรือจะทำหน้า Confirm เฉพาะก็ได้)
        showCandidateJpg((uint8_t)n);
      } else {
        showIdleScreen("Bad CF");
      }
    }
    // ===== [END ADD] =====

    Serial.println(msg);  // เดิม (ยังพิมพ์ log ต่อไปตามปกติ)
  }

  // อัลตราโซนิกตัดสินใจหลับ (ของเดิม)
  ultrasonicTickForSleep();

  // Console commands (USB Serial): ULTRA?/U (เดิม) + W?/WTEST (เพิ่ม)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.equalsIgnoreCase("ULTRA?") || cmd.equalsIgnoreCase("U")) {
      float cm = measureDistanceCm();
      bool ns = nearState;
      if (!isnan(cm)) {
        if (!ns && cm <= NEAR_ON_CM) ns = true;
        if (ns && cm >= NEAR_OFF_CM) ns = false;
      }
      Serial.print("[US:NOW] cm=");
      if (isnan(cm)) Serial.print("NaN");
      else Serial.printf("%.1f", cm);
      Serial.print(" near=");
      Serial.println(ns ? 1 : 0);
    } else if (cmd.equalsIgnoreCase("W?")) {
      // แสดงสถานะขาปลุกตอนนี้ (ต้องมีฟังก์ชัน dbgPrintWakePin เพิ่มไว้แล้ว)
      dbgPrintWakePin("now");
    } else if (cmd.equalsIgnoreCase("WTEST")) {
      // กันหลับชั่วคราว 10s แล้วพิมพ์สถานะขาปลุกทุก ~300ms
      uint32_t t0 = millis();
      while (millis() - t0 < 10000) {
        dbgPrintWakePin("probe");
        delay(300);
      }
    }
  }
}

// ===== [ADD] Robust ultrasonic helpers =====
#ifndef PULSEIN_LONG_TIMEOUT_US
#define PULSEIN_LONG_TIMEOUT_US 50000UL  // สำรอง ถ้าไลบรารีเก่า
#endif

// เกณฑ์กรองค่าที่เชื่อถือได้
static const float MIN_VALID_CM = 5.0f;
static const float MAX_VALID_CM = 300.0f;

// อ่าน echo แบบ robust: รอให้ ECHO เป็น LOW ก่อนทุกครั้ง, ใช้ pulseInLong
unsigned long us_read_echo_once_robust() {
  // กันกรณี ECHO ยังค้าง HIGH จากรอบก่อน
  // รอให้ LOW ก่อน (แต่จำกัดเวลา)
  (void)pulseInLong(ECHO_PIN, LOW, 3000UL);

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // วัดช่วง HIGH ของ ECHO
  return pulseInLong(ECHO_PIN, HIGH, US_TIMEOUT);
}

// วัดหลายครั้ง → มัธยฐาน → คัดกรองช่วง valid

// --- prototypes (place near other forward-declares) ---
unsigned long us_read_echo_once_robust();
float measureDistanceCmRobust() {
  unsigned long a = us_read_echo_once_robust();
  delayMicroseconds(150);
  unsigned long b = us_read_echo_once_robust();
  delayMicroseconds(150);
  unsigned long c = us_read_echo_once_robust();

  // sort a<=b<=c
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    auto t = b;
    b = c;
    c = t;
  }
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  unsigned long us = b;
  if (us == 0) return NAN;  // timeout → ไม่เชื่อถือ

  float cm = (float)us / 58.0f;
  if (cm < MIN_VALID_CM || cm > MAX_VALID_CM) return NAN;  // กรองค่าหลอก
  return cm;
}
