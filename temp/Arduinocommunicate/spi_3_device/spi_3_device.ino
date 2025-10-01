/*#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <MFRC522.h>

#define SD_CS   13
#define TFT_CS  15
#define RFID_CS 5
#define RST_PIN 27  // RFID reset

TFT_eSPI tft = TFT_eSPI();
MFRC522 rfid(RFID_CS, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin();

  // ตั้ง CS ทุกตัว HIGH ก่อน
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);
  pinMode(RFID_CS, OUTPUT); digitalWrite(RFID_CS, HIGH);

  delay(1000);
  Serial.println("=== SPI CS Test ===");

  // ทดสอบ SD Card
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(SD_CS, LOW); // เลือก SD
  if (SD.begin(SD_CS)) {
    Serial.println("SD Card: OK");
  } else {
    Serial.println("SD Card: FAIL");
  }
  digitalWrite(SD_CS, HIGH); // ปิด SD

  // ทดสอบ TFT
  digitalWrite(SD_CS, HIGH);
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(TFT_CS, LOW); // เลือก TFT
  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TFT OK", 10, 10);
  Serial.println("TFT: Initialized");
  digitalWrite(TFT_CS, HIGH); // ปิด TFT

  // ทดสอบ RFID
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(RFID_CS, LOW); // เลือก RFID
  rfid.PCD_Init();
  if (rfid.PCD_PerformSelfTest()) {
    Serial.println("RFID: OK (Self test passed)");
  } else {
    Serial.println("RFID: FAIL (Self test)");
  }
  digitalWrite(RFID_CS, HIGH); // ปิด RFID

  Serial.println("=== Test Finished ===");
}

void loop() {
  // วนรอเฉยๆ
}*/
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <MFRC522.h>

#define SD_CS   13
#define TFT_CS  15
#define RFID_CS 5
#define RST_PIN 27  // RFID reset

TFT_eSPI tft = TFT_eSPI();
MFRC522 rfid(RFID_CS, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin();

  // ตั้ง CS ทุกตัว HIGH ก่อน
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);
  pinMode(RFID_CS, OUTPUT); digitalWrite(RFID_CS, HIGH);

  delay(1000);
  Serial.println("=== SPI CS Test ===");

  // --- ทดสอบ SD Card ---
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(SD_CS, LOW);
  if (SD.begin(SD_CS)) Serial.println("SD Card: OK");
  else Serial.println("SD Card: FAIL");
  digitalWrite(SD_CS, HIGH);

  // --- ทดสอบ TFT ---
  digitalWrite(SD_CS, HIGH);
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(TFT_CS, LOW);
  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TFT OK", 10, 10);
  Serial.println("TFT: Initialized");
  digitalWrite(TFT_CS, HIGH);

  // --- ทดสอบ RFID ---
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(RFID_CS, LOW);
  rfid.PCD_Init();
  Serial.println("RFID: Initialized");
  digitalWrite(RFID_CS, HIGH);

  Serial.println("=== Setup Finished ===");
}

void loop() {
  // --- ปิดอุปกรณ์อื่นก่อนอ่าน RFID ---
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);

  // --- อ่าน RFID ---
  digitalWrite(RFID_CS, LOW);
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    Serial.print("RFID Card UID: ");
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
      Serial.print(rfid.uid.uidByte[i], HEX);
    }
    Serial.println();
    rfid.PICC_HaltA(); // หยุดการสื่อสารกับการ์ด
  }
  digitalWrite(RFID_CS, HIGH);

  delay(500);
}
