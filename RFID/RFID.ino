#include <Keypad_I2C.h>         // ไลบรารีสำหรับการเชื่อมต่อและอ่านข้อมูลจาก Keypad ผ่าน I2C
#include <Wire.h>               // ไลบรารีสำหรับการสื่อสาร I2C ซึ่งใช้ในการเชื่อมต่อระหว่างไมโครคอนโทรลเลอร์กับอุปกรณ์อื่นๆ
#include <Keypad.h>             // ไลบรารีสำหรับการจัดการ Keypad เพื่อรับอินพุตจากผู้ใช้งาน
#include <LiquidCrystal_I2C.h>  // ไลบรารีสำหรับควบคุมจอแสดงผล LCD ผ่านการสื่อสารแบบ I2C
#include <SPI.h>                // ไลบรารีสำหรับการสื่อสารแบบ SPI ซึ่งใช้เชื่อมต่อกับโมดูล RFID
#include <MFRC522.h>            // ไลบรารีสำหรับควบคุมการอ่านข้อมูลจากบัตร RFID โดยใช้โมดูล MFRC522
#include <EEPROM.h>             // ไลบรารีสำหรับการอ่านและเขียนข้อมูลใน EEPROM (หน่วยความจำถาวร) เพื่อเก็บข้อมูลบัตร RFID ที่ลงทะเบียนแล้ว
#include <SoftwareSerial.h>     // ไลบรารีสำหรับการสื่อสารแบบอนุกรมเสมือนผ่านพอร์ต RX/TX เพื่อเชื่อมต่อกับอุปกรณ์อื่นๆ (เช่น โมดูลภายนอก)


// ===== I2C addresses (แก้ตามผลสแกนจริง) =====
#define LCD_ADDR 0x27     // ถ้าไม่ขึ้น ลอง 0x3F
#define KEYPAD_ADDR 0x20  // PCF8574 ของ keypad

// ===== UI text positions =====
const uint8_t POS_SPINNER_X = 18, POS_SPINNER_Y = 0;  // มุมขวาบน
const uint8_t POS_ARROW_X = 1, POS_ARROW_Y = 2;       // ▶
const uint8_t POS_HINT_X = 2, POS_HINT_Y = 2;         // "#=Confirm  *=Clear"
const uint8_t POS_TITLE_X = 2, POS_TITLE_Y = 1;       // "Select:"
const uint8_t BOX_LEFT_X = 10;                        // กล่องเลข [n]
const uint8_t BOX_TOP_Y = 0;

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


// ข้อมูลการเชื่อมต่อ RFID
#define SS_PIN 10             // กำหนดขา SS ของโมดูล RFID ให้เชื่อมต่อกับพิน 10
#define RST_PIN 5             // กำหนดขา Reset ของโมดูล RFID ให้เชื่อมต่อกับพิน 5
const int EEPROM_SIZE = 512;  // กำหนดขนาดของ EEPROM ที่ใช้เก็บข้อมูล UID การ์ด (512 ไบต์)
const int CARD_SIZE = 8;      // กำหนดขนาดของ UID การ์ด (8 ไบต์) ที่เก็บใน EEPROM

SoftwareSerial mySerial(8, 9);  // ใช้ SoftwareSerial สำหรับการสื่อสารกับอุปกรณ์อื่นๆ ผ่านขา Rx (พิน 8) และ Tx (พิน 9)




LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);  // สร้างออบเจ็กต์ lcd สำหรับควบคุมจอ LCD ขนาด 16x2 ที่มี address I2C เป็น 0x27

MFRC522 rfid(SS_PIN, RST_PIN);  // สร้างออบเจ็กต์ rfid สำหรับควบคุมโมดูล RFID โดยใช้ขา SS และ RST ที่กำหนด

String enteredCode = "";           // สร้างตัวแปรเก็บรหัสที่ผู้ใช้ป้อนจาก Keypad
unsigned long lastActionTime = 0;  // ตัวแปรเก็บเวลาล่าสุดที่มีการกระทำ (ใช้ในการควบคุมการปิดประตูอัตโนมัติ)


