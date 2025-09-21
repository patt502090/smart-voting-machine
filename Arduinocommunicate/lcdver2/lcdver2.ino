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

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
#include <TMRpcm.h>
#include <SPI.h>
#include <SD.h>
#include <avr/wdt.h>

// ===== SD / Audio =====
#define SD_ChipSelectPin 4
TMRpcm tmrpcm;

// ===== BUZZER CONFIG =====
#define BUZZER_PIN 5      // D5
#define BUZZER_PASSIVE 1  // 1=Passive piezo (tone/noTone), 0=Active buzzer (on/off)

// ===== I2C addresses =====
#define LCD_ADDR 0x27
#define KEYPAD_ADDR 0x20

LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

// ===== Keypad map 4x4 =====
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 0, 1, 2, 3 };  // PCF8574 P0..P3 = ROW0..ROW3
byte colPins[COLS] = { 4, 5, 6, 7 };  // PCF8574 P4..P7 = COL0..COL3
Keypad_I2C kpd(makeKeymap(keys), rowPins, colPins, ROWS, COLS, KEYPAD_ADDR);

// ===== State/UI =====
enum Page { PAGE_VOTE, PAGE_CONFIRM, PAGE_REG_PASS };
Page page = PAGE_VOTE;
int currentChoice = -1;          // -1=ยังไม่เลือก, 0=งด, 1..9=ผู้สมัคร
unsigned long confirmUntil = 0;  // แสดงหน้า Confirm แบบ non-blocking

// ===== Timers =====
const unsigned long interval = 5000;  // 5 วินาที กันสั่งเล่นถี่เกิน
unsigned long lastPlayTime  = 0;
unsigned long lastPlayTime1 = 0;
unsigned long lastPlayTime2 = 0;
unsigned long lastPlayTime3 = 0;
unsigned long lastPlayTime4 = 0;
unsigned long lastPlayTime5 = 0;
unsigned long lastPlayTime6 = 0;
unsigned long lastPlayTime7 = 0;

// ===== CGRAM icons =====
byte I_TL[8]  = { B11100, B10000, B10000, B10000, B10000, B10000, B10000, B10000 };
byte I_TR[8]  = { B00111, B00001, B00001, B00001, B00001, B00001, B00001, B00001 };
byte I_BL[8]  = { B10000, B10000, B10000, B10000, B10000, B10000, B10000, B11100 };
byte I_BR[8]  = { B00001, B00001, B00001, B00001, B00001, B00001, B00001, B00111 };
byte I_H[8]   = { B11111, B00000, B00000, B00000, B00000, B00000, B00000, B11111 };
byte I_V[8]   = { B10001, B10001, B10001, B10001, B10001, B10001, B10001, B10001 };
byte I_AR[8]  = { B00100, B00110, B00111, B11111, B00111, B00110, B00100, B00000 };   // ▶
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
  lcd.setCursor(0, 0);  lcd.write((byte)0);
  lcd.setCursor(19, 0); lcd.write((byte)1);
  lcd.setCursor(0, 3);  lcd.write((byte)2);
  lcd.setCursor(19, 3); lcd.write((byte)3);
  // ขอบบน/ล่าง
  for (int x = 1; x < 19; x++) {
    lcd.setCursor(x, 0); lcd.write((byte)4);
    lcd.setCursor(x, 3); lcd.write((byte)4);
  }
  // ขอบซ้าย/ขวา
  for (int y = 1; y < 3; y++) {
    lcd.setCursor(0, y);  lcd.write((byte)5);
    lcd.setCursor(19, y); lcd.write((byte)5);
  }
}

// ===== UI positions =====
const uint8_t POS_SPINNER_X = 18, POS_SPINNER_Y = 0;  // มุมขวาบน
const uint8_t POS_ARROW_X   = 1,  POS_ARROW_Y   = 2;  // ▶
const uint8_t POS_HINT_X    = 2,  POS_HINT_Y    = 2;  // "#=Confirm  *=Clear"
const uint8_t POS_TITLE_X   = 2,  POS_TITLE_Y   = 1;  // "Select:"
const uint8_t BOX_LEFT_X    = 10;                     // กล่องเลข [n]
const uint8_t BOX_TOP_Y     = 0;

// ===== Anim (non-blocking) =====
const char  spinFrames[4]   = { '|', '/', '-', '\\' };
uint8_t     spinIdx         = 0;
unsigned long lastSpinMs    = 0;
const unsigned long SPIN_MS = 160;

