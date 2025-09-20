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
HardwareSerial mySerial(2);        // UART2 : ใช้คุยกับบอร์ด/จออีกตัว (TX=17, RX=16)
HardwareSerial FingerSerial(1);    // UART1 : โมดูลลายนิ้วมือ
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerSerial);

// ---------- RFID ----------
#define SS_PIN   5
#define RST_PIN 27
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- I/O ----------
const int EEPROM_SIZE  = 512;
const int buzzerPin    = 12;
const int switchPin33  = 33;  // Register
const int switchPin32  = 32;  // Delete
const int ledPin       = 13;

// ---------- Finger UART Pins ----------
const int FINGER_RX = 26; // ESP32 RX1 pin to sensor TX
const int FINGER_TX = 25; // ESP32 TX1 pin to sensor RX

// ================== ULTRASONIC (HC-SR04) ==================
const int TRIG_PIN = 4;    // ตามที่คุณต่อ
const int ECHO_PIN = 21;   // ตามที่คุณต่อ (ลดระดับแรงดันลง 3.3V แล้ว)

const uint16_t US_INTERVAL_MS = 200;       // วัดทุก ~200ms
const unsigned long US_TIMEOUT_US = 25000;  // pulseIn timeout ~4.3m

// เกณฑ์ NEAR/FAR + ฮิสเทอรีส (ปรับ runtime ได้ด้วยคำสั่ง NEARTHR/FARTHR)
volatile float NEAR_ON_CM  = 20.0;  // เข้า NEAR เมื่อ <= 20 cm
volatile float NEAR_OFF_CM = 30.0;  // กลับ FAR เมื่อ >= 30 cm

bool nearState = false;     // สถานะล่าสุด
uint32_t lastUSms = 0;

// อ่าน 1 ครั้ง
inline unsigned long us_read_echo_once() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return pulseIn(ECHO_PIN, HIGH, US_TIMEOUT_US); // us
}

// อ่าน 3 ครั้ง -> มัธยฐาน (กันสไปค์)
float measureDistanceCm() {
  unsigned long a = us_read_echo_once(); delayMicroseconds(150);
  unsigned long b = us_read_echo_once(); delayMicroseconds(150);
  unsigned long c = us_read_echo_once();
  // sort a<=b<=c
  if (a>b){ auto t=a; a=b; b=t; }
  if (b>c){ auto t=b; b=c; c=t; }
  if (a>b){ auto t=a; a=b; b=t; }
  unsigned long us = b;
  if (us == 0) return NAN;
  return (float)us / 58.0f; // ~ cm
}

// ส่งสถานะไปยังทั้งสองพอร์ต
void publishUltra(float cm, bool near, bool onlyChange=true) {
  static bool last = false;
  if (!onlyChange || near != last) {
    last = near;
    mySerial.println(near ? "NEAR" : "FAR");            // → ให้ Odroid/AI รับง่าย
  }
  // DEBUG บน USB Serial เสมอ (1 บรรทัดสั้นๆ)
  Serial.print("ULTRA cm=");
  if (isnan(cm)) Serial.print("NaN");
  else Serial.printf("%.1f", cm);
  Serial.print(" near=");
  Serial.println(near ? 1 : 0);
}

void ultrasonicTask() {
  if (millis() - lastUSms < US_INTERVAL_MS) return;
  lastUSms = millis();

  float cm = measureDistanceCm();
  if (isnan(cm)) {
    // timeout: ไม่อัพเดตสถานะ แต่พิมพ์ดีบัก
    Serial.println("ULTRA cm=NaN near=?");
    return;
  }

  bool newNear = nearState;
  if (!nearState && cm <= NEAR_ON_CM)  newNear = true;
  if ( nearState && cm >= NEAR_OFF_CM) newNear = false;

  publishUltra(cm, newNear, /*onlyChange=*/true);
  nearState = newNear;
}