bool messageShown = false;  // ตัวแปรระบุว่าข้อความฉุกเฉินแสดงแล้วหรือยัง

bool registrationMode = false;            // สถานะการลงทะเบียนการ์ดใหม่
bool deleteMode = false;                  // สถานะการลบการ์ดจากระบบ
unsigned long lastKeypadPressTime = 0;    // เก็บเวลาที่กดปุ่ม Keypad ครั้งล่าสุด
const unsigned long debounceDelay = 200;  // หน่วงเวลา 200 มิลลิวินาที เพื่อป้องกันการกดปุ่มซ้ำ (ป้องกันการรบกวนจากการกดปุ่มหลายครั้ง)




// กำหนด pin ใหม่
const int buzzerPin = 6;  // ย้ายการเชื่อมต่อ Buzzer ไปยังพิน 6

const int switchPin4 = 3;  // ย้ายการเชื่อมต่อสวิตช์ไปยังพิน 4
const int switchPin2 = 2;  // ย้ายการเชื่อมต่อสวิตช์ไปยังพิน 2
const int ledPin = 3;      // กำหนดให้ LED เชื่อมต่อที่พิน 3


// ===== State =====
enum Page { PAGE_VOTE,
            PAGE_CONFIRM };
Page page = PAGE_VOTE;
int currentChoice = -1;  // -1=ยังไม่เลือก, 0=งด, 1..9=ผู้สมัคร


// ===== CGRAM icons (มุม/เส้น/ลูกศร/ติ๊ก) =====
byte I_TL[8] = { B11100, B10000, B10000, B10000, B10000, B10000, B10000, B10000 };
byte I_TR[8] = { B00111, B00001, B00001, B00001, B00001, B00001, B00001, B00001 };
byte I_BL[8] = { B10000, B10000, B10000, B10000, B10000, B10000, B10000, B11100 };
byte I_BR[8] = { B00001, B00001, B00001, B00001, B00001, B00001, B00001, B00111 };
byte I_H[8] = { B11111, B00000, B00000, B00000, B00000, B00000, B00000, B11111 };
byte I_V[8] = { B10001, B10001, B10001, B10001, B10001, B10001, B10001, B10001 };
byte I_AR[8] = { B00100, B00110, B00111, B11111, B00111, B00110, B00100, B00000 };   // ▶
byte I_CHK[8] = { B00000, B00001, B00011, B10110, B11100, B01000, B00000, B00000 };  // ✓

// ===== Anim (non-blocking) =====
const char spinFrames[4] = { '|', '/', '-', '\\' };
uint8_t spinIdx = 0;
unsigned long lastSpinMs = 0;
const unsigned long SPIN_MS = 160;

bool boxOn = true;
unsigned long lastBlinkMs = 0;
const unsigned long BLINK_MS = 380;


void setup() {
  Wire.begin();  // เริ่มต้นการสื่อสารผ่าน I2C สำหรับอุปกรณ์ที่เชื่อมต่อด้วย I2C (เช่น Keypad, LCD)

  Serial.begin(9600);    // เริ่มต้นการสื่อสารอนุกรม (Serial) ด้วยบอดเรต 9600 สำหรับการดีบัก
  mySerial.begin(9600);  // เริ่มต้นการสื่อสารอนุกรมเสมือน (SoftwareSerial) ด้วยบอดเรต 9600 เพื่อสื่อสารกับอุปกรณ์ภายนอก (เช่น โค้ดหรือบอร์ดอื่น)



  lcd.init();
  lcd.backlight();

  // เริ่มที่หน้า Vote
  page = PAGE_VOTE;
  currentChoice = -1;
  drawVoteUI_base();

  kpd.begin(makeKeymap(keys));
  kpd.setDebounceTime(25);
  kpd.setHoldTime(500);

  SPI.begin();      // เริ่มต้นการทำงานของ SPI เพื่อใช้ในการสื่อสารกับโมดูล RFID
  rfid.PCD_Init();  // เริ่มต้นการทำงานของโมดูล RFID (MFRC522)

  pinMode(buzzerPin, OUTPUT);         // กำหนดให้ขา buzzerPin ทำหน้าที่ส่งออก (OUTPUT) สำหรับการควบคุมเสียงเตือน
  pinMode(switchPin2, INPUT_PULLUP);  // กำหนดให้ขา switchPin เป็นอินพุตพร้อมเปิดตัวต้านทานภายใน (INPUT_PULLUP) เพื่อใช้กับสวิตช์
  pinMode(ledPin, OUTPUT);            // กำหนดให้ขา ledPin ทำหน้าที่ส่งออก (OUTPUT) สำหรับควบคุม LED
  pinMode(switchPin4, INPUT_PULLUP);  // กำหนดให้ขา switchPin เป็นอินพุตพร้อมเปิดตัวต้านทานภายใน (INPUT_PULLUP) เพื่อใช้กับสวิตช์


  // ตั้งค่า LED เริ่มต้นเป็น "ปิด"
  digitalWrite(ledPin, LOW);  // ปิด LED โดยเริ่มต้นให้ขา ledPin เป็น LOW

  // แสดงสถานะระบบพร้อมทำงาน
  //displaySystemReady();  // เรียกฟังก์ชันแสดงผลสถานะ "พร้อมทำงาน" บนจอ LCD
}

