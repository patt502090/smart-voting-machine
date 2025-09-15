/*
  UNO + LCD 20x4 I2C (HW-061) + Keypad 4x4 via PCF8574 (I2C)
  UX:
    - 0..9 เลือก, '*'=ล้าง, '#'=ยืนยัน
    - ระหว่างเลือก: สปินเนอร์มุมขวาบน + กล่องเลขกระพริบ + cursor blink
  I2C wiring:
    A4=SDA, A5=SCL → LCD(0x27/0x3F) + PCF8574(keypad=0x20)
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Keypad_I2C.h>

// ===== I2C addresses (แก้ตามผลสแกนจริง) =====
#define LCD_ADDR     0x27      // ถ้าไม่ขึ้น ลอง 0x3F
#define KEYPAD_ADDR  0x20      // PCF8574 ของ keypad

LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

// ===== Keypad map 4x4 =====
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {0,1,2,3}; // PCF8574 P0..P3 = ROW0..ROW3
byte colPins[COLS] = {4,5,6,7}; // PCF8574 P4..P7 = COL0..COL3
Keypad_I2C kpd( makeKeymap(keys), rowPins, colPins, ROWS, COLS, KEYPAD_ADDR );

// ===== State =====
enum Page { PAGE_VOTE, PAGE_CONFIRM };
Page page = PAGE_VOTE;
int  currentChoice = -1;           // -1=ยังไม่เลือก, 0=งด, 1..9=ผู้สมัคร

// ===== CGRAM icons (มุม/เส้น/ลูกศร/ติ๊ก) =====
byte I_TL[8]  = {B11100,B10000,B10000,B10000,B10000,B10000,B10000,B10000};
byte I_TR[8]  = {B00111,B00001,B00001,B00001,B00001,B00001,B00001,B00001};
byte I_BL[8]  = {B10000,B10000,B10000,B10000,B10000,B10000,B10000,B11100};
byte I_BR[8]  = {B00001,B00001,B00001,B00001,B00001,B00001,B00001,B00111};
byte I_H [8]  = {B11111,B00000,B00000,B00000,B00000,B00000,B00000,B11111};
byte I_V [8]  = {B10001,B10001,B10001,B10001,B10001,B10001,B10001,B10001};
byte I_AR[8]  = {B00100,B00110,B00111,B11111,B00111,B00110,B00100,B00000}; // ▶
byte I_CHK[8] = {B00000,B00001,B00011,B10110,B11100,B01000,B00000,B00000}; // ✓

void loadIcons(){
  lcd.createChar(0, I_TL);
  lcd.createChar(1, I_TR);
  lcd.createChar(2, I_BL);
  lcd.createChar(3, I_BR);
  lcd.createChar(4, I_H );
  lcd.createChar(5, I_V );
  lcd.createChar(6, I_AR);
  lcd.createChar(7, I_CHK);
}

void drawFrame(){
  lcd.clear();
  // มุม
  lcd.setCursor(0,0);  lcd.write((byte)0);
  lcd.setCursor(19,0); lcd.write((byte)1);
  lcd.setCursor(0,3);  lcd.write((byte)2);
  lcd.setCursor(19,3); lcd.write((byte)3);
  // ขอบบน/ล่าง
  for(int x=1;x<19;x++){ lcd.setCursor(x,0); lcd.write((byte)4); }
  for(int x=1;x<19;x++){ lcd.setCursor(x,3); lcd.write((byte)4); }
  // ขอบซ้าย/ขวา
  for(int y=1;y<3;y++){ lcd.setCursor(0,y);  lcd.write((byte)5); }
  for(int y=1;y<3;y++){ lcd.setCursor(19,y); lcd.write((byte)5); }
}

// ===== UI text positions =====
const uint8_t POS_SPINNER_X = 18, POS_SPINNER_Y = 0; // มุมขวาบน
const uint8_t POS_ARROW_X   = 1,  POS_ARROW_Y   = 2; // ▶
const uint8_t POS_HINT_X    = 2,  POS_HINT_Y    = 2; // "#=Confirm  *=Clear"
const uint8_t POS_TITLE_X   = 2,  POS_TITLE_Y   = 1; // "Select:"
const uint8_t BOX_LEFT_X    = 10;                    // กล่องเลข [n]
const uint8_t BOX_TOP_Y     = 0;

// ===== Anim (non-blocking) =====
const char spinFrames[4] = {'|','/','-','\\'};
uint8_t spinIdx = 0;
unsigned long lastSpinMs = 0;
const unsigned long SPIN_MS = 160;

bool boxOn = true;
unsigned long lastBlinkMs = 0;
const unsigned long BLINK_MS = 380;

// วาดกล่อง [ n ] (หรือซ่อนกล่อง — แสดงเฉพาะตัวเลข)
void drawChoiceBox(bool show){
  // เคลียร์พื้นที่ 10..14 x 0..2 ก่อน
  for(uint8_t y=0; y<=2; y++){
    lcd.setCursor(BOX_LEFT_X, BOX_TOP_Y + y);
    lcd.print("     ");
  }
  if(currentChoice <= 0){ // 0=งด → เราใช้ "00"
    lcd.setCursor(12,1);
    if(show) lcd.print("00");
    else     lcd.print("  ");
    return;
  }
  if(show){
    // วาดกรอบ
    lcd.setCursor(10,0); lcd.write((byte)0);
    for(int x=11;x<=13;x++){ lcd.setCursor(x,0); lcd.write((byte)4); }
    lcd.setCursor(14,0); lcd.write((byte)1);
    lcd.setCursor(10,1); lcd.write((byte)5);
    lcd.setCursor(14,1); lcd.write((byte)5);
    lcd.setCursor(10,2); lcd.write((byte)2);
    for(int x=11;x<=13;x++){ lcd.setCursor(x,2); lcd.write((byte)4); }
    lcd.setCursor(14,2); lcd.write((byte)3);
    // ตัวเลขกลางกล่อง
    lcd.setCursor(12,1); lcd.print(currentChoice);
  }else{
    // โหมดซ่อนกรอบ: โชว์เฉพาะตัวเลขตรงกลาง (ลอย ๆ)
    lcd.setCursor(12,1); lcd.print(currentChoice);
  }
}

void drawVoteUI_base(){
  loadIcons();
  drawFrame();
  // หัวข้อ + arrow + hint
  lcd.setCursor(POS_TITLE_X, POS_TITLE_Y); lcd.print("Select: 0=Abstain");
  lcd.setCursor(POS_ARROW_X, POS_ARROW_Y); lcd.write((byte)6); // ▶
  lcd.setCursor(POS_HINT_X,  POS_HINT_Y ); lcd.print("#=Confirm  *=Clear");

  // เคอร์เซอร์กระพริบตรงบรรทัดคำสั่ง (ให้รู้ว่า active)
  lcd.setCursor(POS_HINT_X, POS_HINT_Y);
  lcd.blink();

  // ถ้ายังไม่เลือกอะไร
  if(currentChoice < 0){
    lcd.setCursor(2,0); lcd.print("Choose 1..9     ");
  }else if(currentChoice == 0){
    lcd.setCursor(2,0); lcd.print("Choice: Abstain ");
  }else{
    lcd.setCursor(2,0); lcd.print("Choice:         ");
  }
  // พื้นที่กล่อง
  drawChoiceBox(currentChoice >= 0 ? true : false);

  // รีเซ็ตอนิเมชัน
  spinIdx = 0; lastSpinMs = millis();
  boxOn = true; lastBlinkMs = millis();
  // วาดเฟรมแรกของสปินเนอร์
  lcd.setCursor(POS_SPINNER_X, POS_SPINNER_Y); lcd.print(spinFrames[spinIdx]);
}

void animateDuringSelect(){
  unsigned long now = millis();
  // spinner
  if(now - lastSpinMs >= SPIN_MS){
    lastSpinMs = now;
    spinIdx = (spinIdx + 1) & 0x03;
    lcd.setCursor(POS_SPINNER_X, POS_SPINNER_Y);
    lcd.print(spinFrames[spinIdx]);
  }
  // blink กล่องเลข (ถ้ามีการเลือกแล้ว)
  if(currentChoice >= 0 && now - lastBlinkMs >= BLINK_MS){
    lastBlinkMs = now;
    boxOn = !boxOn;
    drawChoiceBox(boxOn);
  }
}

void drawConfirmUI(){
  lcd.noBlink();
  loadIcons();
  drawFrame();
  lcd.setCursor(2,1); lcd.print("Confirmed:");
  if(currentChoice==0){
    lcd.setCursor(13,1); lcd.print("Abstain");
  }else{
    lcd.setCursor(13,1); lcd.print("No.");
    lcd.print(currentChoice);
  }
  lcd.setCursor(1,2);  lcd.write((byte)7); // ✓
  lcd.setCursor(18,2); lcd.write((byte)7);
  lcd.setCursor(5,3);  lcd.print("Saved! Thank you");
}

// Hook: เรียกเมื่อยืนยัน
void onConfirmVote(int choice){
  Serial.print("CF:"); Serial.println(choice); // ไว้ซิงก์ภายหลัง
}

// ============ SETUP / LOOP ============
void setup(){
  Wire.begin();                 // UNO: SDA=A4, SCL=A5
  Serial.begin(115200);

  lcd.init(); lcd.backlight();

  // เริ่มที่หน้า Vote
  page = PAGE_VOTE;
  currentChoice = -1;
  drawVoteUI_base();

  kpd.begin( makeKeymap(keys) );
  kpd.setDebounceTime(25);
  kpd.setHoldTime(500);
}

void loop(){
  // อ่านปุ่ม
  char k = kpd.getKey();
  if(k){
    if(k>='0' && k<='9'){
      currentChoice = k - '0';
      drawVoteUI_base();              // รีเฟรชฐาน + รีเซ็ตอนิเมชัน
    } else if(k=='*'){
      currentChoice = -1;
      drawVoteUI_base();
    } else if(k=='#'){
      if(currentChoice >= 0){
        page = PAGE_CONFIRM;
        drawConfirmUI();
        onConfirmVote(currentChoice);
        delay(1000);                  // แสดงผลยืนยันสั้น ๆ
        currentChoice = -1;
        page = PAGE_VOTE;
        drawVoteUI_base();
      }
    }
  }

  // อนิเมชันระหว่างหน้าเลือก
  if(page == PAGE_VOTE){
    animateDuringSelect();
  }
}