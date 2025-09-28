/*
  UNO + LCD 20x4 I2C (HW-061) + Keypad 4x4 via PCF8574 (I2C) + Buzzer "Arcade" SFX
  - 0..9 เลือก, '*'=ล้าง (เสียงลง), '#'=ยืนยัน (แฟนแฟร์ 4 โน้ต)
  - A/B/C/D = คลิกโทนสูง
  - ยืนยันแล้วส่ง "CF:X" ทาง Serial (ไป ESP32/PC)

  Wiring:
    I2C: A4=SDA, A5=SCL → LCD(0x27/0x3F) + PCF8574(keypad=0x20)
    Buzzer (2 ขา):
      Passive piezo: + → D5 (ผ่าน R 100–220Ω แนะนำ), − → GND
      Active buzzer : + → D5 (ตั้ง BUZZER_PASSIVE เป็น 0),       − → GND
    Speaker (TMRpcm): D9 → ขา + ของลำโพง (มี R 100–220Ω), − → GND
    SD: CS=4 (ตามบอร์ด SD card module)
*/

// =======================
// 1) Includes & Libraries
// =======================
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
#include <TMRpcm.h>
#include <RTClib.h>

// =======================
// 2) Hardware Config
// =======================
#define ESP_INT_PIN 3       // INT1 (D3) จาก ESP32
#define SD_ChipSelectPin 4  // SD CS
#define BUZZER_PIN 5        // D5
#define BUZZER_PASSIVE 1    // 1=Passive piezo (tone/noTone), 0=Active buzzer (on/off)
#define LCD_ADDR 0x27
#define KEYPAD_ADDR 0x20

// =======================
// 3) System & App Timers
// =======================
#define IDLE_SLEEP_MS 600000000UL    // ว่าง 60s -> หลับลึก (คงค่าเดิมตามไฟล์ต้นฉบับ)
const unsigned long interval = 100;  // กันสั่งเล่นถี่เกิน (มิลลิวินาที)

// =======================
// 4) Globals
// =======================
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
TMRpcm tmrpcm;
RTC_DS1307 rtc;

unsigned long lastActivityMs = 0;
unsigned long lastPlayTime = 0;


volatile bool wokeFromEsp = false;
volatile bool showingTally = false;

bool waitRToExit = false;
bool waitTToExit = false;

// =======================
// 5) Keypad Mapping
// =======================
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 0, 1, 2, 3 };  // PCF8574 P0..P3
byte colPins[COLS] = { 4, 5, 6, 7 };  // PCF8574 P4..P7
Keypad_I2C kpd(makeKeymap(keys), rowPins, colPins, ROWS, COLS, KEYPAD_ADDR);

// =======================
// 6) App State Machine
// =======================
enum AdminAction { ACT_NONE,
                   ACT_REG,
                   ACT_TALLY,
                   ACT_CLEAR };
enum Page { PAGE_WAIT,
            PAGE_VOTE,
            PAGE_CONFIRM,
            PAGE_REG_PASS };

AdminAction pendingAction = ACT_NONE;
Page page = PAGE_WAIT;

bool canVote = false;            // true เมื่อได้รับ 'O' จาก ESP32
int currentChoice = -1;          // -1=ยังไม่เลือก, 0=งด, 1..9=ผู้สมัคร
unsigned long confirmUntil = 0;  // แสดงหน้า Confirm แบบ non-blocking

// =======================
// 7) EEPROM Vote Tally
// =======================
// โครงสร้าง:
// [0..3]   : MAGIC 'VOTE' (0x564F5445) ไว้เช็คว่ามีการ init แล้ว
// [4..43]  : ตัวนับ 10 ช่อง (0..9) อย่างละ uint32_t → รวม 40 ไบต์
static const uint32_t EE_MAGIC = 0x564F5445UL;  // 'VOTE'
static const int EE_MAGIC_ADDR = 0;
static const int EE_COUNTS_ADDR = 4;  // เริ่มเก็บตัวนับที่นี่
static inline int ee_ofs(uint8_t idx) {
  return EE_COUNTS_ADDR + (idx * 4);
}

// =======================
// 8) UI & Animation State
// =======================
// Spinner
const char spinFrames[4] = { '|', '/', '-', '\\' };
uint8_t spinIdx = 0;
unsigned long lastSpinMs = 0;
const unsigned long SPIN_MS = 160;