bool boxOn                  = true;
unsigned long lastBlinkMs   = 0;
const unsigned long BLINK_MS= 380;

void drawChoiceBox(bool show) {
  // เคลียร์พื้นที่ 10..14 x 0..2 ก่อน
  for (uint8_t y = 0; y <= 2; y++) {
    lcd.setCursor(BOX_LEFT_X, BOX_TOP_Y + y);
    lcd.print(F("     "));
  }
  if (currentChoice <= 0) {  // 0=งด → "00"
    lcd.setCursor(12, 1);
    if (show) lcd.print(F("00"));
    else      lcd.print(F("  "));
    return;
  }
  if (show) {
    // วาดกรอบ
    lcd.setCursor(10, 0); lcd.write((byte)0);
    for (int x = 11; x <= 13; x++) { lcd.setCursor(x, 0); lcd.write((byte)4); }
    lcd.setCursor(14, 0); lcd.write((byte)1);
    lcd.setCursor(10, 1); lcd.write((byte)5);
    lcd.setCursor(14, 1); lcd.write((byte)5);
    lcd.setCursor(10, 2); lcd.write((byte)2);
    for (int x = 11; x <= 13; x++) { lcd.setCursor(x, 2); lcd.write((byte)4); }
    lcd.setCursor(14, 2); lcd.write((byte)3);
    // ตัวเลขกลางกล่อง
    lcd.setCursor(12, 1);
    lcd.print(currentChoice);
  } else {
    // โชว์เฉพาะตัวเลข
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
    lcd.setCursor(2, 0); lcd.print(F("Choose 1..9     "));
  } else if (currentChoice == 0) {
    lcd.setCursor(2, 0); lcd.print(F("Choice: Abstain "));
  } else {
    lcd.setCursor(2, 0); lcd.print(F("Choice:         "));
  }
  drawChoiceBox(currentChoice >= 0);

  // รีเซ็ตอนิเมชัน
  spinIdx = 0; lastSpinMs = millis();
  boxOn   = true; lastBlinkMs = millis();
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
    lcd.setCursor(13, 1); lcd.print(F("Abstain"));
  } else {
    lcd.setCursor(13, 1); lcd.print(F("No.")); lcd.print(currentChoice);
  }
  lcd.setCursor(1, 2);  lcd.write((byte)7);  // ✓
  lcd.setCursor(18, 2); lcd.write((byte)7);
  lcd.setCursor(5, 3);  lcd.print(F("Saved! Thank you"));
}

/* ===== BUZZER SFX (Arcade pack) ===== */
class BuzzerSFX {
public:
  void init() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    seq = nullptr; count = idx = phase = 0; phaseEnd = 0;
  }
  void playBoot()    { start(SEQ_BOOT,    4); }
  void playClick()   { start(SEQ_CLICK,   1); }
  void playClickHi() { start(SEQ_CLICK_HI,1); }
  void playBack()    { start(SEQ_BACK,    2); }
  void playError()   { start(SEQ_ERROR,   3); }
  void playConfirm() { start(SEQ_CONFIRM, 2); }
  void playFanfare() { start(SEQ_FANFARE, 4); }

  void update() {
    if (!seq || idx >= count) return;
    unsigned long now = millis();
    const Step& st = seq[idx];

    if (phase == 0) {  // start ON
#if BUZZER_PASSIVE
      if (st.freq == 0) noTone(BUZZER_PIN);
      else              tone(BUZZER_PIN, st.freq);
#else
      digitalWrite(BUZZER_PIN, HIGH);
#endif
      phase = 1; phaseEnd = now + st.dur; return;
    }
    if (phase == 1 && (long)(now - phaseEnd) >= 0) { // end ON -> GAP
#if BUZZER_PASSIVE
      noTone(BUZZER_PIN);
#else
      digitalWrite(BUZZER_PIN, LOW);
#endif
      phase = 2; phaseEnd = now + st.gap; return;
    }
    if (phase == 2 && (long)(now - phaseEnd) >= 0) { // next
      idx++; phase = 0; if (idx >= count) { seq = nullptr; }
    }
  }