void loop() {
  int switchDel = digitalRead(switchPin4);  // อ่านค่าจากสวิตช์เพื่อดูว่ามีการกดสวิตช์หรือไม่
  int switchReg = digitalRead(switchPin2);  // อ่านค่าจากสวิตช์เพื่อดูว่ามีการกดสวิตช์หรือไม่

  if (switchReg == LOW) {
    Serial.println("reg");
    registerCard();

  } else if (switchDel == LOW) {
    Serial.println("del");
    deleteCard();
  }



  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {  // ถ้ามีการ์ดใหม่เข้ามาและอ่านข้อมูลการ์ดได้
    lcd.clear();                                                     // ล้างหน้าจอ LCD
    lcd.setCursor(0, 0);                                             // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
    lcd.print("Scanning");                                           // แสดงข้อความ "Scanning" บนจอ LCD
    Serial.print("NUID tag is :");                                   // แสดงข้อความเริ่มต้นใน Serial Monitor
    String ID = "";                                                  // ประกาศตัวแปรเพื่อเก็บ ID ของการ์ด
    for (byte i = 0; i < rfid.uid.size; i++) {                       // วนลูปเพื่ออ่าน UID ของการ์ด
      lcd.print(".");                                                // แสดงจุดบนจอ LCD เพื่อแสดงการทำงาน
      ID.concat(String(rfid.uid.uidByte[i] < 0x10 ? "0" : ""));      // เพิ่มศูนย์หน้าถ้าค่าต่ำกว่า 16 (0x10)
      ID.concat(String(rfid.uid.uidByte[i], HEX));                   // แปลงค่า UID เป็นเลขฐาน 16 และเพิ่มเข้าไปใน ID
      delay(300);                                                    // หน่วงเวลา 300 มิลลิวินาทีเพื่อให้การแสดงผลชัดเจน
    }
    ID.toUpperCase();  // เปลี่ยน ID ให้เป็นตัวพิมพ์ใหญ่

    // ลบช่องว่างทั้งหมดออกเพื่อให้แน่ใจว่าการเปรียบเทียบเป็นไปอย่างถูกต้อง
    ID.replace(" ", "");  // ลบช่องว่างใน ID
    Serial.println(ID);   // แสดง ID ของการ์ดใน Serial Monitor

    if (isCardRegistered(ID)) {    // ตรวจสอบว่าการ์ด RFID ได้รับอนุญาตแล้วหรือไม่
      Serial.println("found");     // ถ้าการ์ดถูกลงทะเบียน ให้เรียกฟังก์ชัน activateMotor() เพื่อเปิดประตู
    } else {                       // ถ้าการ์ดไม่ถูกลงทะเบียน
      lcd.clear();                 // ล้างหน้าจอ LCD
      lcd.setCursor(0, 0);         // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
      lcd.print("Wrong card!");    // แสดงข้อความ "Wrong card!" บนจอ LCD
      tone(buzzerPin, 1000, 200);  // ส่งเสียงเตือนครั้งที่ 1
      digitalWrite(ledPin, HIGH);  // เปิด LED เพื่อแสดงสถานะ
      delay(300);                  // รอ 300 มิลลิวินาที
      digitalWrite(ledPin, LOW);   // ปิด LED
      delay(300);                  // รอ 300 มิลลิวินาที
      tone(buzzerPin, 1000, 200);  // ส่งเสียงเตือนครั้งที่ 2
      digitalWrite(ledPin, HIGH);  // เปิด LED อีกครั้ง
      delay(300);                  // รอ 300 มิลลิวินาที
      digitalWrite(ledPin, LOW);   // ปิด LED
      delay(1500);                 // รอ 1500 มิลลิวินาที
      
                                   // resetDisplayAfterDelay();  // รีเซ็ตหน้าจอ LCD เพื่อแสดงสถานะพร้อมทำงาน
    }
  }

  char k = kpd.getKey();
  if (k) {
    if (k >= '0' && k <= '9') {
      currentChoice = k - '0';
      drawVoteUI_base();  // รีเฟรชฐาน + รีเซ็ตอนิเมชัน
    } else if (k == '*') {
      currentChoice = -1;
      drawVoteUI_base();
    } else if (k == '#') {
      if (currentChoice >= 0) {
        page = PAGE_CONFIRM;
        drawConfirmUI();
        onConfirmVote(currentChoice);
        delay(1000);  // แสดงผลยืนยันสั้น ๆ
        currentChoice = -1;
        page = PAGE_VOTE;
        drawVoteUI_base();
      }
    }
  }

  // อนิเมชันระหว่างหน้าเลือก
  if (page == PAGE_VOTE) {
    animateDuringSelect();
  }
}