// กระพริบกรอบเลข
bool boxOn = true;
unsigned long lastBlinkMs = 0;
const unsigned long BLINK_MS = 380;

// Ready .. จุดต่อท้าย
uint8_t readyDots = 0;
unsigned long lastReadyMs = 0;
const unsigned long READY_MS = 350;

// ตำแหน่ง UI
const uint8_t POS_SPINNER_X = 18, POS_SPINNER_Y = 0;
const uint8_t POS_ARROW_X = 1, POS_ARROW_Y = 2;
const uint8_t POS_HINT_X = 2, POS_HINT_Y = 2;
const uint8_t POS_TITLE_X = 2, POS_TITLE_Y = 1;
const uint8_t BOX_LEFT_X = 10;
const uint8_t BOX_TOP_Y = 0;

// =======================
// 9) Admin PIN State
// =======================
bool fregis = false;
bool regisstatus = false;
bool passMsgShown = false;
static const uint8_t PIN_MAX = 8;
char passBuf[PIN_MAX + 1] = { 0 };  // 4 หลัก + '\0'
uint8_t passLen = 0;                // 0..4
const char savedPass[] = "1234";

// =======================
// 10) Forward Declarations
// =======================
// (ให้จัดลำดับเรียกใช้ได้ชัดเจน โดยไม่ต้องพึ่ง prototype ที่ Arduino สร้างอัตโนมัติ)
inline void noteActivity();
bool playIfIdle(const char* path);
void drawVoteUI_base();
void drawReadyUI_base();
void drawConfirmUI();
void animateDuringSelect();
void startPass(AdminAction a);
void regispage(char k);
void eeprom_vote_dump();
void eeprom_vote_clear_all();
void onConfirmVote(int choice);
void afterWake();
void clock_ready_tick(bool forceFirst = false);

// =======================
// 11) EEPROM Vote Helpers
// =======================
void eeprom_vote_init() {
  uint32_t m;
  EEPROM.get(EE_MAGIC_ADDR, m);
  if (m != EE_MAGIC) {
    EEPROM.put(EE_MAGIC_ADDR, EE_MAGIC);
    for (uint8_t i = 0; i < 10; ++i) {
      uint32_t z = 0;
      EEPROM.put(ee_ofs(i), z);
    }
  }
}

uint32_t eeprom_vote_get(uint8_t idx) {
  uint32_t v = 0;
  if (idx < 10) EEPROM.get(ee_ofs(idx), v);
  return v;
}

void eeprom_vote_add(uint8_t idx, uint32_t delta = 1) {
  if (idx >= 10) return;
  uint32_t v = 0;
  EEPROM.get(ee_ofs(idx), v);
  v += delta;
  EEPROM.put(ee_ofs(idx), v);  // EEPROM.put ลด wear
}

void eeprom_vote_dump() {
  showingTally = true;
  Serial.println(F("[VOTE TALLY]"));

  lcd.clear();

  // หาแชมป์
  uint8_t winner = 0;
  uint32_t maxv = 0;
  bool tie = false;

  for (uint8_t i = 0; i < 10; ++i) {
    uint32_t v = eeprom_vote_get(i);

    // --- Serial ---
    Serial.print(F("No."));
    Serial.print(i);
    Serial.print(F(" = "));
    Serial.println(v);

    // --- หา winner ---
    if (i == 0 || v > maxv) {
      maxv = v;
      winner = i;
      tie = false;
    } else if (v == maxv) {
      tie = true;  // คะแนนเท่ากันกับ max ปัจจุบัน
    }

    // --- LCD: วาง 3 คอลัมน์ต่อแถว (0,7,14) และ N9 อยู่แถวล่างซ้าย ---
    if (!tmrpcm.isPlaying()) {
      uint8_t row = (i <= 8) ? (i / 3) : 3;
      uint8_t col = (i <= 8) ? (i % 3) : 0;
      uint8_t x = (col == 0) ? 0 : (col == 1) ? 7
                                              : 14;

      lcd.setCursor(x, row);
      lcd.print('N');
      lcd.print(i);
      lcd.print('=');
      lcd.print(v);
      lcd.print(' ');
      lcd.print(' ');
    }
  }

  // แสดง Winner ข้างๆ N9 (คอลัมน์ 7 ของแถวล่าง)
  // ถ้ามีคะแนนเสมอหลายหมายเลข จะแสดง "Tie"
  lcd.setCursor(7, 3);  // ช่องกลางของแถวสุดท้าย
  lcd.print(F("Winner="));
  if (tie) {
    lcd.print(F("Tie"));
  } else {
    lcd.print(winner);
  }
}


