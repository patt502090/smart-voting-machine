/*
  LCD 20x4 (I2C HW-061) — Beautiful UI with CGRAM icons
  - หน้า Welcome (กรอบ+ไอคอน), หน้า Vote (กล่องไฮไลต์+ลูกศร), หน้า Confirm (ติ๊กถูก)
  - ใช้ร่วมกับ Keypad_I2C ได้ (บัส I2C ร่วม LCD/PCF8574)
  - แนบตัวอย่าง "แบนเนอร์ไทย" สลับ CGRAM สำหรับคำไทยสั้น ๆ

  Wiring (UNO):
    SDA=A4, SCL=A5, 5V, GND → ไปทั้ง LCD + PCF8574 (keypad)
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---- LCD address & size ----
#define LCD_ADDR 0x27      // ถ้าไม่เจอ เปลี่ยนเป็น 0x3F
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

// ================== CGRAM SET: ICONS (กรอบ/เส้น/ลูกศร/ติ๊ก) ==================
/* index 0..7 */
byte I_TL[8]  = {B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111}; // จะทับด้วยมุมจริง
byte I_TR[8]  = {B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111};
byte I_BL[8]  = {B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111};
byte I_BR[8]  = {B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111};
byte I_H[8]   = {B11111,B00000,B00000,B00000,B00000,B00000,B00000,B11111}; // เส้นนอนหนา
byte I_V[8]   = {B10001,B10001,B10001,B10001,B10001,B10001,B10001,B10001}; // เส้นตั้ง
byte I_AR[8]  = {B00100,B00110,B00111,B11111,B00111,B00110,B00100,B00000}; // ลูกศร ▶
byte I_CHK[8] = {B00000,B00001,B00011,B10110,B11100,B01000,B00000,B00000}; // ✓

/* มุมโค้งเบา ๆ */
byte I_TL2[8] = {B11100,B10000,B10000,B10000,B10000,B10000,B10000,B10000};
byte I_TR2[8] = {B00111,B00001,B00001,B00001,B00001,B00001,B00001,B00001};
byte I_BL2[8] = {B10000,B10000,B10000,B10000,B10000,B10000,B10000,B11100};
byte I_BR2[8] = {B00001,B00001,B00001,B00001,B00001,B00001,B00001,B00111};

// โหลดชุดไอคอนลง CGRAM
void loadIconSet(){
  lcd.createChar(0, I_TL2);
  lcd.createChar(1, I_TR2);
  lcd.createChar(2, I_BL2);
  lcd.createChar(3, I_BR2);
  lcd.createChar(4, I_H);
  lcd.createChar(5, I_V);
  lcd.createChar(6, I_AR);
  lcd.createChar(7, I_CHK);
}

// วาดกรอบเต็มจอ (ใช้ไอคอนมุม/เส้น)
void drawFrame(){
  lcd.clear();
  // มุม
  lcd.setCursor(0,0);  lcd.write((byte)0);
  lcd.setCursor(19,0); lcd.write((byte)1);
  lcd.setCursor(0,3);  lcd.write((byte)2);
  lcd.setCursor(19,3); lcd.write((byte)3);
  // ด้านบน/ล่าง
  for(int x=1;x<19;x++){ lcd.setCursor(x,0); lcd.write((byte)4); }
  for(int x=1;x<19;x++){ lcd.setCursor(x,3); lcd.write((byte)4); }
  // ด้านซ้าย/ขวา
  for(int y=1;y<3;y++){ lcd.setCursor(0,y);  lcd.write((byte)5); }
  for(int y=1;y<3;y++){ lcd.setCursor(19,y); lcd.write((byte)5); }
}

// ================== (OPTION) CGRAM SET: THAI BANNER (ตัวอย่าง) ==================
// ใส่ชุด 7 อักษรไทย + 1 ไอคอน เพื่อแสดงคำไทยสั้น ๆ บนแถวเดียว (จำกัด 8 ช่อง CGRAM)
byte TH0[8] = {B00001,B01110,B10001,B00101,B01011,B10001,B00000,B00000}; // ส (ตัวอย่าง)
byte TH1[8] = {B01000,B01110,B00000,B01110,B01010,B00010,B00110,B00110}; // วั
byte TH2[8] = {B01101,B11111,B00000,B01110,B10001,B11101,B10101,B11101}; // ดี
byte TH3[8] = {B01101,B11111,B00001,B11001,B01001,B01001,B01001,B01111}; // ปี
byte TH4[8] = {B01100,B11010,B10110,B01010,B00010,B00010,B00010,B00001}; // ใ
byte TH5[8] = {B00000,B00000,B11011,B01011,B01010,B01101,B01001,B00000}; // ห
byte TH6[8] = {B00001,B00001,B11001,B11001,B01001,B11101,B11011,B00000}; // ม่
byte HEART[8]={B00000,B01010,B11111,B11111,B11111,B01110,B00100,B00000}; // หัวใจ