void registerCard() {          // ฟังก์ชันสำหรับลงทะเบียนการ์ดใหม่
  String newUID = "";          // ประกาศตัวแปรสำหรับเก็บ UID ของการ์ดใหม่
  lcd.clear();                 // ล้างหน้าจอ LCD
  lcd.setCursor(0, 0);         // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
  lcd.print("Scan new card");  // แสดงข้อความ "Scan new card" บนจอ LCD


 while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {  // ถ้ายังไม่มีการสแกนการ์ดใหม่
    char key = kpd.getKey();  // ตรวจสอบการกดปุ่มจาก Keypad
    if (key == 'D') {  // ถ้าผู้ใช้กดปุ่ม 'D' เพื่อยกเลิก
      Serial.println("Registration cancelled.");  // แสดงข้อความ "Registration cancelled." ใน Serial Monitor
      lcd.clear();  // ล้างหน้าจอ LCD
      lcd.setCursor(0, 0);  // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
      lcd.print("Registration");  // แสดงข้อความ "Registration" บนจอ LCD
      lcd.setCursor(0, 1);  // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 1 คอลัมน์ที่ 0
      lcd.print("Cancelled!");  // แสดงข้อความ "Cancelled!" บนจอ LCD
      tone(buzzerPin, 1000, 200);  // ส่งเสียงเตือนครั้งที่ 1
      digitalWrite(ledPin, HIGH);  // เปิด LED เพื่อแสดงสถานะ
      delay(300);  // รอ 300 มิลลิวินาที
      digitalWrite(ledPin, LOW);  // ปิด LED
      delay(300);  // รอ 300 มิลลิวินาที
      tone(buzzerPin, 1000, 200);  // ส่งเสียงเตือนครั้งที่ 2
      digitalWrite(ledPin, HIGH);  // เปิด LED อีกครั้ง
      delay(300);  // รอ 300 มิลลิวินาที
      digitalWrite(ledPin, LOW);  // ปิด LED
      delay(1000);  // รอ 1000 มิลลิวินาที
      //resetDisplayAfterDelay();  // รีเซ็ตหน้าจอ LCD ให้แสดงสถานะพร้อมทำงาน
      return;  // ยกเลิกการลงทะเบียนและออกจากฟังก์ชัน
    }
    delay(100);  // หน่วงเวลาสำหรับการตรวจสอบครั้งต่อไป
  }

  // อ่าน UID ของการ์ดใหม่
  for (byte i = 0; i < rfid.uid.size; i++) {                       // วนลูปตามขนาดของ UID การ์ด
    newUID.concat(String(rfid.uid.uidByte[i] < 0x10 ? "0" : ""));  // ถ้าค่าต่ำกว่า 16 (0x10) ให้เพิ่ม "0" ข้างหน้า
    newUID.concat(String(rfid.uid.uidByte[i], HEX));               // แปลงค่าแต่ละไบต์ของ UID เป็นเลขฐาน 16 และรวมเข้าไปใน newUID
  }
  newUID.toUpperCase();     // แปลงตัวอักษรใน newUID เป็นตัวพิมพ์ใหญ่
  newUID.replace(" ", "");  // ลบช่องว่างทั้งหมดออกจาก newUID

  if (storeCardInEEPROM(newUID)) {         // ตรวจสอบว่าการ์ดใหม่ถูกบันทึกใน EEPROM สำเร็จหรือไม่
    Serial.print("New UID Registered: ");  // แสดงข้อความใน Serial Monitor
    Serial.println(newUID);                // แสดง UID ของการ์ดที่ลงทะเบียนสำเร็จ
    lcd.clear();                           // ล้างหน้าจอ LCD
    lcd.setCursor(0, 0);                   // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
    lcd.print("Card Registered!");         // แสดงข้อความ "Card Registered!" บนจอ LCD
    tone(buzzerPin, 1000, 200);            // ส่งเสียงเตือนครั้งที่ 1
    digitalWrite(ledPin, HIGH);            // เปิด LED เพื่อแสดงสถานะ
    delay(300);                            // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);             // ปิด LED
    delay(300);                            // รอ 300 มิลลิวินาที
    tone(buzzerPin, 1000, 200);            // ส่งเสียงเตือนครั้งที่ 2
    digitalWrite(ledPin, HIGH);            // เปิด LED อีกครั้ง
    delay(300);                            // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);             // ปิด LED
  } else {                                 // ถ้าการบันทึกการ์ดใน EEPROM ล้มเหลว
    lcd.clear();                           // ล้างหน้าจอ LCD
    lcd.setCursor(0, 0);                   // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
    lcd.print("Failed to Register");       // แสดงข้อความ "Failed to Register" บนจอ LCD
    lcd.setCursor(0, 1);                   // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 1 คอลัมน์ที่ 0
    lcd.print("Card Full");                // แสดงข้อความ "Card Full" บนจอ LCD
    tone(buzzerPin, 1000, 200);            // ส่งเสียงเตือนครั้งที่ 1
    digitalWrite(ledPin, HIGH);            // เปิด LED เพื่อแสดงสถานะ
    delay(300);                            // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);             // ปิด LED
    delay(300);                            // รอ 300 มิลลิวินาที
    tone(buzzerPin, 1000, 200);            // ส่งเสียงเตือนครั้งที่ 2
    digitalWrite(ledPin, HIGH);            // เปิด LED อีกครั้ง
    delay(300);                            // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);             // ปิด LED
  }
  delay(2000);  // รอ 2 วินาที
  //displaySystemReady();  // แสดงสถานะว่าระบบพร้อมทำงาน
}


