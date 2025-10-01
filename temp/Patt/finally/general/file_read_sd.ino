#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>

#define SD_CS 13        // CS ของ SD การ์ด
#define TFT_CS 15        // ถ้ามีขา CS ของจอ ให้ตั้งไว้เพื่อดัน HIGH กันบัสชน

TFT_eSPI tft;

void showRootOnTFT() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  int y = 4, count = 0;
  tft.drawString("Files in / :", 4, y); y += 14;

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    tft.drawString("Open / failed", 4, y);
    Serial.println("Open / failed");
    return;
  }

  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) {
      String name = f.name(); if (name.length() > 24) name = name.substring(0,24);
      tft.drawString(String(++count) + ". <DIR> " + name, 4, y); y += 14;
      Serial.printf("   <DIR> %s\n", f.name());
    } else {
      String name = f.name(); if (name.length() > 24) name = name.substring(0,24);
      tft.drawString(String(++count) + ". " + name, 4, y); y += 14;
      Serial.printf("%8u  %s\n", (unsigned)f.size(), f.name());
    }
    if (y > tft.height() - 14) break; // พื้นที่จอพอประมาณ
  }

  if (count == 0) {
    tft.drawString("** Empty or no files **", 4, y);
    Serial.println("** Empty or no files **");
  }
}

void setup() {
  Serial.begin(115200);

  // กันบัสชนกับจอ: ดัน CS จอ HIGH (ถ้าจอมีขา CS)
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Mounting SD...", 10, 10);

  // แชร์ SPI เดียวกับจอ - retry ไปเรื่อยๆจนกว่าจะสำเร็จ
  int retryCount = 0;
  while (!SD.begin(SD_CS, tft.getSPIinstance())) {
    retryCount++;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD mount failed", 10, 10);
    tft.drawString("Retry: " + String(retryCount), 10, 30);
    tft.drawString("Please insert SD card", 10, 50);
    Serial.printf("SD mount failed - Retry %d\n", retryCount);
    delay(2000);  // รอ 2 วินาทีก่อนลองใหม่
  }
  
  // สำเร็จแล้ว - แสดงข้อความสำเร็จ
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("SD mounted successfully!", 10, 10);
  tft.drawString("After " + String(retryCount) + " retries", 10, 30);
  Serial.printf("SD mounted successfully after %d retries!\n", retryCount);
  delay(1500);

  // แสดงชนิดและขนาดå
  uint8_t ct = SD.cardType();
  const char* type = (ct==CARD_MMC)?"MMC":(ct==CARD_SD)?"SDSC":(ct==CARD_SDHC)?"SDHC":"UNKNOWN";
  uint64_t sizeMB = SD.cardSize() / (1024ULL*1024ULL);
  Serial.printf("Mounted: %s, %llu MB\n", type, sizeMB);

  // ลิสต์ไฟล์ใน ROOT
  showRootOnTFT();
}

void loop() {
  // ตรวจสอบ SD card status ทุก 5 วินาที
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      // SD card ถูกลบออก - แสดงข้อความและรอ
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("SD card removed!", 10, 10);
      tft.drawString("Please reinsert SD card", 10, 30);
      tft.drawString("System will retry...", 10, 50);
      Serial.println("SD card removed - waiting for reinsertion...");
      
      // รอจนกว่า SD card จะกลับมา
      while (SD.cardType() == CARD_NONE) {
        delay(1000);
        Serial.println("Waiting for SD card...");
      }
      
      // SD card กลับมาแล้ว - remount
      Serial.println("SD card detected - remounting...");
      SD.end();
      delay(1000);
      
      // Retry mount
      int retryCount = 0;
      while (!SD.begin(SD_CS, tft.getSPIinstance())) {
        retryCount++;
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("Remounting SD...", 10, 10);
        tft.drawString("Retry: " + String(retryCount), 10, 30);
        Serial.printf("SD remount failed - Retry %d\n", retryCount);
        delay(2000);
      }
      
      // สำเร็จแล้ว
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("SD remounted!", 10, 10);
      tft.drawString("After " + String(retryCount) + " retries", 10, 30);
      Serial.printf("SD remounted successfully after %d retries!\n", retryCount);
      delay(1500);
      
      // แสดงรายการไฟล์ใหม่
      showRootOnTFT();
    }
  }
  
  delay(100);
}