void loadThaiSet(){
  lcd.createChar(0, TH0);
  lcd.createChar(1, TH1);
  lcd.createChar(2, TH2);
  lcd.createChar(3, TH3);
  lcd.createChar(4, TH4);
  lcd.createChar(5, TH5);
  lcd.createChar(6, TH6);
  lcd.createChar(7, HEART);
}

// ตัวอย่างแบนเนอร์ไทย (หน้า Welcome) — ใช้ 8 CGRAM
void drawThaiBanner(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.write((byte)7); // ♥ ซ้าย
  // "สวัสดีปีใหม่" (ตัวอย่าง — เปลี่ยนคำได้ตามต้องการ แต่จำกัด 8 ช่อง)
  lcd.setCursor(3,0); lcd.write((byte)0);
  lcd.setCursor(4,0); lcd.write((byte)1);
  lcd.setCursor(5,0); lcd.write((byte)0);
  lcd.setCursor(6,0); lcd.write((byte)2);
  lcd.setCursor(7,0); lcd.write((byte)3);
  lcd.setCursor(8,0); lcd.write((byte)4);
  lcd.setCursor(9,0); lcd.write((byte)5);
  lcd.setCursor(10,0);lcd.write((byte)6);
  lcd.setCursor(19,0);lcd.write((byte)7); // ♥ ขวา

  lcd.setCursor(2,2); lcd.print("SMART VOTE READY");
}

// ================== UI PAGES ==================
enum Page {WELCOME, VOTE, CONFIRM};
Page page = WELCOME;
int  currentChoice = -1; // -1=none, 0=Abstain, 1..9

// วาดเมนูเลือกให้ดูโปร (ใช้เฟรม + ลูกศร + กล่องเน้น)
void drawVoteUI(){
  loadIconSet();
  drawFrame();
  lcd.setCursor(2,1); lcd.print("Select: 0=Abstain");
  lcd.setCursor(2,2); lcd.print("#=Confirm  *=Clear");
  if(currentChoice < 0){
    lcd.setCursor(2,0); lcd.print("Choose 1..9");
  }else if(currentChoice==0){
    lcd.setCursor(2,0); lcd.print("Choice: Abstain  ");
  }else{
    // ไฮไลต์ด้วยกล่องหยาบ ๆ
    lcd.setCursor(2,0); lcd.print("Choice:");
    // กล่อง [ n ]
    lcd.setCursor(10,0); lcd.write((byte)0); // TL
    for(int x=11;x<=13;x++){ lcd.setCursor(x,0); lcd.write((byte)4); }
    lcd.setCursor(14,0); lcd.write((byte)1); // TR
    lcd.setCursor(10,1); lcd.write((byte)5);
    lcd.setCursor(12,1); lcd.print(currentChoice);
    lcd.setCursor(14,1); lcd.write((byte)5);
    lcd.setCursor(10,2); lcd.write((byte)2); // BL
    for(int x=11;x<=13;x++){ lcd.setCursor(x,2); lcd.write((byte)4); }
    lcd.setCursor(14,2); lcd.write((byte)3); // BR
  }
  // ลูกศรชี้บรรทัดคำสั่ง
  lcd.setCursor(1,2); lcd.write((byte)6);
}

// หน้ายืนยัน
void drawConfirm(){
  loadIconSet();
  drawFrame();
  lcd.setCursor(2,1); lcd.print("Confirmed:");
  if(currentChoice==0) lcd.setCursor(13,1), lcd.print("Abstain");
  else                 lcd.setCursor(13,1), lcd.print(currentChoice);

  // ติ๊กถูกใหญ่ (ซ้าย/ขวา)
  lcd.setCursor(1,2);  lcd.write((byte)7);
  lcd.setCursor(18,2); lcd.write((byte)7);

  lcd.setCursor(5,3); lcd.print("Saved! Thank you");
}

// ================== DEMO FLOW ==================
unsigned long t0=0;

void setup(){
  Wire.begin();
  lcd.init();
  lcd.backlight();
  Serial.begin(115200);

  // หน้า Welcome แบบไทย (แบนเนอร์ไทย 1–2 วินาที)
  loadThaiSet();
  drawThaiBanner();
  t0 = millis();
}

void loop(){
  if(page==WELCOME){
    if(millis()-t0 > 1500){
      page = VOTE;
      currentChoice = -1;
      drawVoteUI();
    }
  }
  // ตัวอย่างเดโม่: อ่านปุ่มจาก Serial Monitor เพื่อสลับหน้า (เพราะยังไม่ผูก keypad ที่นี่)
  // พิมพ์ 0..9 แล้วกด Enter เพื่อพรีวิว, พิมพ์ # เพื่อคอนเฟิร์ม, * เพื่อล้าง
  if(Serial.available()){
    char c = Serial.read();
    if(c>='0' && c<='9'){ currentChoice = c-'0'; drawVoteUI(); }
    else if(c=='*'){ currentChoice = -1; drawVoteUI(); }
    else if(c=='#' && currentChoice>=0){ page=CONFIRM; drawConfirm(); delay(1200); page=VOTE; currentChoice=-1; drawVoteUI(); }
  }
}