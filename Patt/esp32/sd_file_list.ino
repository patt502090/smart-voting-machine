#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>

#define SD_CS 13        // CS ของ SD การ์ด
#define TFT_CS 15       // CS ของจอ (ถ้ามี)

TFT_eSPI tft;

void showFileList() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  int y = 4, count = 0;
  tft.drawString("Files in SD Card:", 4, y); 
  y += 16;

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    tft.drawString("Cannot open /", 4, y);
    Serial.println("Cannot open root directory");
    return;
  }

  // แสดงไฟล์ทั้งหมดใน root directory
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) {
      // แสดงโฟลเดอร์
      String name = f.name(); 
      if (name.length() > 20) name = name.substring(0, 20) + "...";
      tft.drawString(String(++count) + ". [DIR] " + name, 4, y); 
      y += 14;
      Serial.printf("   [DIR] %s\n", f.name());
    } else {
      // แสดงไฟล์
      String name = f.name(); 
      if (name.length() > 20) name = name.substring(0, 20) + "...";
      tft.drawString(String(++count) + ". " + name, 4, y); 
      y += 14;
      Serial.printf("%8u bytes - %s\n", (unsigned)f.size(), f.name());
    }
    
    // จำกัดจำนวนบรรทัดที่แสดงบนจอ
    if (y > tft.height() - 14) {
      tft.drawString("... (more files)", 4, y);
      break;
    }
  }

  if (count == 0) {
    tft.drawString("No files found", 4, y);
    Serial.println("No files found in root directory");
  } else {
    tft.drawString("Total: " + String(count) + " items", 4, tft.height() - 14);
    Serial.printf("Total files and directories: %d\n", count);
  }
  
  root.close();
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 SD Card File List");

  // ป้องกันการชนกันของ SPI bus
  pinMode(TFT_CS, OUTPUT); 
  digitalWrite(TFT_CS, HIGH);

  // เริ่มต้นจอ TFT
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Initializing...", 10, 10);

  // พยายามเชื่อมต่อ SD Card
  int retryCount = 0;
  while (!SD.begin(SD_CS, tft.getSPIinstance())) {
    retryCount++;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD Card Error", 10, 10);
    tft.drawString("Retry: " + String(retryCount), 10, 30);
    tft.drawString("Check SD card", 10, 50);
    Serial.printf("SD mount failed - Retry %d\n", retryCount);
    delay(2000);
  }
  
  // แสดงข้อความสำเร็จ
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("SD Card Ready!", 10, 10);
  tft.drawString("Retries: " + String(retryCount), 10, 30);
  Serial.printf("SD card mounted successfully after %d retries\n", retryCount);
  delay(1500);

  // แสดงข้อมูล SD Card
  uint8_t cardType = SD.cardType();
  const char* type = (cardType == CARD_MMC) ? "MMC" : 
                     (cardType == CARD_SD) ? "SDSC" : 
                     (cardType == CARD_SDHC) ? "SDHC" : "UNKNOWN";
  uint64_t sizeMB = SD.cardSize() / (1024ULL * 1024ULL);
  
  Serial.printf("Card Type: %s\n", type);
  Serial.printf("Card Size: %llu MB\n", sizeMB);

  // แสดงรายการไฟล์
  showFileList();
}

void loop() {
  // ตรวจสอบสถานะ SD Card ทุก 10 วินาที
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      // SD Card ถูกลบออก
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("SD Card Removed!", 10, 10);
      tft.drawString("Please reinsert", 10, 30);
      Serial.println("SD card removed");
      
      // รอจนกว่า SD Card จะกลับมา
      while (SD.cardType() == CARD_NONE) {
        delay(1000);
      }
      
      // Remount SD Card
      Serial.println("SD card detected - remounting...");
      SD.end();
      delay(1000);
      
      while (!SD.begin(SD_CS, tft.getSPIinstance())) {
        delay(1000);
        Serial.println("Remounting SD card...");
      }
      
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("SD Card Restored!", 10, 10);
      Serial.println("SD card remounted successfully");
      delay(1500);
      
      // แสดงรายการไฟล์ใหม่
      showFileList();
    }
  }
  
  delay(100);
}
