#define BLYNK_TEMPLATE_ID "TMPL6G6KsJzqK"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "RUBdFFrRrLJ99YHyTgYN5rew8gfkPzaH"

#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Adafruit_Fingerprint.h>

// ---------- Serial / UART ----------
HardwareSerial mySerial(2);        // UART2 : ใช้คุยกับบอร์ด/จออีกตัว ตามที่คุณใช้อยู่ (TX=17, RX=16 ด้านล่าง)
HardwareSerial FingerSerial(1);    // UART1 : ใช้คุยกับโมดูลลายนิ้วมือ
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerSerial);

// ---------- RFID ----------
#define SS_PIN   5
#define RST_PIN 27
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- I/O ----------
const int EEPROM_SIZE  = 512;
const int buzzerPin    = 12;
const int switchPin33  = 33;  // สวิตช์ Register
const int switchPin32  = 32;  // สวิตช์ Delete
const int ledPin       = 13;

// ---------- Finger UART Pins (ปรับให้ตรงบอร์ดคุณ) ----------
const int FINGER_RX = 26; // ESP32 RX1 pin to sensor TX
const int FINGER_TX = 25; // ESP32 TX1 pin to sensor RX

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
const uint8_t  VERSION = 1;
const int      HDR_SIZE = 16;
const int      UID_HEX_MAX = 16;
const int      RECORD_SIZE = 20;
const int      BASE = HDR_SIZE;
const uint8_t  VALID_FLAG = 0xA5;
const uint8_t  EMPTY_FLAG = 0xFF;
const int      MAX_RECORDS = (EEPROM_SIZE - BASE) / RECORD_SIZE; // ~= 24

// ---------- Utils ----------
struct Rec {
  char     uid[UID_HEX_MAX]; // ไม่รับ '\0' เสมอ ให้เก็บเป็น 16 ชาร์ (ถ้าน้อยกว่าก็ 0x00 padding)
  uint8_t  fp_id;
  uint8_t  voted;            // 0/1
  uint8_t  valid;            // VALID_FLAG หรือ EMPTY_FLAG
  uint8_t  reserved;
};

void eepromWriteBytes(int addr, const uint8_t* data, int len) {
  for (int i=0; i<len; ++i) EEPROM.write(addr+i, data[i]);
}

void eepromReadBytes(int addr, uint8_t* data, int len) {
  for (int i=0; i<len; ++i) data[i] = EEPROM.read(addr+i);
}

void writeHeader() {
  uint8_t hdr[HDR_SIZE] = {0};
  hdr[0] = 'V'; hdr[1] = 'O'; hdr[2] = 'T'; hdr[3] = 'E';
  hdr[4] = VERSION;
  // rest zero
  eepromWriteBytes(0, hdr, HDR_SIZE);
  EEPROM.commit();
}

bool headerOK() {
  uint8_t h[5];
  for (int i=0;i<5;i++) h[i] = EEPROM.read(i);
  return (h[0]=='V' && h[1]=='O' && h[2]=='T' && h[3]=='E' && h[4]==VERSION);
}

int recAddr(int idx) { return BASE + idx*RECORD_SIZE; }

void readRec(int idx, Rec &r) {
  uint8_t buf[RECORD_SIZE];
  eepromReadBytes(recAddr(idx), buf, RECORD_SIZE);
  for (int i=0; i<UID_HEX_MAX; ++i) r.uid[i] = (char)buf[i];
  r.fp_id    = buf[16];
  r.voted    = buf[17];
  r.valid    = buf[18];
  r.reserved = buf[19];
}

void writeRec(int idx, const Rec &r) {
  uint8_t buf[RECORD_SIZE];
  for (int i=0; i<UID_HEX_MAX; ++i) buf[i] = (uint8_t)r.uid[i];
  buf[16] = r.fp_id;
  buf[17] = r.voted;
  buf[18] = r.valid;
  buf[19] = r.reserved;
  eepromWriteBytes(recAddr(idx), buf, RECORD_SIZE);
  EEPROM.commit();
}

void clearRec(int idx) {
  Rec r{};
  for (int i=0;i<UID_HEX_MAX;++i) r.uid[i]=0x00;
  r.fp_id=0; r.voted=0; r.valid=EMPTY_FLAG; r.reserved=0;
  writeRec(idx, r);
}

int findFreeSlot() {
  for (int i=0;i<MAX_RECORDS;++i) {
    Rec r; readRec(i,r);
    if (r.valid!=VALID_FLAG) return i;
  }
  return -1;
}

bool sameUID16(const char a[UID_HEX_MAX], const char b[UID_HEX_MAX]) {
  for (int i=0;i<UID_HEX_MAX;++i) if (a[i]!=b[i]) return false;
  return true;
}