void deleteCard() {             // ฟังก์ชันสำหรับลบการ์ดที่ลงทะเบียนในระบบ
  String cardToDelete = "";     // ประกาศตัวแปรสำหรับเก็บ UID ของการ์ดที่จะลบ
  lcd.clear();                  // ล้างหน้าจอ LCD
  lcd.setCursor(0, 0);          // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
  lcd.print("Scan to delete");  // แสดงข้อความ "Scan to delete" บนจอ LCD

  // รอการสแกนการ์ดเพื่อทำการลบ
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {  // ตรวจสอบว่ามีการ์ดใหม่ถูกสแกนหรือไม่
    char key = kpd.getKey();                                           // ตรวจสอบการกดปุ่มจาก Keypad
    if (key == 'C') {                                                     // ถ้าผู้ใช้กดปุ่ม 'C' เพื่อยกเลิกการลบ
      Serial.println("Delete operation cancelled.");                      // แสดงข้อความ "Delete operation cancelled." ใน Serial Monitor
      lcd.clear();                                                        // ล้างหน้าจอ LCD
      lcd.setCursor(0, 0);                                                // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
      lcd.print("Delete");                                                // แสดงข้อความ "Delete" บนจอ LCD
      lcd.setCursor(0, 1);                                                // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 1 คอลัมน์ที่ 0
      lcd.print("Cancelled!");                                            // แสดงข้อความ "Cancelled!" บนจอ LCD
      tone(buzzerPin, 1000, 200);                                         // ส่งเสียงเตือนครั้งที่ 1
      digitalWrite(ledPin, HIGH);                                         // เปิด LED เพื่อแสดงสถานะ
      delay(300);                                                         // รอ 300 มิลลิวินาที
      digitalWrite(ledPin, LOW);                                          // ปิด LED
      delay(300);                                                         // รอ 300 มิลลิวินาที
      tone(buzzerPin, 1000, 200);                                         // ส่งเสียงเตือนครั้งที่ 2
      digitalWrite(ledPin, HIGH);                                         // เปิด LED อีกครั้ง
      delay(300);                                                         // รอ 300 มิลลิวินาที
      digitalWrite(ledPin, LOW);                                          // ปิด LED
      delay(1000);                                                        // รอ 1000 มิลลิวินาที
      //resetDisplayAfterDelay();  // รีเซ็ตหน้าจอ LCD ให้แสดงสถานะพร้อมทำงาน
      return;  // ยกเลิกการลบการ์ดและออกจากฟังก์ชัน
    }
    delay(100);  // หน่วงเวลาสำหรับการตรวจสอบครั้งถัดไป
  }

  // อ่าน UID ของการ์ดที่จะลบ
  for (byte i = 0; i < rfid.uid.size; i++) {                             // วนลูปตามขนาดของ UID การ์ด
    cardToDelete.concat(String(rfid.uid.uidByte[i] < 0x10 ? "0" : ""));  // ถ้าค่า UID ต่ำกว่า 16 (0x10) ให้เพิ่ม "0" ข้างหน้า
    cardToDelete.concat(String(rfid.uid.uidByte[i], HEX));               // แปลงค่าแต่ละไบต์ของ UID เป็นเลขฐาน 16 และรวมเข้าไปใน cardToDelete
  }
  cardToDelete.toUpperCase();     // แปลงตัวอักษรใน cardToDelete เป็นตัวพิมพ์ใหญ่
  cardToDelete.replace(" ", "");  // ลบช่องว่างทั้งหมดออกจาก cardToDelete

  if (removeCardFromEEPROM(cardToDelete)) {  // ตรวจสอบว่าการลบการ์ดจาก EEPROM สำเร็จหรือไม่
    Serial.println("Card Deleted");          // แสดงข้อความ "Card Deleted" ใน Serial Monitor
    lcd.clear();                             // ล้างหน้าจอ LCD
    lcd.setCursor(0, 0);                     // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
    lcd.print("Card Deleted!");              // แสดงข้อความ "Card Deleted!" บนจอ LCD
    tone(buzzerPin, 1000, 200);              // ส่งเสียงเตือนครั้งที่ 1
    digitalWrite(ledPin, HIGH);              // เปิด LED เพื่อแสดงสถานะ
    delay(300);                              // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);               // ปิด LED
    delay(300);                              // รอ 300 มิลลิวินาที
    tone(buzzerPin, 1000, 200);              // ส่งเสียงเตือนครั้งที่ 2
    digitalWrite(ledPin, HIGH);              // เปิด LED อีกครั้ง
    delay(300);                              // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);               // ปิด LED
  } else {                                   // ถ้าการ์ดไม่ถูกพบใน EEPROM
    Serial.println("Card not found");        // แสดงข้อความ "Card not found" ใน Serial Monitor
    lcd.clear();                             // ล้างหน้าจอ LCD
    lcd.setCursor(0, 0);                     // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
    lcd.print("Card not found!");            // แสดงข้อความ "Card not found!" บนจอ LCD
    tone(buzzerPin, 1000, 200);              // ส่งเสียงเตือนครั้งที่ 1
    digitalWrite(ledPin, HIGH);              // เปิด LED เพื่อแสดงสถานะ
    delay(300);                              // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);               // ปิด LED
    delay(300);                              // รอ 300 มิลลิวินาที
    tone(buzzerPin, 1000, 200);              // ส่งเสียงเตือนครั้งที่ 2
    digitalWrite(ledPin, HIGH);              // เปิด LED อีกครั้ง
    delay(300);                              // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);               // ปิด LED
  }
  delay(2000);  // รอ 2 วินาที
  //displaySystemReady();  // แสดงสถานะว่าระบบพร้อมทำงาน
}