void eeprom_vote_clear_all() {
  for (uint8_t i = 0; i < 10; ++i) {
    uint32_t z = 0;
    EEPROM.put(ee_ofs(i), z);
  }
  Serial.println(F("TALLY CLEARED"));
}

// =======================
// 12) LCD Icons & Frame
// =======================
byte I_TL[8] = { B11100, B10000, B10000, B10000, B10000, B10000, B10000, B10000 };
byte I_TR[8] = { B00111, B00001, B00001, B00001, B00001, B00001, B00001, B00001 };
byte I_BL[8] = { B10000, B10000, B10000, B10000, B10000, B10000, B10000, B11100 };
byte I_BR[8] = { B00001, B00001, B00001, B00001, B00001, B00001, B00001, B00111 };
byte I_H[8] = { B11111, B00000, B00000, B00000, B00000, B00000, B00000, B11111 };
byte I_V[8] = { B10001, B10001, B10001, B10001, B10001, B10001, B10001, B10001 };
byte I_AR[8] = { B00100, B00110, B00111, B11111, B00111, B00110, B00100, B00000 };   // ▶
byte I_CHK[8] = { B00000, B00001, B00011, B10110, B11100, B01000, B00000, B00000 };  // ✓

void loadIcons() {
  lcd.createChar(0, I_TL);
  lcd.createChar(1, I_TR);
  lcd.createChar(2, I_BL);
  lcd.createChar(3, I_BR);
  lcd.createChar(4, I_H);
  lcd.createChar(5, I_V);
  lcd.createChar(6, I_AR);
  lcd.createChar(7, I_CHK);
}

void drawFrame() {
  lcd.clear();
  // มุม
  lcd.setCursor(0, 0);
  lcd.write((byte)0);
  lcd.setCursor(19, 0);
  lcd.write((byte)1);
  lcd.setCursor(0, 3);
  lcd.write((byte)2);
  lcd.setCursor(19, 3);
  lcd.write((byte)3);
  // ขอบบน/ล่าง
  for (int x = 1; x < 19; x++) {
    lcd.setCursor(x, 0);
    lcd.write((byte)4);
    lcd.setCursor(x, 3);
    lcd.write((byte)4);
  }
  // ขอบซ้าย/ขวา
  for (int y = 1; y < 3; y++) {
    lcd.setCursor(0, y);
    lcd.write((byte)5);
    lcd.setCursor(19, y);
    lcd.write((byte)5);
  }
}

// =======================
// 13) UI Helpers
// =======================
inline void lcd2(uint8_t v) {
  lcd.print(v / 10);
  lcd.print(v % 10);
}

void drawChoiceBox(bool show) {
  // เคลียร์พื้นที่ 10..14 x 0..2 ก่อน
  for (uint8_t y = 0; y <= 2; y++) {
    lcd.setCursor(BOX_LEFT_X, BOX_TOP_Y + y);
    lcd.print(F("     "));
  }

  if (currentChoice <= 0) {
    lcd.setCursor(12, 1);
    if (show) lcd.print(F("0"));
    else lcd.print(F("  "));
    return;
  }

  if (show) {
    // วาดกรอบ
    lcd.setCursor(10, 0);
    lcd.write((byte)0);
    for (int x = 11; x <= 13; x++) {
      lcd.setCursor(x, 0);
      lcd.write((byte)4);
    }
    lcd.setCursor(14, 0);
    lcd.write((byte)1);
    lcd.setCursor(10, 1);
    lcd.write((byte)5);
    lcd.setCursor(14, 1);
    lcd.write((byte)5);
    lcd.setCursor(10, 2);
    lcd.write((byte)2);
    for (int x = 11; x <= 13; x++) {
      lcd.setCursor(x, 2);
      lcd.write((byte)4);
    }
    lcd.setCursor(14, 2);
    lcd.write((byte)3);

    lcd.setCursor(12, 1);
    lcd.print(currentChoice);
  } else {
    lcd.setCursor(12, 1);
    lcd.print(currentChoice);
  }
}

