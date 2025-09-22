#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <JPEGDecoder.h>

#define SD_CS 5
#define TFT_CS 15

TFT_eSPI tft;

bool isJpg(const String& s) { String x=s; x.toLowerCase(); return x.endsWith(".jpg") || x.endsWith(".jpeg"); }

// เก็บรายชื่อรูปจากราก /
const int MAX_FILES = 64;
String files[MAX_FILES];
int fileCount = 0;

/**
 * สแกนไฟล์ที่อยู่ในรากของ SD Card หาคนที่มีนามสกุลจบ ".jpg" หรือ ".jpeg"
 * และเก็บชื่อไฟล์ลงในตาราง files[], fileCount จะมีค่าเท่ากับจำนวนไฟล์ที่สแกนได้
 * หลังจากนั้นจะเรียงชื่อไฟล์ให้นิ่ง (เช่น 0.jpg..9.jpg)
 */
void scanRootJpegs() {
  fileCount = 0;
  File root = SD.open("/");
  if (!root || !root.isDirectory()) return;

  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    String name = f.name();                   // อาจเป็น "0.jpg" หรือ "/0.jpg"
    if (name.startsWith(".") || name.startsWith("._")) continue; // ข้ามไฟล์ซ่อน macOS
    if (!name.startsWith("/")) name = "/" + name;
    if (isJpg(name) && fileCount < MAX_FILES) files[fileCount++] = name;
  }

  // เรียงชื่อไฟล์ให้นิ่ง (เช่น 0.jpg..9.jpg)
  for (int i=0;i<fileCount;i++)
    for (int j=i+1;j<fileCount;j++)
      if (files[j].compareTo(files[i]) < 0) {
        String tmp = files[i]; files[i] = files[j]; files[j] = tmp;
      }
}

void drawJpegFull(const char* path) {
  File jf = SD.open(path, FILE_READ);
  if (!jf) { Serial.printf("Open fail: %s\n", path); return; }
  if (!JpegDec.decodeSdFile(jf)) { Serial.println("Unsupported JPEG"); return; }

  int W = tft.width(), H = tft.height();
  int w = JpegDec.width, h = JpegDec.height;

  // ตำแหน่งวาด: ถ้าเท่าจอจะได้ (0,0); ถ้าเล็กกว่า/ใหญ่กว่า จะจัดกึ่งกลาง
  int x = (W - w) / 2;
  int y = (H - h) / 2;

  bool old = tft.getSwapBytes();
  tft.setSwapBytes(true);

  while (JpegDec.read()) {
    uint16_t* p   = JpegDec.pImage;
    int mcu_w     = JpegDec.MCUWidth;
    int mcu_h     = JpegDec.MCUHeight;
    int mcu_x     = JpegDec.MCUx * mcu_w + x;
    int mcu_y     = JpegDec.MCUy * mcu_h + y;

    int win_w = (JpegDec.MCUx == JpegDec.MCUSPerRow - 1 && (w % mcu_w)) ? (w % mcu_w) : mcu_w;
    int win_h = (JpegDec.MCUy == JpegDec.MCUSPerCol - 1 && (h % mcu_h)) ? (h % mcu_h) : mcu_h;

    if (mcu_x >= 0 && mcu_y >= 0 && (mcu_x + win_w) <= tft.width() && (mcu_y + win_h) <= tft.height()) {
      tft.pushImage(mcu_x, mcu_y, win_w, win_h, p);
    } else if ((mcu_y + win_h) > tft.height()) {
      JpegDec.abort(); // หลุดล่างแล้ว หยุด
    }
  }

  tft.setSwapBytes(old);
}

void setup() {
  Serial.begin(115200);

  // กันบัสชนกับจอ: ดัน CS จอ HIGH
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);

  tft.begin();
  tft.setRotation(2);                // แนวตั้ง
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Mount SD...", 10, 10);

  if (!SD.begin(SD_CS, tft.getSPIinstance())) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD mount failed", 10, 10);
    return;
  }

  scanRootJpegs();

  if (fileCount == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("No JPG in /", 10, 10);
  }
}

void loop() {
  if (fileCount == 0) return;

  static int idx = 0;

  tft.fillScreen(TFT_BLACK);
  drawJpegFull(files[idx].c_str());
  delay(5000);                       // รูปละ 5 วิ

  idx = (idx + 1) % fileCount;       // วนไปเรื่อย ๆ
}