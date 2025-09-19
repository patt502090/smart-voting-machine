/*
  UNO + LCD 20x4 I2C (HW-061) + Keypad 4x4 via PCF8574 (I2C) + Buzzer "Arcade" SFX
  - 0..9 เลือก, '*'=ล้าง (เสียงลง), '#'=ยืนยัน (แฟนแฟร์ 4 โน้ต)
  - A/B/C/D = คลิกโทนสูง
  - ยืนยันแล้วส่ง "CF:X" ทาง Serial (ไป ESP32/PC)
  - ไม่มี thermal printer

  Wiring:
    I2C: A4=SDA, A5=SCL → LCD(0x27/0x3F) + PCF8574(keypad=0x20)
    Buzzer (2 ขา):
      Passive piezo: + → D5 (ผ่าน R 100–220Ω แนะนำ), − → GND
      Active buzzer : + → D5 (ตั้ง BUZZER_PASSIVE เป็น 0),       − → GND
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
//mark update---------------
//#define _SS_MAX_RX_BUFF 16   // จากค่าเดิม 64 -> เหลือ 16 ไบต์
//#include <SoftwareSerial.h>
#include <TMRpcm.h>
#include <SPI.h>
#include <SD.h>
#define SD_ChipSelectPin 4

//#include <NeoSWSerial.h>
//const uint8_t RX_PIN = 2;   // คุณตั้งไว้ 8
//const uint8_t TX_PIN = 7;   // คุณตั้งไว้ 7
//NeoSWSerial mySerial(2, 7);
TMRpcm tmrpcm;
//SoftwareSerial mySerial(8, 7);  // RX, TX

bool isLikeString(String msg, String Text);
unsigned long lastPlayTime = 0;      // เวลาเล่นเสียงล่าสุด
unsigned long lastPlayTime1 = 0;      // เวลาเล่นเสียงล่าสุด
unsigned long lastPlayTime2 = 0;      // เวลาเล่นเสียงล่าสุด
const unsigned long interval = 500;  // หน่วง 5 วินาที

//------------------

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
enum Page { PAGE_VOTE,
            PAGE_CONFIRM };
Page page = PAGE_VOTE;
int currentChoice = -1;          // -1=ยังไม่เลือก, 0=งด, 1..9=ผู้สมัคร
unsigned long confirmUntil = 0;  // แสดงหน้า Confirm แบบ non-blocking

// ===== CGRAM icons =====
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

// ===== UI positions =====
const uint8_t POS_SPINNER_X = 18, POS_SPINNER_Y = 0;  // มุมขวาบน
const uint8_t POS_ARROW_X = 1, POS_ARROW_Y = 2;       // ▶
const uint8_t POS_HINT_X = 2, POS_HINT_Y = 2;         // "#=Confirm  *=Clear"
const uint8_t POS_TITLE_X = 2, POS_TITLE_Y = 1;       // "Select:"
const uint8_t BOX_LEFT_X = 10;                        // กล่องเลข [n]
const uint8_t BOX_TOP_Y = 0;

// ===== Anim (non-blocking) =====
const char spinFrames[4] = { '|', '/', '-', '\\' };
uint8_t spinIdx = 0;
unsigned long lastSpinMs = 0;
const unsigned long SPIN_MS = 160;

bool boxOn = true;
unsigned long lastBlinkMs = 0;
const unsigned long BLINK_MS = 380;

void drawChoiceBox(bool show) {
  // เคลียร์พื้นที่ 10..14 x 0..2 ก่อน
  for (uint8_t y = 0; y <= 2; y++) {
    lcd.setCursor(BOX_LEFT_X, BOX_TOP_Y + y);
    lcd.print(F("     "));
  }
  if (currentChoice <= 0) {  // 0=งด → "00"
    lcd.setCursor(12, 1);
    if (show) lcd.print(F("00"));
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
    lcd.setCursor(2, 0);
    lcd.print(F("Choose 1..9     "));
  } else if (currentChoice == 0) {
    lcd.setCursor(2, 0);
    lcd.print(F("Choice: Abstain "));
  } else {
    lcd.setCursor(2, 0);
    lcd.print(F("Choice:         "));
  }
  drawChoiceBox(currentChoice >= 0 ? true : false);

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
  lcd.write((byte)7);  // ✓
  lcd.setCursor(18, 2);
  lcd.write((byte)7);
  lcd.setCursor(5, 3);
  lcd.print(F("Saved! Thank you"));
}

/* ===== BUZZER SFX (Arcade pack) ===== */
class BuzzerSFX {
public:
  void init() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    seq = nullptr;
    count = idx = phase = 0;
    phaseEnd = 0;
  }

  // ชุดเสียงพร้อมใช้ (ใช้จำนวนสมาชิกแบบ fixed เพื่อหลบ sizeof กับ incomplete type)
  void playBoot() {
    start(SEQ_BOOT, 4);
  }  // jingle เปิดเครื่อง
  void playClick() {
    start(SEQ_CLICK, 1);
  }  // คลิกปกติ
  void playClickHi() {
    start(SEQ_CLICK_HI, 1);
  }  // คลิกโทนสูง
  void playBack() {
    start(SEQ_BACK, 2);
  }  // ย้อน/ล้าง
  void playError() {
    start(SEQ_ERROR, 3);
  }  // ผิดเงื่อนไข
  void playConfirm() {
    start(SEQ_CONFIRM, 2);
  }  // ยืนยันสั้น
  void playFanfare() {
    start(SEQ_FANFARE, 4);
  }  // แฟนแฟร์ 4 โน้ต

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
    if (phase == 2 && (long)(now - phaseEnd) >= 0) {  // next step / done
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

  // โน้ต (Hz)
  enum {
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

  // ลิสต์เอฟเฟกต์ (ประกาศขนาดชัดเจน)
  static const Step SEQ_BOOT[4];      // C5-E5-G5-C6
  static const Step SEQ_CLICK[1];     // ติ๊กสั้นกลาง
  static const Step SEQ_CLICK_HI[1];  // ติ๊กสั้นสูง
  static const Step SEQ_BACK[2];      // สูงลงต่ำ
  static const Step SEQ_ERROR[3];     // ลดหลั่น 3 จังหวะ
  static const Step SEQ_CONFIRM[2];   // ติ๊ง-ติง
  static const Step SEQ_FANFARE[4];   // G5–B5–D6–G6

  const Step* seq;
  uint8_t count, idx, phase;
  unsigned long phaseEnd;

  void start(const Step* s, uint8_t n) {
    seq = s;
    count = n;
    idx = 0;
    phase = 0;
    phaseEnd = 0;
  }
};

// นิยามโน้ตจริง ๆ
const BuzzerSFX::Step BuzzerSFX::SEQ_BOOT[4] = { { BuzzerSFX::N_C5, 90, 15 }, { BuzzerSFX::N_E5, 90, 15 }, { BuzzerSFX::N_G5, 90, 15 }, { BuzzerSFX::N_C6, 150, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_CLICK[1] = { { 1500, 25, 8 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_CLICK_HI[1] = { { 1900, 18, 5 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_BACK[2] = { { BuzzerSFX::N_E5, 70, 15 }, { BuzzerSFX::N_C5, 110, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_ERROR[3] = { { 400, 140, 35 }, { 320, 140, 35 }, { 250, 180, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_CONFIRM[2] = { { BuzzerSFX::N_A5, 120, 25 }, { BuzzerSFX::N_C6, 140, 0 } };
const BuzzerSFX::Step BuzzerSFX::SEQ_FANFARE[4] = { { BuzzerSFX::N_G5, 90, 15 }, { BuzzerSFX::N_B5, 90, 15 }, { BuzzerSFX::N_D6, 110, 15 }, { BuzzerSFX::N_G6, 160, 0 } };

BuzzerSFX buzzer;  // อินสแตนซ์เดียว

// ===== Hook: เรียกเมื่อยืนยัน =====
void onConfirmVote(int choice) {
  Serial.print(F("CF:"));
  Serial.println(choice);  // ส่งให้ ESP32/PC
  buzzer.playFanfare();    // แฟนแฟร์ 4 โน้ต
}


//mark update
const long ESP32_BAUD = 9600;  // ให้ UNO–ESP32 ใช้ 9600 จะนิ่งสุด

inline void serial_pause_hs() {
  Serial.flush();  // ดันบัฟเฟอร์ส่งให้หมดก่อน
  Serial.end();    // ปิด UART + ISR
  delay(2);
}

inline void serial_resume_hs() {
  Serial.begin(ESP32_BAUD);
  delay(2);
}

void playWavBlocking(const char* path) {
  serial_pause_hs();  // ✨ ปิด Serial ชั่วคราว
  tmrpcm.play(path);
  while (tmrpcm.isPlaying()) {
    buzzer.update();
  }
  serial_resume_hs();  // ✨ เปิดกลับ
}


// ============ SETUP / LOOP ============
void setup() {

  Serial.begin(9600);

  if (!SD.begin(SD_ChipSelectPin)) {  // see if the card is present and can be initialized:
    Serial.println("SD fail");
    return;  // don't do anything more if not
  }
  tmrpcm.speakerPin = 9;  //5,6,11 or 46 on Mega, 9 on Uno, Nano, etc
  Serial.println("test");
  tmrpcm.setVolume(5);
  tmrpcm.quality(1);
  Wire.begin();  // UNO: SDA=A4, SCL=A5


  lcd.init();
  lcd.backlight();

  //mark update-------------------------


  tmrpcm.play("sawadh.wav");

  //playWavBlocking("sound/sawadh.wav");
  //mySerial.begin(9600);
  //--------------------
  buzzer.init();
  buzzer.playBoot();  // jingle เปิดเครื่อง

  page = PAGE_VOTE;
  currentChoice = -1;
  drawVoteUI_base();

  kpd.begin(makeKeymap(keys));
  kpd.setDebounceTime(25);
  kpd.setHoldTime(500);
}


String enteredPass = "";
String savedPass = "";
bool fregis = false;  // สถานะเข้าสู่โหมดกรอกรหัส

bool playIfIdle(const char* path) {
  if (tmrpcm.isPlaying()) return false;
  unsigned long now = millis();
  if (now - lastPlayTime < interval) return false;
  tmrpcm.play(path);
  lastPlayTime = now;
  return true;
}


void loop() {
  // อ่านคีย์
  char k = kpd.getKey();
  if (k) {
    if (k >= '0' && k <= '9') {
      currentChoice = k - '0';
      drawVoteUI_base();
      buzzer.playClick();  // เสียงคลิกตอนกดเลข
    } else if (k == '*') {
      if (currentChoice >= 0) buzzer.playBack();  // ล้างค่า -> เสียงย้อน
      currentChoice = -1;
      drawVoteUI_base();
    } else if (k == '#') {
      if (currentChoice >= 0) {
        page = PAGE_CONFIRM;
        drawConfirmUI();
        onConfirmVote(currentChoice);      // ส่ง CF:X + แฟนแฟร์
        confirmUntil = millis() + 1000UL;  // โชว์หน้า Confirm 1 วินาที (non-blocking)
        currentChoice = -1;                // เคลียร์ตัวเลือกทันที
      } else {
        buzzer.playError();  // ยังไม่เลือกแต่กดยืนยัน
      }
    } else {
      // A/B/C/D → คลิกโทนสูง
      buzzer.playClickHi();
    }
  }

  // อนิเมชัน + จัดการหน้า Confirm non-blocking + อัปเดตเสียง
  if (page == PAGE_VOTE) animateDuringSelect();
  if (page == PAGE_CONFIRM && (long)(millis() - confirmUntil) >= 0) {
    page = PAGE_VOTE;
    drawVoteUI_base();
  }
  buzzer.update();
  //Serial.println("Vote1");
  delay(500);
  

  // mark update
  String msg="";
  if (Serial.available()) {
     msg = Serial.readStringUntil('\n');
    Serial.println("ESP32: " + msg);
    msg.trim();
    lcd.clear();
    // มุม
    lcd.setCursor(0, 0);
    lcd.print(msg);
   
      
    
    
    
    


    //msg = "";
  }
 if (msg == "S") {
      //Serial.println("readrfid");
      unsigned long currentTime = millis();

      // ถ้ายังไม่ถึง 5 วินาทีตั้งแต่เล่นครั้งล่าสุด ให้ข้าม
      if (currentTime - lastPlayTime >= interval) {
        tmrpcm.play("re.wav");

        lastPlayTime = currentTime;  // เก็บเวลาไว้
      }

    } else if (msg == "W") {
            //Serial.println("readrfid");
      unsigned long currentTime = millis();

      // ถ้ายังไม่ถึง 5 วินาทีตั้งแต่เล่นครั้งล่าสุด ให้ข้าม
      if (currentTime - lastPlayTime1 >= interval) {
        tmrpcm.play("n.wav");

        lastPlayTime1 = currentTime;  // เก็บเวลาไว้
      }
    }
      else if (msg == "C") {
            //Serial.println("readrfid");
      
        tmrpcm.play("c.wav");
       }
        



  /*
  if (msg == "Scanning") {
    //Serial.println("readrfid");
    unsigned long currentTime = millis();

    // ถ้ายังไม่ถึง 5 วินาทีตั้งแต่เล่นครั้งล่าสุด ให้ข้าม
    if (currentTime - lastPlayTime >= interval) {
      tmrpcm.play("sound/readrfid.wav");

      lastPlayTime = currentTime;  // เก็บเวลาไว้
    }

  } /*
    if (msg == "Wrong card!") {
      //Serial.println("readrfid");
      unsigned long currentTime = millis();

      // ถ้ายังไม่ถึง 5 วินาทีตั้งแต่เล่นครั้งล่าสุด ให้ข้าม
      if (currentTime - lastPlayTime >= interval) {
        tmrpcm.play("sound/noreg.wav");

        lastPlayTime = currentTime;  // เก็บเวลาไว้
      }
    }
    /*else if (msg == "Scan new card") {
      //Serial.println("readrfid");
      unsigned long currentTime = millis();

      // ถ้ายังไม่ถึง 5 วินาทีตั้งแต่เล่นครั้งล่าสุด ให้ข้าม
      if (currentTime - lastPlayTime >= interval) {
        tmrpcm.play("sound/noreg.wav");

        lastPlayTime = currentTime;  // เก็บเวลาไว้
      }
    }
    else if(msg == "regis")
    {
      fregis = true;
    }
/*  }
  

if (fregis) {
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print("Enter pass:");
    lcd.setCursor(4, 1);
    lcd.print(enteredPass.length() > 0 ? String('*', enteredPass.length()) : "");

    if (k) {
      if (k >= '0' && k <= '9') {
        if (enteredPass.length() < 4) {
          enteredPass += k;
        }
      } else if (k == '*') {
        // ลบตัวล่าสุด
        if (enteredPass.length() > 0) {
          enteredPass.remove(enteredPass.length() - 1);
        }
      } else if (k == '#') {
        if (enteredPass.length() == 4) {
          savedPass = enteredPass;   // เก็บรหัสจริง
          Serial.println("Saved pass = " + savedPass);
          enteredPass = "";
          fregis = false;            // ออกจากโหมดกรอกรหัส
          lcd.clear();
          lcd.setCursor(2, 0);
          lcd.print("Pass Saved!");
          delay(1000);
          lcd.clear();
        } else {
          lcd.setCursor(0, 1);
          lcd.print("Must be 4 digits");
          delay(1000);
        }
      }
    }
    return; // ไม่ให้ไปทำงาน loop ส่วนอื่น
  }*/




  /*
    if (isLikeString(msg,"Scanning")) {
      {

        unsigned long currentTime = millis();

        // ถ้ายังไม่ถึง 5 วินาทีตั้งแต่เล่นครั้งล่าสุด ให้ข้าม
        if (currentTime - lastPlayTime >= interval) {
          tmrpcm.play("sound/readrfid.wav");
          lastPlayTime = currentTime;  // เก็บเวลาไว้
        }
      }
    }
    if (isLikeString(msg,"Wrong card!")) {

      unsigned long currentTime = millis();

      // ถ้ายังไม่ถึง 5 วินาทีตั้งแต่เล่นครั้งล่าสุด ให้ข้าม
      if (currentTime - lastPlayTime >= interval) {
        tmrpcm.play("sound/noreg.wav");
        lastPlayTime = currentTime;  // เก็บเวลาไว้
      }
    }
  }**/
}


//mark update
bool isLikeString(String text, String target) {
  text.toLowerCase();
  text.trim();



  // ถ้าความยาวต่างกันเกิน 2 ตัว ไม่ต้องเช็คเลย
  if (abs((int)text.length() - (int)target.length()) > 2) {
    return false;
  }

  int diff = 0;
  int len = min(text.length(), target.length());

  // เช็คทีละตัวอักษร
  for (int i = 0; i < len; i++) {
    if (text[i] != target[i]) {
      diff++;
    }
  }

  // บวกความต่างของความยาวเข้าไปด้วย
  diff += abs((int)text.length() - (int)target.length());

  // ยอมให้ต่างกันไม่เกิน 2 ตัวอักษร
  return (diff <= 2);
}