// =============== โปรโตคอล Serial กับ AI (ขา mySerial) ===============
// คำสั่งเข้า:
//   "ULTRA?"           -> ตอบหนึ่งบรรทัด "ULTRA cm=xx.x near=0/1"
//   "NEARTHR <cm>"     -> ตั้ง NEAR_ON_CM
//   "FARTHR <cm>"      -> ตั้ง NEAR_OFF_CM
// (เผื่ออนาคต AI ปรับ threshold runtime)
void handleMySerialCommand(const String &line) {
  String cmd = line; cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.equalsIgnoreCase("ULTRA?")) {
    float cm = measureDistanceCm();
    bool ns = nearState;
    if (!isnan(cm)) {
      // คำนวณ near ทันทีตามปัจจุบัน (ไม่เปลี่ยนสถานะหลัก)
      if (!ns && cm <= NEAR_ON_CM)  ns = true;
      if ( ns && cm >= NEAR_OFF_CM) ns = false;
    }
    mySerial.print("ULTRA cm=");
    if (isnan(cm)) mySerial.print("NaN");
    else mySerial.printf("%.1f", cm);
    mySerial.print(" near=");
    mySerial.println(ns ? 1 : 0);
    return;
  }

  if (cmd.startsWith("NEARTHR ")) {
    float v = cmd.substring(8).toFloat();
    if (v > 0) {
      NEAR_ON_CM = v;
      mySerial.print("OK NEARTHR=");
      mySerial.println(NEAR_ON_CM, 1);
    } else {
      mySerial.println("ERR");
    }
    return;
  }

  if (cmd.startsWith("FARTHR ")) {
    float v = cmd.substring(7).toFloat();
    if (v > 0) {
      NEAR_OFF_CM = v;
      mySerial.print("OK FARTHR=");
      mySerial.println(NEAR_OFF_CM, 1);
    } else {
      mySerial.println("ERR");
    }
    return;
  }

  // อื่นๆ -> ท่อไป Serial หลักเพื่อดีบัก
  Serial.print("mySerial> "); Serial.println(cmd);
}

// ---------- Durable Storage Layout (EEPROM) ----------
const uint32_t MAGIC = 0x564F5445UL;  // 'VOTE'
const uint8_t  VERSION = 1;
const int      HDR_SIZE = 16;
const int      UID_HEX_MAX = 16;
const int      RECORD_SIZE = 20;
const int      BASE = HDR_SIZE;
const uint8_t  VALID_FLAG = 0xA5;
const uint8_t  EMPTY_FLAG = 0xFF;
const int      MAX_RECORDS = (EEPROM_SIZE - BASE) / RECORD_SIZE; // ~= 24

struct Rec {
  char     uid[UID_HEX_MAX];
  uint8_t  fp_id;
  uint8_t  voted;
  uint8_t  valid;
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
  for (int i=0;i<UID_HEX_MAX;i++) out16[i] = (i < uidHex.length()) ? uidHex.charAt(i) : 0x00;
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

// ---------- Fingerprint helpers ----------
bool fingerBegin() {
  FingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  finger.begin(57600);
  delay(200);
  return finger.verifyPassword();
}
int quickSearchFingerprint(uint32_t timeout_ms = 10000) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeout_ms) {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { delay(50); continue; }
    if (p != FINGERPRINT_OK) { delay(50); continue; }
    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) { delay(50); continue; }
    p = finger.fingerFastSearch();
    if (p == FINGERPRINT_OK) return finger.fingerID;
    else return -1;
  }
  return -1;
}
bool setVotedByIndex(int idx, uint8_t v) {
  if (idx<0 || idx>=MAX_RECORDS) return false;
  Rec r; readRec(idx,r);
  if (r.valid!=VALID_FLAG) return false;
  r.voted = v?1:0;
  writeRec(idx,r);
  return true;
}
int enrollFingerprint(uint8_t fp_id) {
  int p = -1;
  Serial.printf("Enroll FP id=%d : place finger\n", fp_id);
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) { delay(50); continue; }
    if (p == FINGERPRINT_PACKETRECIEVEERR) return p;
    if (p == FINGERPRINT_IMAGEFAIL)       return p;
  }
  p = finger.image2Tz(1); if (p != FINGERPRINT_OK) return p;
  Serial.println("Remove finger");
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(50);
  Serial.println("Place same finger again");
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) { delay(50); continue; }
    if (p == FINGERPRINT_PACKETRECIEVEERR) return p;
    if (p == FINGERPRINT_IMAGEFAIL)       return p;
  }
  p = finger.image2Tz(2); if (p != FINGERPRINT_OK) return p;
  p = finger.createModel(); if (p != FINGERPRINT_OK) return p;
  p = finger.storeModel(fp_id);
  return p; // FINGERPRINT_OK = 0x00
}
int matchFingerprint() {
  uint8_t p = finger.getImage(); if (p != FINGERPRINT_OK)  return -1;
  p = finger.image2Tz();         if (p != FINGERPRINT_OK)  return -1;
  p = finger.fingerFastSearch(); if (p != FINGERPRINT_OK)  return -1;
  return finger.fingerID;
}

// ---------- App Logic ----------
String readRFIDasHex() {
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
  if (r.fp_id>0) finger.deleteModel(r.fp_id);
  clearRec(idx);
  return true;
}