bool isCardRegistered(String card) {                  // ฟังก์ชันสำหรับตรวจสอบว่าการ์ดที่กำหนดถูกลงทะเบียนใน EEPROM หรือไม่
  for (int i = 0; i < EEPROM_SIZE; i += CARD_SIZE) {  // วนลูปตรวจสอบทุกการ์ดที่ถูกเก็บใน EEPROM ทีละบล็อกตามขนาดของการ์ด (CARD_SIZE)
    String storedCard = "";                           // สร้างตัวแปรเก็บการ์ดที่อ่านได้จาก EEPROM
    for (int j = 0; j < CARD_SIZE; j++) {             // วนลูปอ่านข้อมูลของการ์ดแต่ละไบต์จาก EEPROM
      storedCard += char(EEPROM.read(i + j));         // อ่านค่าแต่ละไบต์และเพิ่มเข้าไปในตัวแปร storedCard
    }
    if (storedCard == card) {  // ถ้าการ์ดที่อ่านได้ตรงกับการ์ดที่กำหนด
      return true;             // คืนค่า true แสดงว่าการ์ดนี้ถูกลงทะเบียนแล้ว
    }
  }
  return false;  // ถ้าไม่พบการ์ดที่ตรงกัน คืนค่า false แสดงว่าการ์ดนี้ยังไม่ถูกลงทะเบียน
}