private:
  struct Step { uint16_t freq; uint16_t dur; uint16_t gap; };  // ms

  // โน้ต (Hz)
  enum {
    N_C5=523, N_D5=587, N_E5=659, N_F5=698, N_G5=784, N_A5=880, N_B5=988,
    N_C6=1047, N_D6=1175, N_E6=1319, N_F6=1397, N_G6=1568, N_A6=1760
  };

  static const Step SEQ_BOOT[4];
  static const Step SEQ_CLICK[1];
  static const Step SEQ_CLICK_HI[1];
  static const Step SEQ_BACK[2];
  static const Step SEQ_ERROR[3];
  static const Step SEQ_CONFIRM[2];
  static const Step SEQ_FANFARE[4];

  const Step* seq; uint8_t count, idx, phase; unsigned long phaseEnd;

  void start(const Step* s, uint8_t n) {
    // ถ้ากำลังเล่น WAV อยู่ ให้ข้ามไม่เปิดบัซเซอร์ (กันชนกับ TMRpcm)
    if (tmrpcm.isPlaying()) return;
    seq = s; count = n; idx = 0; phase = 0; phaseEnd = 0;
  }
};

// นิยามโน้ตจริง ๆ
const BuzzerSFX::Step BuzzerSFX::SEQ_BOOT[4]      = {{BuzzerSFX::N_C5,  90, 15},{BuzzerSFX::N_E5,  90, 15},{BuzzerSFX::N_G5,110,15},{BuzzerSFX::N_C6,150,0}};
const BuzzerSFX::Step BuzzerSFX::SEQ_CLICK[1]     = {{1500, 25, 8}};
const BuzzerSFX::Step BuzzerSFX::SEQ_CLICK_HI[1]  = {{1900, 18, 5}};
const BuzzerSFX::Step BuzzerSFX::SEQ_BACK[2]      = {{BuzzerSFX::N_E5, 70, 15},{BuzzerSFX::N_C5,110,0}};
const BuzzerSFX::Step BuzzerSFX::SEQ_ERROR[3]     = {{400, 140, 35},{320, 140, 35},{250, 180, 0}};
const BuzzerSFX::Step BuzzerSFX::SEQ_CONFIRM[2]   = {{BuzzerSFX::N_A5,120,25},{BuzzerSFX::N_C6,140,0}};
const BuzzerSFX::Step BuzzerSFX::SEQ_FANFARE[4]   = {{BuzzerSFX::N_G5,90,15},{BuzzerSFX::N_B5,90,15},{BuzzerSFX::N_D6,110,15},{BuzzerSFX::N_G6,160,0}};

BuzzerSFX buzzer;

// ===== Hook: เรียกเมื่อยืนยัน =====
void onConfirmVote(int choice) {
  Serial.print(F("CF:")); Serial.println(choice);
  buzzer.playFanfare(); // ในตัวมันเช็ค tmrpcm.isPlaying() แล้ว
}

// ===== Watchdog helpers =====
void wdt_sanity_boot() { MCUSR = 0; wdt_disable(); }

[[noreturn]] void reset_via_wdt() {
  wdt_enable(WDTO_15MS);
  while (true) {}
}

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

// ===== App helpers =====
bool playIfIdle(const char* path) {
  if (tmrpcm.isPlaying()) return false;
  unsigned long now = millis();
  if (now - lastPlayTime < interval) return false;
  tmrpcm.play((char*)path);
  lastPlayTime = now;
  return true;
}

// สำหรับหน้าตั้งรหัส
String enteredPass = "";
String savedPass   = "1234";
bool   fregis      = false;
bool   regisstatus = false;

void printMaskedAt(uint8_t x, uint8_t y, const String& s) {
  lcd.setCursor(x, y);
  for (uint8_t i = 0; i < s.length(); i++) lcd.print('*');
  // ลบที่เหลือ (กัน ghost char)
  for (uint8_t i = s.length(); i < 4; i++) lcd.print(' ');
}

// ===== Vote handler =====
void vote(char k) {
  if (k >= '0' && k <= '9') {
    currentChoice = k - '0';
    drawVoteUI_base();
    buzzer.playClick();  // มี gate ภายใน
  } else if (k == '*') {
    if (currentChoice >= 0) buzzer.playBack();
    currentChoice = -1;
    drawVoteUI_base();
  } else if (k == '#') {
    if (currentChoice >= 0) {
      page = PAGE_CONFIRM;
      drawConfirmUI();
      onConfirmVote(currentChoice);
      confirmUntil = millis() + 1000UL;
      currentChoice = -1;
    } else {
      buzzer.playError();
    }
  }
}