void registerCardAndFingerprint() {
  mySerial.println("regis");
  Serial.println("Registration mode... Tap a new card");
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) { delay(50); }
  String uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (findByUID(uidHex) >= 0) {
    Serial.println("This card is already registered.");
    tone(buzzerPin, 1200, 150);
    return;
  }

  Serial.println("Place finger to check duplication...");
  int existing_fp = quickSearchFingerprint(10000);
  if (existing_fp >= 0) {
    int idxExisting = findByFPID(existing_fp);
    if (idxExisting >= 0) {
      Rec rExist; readRec(idxExisting, rExist);
      Serial.printf("Duplicate finger detected! Already linked to another card (FP_ID=%d). Abort.\n", existing_fp);
      tone(buzzerPin, 600, 400);
      return;
    } else {
      Serial.printf("Found stale FP template (id=%d) without EEPROM record. Deleting stale template.\n", existing_fp);
      finger.deleteModel(existing_fp);
    }
  }

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
    finger.deleteModel(chosen_fp_id);
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

  Rec r; readRec(idx, r);
  Serial.printf("Verify fingerprint to delete (expect FP_ID=%d)\n", r.fp_id);
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000) {
    matched = matchFingerprint();
    if (matched >= 0) break;
    delay(50);
  }
  if (matched < 0 || matched != r.fp_id) {
    Serial.println("Fingerprint verify failed / timeout. Abort delete.");
    tone(buzzerPin, 600, 400);
    return;
  }

  if (r.fp_id > 0) {
    uint8_t p = finger.deleteModel(r.fp_id);
    if (p != FINGERPRINT_OK) {
      Serial.printf("Delete template failed (code=%d). Continue to clear record.\n", p);
    }
  }

  clearRec(idx);
  Serial.println("Card + Fingerprint deleted");
  tone(buzzerPin, 1200, 150); delay(150); tone(buzzerPin, 1200, 150);
}

void normalScanFlow() {
  Serial.println("Scan card...");
  mySerial.println("S");
  String uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  int idx = findByUID(uidHex);
  if (idx < 0) {
    Serial.println("Unknown card");
    tone(buzzerPin, 1000, 200);
    digitalWrite(ledPin, HIGH); delay(200); digitalWrite(ledPin, LOW); delay(150);
    tone(buzzerPin, 1000, 200);
    return;
  }

  Rec r; readRec(idx, r);
  if (r.voted == 1) {
    Serial.println("Already voted for this card holder.");
    mySerial.println("W");
    tone(buzzerPin, 700, 300);
    return;
  }

  Serial.printf("Card OK. Please verify fingerprint (expect FP_ID=%d)\n", r.fp_id);
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000) {
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

  mySerial.println("OK");
  tone(buzzerPin, 1500, 120);
  digitalWrite(ledPin, HIGH); delay(120); digitalWrite(ledPin, LOW);
  setVotedByIndex(idx, 1);
}

// ---------- Setup / Loop ----------
void setup() {
  Wire.begin();
  Serial.begin(9600);

  // UART2
  mySerial.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17

  EEPROM.begin(EEPROM_SIZE);
  if (!headerOK()) {
    Serial.println("Init header...");
    writeHeader();
    for (int i=0;i<MAX_RECORDS;i++) clearRec(i);
  }

  SPI.begin();
  rfid.PCD_Init();

  pinMode(buzzerPin, OUTPUT);
  pinMode(switchPin33, INPUT_PULLUP);
  pinMode(switchPin32, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  if (!fingerBegin()) {
    Serial.println("Fingerprint module not found. Check wiring.");
  } else {
    Serial.println("Fingerprint module ready.");
  }

  Serial.printf("MAX_RECORDS=%d, RECORD_SIZE=%d\n", MAX_RECORDS, RECORD_SIZE);
  Serial.println("Ultrasonic ready.");
}

void loop() {
  // ปุ่มโหมด
  int switchReg = digitalRead(switchPin33);
  int switchDel = digitalRead(switchPin32);

  if (switchReg == LOW) {
    while (digitalRead(switchPin33)==LOW) delay(10);
    registerCardAndFingerprint();
    delay(300);
    return;
  } else if (switchDel == LOW) {
    while (digitalRead(switchPin32)==LOW) delay(10);
    deleteCardFlow();
    delay(300);
    return;
  }

  // โหมดปกติ: RFID -> Fingerprint
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    normalScanFlow();
  }

  // ===== Ultrasonic NEAR/FAR loop (ไม่บล็อก) =====
  ultrasonicTask();

  // รับคำสั่งจาก mySerial (ฝั่ง AI/คอม)
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');
    handleMySerialCommand(msg);
  }
}