void uidToFixed16(const String &uidHex, char out16[UID_HEX_MAX]) {
  // ตัด/แพดให้ยาว 16 ตัวอักษร
  // (UID 4 ไบต์ => 8 ตัวอักษร, UID 7/10 ไบต์ => 14/20 ตัวอักษร → เก็บ 16 ตัวอักษรแรกพอ)
  for (int i=0;i<UID_HEX_MAX;i++) {
    out16[i] = (i < uidHex.length()) ? uidHex.charAt(i) : 0x00;
  }
}

int findByUID(const String &uidHex) {
  char key[UID_HEX_MAX]; uidToFixed16(uidHex, key);
  for (int i=0;i<MAX_RECORDS;++i) {
    Rec r; readRec(i,r);
    if (r.valid==VALID_FLAG && sameUID16(r.uid, key)) return i;
  }
  return -1;
}

int findByFPID(uint8_t fp) {
  for (int i = 0; i < MAX_RECORDS; ++i) {
    Rec r; readRec(i, r);
    if (r.valid == VALID_FLAG && r.fp_id == fp) return i;
  }
  return -1;
}

// สแกนนิ้วแบบเร็วเพื่อเช็กว่ามีนิ้วนี้อยู่ในฐานแล้วหรือไม่
int quickSearchFingerprint(uint32_t timeout_ms = 10000) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeout_ms) {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { delay(50); continue; }
    if (p != FINGERPRINT_OK) { delay(50); continue; }
    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) { delay(50); continue; }
    p = finger.fingerFastSearch();         // ค้นหาในฐานของเซ็นเซอร์
    if (p == FINGERPRINT_OK) return finger.fingerID;  // พบแล้ว → คืน fp_id เดิม
    else return -1;                        // ไม่พบ → นิ้วใหม่น่าจะยังไม่อยู่ในฐาน
  }
  return -1; // timeout
}


bool setVotedByIndex(int idx, uint8_t v) {
  if (idx<0 || idx>=MAX_RECORDS) return false;
  Rec r; readRec(idx,r);
  if (r.valid!=VALID_FLAG) return false;
  r.voted = v?1:0;
  writeRec(idx,r);
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
    if (p == FINGERPRINT_NOFINGER) { delay(50); continue; }
    if (p == FINGERPRINT_PACKETRECIEVEERR) return p;
    if (p == FINGERPRINT_IMAGEFAIL)       return p;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return p;

  Serial.println("Remove finger");
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(50);

  Serial.println("Place same finger again");
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) { delay(50); continue; }
    if (p == FINGERPRINT_PACKETRECIEVEERR) return p;
    if (p == FINGERPRINT_IMAGEFAIL)       return p;
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) return p;

  p = finger.createModel();
  if (p != FINGERPRINT_OK) return p;

  p = finger.storeModel(fp_id);
  return p; // FINGERPRINT_OK = 0x00
}

int matchFingerprint() {
  // จับภาพ → แปลง → ค้นหา เร็ว
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK)  return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)  return -1;
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK)  return -1;
  return finger.fingerID; // ตำแหน่งที่ match
}

// ---------- App Logic ----------
String readRFIDasHex() {
  // คืนเป็นตัวอักษร hex (ไม่เว้นวรรค), ตัวพิมพ์ใหญ่, ยาวเท่าจำนวน uid.size*2 (สูงสุด ~20 chars)
  String ID="";
  for (byte i=0;i<rfid.uid.size;i++){
    if (rfid.uid.uidByte[i] < 0x10) ID += "0";
    ID += String(rfid.uid.uidByte[i], HEX);
  }
  ID.toUpperCase();
  ID.replace(" ", "");
  return ID;
}

bool storeNewRecord(const String &uidHex, uint8_t fp_id) {
  int slot = findFreeSlot();
  if (slot<0) return false;
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
  if (idx<0) return false;
  Rec r; readRec(idx, r);
  // ลบในโมดูลลายนิ้วมือด้วย
  if (r.fp_id>0) {
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
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) { delay(50); }
  String uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

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
      Rec rExist; readRec(idxExisting, rExist);
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
  bool used[200]; for (int i=0; i<200; i++) used[i] = false;
  for (int i=0; i<MAX_RECORDS; i++) {
    Rec r; readRec(i, r);
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
    tone(buzzerPin, 1600, 120); delay(200); tone(buzzerPin, 1600, 120);
  } else {
    Serial.println("EEPROM full. Cannot store new record.");
    tone(buzzerPin, 500, 500);
    finger.deleteModel(chosen_fp_id);  // roll back
  }
}


