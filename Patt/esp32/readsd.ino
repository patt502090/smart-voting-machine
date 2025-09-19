#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>

#define SD_CS 5          // CS ของ SD การ์ด
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

  // แชร์ SPI เดียวกับจอ
  if (!SD.begin(SD_CS, tft.getSPIinstance())) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD mount failed", 10, 10);
    Serial.println("SD mount failed");
    return;
  }

  // แสดงชนิดและขนาด
  uint8_t ct = SD.cardType();
  const char* type = (ct==CARD_MMC)?"MMC":(ct==CARD_SD)?"SDSC":(ct==CARD_SDHC)?"SDHC":"UNKNOWN";
  uint64_t sizeMB = SD.cardSize() / (1024ULL*1024ULL);
  Serial.printf("Mounted: %s, %llu MB\n", type, sizeMB);

  // ลิสต์ไฟล์ใน ROOT
  showRootOnTFT();
}

void loop() {
  // ไม่ทำอะไรต่อ ปล่อยให้ดูรายการไฟล์
}