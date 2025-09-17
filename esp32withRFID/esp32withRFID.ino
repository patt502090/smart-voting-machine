
#define BLYNK_TEMPLATE_ID "TMPL6G6KsJzqK"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "RUBdFFrRrLJ99YHyTgYN5rew8gfkPzaH"

#include <Wire.h>  // ไลบรารีสำหรับการสื่อสาร I2C ซึ่งใช้ในการเชื่อมต่อระหว่างไมโครคอนโทรลเลอร์กับอุปกรณ์อื่นๆ
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>      // ไลบรารีสำหรับการสื่อสารแบบ SPI ซึ่งใช้เชื่อมต่อกับโมดูล RFID
#include <MFRC522.h>  // ไลบรารีสำหรับควบคุมการอ่านข้อมูลจากบัตร RFID โดยใช้โมดูล MFRC522
#include <EEPROM.h>   // ไลบรารีสำหรับการอ่านและเขียนข้อมูลใน EEPROM (หน่วยความจำถาวร) เพื่อเก็บข้อมูลบัตร RFID ที่ลงทะเบียนแล้ว
//#include <SoftwareSerial.h>     // ไลบรารีสำหรับการสื่อสารแบบอนุกรมเสมือนผ่านพอร์ต RX/TX เพื่อเชื่อมต่อกับอุปกรณ์อื่นๆ (เช่น โมดูลภายนอก)

HardwareSerial mySerial(2);  // UART2


// ข้อมูลการเชื่อมต่อ RFID
#define SS_PIN 5              // กำหนดขา SS ของโมดูล RFID ให้เชื่อมต่อกับพิน 10
#define RST_PIN 27            // กำหนดขา Reset ของโมดูล RFID ให้เชื่อมต่อกับพิน 5
const int EEPROM_SIZE = 512;  // กำหนดขนาดของ EEPROM ที่ใช้เก็บข้อมูล UID การ์ด (512 ไบต์)
const int CARD_SIZE = 8;      // กำหนดขนาดของ UID การ์ด (8 ไบต์) ที่เก็บใน EEPROM

//SoftwareSerial mySerial(8, 9);  // ใช้ SoftwareSerial สำหรับการสื่อสารกับอุปกรณ์อื่นๆ ผ่านขา Rx (พิน 8) และ Tx (พิน 9)


MFRC522 rfid(SS_PIN, RST_PIN);  // สร้างออบเจ็กต์ rfid สำหรับควบคุมโมดูล RFID โดยใช้ขา SS และ RST ที่กำหนด

String enteredCode = "";           // สร้างตัวแปรเก็บรหัสที่ผู้ใช้ป้อนจาก Keypad
unsigned long lastActionTime = 0;  // ตัวแปรเก็บเวลาล่าสุดที่มีการกระทำ (ใช้ในการควบคุมการปิดประตูอัตโนมัติ)


bool messageShown = false;  // ตัวแปรระบุว่าข้อความฉุกเฉินแสดงแล้วหรือยัง

bool registrationMode = false;            // สถานะการลงทะเบียนการ์ดใหม่
bool deleteMode = false;                  // สถานะการลบการ์ดจากระบบ
unsigned long lastKeypadPressTime = 0;    // เก็บเวลาที่กดปุ่ม Keypad ครั้งล่าสุด
const unsigned long debounceDelay = 200;  // หน่วงเวลา 200 มิลลิวินาที เพื่อป้องกันการกดปุ่มซ้ำ (ป้องกันการรบกวนจากการกดปุ่มหลายครั้ง)




// กำหนด pin ใหม่
const int buzzerPin = 12;   // ย้ายการเชื่อมต่อ Buzzer ไปยังพิน 6
const int servoPin = 7;     // ย้ายการเชื่อมต่อ Servo Motor ไปยังพิน 7
const int switchPin4 = 33;  // ย้ายการเชื่อมต่อสวิตช์ไปยังพิน 4
const int switchPin2 = 32;  // ย้ายการเชื่อมต่อสวิตช์ไปยังพิน 2
const int ledPin = 13;      // กำหนดให้ LED เชื่อมต่อที่พิน 3