void deleteCardFlow() {
  Serial.println("Delete mode... Tap a card to delete");
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    delay(50);
  }
  String uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  int idx = findByUID(uidHex);
  if (idx < 0) {
    Serial.println("Card not found");
    tone(buzzerPin, 600, 300);
    return;
  }

  // โหลดเรคคอร์ดเพื่อรู้ fp_id ของเจ้าของบัตร
  Rec r; readRec(idx, r);

  // ✅ ขั้นตอน “ยืนยันลายนิ้วมือก่อนลบ”
  Serial.printf("Verify fingerprint to delete (expect FP_ID=%d)\n", r.fp_id);
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000) {           // รอสูงสุด 15 วินาที
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
  tone(buzzerPin, 1200, 150); delay(150); tone(buzzerPin, 1200, 150);
}


void normalScanFlow() {
  // แตะบัตร → ตรวจว่าลงทะเบียนหรือยัง → ถ้าลงทะเบียน ต้องสแกนนิ้วให้ "ตรงกับ fp_id" ของบัตรนั้น
  
  Serial.println("Scan card...");

  //if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  mySerial.println("S");
  String uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  int idx = findByUID(uidHex);
  if (idx < 0) {
    Serial.println("Unknown card");
    //mySerial.println("W");
    // ระฆัง + ไฟ
    tone(buzzerPin, 1000, 200);
    digitalWrite(ledPin, HIGH); delay(200); digitalWrite(ledPin, LOW); delay(150);
    tone(buzzerPin, 1000, 200);
    return;
  }

  Rec r; readRec(idx, r);
  // ถ้าใช้ในระบบโหวต: block ถ้า voted=1 แล้ว
  if (r.voted == 1) {
    Serial.println("Already voted for this card holder.");
    mySerial.println("W");
    tone(buzzerPin, 700, 300);
    return;
  }

  Serial.printf("Card OK. Please verify fingerprint (expect FP_ID=%d)\n", r.fp_id);
  // จับนิ้วแล้ว match
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000) { // รอสูงสุด 15 วินาที
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

  // ผ่านเงื่อนไข: บัตร+นิ้ว ตรงกัน → ถือว่าสำเร็จ
  mySerial.println("OK");
  tone(buzzerPin, 1500, 120);
  digitalWrite(ledPin, HIGH); delay(120); digitalWrite(ledPin, LOW);

  // ถ้าเป็นระบบโหวต: mark voted = 1
  setVotedByIndex(idx, 1);
}

// ---------- Setup / Loop ----------
void setup() {
  Wire.begin();
  Serial.begin(9600);

  // UART2 (debug/เชื่อมต่ออุปกรณ์ลูกโซ่ที่คุณใช้อยู่)
  mySerial.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17

  EEPROM.begin(EEPROM_SIZE);
  if (!headerOK()) {
    Serial.println("Init header...");
    writeHeader();
    // เคลียร์ทุกเรคคอร์ด
    for (int i=0;i<MAX_RECORDS;i++) clearRec(i);
  }

  SPI.begin();
  rfid.PCD_Init();

  pinMode(buzzerPin, OUTPUT);
  pinMode(switchPin33, INPUT_PULLUP);
  pinMode(switchPin32, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  if (!fingerBegin()) {
    Serial.println("Fingerprint module not found. Check wiring.");
    // ทำงานต่อได้ แต่ฟีเจอร์นิ้วจะใช้ไม่ได้
  } else {
    Serial.println("Fingerprint module ready.");
  }

  Serial.printf("MAX_RECORDS=%d, RECORD_SIZE=%d\n", MAX_RECORDS, RECORD_SIZE);
}

void loop() {
  int switchReg = digitalRead(switchPin33);
  int switchDel = digitalRead(switchPin32);

  if (switchReg == LOW) {
    // โหมดลงทะเบียน: บัตร + ลายนิ้วมือ (คู่กัน)
    while (digitalRead(switchPin33)==LOW) delay(10); // รอปล่อยปุ่ม
    registerCardAndFingerprint();
    delay(300);
    return;
  }

  else if (switchDel == LOW) {
    // โหมดลบเรคคอร์ด (บัตร) + ลบ template ในนิ้ว
    while (digitalRead(switchPin32)==LOW) delay(10);
    deleteCardFlow();
    delay(300);
    return;
  }

  // โหมดใช้งานปกติ: แตะบัตร → ต้องยืนยันนิ้วเจ้าของบัตร
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    normalScanFlow();
  }

  // pipe ข้อความจากพอร์ตลูกโซ่ (option)
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');
    Serial.println(msg);
  }
}