void drawVoteUI_base() {
  loadIcons();
  drawFrame();

  // หัวข้อ + arrow + hint
  lcd.setCursor(POS_TITLE_X, POS_TITLE_Y);
  lcd.print(F("Select: 0=Abstain"));
  lcd.setCursor(POS_ARROW_X, POS_ARROW_Y);
  lcd.write((byte)6);  // ▶
  lcd.setCursor(POS_HINT_X, POS_HINT_Y);
  lcd.print(F("#=Confirm  *=Clear"));

  // เคอร์เซอร์กระพริบตรงบรรทัดคำสั่ง
  lcd.setCursor(POS_HINT_X, POS_HINT_Y);
  lcd.blink();

  // ข้อความสถานะ
  if (currentChoice < 0) {
    lcd.setCursor(2, 0);
    lcd.print(F("Choose 1..9     "));
  } else if (currentChoice == 0) {
    lcd.setCursor(2, 0);
    lcd.print(F("Choice: Abstain "));
  } else {
    lcd.setCursor(2, 0);
    lcd.print(F("Choice:         "));
  }
  drawChoiceBox(currentChoice >= 0);

  // รีเซ็ตอนิเมชัน
  spinIdx = 0;
  lastSpinMs = millis();
  boxOn = true;
  lastBlinkMs = millis();
  lcd.setCursor(POS_SPINNER_X, POS_SPINNER_Y);
  lcd.print(spinFrames[spinIdx]);
}

void animateDuringSelect() {
  unsigned long now = millis();
  // spinner
  if (now - lastSpinMs >= SPIN_MS) {
    lastSpinMs = now;
    spinIdx = (spinIdx + 1) & 0x03;
    lcd.setCursor(POS_SPINNER_X, POS_SPINNER_Y);
    lcd.print(spinFrames[spinIdx]);
  }
  // blink กล่องเลข
  if (currentChoice >= 0 && now - lastBlinkMs >= BLINK_MS) {
    lastBlinkMs = now;
    boxOn = !boxOn;
    drawChoiceBox(boxOn);
  }
}

void drawConfirmUI() {
  lcd.noBlink();
  loadIcons();
  drawFrame();
  lcd.setCursor(2, 1);
  lcd.print(F("Confirmed:"));
  if (currentChoice == 0) {
    lcd.setCursor(13, 1);
    lcd.print(F("Abstain"));
  } else {
    lcd.setCursor(13, 1);
    lcd.print(F("No."));
    lcd.print(currentChoice);
  }
  lcd.setCursor(1, 2);
  lcd.write((byte)7);
  lcd.setCursor(18, 2);
  lcd.write((byte)7);
  lcd.setCursor(5, 3);
  lcd.print(F("Saved! Thank you"));
}

// หน้า Ready: แสดงวันที่/เวลา + "Ready to vote"
void clock_ready_tick(bool forceFirst) {
  static uint8_t lastSec = 255;
  if (showingTally) return;
  if (page != PAGE_WAIT) return;
  if (tmrpcm.isPlaying()) return;

  DateTime t = rtc.now();
  if (!forceFirst) {
    if (t.second() == lastSec) return;
  }
  lastSec = t.second();

  // บรรทัด 0: วันที่ → dd/mm/yyyy
  lcd.setCursor(5, 0);
  lcd2(t.day());
  lcd.print('/');
  lcd2(t.month());
  lcd.print('/');
  lcd.print(t.year());
  lcd.print(F("       "));

  // บรรทัด 1: เวลา → hh:mm:ss
  lcd.setCursor(5, 1);
  lcd2(t.hour());
  lcd.print(':');
  lcd2(t.minute());
  lcd.print(':');
  lcd2(t.second());
  lcd.print(F("             "));
}

void drawReadyUI_base() {
  lcd.noBlink();
  lcd.clear();

  // บรรทัด 2: ข้อความสถานะคงที่
  lcd.setCursor(4, 2);
  lcd.print(F("Ready to vote"));

  // เคลียร์บรรทัดวันที่/เวลา (บรรทัด 0 และ 1)
  lcd.setCursor(0, 0);
  lcd.print(F("                    "));
  lcd.setCursor(0, 1);
  lcd.print(F("                    "));

  // อัปเดตนาฬิกาครั้งแรกทันที
  clock_ready_tick(true);
}