bool storeCardInEEPROM(String card) {                   // ฟังก์ชันสำหรับบันทึกการ์ดใหม่ลงใน EEPROM
  if (!isCardRegistered(card)) {                        // ตรวจสอบว่าการ์ดนี้ยังไม่ได้ลงทะเบียนในระบบ
    for (int i = 0; i < EEPROM_SIZE; i += CARD_SIZE) {  // วนลูปผ่าน EEPROM ทีละบล็อกตามขนาดของการ์ด (CARD_SIZE)
      if (EEPROM.read(i) == 255) {                      // ตรวจสอบว่าตำแหน่งใน EEPROM ว่าง (ค่า 255 หมายถึงว่าง)
        for (int j = 0; j < CARD_SIZE; j++) {           // วนลูปตามขนาดของการ์ดเพื่อบันทึกลงใน EEPROM
          EEPROM.write(i + j, card[j]);                 // เขียนข้อมูลการ์ดทีละตัวอักษรลงใน EEPROM
        }
        return true;  // บันทึกการ์ดสำเร็จ
      }
    }
  }
  return false;  // ถ้าการ์ดถูกลงทะเบียนแล้ว หรือไม่มีพื้นที่ว่างใน EEPROM จะคืนค่า false
}

bool removeCardFromEEPROM(String card) {              // ฟังก์ชันสำหรับลบการ์ดจาก EEPROM
  for (int i = 0; i < EEPROM_SIZE; i += CARD_SIZE) {  // วนลูปผ่าน EEPROM ทีละบล็อกตามขนาดของการ์ด (CARD_SIZE)
    String storedCard = "";                           // สร้างตัวแปรเก็บการ์ดที่อ่านได้จาก EEPROM
    for (int j = 0; j < CARD_SIZE; j++) {             // วนลูปตามขนาดของการ์ด
      storedCard += char(EEPROM.read(i + j));         // อ่านข้อมูลการ์ดจาก EEPROM และเพิ่มลงในตัวแปร storedCard
    }
    if (storedCard == card) {                // ถ้าการ์ดที่อ่านได้ตรงกับการ์ดที่ต้องการลบ
      for (int j = 0; j < CARD_SIZE; j++) {  // วนลูปตามขนาดของการ์ด
        EEPROM.write(i + j, 255);            // เขียนค่า 255 ลงใน EEPROM เพื่อลบการ์ดออก
      }
      return true;  // การลบสำเร็จ
    }
  }
  return false;  // ถ้าหากไม่พบการ์ดที่ต้องการลบใน EEPROM จะคืนค่า false
}



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
  }
  for (int x = 1; x < 19; x++) {
    lcd.setCursor(x, 3);
    lcd.write((byte)4);
  }
  // ขอบซ้าย/ขวา
  for (int y = 1; y < 3; y++) {
    lcd.setCursor(0, y);
    lcd.write((byte)5);
  }
  for (int y = 1; y < 3; y++) {
    lcd.setCursor(19, y);
    lcd.write((byte)5);
  }
}