void setup() {
  Wire.begin();  // เริ่มต้นการสื่อสารผ่าน I2C สำหรับอุปกรณ์ที่เชื่อมต่อด้วย I2C (เช่น Keypad, LCD)

  Serial.begin(115200);  // เริ่มต้นการสื่อสารอนุกรม (Serial) ด้วยบอดเรต 9600 สำหรับการดีบัก
  //mySerial.begin(9600);  // เริ่มต้นการสื่อสารอนุกรมเสมือน (SoftwareSerial) ด้วยบอดเรต 9600 เพื่อสื่อสารกับอุปกรณ์ภายนอก (เช่น โค้ดหรือบอร์ดอื่น)
  EEPROM.begin(EEPROM_SIZE);
  mySerial.begin(115200, SERIAL_8N1, 16, 17);

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
  Serial.println(switchDel);
  if (switchReg == LOW) {
    Serial.println("reg");
    registerCard();

  } else if (switchDel == LOW) {
    Serial.println("del");
    deleteCard();
  }


 
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {  // ถ้ามีการ์ดใหม่เข้ามาและอ่านข้อมูลการ์ดได้
                                      
    mySerial.println("Scanning");                                           // แสดงข้อความ "Scanning" บนจอ LCD
    Serial.print("NUID tag is :");                                   // แสดงข้อความเริ่มต้นใน Serial Monitor
    String ID = "";                                                  // ประกาศตัวแปรเพื่อเก็บ ID ของการ์ด
    for (byte i = 0; i < rfid.uid.size; i++) {                       // วนลูปเพื่ออ่าน UID ของการ์ด
                                                   // แสดงจุดบนจอ LCD เพื่อแสดงการทำงาน
      ID.concat(String(rfid.uid.uidByte[i] < 0x10 ? "0" : ""));      // เพิ่มศูนย์หน้าถ้าค่าต่ำกว่า 16 (0x10)
      ID.concat(String(rfid.uid.uidByte[i], HEX));                   // แปลงค่า UID เป็นเลขฐาน 16 และเพิ่มเข้าไปใน ID
      delay(10);                                                     // หน่วงเวลา 300 มิลลิวินาทีเพื่อให้การแสดงผลชัดเจน
    }
    ID.toUpperCase();  // เปลี่ยน ID ให้เป็นตัวพิมพ์ใหญ่

    // ลบช่องว่างทั้งหมดออกเพื่อให้แน่ใจว่าการเปรียบเทียบเป็นไปอย่างถูกต้อง
    ID.replace(" ", "");  // ลบช่องว่างใน ID
    Serial.println(ID);   // แสดง ID ของการ์ดใน Serial Monitor

    if (isCardRegistered(ID)) {    // ตรวจสอบว่าการ์ด RFID ได้รับอนุญาตแล้วหรือไม่
      Serial.println("found");     // ถ้าการ์ดถูกลงทะเบียน ให้เรียกฟังก์ชัน activateMotor() เพื่อเปิดประตู
    } else {                       // ถ้าการ์ดไม่ถูกลงทะเบียน
      
      mySerial.println("Wrong card!");    // แสดงข้อความ "Wrong card!" บนจอ LCD
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
}



void registerCard() {          // ฟังก์ชันสำหรับลงทะเบียนการ์ดใหม่
  String newUID = "";          // ประกาศตัวแปรสำหรับเก็บ UID ของการ์ดใหม่
  mySerial.println("Scan new card");  // แสดงข้อความ "Scan new card" บนจอ LCD
  Serial.println("Registration");
  // รอการสแกนการ์ดใหม่
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {  // ถ้ายังไม่มีการสแกนการ์ดใหม่
                                                                          // ตรวจสอบการกดปุ่มจาก Keypad
    Serial.println("Registration cancelled.");

    delay(50);  // หน่วงเวลาสำหรับการตรวจสอบครั้งต่อไป
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
    //lcd.clear();                           // ล้างหน้าจอ LCD
    //lcd.setCursor(0, 0);                   // ตั้งตำแหน่งเคอร์เซอร์ที่แถวที่ 0 คอลัมน์ที่ 0
    mySerial.println("Card Registered!");         // แสดงข้อความ "Card Registered!" บนจอ LCD
    /*tone(buzzerPin, 1000, 200);            // ส่งเสียงเตือนครั้งที่ 1
    digitalWrite(ledPin, HIGH);            // เปิด LED เพื่อแสดงสถานะ
    delay(300);                            // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);             // ปิด LED
    delay(300);                            // รอ 300 มิลลิวินาที
    tone(buzzerPin, 1000, 200);            // ส่งเสียงเตือนครั้งที่ 2
    digitalWrite(ledPin, HIGH);            // เปิด LED อีกครั้ง
    delay(300);                            // รอ 300 มิลลิวินาที
    digitalWrite(ledPin, LOW);             // ปิด LED*/
  } else {                                 // ถ้าการบันทึกการ์ดใน EEPROM ล้มเหลว
    /*lcd.clear();                           // ล้างหน้าจอ LCD
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
    digitalWrite(ledPin, LOW);             // ปิด LED*/
  }

  //displaySystemReady();  // แสดงสถานะว่าระบบพร้อมทำงาน
}


void deleteCard() {             // ฟังก์ชันสำหรับลบการ์ดที่ลงทะเบียนในระบบ
  String cardToDelete = "";     // ประกาศตัวแปรสำหรับเก็บ UID ของการ์ดที่จะลบ
  
  //lcd.print("Scan to delete");  // แสดงข้อความ "Scan to delete" บนจอ LCD

  // รอการสแกนการ์ดเพื่อทำการลบ
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {  // ตรวจสอบว่ามีการ์ดใหม่ถูกสแกนหรือไม่


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
    /*lcd.clear();                             // ล้างหน้าจอ LCD
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
    digitalWrite(ledPin, LOW);               // ปิด LED**/
  } else {                                   // ถ้าการ์ดไม่ถูกพบใน EEPROM
    Serial.println("Card not found");        // แสดงข้อความ "Card not found" ใน Serial Monitor
    /*lcd.clear();                             // ล้างหน้าจอ LCD
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
    digitalWrite(ledPin, LOW);               // ปิด LED**/
  }
  delay(100);  // รอ 2 วินาที
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