// =======================
// 14) Buzzer SFX (Arcade)
// =======================
class BuzzerSFX {
public:
  void init() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    seq = nullptr;
    count = idx = phase = 0;
    phaseEnd = 0;
  }

  void playClick() {
    start(SEQ_CLICK, 1);
  }
  void playClickHi() {
    start(SEQ_CLICK_HI, 1);
  }
  void playBack() {
    start(SEQ_BACK, 2);
  }
  void playError() {
    start(SEQ_ERROR, 3);
  }

  void playFanfare() {
    start(SEQ_FANFARE, 4);
  }

  void update() {
    if (!seq || idx >= count) return;
    unsigned long now = millis();
    const Step& st = seq[idx];

    if (phase == 0) {  // start ON
#if BUZZER_PASSIVE
      if (st.freq == 0) noTone(BUZZER_PIN);
      else tone(BUZZER_PIN, st.freq);
#else
      digitalWrite(BUZZER_PIN, HIGH);
#endif
      phase = 1;
      phaseEnd = now + st.dur;
      return;
    }
    if (phase == 1 && (long)(now - phaseEnd) >= 0) {  // end ON -> GAP
#if BUZZER_PASSIVE
      noTone(BUZZER_PIN);
#else
      digitalWrite(BUZZER_PIN, LOW);
#endif
      phase = 2;
      phaseEnd = now + st.gap;
      return;
    }
    if (phase == 2 && (long)(now - phaseEnd) >= 0) {  // next
      idx++;
      phase = 0;
      if (idx >= count) { seq = nullptr; }
    }
  }

private:
  struct Step {
    uint16_t freq;
    uint16_t dur;
    uint16_t gap;
  };  // ms

  enum {  // โน้ต (Hz)
    N_C5 = 523,
    N_D5 = 587,
    N_E5 = 659,
    N_F5 = 698,
    N_G5 = 784,
    N_A5 = 880,
    N_B5 = 988,
    N_C6 = 1047,
    N_D6 = 1175,
    N_E6 = 1319,
    N_F6 = 1397,
    N_G6 = 1568,
    N_A6 = 1760
  };

  static const Step SEQ_BOOT[4];
  static const Step SEQ_CLICK[1];
  static const Step SEQ_CLICK_HI[1];
  static const Step SEQ_BACK[2];
  static const Step SEQ_ERROR[3];
  static const Step SEQ_CONFIRM[2];
  static const Step SEQ_FANFARE[4];

  const Step* seq;
  uint8_t count, idx, phase;
  unsigned long phaseEnd;

  void start(const Step* s, uint8_t n) {
    // ถ้ากำลังเล่น WAV อยู่ → ข้ามไม่เปิดบัซเซอร์ (กันชนกับ TMRpcm)  [คงตรรกะเดิม: ปลดคอมเมนต์ได้ตามต้องการ]
    // if (tmrpcm.isPlaying()) return;
    seq = s;
    count = n;
    idx = 0;
    phase = 0;
    phaseEnd = 0;
  }
};