// วาดกล่อง [ n ] (หรือซ่อนกล่อง — แสดงเฉพาะตัวเลข)
void drawChoiceBox(bool show) {
  // เคลียร์พื้นที่ 10..14 x 0..2 ก่อน
  for (uint8_t y = 0; y <= 2; y++) {
    lcd.setCursor(BOX_LEFT_X, BOX_TOP_Y + y);
    lcd.print("     ");
  }
  if (currentChoice <= 0) {  // 0=งด → เราใช้ "00"
    lcd.setCursor(12, 1);
    if (show) lcd.print("00");
    else lcd.print("  ");
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
    // โหมดซ่อนกรอบ: โชว์เฉพาะตัวเลขตรงกลาง (ลอย ๆ)
    lcd.setCursor(12, 1);
    lcd.print(currentChoice);
  }
}

void drawVoteUI_base() {
  loadIcons();
  drawFrame();
  // หัวข้อ + arrow + hint
  lcd.setCursor(POS_TITLE_X, POS_TITLE_Y);
  lcd.print("Select: 0=Abstain");
  lcd.setCursor(POS_ARROW_X, POS_ARROW_Y);
  lcd.write((byte)6);  // ▶
  lcd.setCursor(POS_HINT_X, POS_HINT_Y);
  lcd.print("#=Confirm  *=Clear");

  // เคอร์เซอร์กระพริบตรงบรรทัดคำสั่ง (ให้รู้ว่า active)
  lcd.setCursor(POS_HINT_X, POS_HINT_Y);
  lcd.blink();

  // ถ้ายังไม่เลือกอะไร
  if (currentChoice < 0) {
    lcd.setCursor(2, 0);
    lcd.print("Choose 1..9     ");
  } else if (currentChoice == 0) {
    lcd.setCursor(2, 0);
    lcd.print("Choice: Abstain ");
  } else {
    lcd.setCursor(2, 0);
    lcd.print("Choice:         ");
  }
  // พื้นที่กล่อง
  drawChoiceBox(currentChoice >= 0 ? true : false);

  // รีเซ็ตอนิเมชัน
  spinIdx = 0;
  lastSpinMs = millis();
  boxOn = true;
  lastBlinkMs = millis();
  // วาดเฟรมแรกของสปินเนอร์
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
  // blink กล่องเลข (ถ้ามีการเลือกแล้ว)
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
  lcd.print("Confirmed:");
  if (currentChoice == 0) {
    lcd.setCursor(13, 1);
    lcd.print("Abstain");
  } else {
    lcd.setCursor(13, 1);
    lcd.print("No.");
    lcd.print(currentChoice);
  }
  lcd.setCursor(1, 2);
  lcd.write((byte)7);  // ✓
  lcd.setCursor(18, 2);
  lcd.write((byte)7);
  lcd.setCursor(5, 3);
  lcd.print("Saved! Thank you");
}

// Hook: เรียกเมื่อยืนยัน
void onConfirmVote(int choice) {
  Serial.print("CF:");
  Serial.println(choice);  // ไว้ซิงก์ภายหลัง
}
