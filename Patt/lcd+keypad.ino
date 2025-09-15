/*
  UNO + LCD 20x4 I2C (HW-061) + Keypad 4x4 via PCF8574 + OLED SSD1306 I2C
  - OLED (U8g2) แสดงข้อความ "ไทย" ครบถ้วน (UTF-8)
  - LCD ใช้แสดงเลข/กรอบ/กล่องไฮไลต์ (ไม่ต้องพึ่งฟอนต์ไทย)

  I2C bus: A4=SDA, A5=SCL → LCD(0x27/0x3F) + KEYPAD(0x20) + OLED(0x3C)
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
#include <U8g2lib.h>

// ===== ปรับตามผลสแกนจริง =====
#define LCD_ADDR     0x27     // ถ้าไม่ขึ้น ลอง 0x3F
#define KEYPAD_ADDR  0x20     // PCF8574 keypad (คุณสแกนเจอ 0x20 แล้ว)
#define OLED_RESET   U8X8_PIN_NONE

LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
// OLED SSD1306 128x64 I2C (addr ส่วนใหญ่ 0x3C)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RESET);

// ===== Keypad map 4x4 =====
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {0,1,2,3};
byte colPins[COLS] = {4,5,6,7};
Keypad_I2C kpd( makeKeymap(keys), rowPins, colPins, ROWS, COLS, KEYPAD_ADDR );

// ===== สถานะหลัก =====
enum Page { PAGE_WELCOME, PAGE_VOTE, PAGE_CONFIRM };
Page page = PAGE_WELCOME;
int  currentChoice = -1;   // -1=ยังไม่เลือก, 0=งดออกเสียง, 1..9=ผู้สมัคร
unsigned long tWelcome = 0;

// ---------- OLED: ฟังก์ชันไทย ----------
void oledHeader() {
  u8g2.setFont(u8g2_font_unifont_t_symbols); // ครอบคลุมไทย (ขนาดกำลังดี)
  u8g2.setFontDirection(0);
}

void oledIdle(){
  u8g2.clearBuffer();
  oledHeader();
  u8g2.drawUTF8(0, 16, "เลือกหมายเลขผู้สมัคร");
  u8g2.drawUTF8(0, 36, "0 = งดออกเสียง");
  u8g2.drawUTF8(0, 56, "# = ยืนยัน    * = ลบ");
  u8g2.sendBuffer();
}

void oledChoice(){
  u8g2.clearBuffer();
  oledHeader();
  if(currentChoice < 0){
    u8g2.drawUTF8(0, 24, "ยังไม่ได้เลือก");
    u8g2.drawUTF8(0, 48, "กดตัวเลข 0..9");
  }else if(currentChoice == 0){
    u8g2.drawUTF8(0, 24, "คุณเลือก: งดออกเสียง");
    u8g2.drawUTF8(0, 48, "กด # เพื่อยืนยัน  * เพื่อลบ");
  }else{
    char buf[32];
    snprintf(buf, sizeof(buf), "คุณเลือกหมายเลข: %d", currentChoice);
    u8g2.drawUTF8(0, 24, buf);
    u8g2.drawUTF8(0, 48, "กด # เพื่อยืนยัน  * เพื่อลบ");
  }
  u8g2.sendBuffer();
}

void oledConfirmed(){
  u8g2.clearBuffer();
  oledHeader();
  if(currentChoice == 0) u8g2.drawUTF8(0, 28, "ยืนยัน: งดออกเสียง");
  else {
    char buf[32]; snprintf(buf, sizeof(buf), "ยืนยัน: หมายเลข %d", currentChoice);
    u8g2.drawUTF8(0, 28, buf);
  }
  u8g2.drawUTF8(0, 56, "บันทึกแล้ว! ขอบคุณครับ");
  u8g2.sendBuffer();
}

// ---------- LCD: กล่อง/กรอบ/เลข (ไม่ใช้ฟอนต์ไทย) ----------
byte TL[8]={B11100,B10000,B10000,B10000,B10000,B10000,B10000,B10000};
byte TR[8]={B00111,B00001,B00001,B00001,B00001,B00001,B00001,B00001};
byte BL[8]={B10000,B10000,B10000,B10000,B10000,B10000,B10000,B11100};
byte BR[8]={B00001,B00001,B00001,B00001,B00001,B00001,B00001,B00111};
byte H [8]={B11111,B00000,B00000,B00000,B00000,B00000,B00000,B11111};
byte V [8]={B10001,B10001,B10001,B10001,B10001,B10001,B10001,B10001};

void lcdLoadIcons(){
  lcd.createChar(0, TL); lcd.createChar(1, TR);
  lcd.createChar(2, BL); lcd.createChar(3, BR);
  lcd.createChar(4, H ); lcd.createChar(5, V );
}

void lcdDrawFrame(){
  lcd.clear();
  lcd.setCursor(0,0);  lcd.write((byte)0);
  lcd.setCursor(19,0); lcd.write((byte)1);
  lcd.setCursor(0,3);  lcd.write((byte)2);
  lcd.setCursor(19,3); lcd.write((byte)3);
  for(int x=1;x<19;x++){ lcd.setCursor(x,0); lcd.write((byte)4); }
  for(int x=1;x<19;x++){ lcd.setCursor(x,3); lcd.write((byte)4); }
  for(int y=1;y<3;y++){ lcd.setCursor(0,y);  lcd.write((byte)5); }
  for(int y=1;y<3;y++){ lcd.setCursor(19,y); lcd.write((byte)5); }
}

void lcdVoteUI(){
  lcdLoadIcons();
  lcdDrawFrame();
  // บรรทัดบนโชว์ "Choice" แบบเลข/กล่อง
  lcd.setCursor(2,1); lcd.print("Choice:");
  if(currentChoice < 0){
    lcd.setCursor(10,1); lcd.print("--");
  }else if(currentChoice == 0){
    lcd.setCursor(10,1); lcd.print("00"); // ใช้ 00 เป็นสัญลักษณ์งด
  }else{
    // กล่อง [ n ]
    lcd.setCursor(10,0); lcd.write((byte)0);
    for(int x=11;x<=13;x++){ lcd.setCursor(x,0); lcd.write((byte)4); }
    lcd.setCursor(14,0); lcd.write((byte)1);
    lcd.setCursor(10,1); lcd.write((byte)5);
    lcd.setCursor(12,1); lcd.print(currentChoice);
    lcd.setCursor(14,1); lcd.write((byte)5);
    lcd.setCursor(10,2); lcd.write((byte)2);
    for(int x=11;x<=13;x++){ lcd.setCursor(x,2); lcd.write((byte)4); }
    lcd.setCursor(14,2); lcd.write((byte)3);
  }
  // คำสั่งย่อ (อังกฤษ) — ข้อจำกัดของ LCD
  lcd.setCursor(2,2); lcd.print("#=OK  *=CLR  0=ABST");
}

void lcdConfirm(){
  lcdLoadIcons();
  lcdDrawFrame();
  lcd.setCursor(2,1); lcd.print("Saved:");
  if(currentChoice==0) { lcd.setCursor(9,1); lcd.print("ABST"); }
  else { lcd.setCursor(9,1); lcd.print(currentChoice); }
  lcd.setCursor(5,2); lcd.print("Thank you");
}

// ---------- Hook: เรียกเมื่อยืนยัน (#) ----------
void onConfirmVote(int choice){
  Serial.print("CF:"); Serial.println(choice);  // ส่งออก Serial ไว้ซิงก์ขั้นต่อไป
}

// ================== SETUP / LOOP ==================
void setup(){
  Wire.begin();           // UNO: SDA=A4, SCL=A5
  Serial.begin(115200);

  // LCD
  lcd.init(); lcd.backlight();

  // OLED
  u8g2.begin();

  // Welcome (OLED ไทยเต็ม)
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_unifont_t_symbols);
  u8g2.drawUTF8(0, 24, "ระบบลงคะแนนพร้อมใช้งาน");
  u8g2.drawUTF8(0, 48, "กดตัวเลขเพื่อเริ่ม");
  u8g2.sendBuffer();

  // LCD โชว์กรอบรอเลือก
  lcdVoteUI();

  // Keypad_I2C
  kpd.begin( makeKeymap(keys) );
  kpd.setDebounceTime(25);
  kpd.setHoldTime(500);

  page = PAGE_VOTE;
}

void loop(){
  char k = kpd.getKey();
  if(k){
    if(k>='0' && k<='9'){
      currentChoice = k - '0';
      lcdVoteUI();
      oledChoice();
    } else if(k=='*'){
      currentChoice = -1;
      lcdVoteUI();
      oledChoice();
    } else if(k=='#'){
      if(currentChoice >= 0){
        onConfirmVote(currentChoice);
        page = PAGE_CONFIRM;
        lcdConfirm();
        oledConfirmed();
        delay(1200);
        currentChoice = -1;
        page = PAGE_VOTE;
        lcdVoteUI();
        oledIdle();
      }
    }
  }
}