// นิยามแพตเทิร์นเสียง
const BuzzerSFX::Step BuzzerSFX::SEQ_BOOT[4] = { { BuzzerSFX::N_C5, 90, 15 }, { BuzzerSFX::N_E5, 90, 15 }, { BuzzerSFX::N_G5, 110, 15 }, { BuzzerSFX::N_C6, 150, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_CLICK[1] = { { 1500, 25, 8 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_CLICK_HI[1] = { { 1900, 18, 5 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_BACK[2] = { { BuzzerSFX::N_E5, 70, 15 }, { BuzzerSFX::N_C5, 110, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_ERROR[3] = { { 400, 140, 35 }, { 320, 140, 35 }, { 250, 180, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_CONFIRM[2] = { { BuzzerSFX::N_A5, 120, 25 }, { BuzzerSFX::N_C6, 140, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_FANFARE[4] = { { BuzzerSFX::N_G5, 90, 15 }, { BuzzerSFX::N_B5, 90, 15 }, { BuzzerSFX::N_D6, 110, 15 }, { BuzzerSFX::N_G6, 160, 0 } };

BuzzerSFX buzzer;

// =======================
// 15) Serial & Preview
// =======================
inline void noteActivity() {
  lastActivityMs = millis();
}

void sendPreview() {
  if (currentChoice < 0) {
    Serial.println(F("SEL:CLEAR"));
  } else {
    Serial.print(F("SEL:"));
    Serial.println(currentChoice);  // 0..9
  }
  noteActivity();
}

// =======================
// 16) Voting & Admin Flow
// =======================
void onConfirmVote(int choice) {
  if (choice >= 0 && choice <= 9) {
    eeprom_vote_add((uint8_t)choice, 1);
  }
  Serial.print(F("CF:"));
  Serial.println(choice);
  buzzer.playFanfare();
  noteActivity();
}

bool playIfIdle(const char* path) {
  if (tmrpcm.isPlaying()) return false;
  unsigned long now = millis();
  if (now - lastPlayTime < interval) return false;
  tmrpcm.play((char*)path);
  lastPlayTime = now;
  return true;
}

// vote() เรียกเมื่อมีคีย์ในหน้าโหวต/คอนเฟิร์ม
void vote(char k) {
  if (!canVote) {
    buzzer.playError();
    return;
  }

  if (k >= '0' && k <= '9') {
    currentChoice = k - '0';
    drawVoteUI_base();
    buzzer.playClick();
    Serial.write(currentChoice);
    sendPreview();
  } else if (k == '*') {
    if (currentChoice >= 0) buzzer.playBack();
    currentChoice = -1;
    drawVoteUI_base();
    sendPreview();
  } else if (k == '#') {
    if (currentChoice >= 0) {
      page = PAGE_CONFIRM;
      drawConfirmUI();
      onConfirmVote(currentChoice);       // ส่ง CF:<n>
      confirmUntil = millis() + 10000UL;  // คงเดิม
      currentChoice = -1;
      canVote = false;
      //.stopPlayback();
      tmrpcm.play("fv.wav");
    } else {
      buzzer.playError();
    }
  }
}

// Admin PIN helpers
void lcdClearLine(uint8_t y) {
  lcd.setCursor(0, y);
  lcd.print(F("                    "));  // 20 ช่อง
}

void printMaskedAt(uint8_t x, uint8_t y, uint8_t len) {
  lcd.setCursor(x, y);
  for (uint8_t i = 0; i < PIN_MAX; i++) lcd.write(i < len ? '*' : ' ');
}

void regispage(char k) {
  if (!k) return;

  if (k >= '0' && k <= '9') {
    if (passMsgShown) {
      lcdClearLine(3);
      passMsgShown = false;
    }
    if (passLen < PIN_MAX) {
      buzzer.playClick();
      passBuf[passLen++] = k;
      passBuf[passLen] = '\0';
      printMaskedAt(4, 2, passLen);
    }
  } else if (k == '*') {
    if (passMsgShown) {
      lcdClearLine(3);
      passMsgShown = false;
    }
    if (passLen > 0) {
      buzzer.playBack();
      passLen--;
      passBuf[passLen] = '\0';
      printMaskedAt(4, 2, passLen);
    }
  } else if (k == '#') {

    if (strcmp(passBuf, savedPass) == 0) {
      buzzer.playFanfare();
      // รหัสถูก → ทำงานตาม pendingAction
      switch (pendingAction) {
        case ACT_REG:
          lcd.clear();
          Serial.print(F("R"));
          lcd.setCursor(2, 0);
          lcd.print(F("Registration OK"));
          page = PAGE_REG_PASS;
          fregis = true;
          break;

        case ACT_TALLY:
          eeprom_vote_dump();  // แสดงผลบน LCD/Serial
          page = PAGE_REG_PASS;
          fregis = true;
          break;

        case ACT_CLEAR:
          eeprom_vote_clear_all();
          lcd.clear();
          lcd.setCursor(2, 0);
          lcd.print(F("Tally cleared"));
          delay(800);
          break;

        default: break;
      }

      // ออกจากโหมดใส่รหัส กลับหน้า READY (ยกเว้นกรณีค้างรอ R/T รอบถัดไป)
      if (!waitRToExit && !waitTToExit) {
        pendingAction = ACT_NONE;
        fregis = false;
        canVote = false;
        page = PAGE_WAIT;
        drawReadyUI_base();
      }
    } else {
      // รหัสผิด
      buzzer.playError();
      lcd.setCursor(0, 3);
      lcd.print(F("   Wrong Password  "));
      passLen = 0;
      passMsgShown = true;
      passBuf[0] = '\0';
      printMaskedAt(4, 2, passLen);
    }
  }
}

void startPass(AdminAction a) {
  pendingAction = a;
  fregis = true;
  passLen = 0;
  passBuf[0] = '\0';
  page = PAGE_REG_PASS;

  lcd.noBlink();
  lcd.clear();
  lcd.setCursor(2, 0);
  switch (a) {
    case ACT_REG: lcd.print(F("Registration (PIN)")); break;
    case ACT_TALLY: lcd.print(F("Show Tally (PIN)")); break;
    case ACT_CLEAR: lcd.print(F("Clear Tally (PIN)")); break;
    default: lcd.print(F("Admin (PIN)")); break;
  }
  lcd.setCursor(2, 1);
  lcd.print(F("Enter Password :"));
  printMaskedAt(4, 2, passLen);
}

// =======================
// 17) Sleep & Watchdog
// =======================
void isrEsp() {
  wokeFromEsp = true;
}

void goSleepPowerDown() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  noInterrupts();
  EIFR = bit(INTF1);  // เคลียร์ธง INT1
#if defined(BODS) && defined(BODSE)
  MCUCR |= _BV(BODS) | _BV(BODSE);
  MCUCR = (MCUCR & ~_BV(BODSE)) | _BV(BODS);
#endif
  interrupts();

  sleep_cpu();  // หลับจริง
  sleep_disable();
}

void prepareBeforeSleep() {
  lcd.noBacklight();
  if (tmrpcm.isPlaying()) tmrpcm.stopPlayback();
  delay(5);
}

void afterWake() {
  lcd.backlight();
  page = canVote ? PAGE_VOTE : PAGE_WAIT;
  if (page == PAGE_VOTE) drawVoteUI_base();
  else drawReadyUI_base();

  delay(20);
  while (Serial.available()) { String s = Serial.readStringUntil('\n'); }
  noteActivity();
}

void wdt_sanity_boot() {
  MCUSR = 0;
  wdt_disable();
}
[[noreturn]] void reset_via_wdt() {
  wdt_enable(WDTO_15MS);
  while (true) {}
}

// =======================
// 18) SD Init (with retry)
// =======================
const uint8_t SD_RETRIES = 3;
void initSD_orReset() {
  for (uint8_t i = 0; i < SD_RETRIES; ++i) {
    if (SD.begin(SD_ChipSelectPin)) {
      Serial.println(F("SD init OK"));
      return;
    }
    Serial.println(F("SD init failed, retrying..."));
    delay(200);
  }
  Serial.println(F("SD init failed -> resetting via WDT"));
  reset_via_wdt();
}

// =======================
// 19) Setup / Loop
// =======================
void setup() {
  rtc.begin();
  rtc.writeSqwPinMode(DS1307_OFF);
  eeprom_vote_init();

  /*const DateTime buildTime(F(__DATE__), F(__TIME__));

  // ถ้า RTC ยังไม่เดิน หรือเวลาเพี้ยนมาก (> 1 วัน) ให้ตั้งใหม่
  if (!rtc.isrunning()) {
    rtc.adjust(buildTime);
  } else {
    DateTime now = rtc.now();
    uint32_t diff = (now.unixtime() > buildTime.unixtime())
                    ? now.unixtime() - buildTime.unixtime()
                    : buildTime.unixtime() - now.unixtime();
    if (diff > 24UL * 60UL * 60UL) {         // เพี้ยนเกิน 1 วัน → ตั้งใหม่
      rtc.adjust(buildTime);
    }
  }*/

  wdt_sanity_boot();
  pinMode(10, OUTPUT);  // คงไว้ตามของเดิม
  Serial.begin(9600);

  tmrpcm.speakerPin = 9;  // UNO/Nano → D9
  tmrpcm.setVolume(5);    // 0..7
  tmrpcm.quality(1);
  initSD_orReset();

  Wire.begin();  // UNO: SDA=A4, SCL=A5
  lcd.init();
  lcd.backlight();

  // เสียงเปิดเครื่อง (ซ้ำสองครั้งตามเดิม ถ้าเล่นไม่ทัน)
  tmrpcm.play("sa.wav");
  if (!tmrpcm.isPlaying()) { tmrpcm.play("sa.wav"); }
  delay(1000);

  buzzer.init();

  // เริ่มที่หน้า WAIT (Ready)
  canVote = false;
  currentChoice = -1;
  page = PAGE_WAIT;
  drawReadyUI_base();

  // Keypad
  kpd.begin(makeKeymap(keys));
  kpd.setDebounceTime(25);
  kpd.setHoldTime(500);

  // สายปลุกจาก ESP32
  pinMode(ESP_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ESP_INT_PIN), isrEsp, FALLING);

  noteActivity();
}



void loop() {
  // ===== 1) Keypad =====
  char k = kpd.getKey();
  if (k) {
    noteActivity();
    //if (k == 'A' || k == 'B' || k == 'C' || k == 'D') {
    //buzzer.playClickHi();
    //return;
    // }
    if (!canVote && page != PAGE_REG_PASS) {
      buzzer.playError();
    } else {
      if (page == PAGE_VOTE || page == PAGE_CONFIRM) {
        vote(k);
      } else if (page == PAGE_REG_PASS) {
        // จัดการใน regispage ด้านล่าง
      }
    }
  }

  // ===== 2) Serial from ESP32 (อักขระเดี่ยว) =====
  int msg = -1;
  while (Serial.available()) {
    noteActivity();
    msg = Serial.read();
    Serial.print(F("ESP "));
    Serial.println((char)msg);
  }

  if (wokeFromEsp) {
    wokeFromEsp = false;
    noteActivity();
  }

  if (msg != -1) {
    if (!tmrpcm.isPlaying()) {
      lcd.setCursor(0, 0);
      lcd.print((char)msg);
      lcd.print(' ');
    }
    //while (tmrpcm.isPlaying());

    if (msg == 'S') { tmrpcm.play("re.wav"); }  // กำลังอ่านบัตร
    else if (msg == 'W') {                      // ยังไม่ลงทะเบียน/เพิกถอนสิทธิ์
      canVote = false;
      page = PAGE_WAIT;
      drawReadyUI_base();
      tmrpcm.play("n.wav");
    } else if (msg == 'G') {
      tmrpcm.play("f.wav");
    } else if (msg == 'J') {
      tmrpcm.play("q.wav");
    } else if (msg == 'P') {
      tmrpcm.play("p.wav");
    } else if (msg == 'L') {
      tmrpcm.play("l.wav");
    } else if (msg == 'B') {
      tmrpcm.play("b.wav");
    } else if (msg == 'Z') {
      tmrpcm.play("z.wav");
    } else if (msg == 'H') {
      tmrpcm.play("h.wav");
    } else if (msg == 'M') {
      tmrpcm.play("m.wav");
    } else if (msg == 'O') {  // ยืนยันตัวตนสำเร็จ
      canVote = true;
      page = PAGE_VOTE;
      drawVoteUI_base();
      tmrpcm.play("c.wav");
      while (tmrpcm.isPlaying())
        ;
      tmrpcm.play("ch.wav");
    } else if (msg == 'R') {
      if (waitRToExit) {
        waitRToExit = false;
        pendingAction = ACT_NONE;
        fregis = false;
        canVote = false;
        page = PAGE_WAIT;
        drawReadyUI_base();
      } else {
        startPass(ACT_REG);
        waitRToExit = true;
      }
    } else if (msg == 'T') {  // Toggle โหมดดูผลโหวต
      if (waitTToExit || showingTally) {
        waitTToExit = false;
        showingTally = false;
        pendingAction = ACT_NONE;
        fregis = false;
        canVote = false;
        page = PAGE_WAIT;
        drawReadyUI_base();
      } else {
        startPass(ACT_TALLY);
        waitTToExit = true;
      }
    } else if (msg == 'X') {
      startPass(ACT_CLEAR);
    }
  }

  // ===== 3) UI Animations =====
  if (page == PAGE_VOTE) animateDuringSelect();
  if (page == PAGE_CONFIRM && (long)(millis() - confirmUntil) >= 0) {
    page = canVote ? PAGE_VOTE : PAGE_WAIT;
    if (page == PAGE_VOTE) drawVoteUI_base();
    else drawReadyUI_base();
  }

  // ===== 4) Admin PIN Page =====
  if (fregis && page == PAGE_REG_PASS) { regispage(k); }

  // ===== 5) Buzzer tick =====
  buzzer.update();

  // ===== 6) Auto-sleep =====
  if (!tmrpcm.isPlaying()) {
    if ((millis() - lastActivityMs) >= IDLE_SLEEP_MS) {
      prepareBeforeSleep();
      wokeFromEsp = false;
      goSleepPowerDown();
      afterWake();
    }
  }

  // ===== 7) Ready UI clock tick =====
  clock_ready_tick();
}