// ============ SETUP / LOOP ============
void setup() {
  wdt_sanity_boot();
  Serial.begin(9600);

  tmrpcm.speakerPin = 9;  // UNO/Nano ใช้ D9
   tmrpcm.setVolume(5);  // 0..7 (แล้วแต่ไฟล์/ลำโพง)
   tmrpcm.quality(1);

  initSD_orReset();

  Wire.begin();  // UNO: SDA=A4, SCL=A5
  lcd.init();
  lcd.backlight();

  // เล่นไฟเปิดเครื่อง (ถ้าหาไฟล์ไม่เจอจะเงียบเฉยๆ)
  tmrpcm.play((char*)"sawadh.wav");
  if (!tmrpcm.isPlaying()) {
    tmrpcm.play((char*)"sawadh.wav");
  }

  buzzer.init();
  buzzer.playBoot();  // jingle เปิดเครื่อง (มี gate ภายใน)

  page = PAGE_VOTE;
  currentChoice = -1;
  drawVoteUI_base();

  kpd.begin(makeKeymap(keys));
  kpd.setDebounceTime(25);
  kpd.setHoldTime(500);
}

void loop() {
  // อ่านคีย์จาก keypad
  char k = kpd.getKey();
  if (k) {
    if (page == PAGE_VOTE || page == PAGE_CONFIRM) {
      vote(k);
    } else if (page == PAGE_REG_PASS) {
      // จัดการในส่วน reg pass ด้านล่าง
    }
  }

  // อ่าน Serial: โปรโตคอลอักขระเดี่ยวจาก ESP32
  int msg = -1;
  while (Serial.available() > 0) {
    msg = Serial.read();
    Serial.print(F("ESP32: "));
    Serial.println((char)msg);
  }

  // ใช้เฉพาะเมื่อมีอักขระจริง
  if (msg != -1) {
    // หลีกเลี่ยงการ clear ระหว่างเล่นเสียง
    if (!tmrpcm.isPlaying()) {
      lcd.setCursor(0, 0);
      lcd.print((char)msg);
      lcd.print(' ');
    }

    if      (msg == 'S') { playIfIdle("re.wav"); }   // กำลังอ่านบัตร
    else if (msg == 'W') { playIfIdle("n.wav"); }    // คุณยังไม่ลงทะเบียน
    else if (msg == 'O') { playIfIdle("c.wav"); }    // ยืนยันตัวตนสำเร็จ
    else if (msg == 'V') { playIfIdle("ch.wav"); }   // ผ่านตรวจสอบ + พร้อมโหวต
    else if (msg == 'R') {                           // เข้าหน้าตั้งรหัส
      fregis = true; page = PAGE_REG_PASS;
      lcd.noBlink(); lcd.clear();
      lcd.setCursor(2, 0); lcd.print(F("Enter pass:"));
      lcd.setCursor(4, 1); printMaskedAt(4, 1, enteredPass);
    }
  }

  // อนิเมชัน / หน้าคอนเฟิร์ม non-blocking
  if (page == PAGE_VOTE) animateDuringSelect();
  if (page == PAGE_CONFIRM && (long)(millis() - confirmUntil) >= 0) {
    page = PAGE_VOTE;
    drawVoteUI_base();
  }

  // หน้า "ตั้งรหัส" (เมื่อ fregis == true)
  if (fregis && page == PAGE_REG_PASS) {
    char kk = k; // ใช้คีย์ที่อ่านไว้ข้างบน
    if (kk) {
      if (kk >= '0' && kk <= '9') {
        if (enteredPass.length() < 4) {
          enteredPass += kk;
          printMaskedAt(4, 1, enteredPass);
        }
      } else if (kk == '*') {
        if (enteredPass.length() > 0) {
          enteredPass.remove(enteredPass.length() - 1);
          printMaskedAt(4, 1, enteredPass);
        }
      } else if (kk == '#') {
        if (enteredPass.length() == 4) {
          savedPass   = enteredPass;
          enteredPass = "";
          fregis = false; regisstatus = true;
          lcd.clear();
          lcd.setCursor(2, 0); lcd.print(F("Pass Saved!"));
          // กลับสู่หน้าโหวต
          page = PAGE_VOTE;
          drawVoteUI_base();
        } else {
          lcd.setCursor(0, 2); lcd.print(F("Must be 4 digits "));
        }
      }
    }
  }

  // อัปเดตบัซเซอร์ทุกเฟรม (state machine)
  buzzer.update();
}
