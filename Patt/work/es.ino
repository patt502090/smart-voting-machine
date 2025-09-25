#define BLYNK_TEMPLATE_ID "TMPL6G6KsJzqK"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "RUBdFFrRrLJ99YHyTgYN5rew8gfkPzaH"

#define WAKE_PIN 33
#include <algorithm>

// ==== must be the very first lines ====
const int UID_HEX_MAX = 16;
struct Rec;

void readRec(int idx, Rec &r);        // tell IDE not to autogenerate wrong prototypes
void writeRec(int idx, const Rec &r); // uses incomplete type by reference (OK)

static int g_selectedCandidate = -1;
static bool g_waitingChoice = false; // อยู่ช่วงรอผู้ใช้เลือก

#include "driver/rtc_io.h" // สำหรับ rtc_gpio_get_level()
#include "esp_system.h"

// ประกาศล่วงหน้าค่าคงที่ที่ struct ใช้ (ถ้าคุณมีเวอร์ชันเป็น #define อยู่แล้ว ข้ามได้)
#if 0 // DISABLE: duplicates UID_HEX_MAX (we already #define it at top)
const int      UID_HEX_MAX = 16;
#endif

// ต้อง “นิยาม” struct Rec ให้เสร็จก่อนฟังก์ชัน readRec()/writeRec()
// (forward declare เฉยๆ ไม่พอ เพราะฟังก์ชันแตะฟิลด์ใน struct)
#if 0 // DISABLE: duplicate struct Rec (already defined at top)
struct Rec {
  char     uid[UID_HEX_MAX];
  uint8_t  fp_id;
  uint8_t  voted;
  uint8_t  valid;
  uint8_t  reserved;
};
#endif

#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Adafruit_Fingerprint.h>

// ===== UI (no-image) =====
enum UIState
{
  UI_BOOT,
  UI_READY,
  UI_SCAN_CARD,
  UI_CARD_OK,
  UI_CARD_FAIL,
  UI_SCAN_FINGER,
  UI_FINGER_OK,
  UI_FINGER_FAIL,
  UI_CONFIRM,
  UI_THANKS,
  UI_ERROR,
  UI_SLEEP,
  UI_WAKE,
  UI_WAIT_CHOICE, // รอผู้ใช้เลือกผู้สมัคร
  UI_SELECTED,    // แสดงว่าผู้ใช้เลือกหมายเลขอะไรแล้ว
  UI_SENDING      // ขณะกำลังส่ง/รอผล
};
static bool uiShownScanCard = false;
static uint32_t uiScanCardShownAt = 0;

UIState g_lastState = UI_READY;
static String g_lastSubtitle;                       // จำ subtitle ล่าสุด
static uint32_t g_lastPaintMs = 0;                  // ไว้คุมคูลดาวน์ (ถ้าต้องการ)
static const uint16_t SAME_STATE_COOLDOWN_MS = 350; // กันสั่น (ปรับได้/จะปิดก็ได้)
static bool isShowingPhoto = false;

// [ADD] จอ + SD + JPG decoder
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <SD.h>

// [ADD] CS pins (ยึดตามฮาร์ดแวร์คุณ)
#define SD_CS 13
#define TFT_CS 15
// RFID_CS = SS_PIN (=5) มีอยู่แล้วจากโค้ดคุณ

// [ADD] สร้างอ็อบเจ็กต์จอ
TFT_eSPI tft;
// ===== Modern UI Transition =====
TFT_eSprite spr(&tft);

enum UITrans
{
  TR_NONE,
  TR_SLIDE_L,
  TR_SLIDE_R,
  TR_SLIDE_UP,
  TR_SLIDE_DOWN,
  TR_FADE
};

// Easing นุ่มๆ (0..1 -> 0..1)
static inline float easeInOutQuad(float x)
{
  return (x < 0.5f) ? 2 * x * x : 1 - powf(-2 * x + 2, 2) / 2;
}

// ===== Added: Deep-sleep support =====
#include "esp_sleep.h"

#include <math.h>

// ==== [ADD] Wake-pin debug helpers (no change to existing code) ====
volatile uint32_t WAKE_edges = 0;
volatile uint32_t WAKE_lastMs = 0;

IRAM_ATTR void WAKE_isr()
{
  // นับทุกครั้งที่มีขอบขึ้น/ลง
  WAKE_edges++;
  WAKE_lastMs = millis();
}

// อ่านระดับจากทั้ง digital และ RTC domain
int wake_digital()
{
  return digitalRead(WAKE_PIN);
}
int wake_rtc()
{
  return rtc_gpio_get_level((gpio_num_t)WAKE_PIN);
}

void dbgPrintWakePin(const char *tag)
{
  Serial.print("[WAKEDBG] ");
  Serial.print(tag);
  Serial.print("  digital=");
  Serial.print(wake_digital());
  Serial.print("  rtc=");
  Serial.print(wake_rtc());
  Serial.print("  edges=");
  Serial.print(WAKE_edges);
  Serial.print("  lastMs=");
  Serial.println(WAKE_lastMs);
}

// --------- Helpers: geometry & text ----------
void fillGradientV(uint16_t c1, uint16_t c2)
{
  // ไล่สีแนวตั้งทั้งจอ
  int W = tft.width(), H = tft.height();
  for (int y = 0; y < H; ++y)
  {
    // linear blend 0..1
    float k = (float)y / (float)(H - 1);
    uint16_t r1 = ((c1 >> 11) & 0x1F), g1 = ((c1 >> 5) & 0x3F), b1 = (c1 & 0x1F);
    uint16_t r2 = ((c2 >> 11) & 0x1F), g2 = ((c2 >> 5) & 0x3F), b2 = (c2 & 0x1F);
    uint16_t r = r1 + (int)((r2 - r1) * k);
    uint16_t g = g1 + (int)((g2 - g1) * k);
    uint16_t b = b1 + (int)((b2 - b1) * k);
    uint16_t c = (r << 11) | (g << 5) | b;
    tft.drawFastHLine(0, y, W, c);
  }
}

int16_t centerX(const String &s, int font)
{
  int w = tft.textWidth(s, font);
  return (tft.width() - w) / 2;
}
void drawCenter(const String &s, int y, int font, uint16_t fg, uint16_t bg = TFT_TRANSPARENT)
{
  tft.setTextColor(fg, bg);
  tft.drawString(s, centerX(s, font), y, font);
}

// --------- Badge / Icons (vector-ish) ----------
void drawShieldHeader(const char *title)
{
  // แถบบนเข้ม + โล่กลาง
  tft.fillRect(0, 0, tft.width(), 30, TFT_BLACK);
  tft.drawRect(0, 0, tft.width(), 30, TFT_WHITE);
  drawCenter(title, 6, 2, TFT_WHITE, TFT_BLACK);

  int cx = tft.width() / 2;
  // โล่ (polygon แบบง่าย)
  tft.fillTriangle(cx - 20, 34, cx + 20, 34, cx, 64, TFT_DARKGREY);
  tft.fillTriangle(cx - 16, 36, cx + 16, 36, cx, 60, TFT_NAVY);
  // ขีดใต้โล่
  tft.fillRect(cx - 26, 70, 52, 3, TFT_WHITE);
}

void drawCheckBadge(int cx, int cy)
{
  tft.fillCircle(cx, cy, 34, TFT_DARKGREEN);
  tft.fillCircle(cx, cy, 30, TFT_GREEN);
  // เช็ค
  tft.drawLine(cx - 16, cy, cx - 6, cy + 12, TFT_WHITE);
  tft.drawLine(cx - 6, cy + 12, cx + 16, cy - 14, TFT_WHITE);
  tft.drawLine(cx - 17, cy, cx - 7, cy + 12, TFT_WHITE);
  tft.drawLine(cx - 7, cy + 12, cx + 17, cy - 14, TFT_WHITE);
}

void drawCrossBadge(int cx, int cy)
{
  tft.fillCircle(cx, cy, 34, TFT_MAROON);
  tft.fillCircle(cx, cy, 30, TFT_RED);
  // กากบาท
  for (int i = -1; i <= 1; i++)
  {
    tft.drawLine(cx - 16, cy - 16 + i, cx + 16, cy + 16 + i, TFT_WHITE);
    tft.drawLine(cx - 16, cy + 16 + i, cx + 16, cy - 16 + i, TFT_WHITE);
  }
}

void drawCardIcon(int cx, int cy)
{
  // การ์ดสี่เหลี่ยม + แถบแม่เหล็ก
  tft.fillRoundRect(cx - 48, cy - 28, 96, 56, 8, TFT_DARKGREY);
  tft.fillRoundRect(cx - 46, cy - 26, 92, 52, 8, TFT_WHITE);
  tft.fillRect(cx - 46, cy - 6, 92, 14, TFT_NAVY);
  tft.fillRect(cx - 40, cy - 20, 40, 6, TFT_LIGHTGREY);
}

// overload สำหรับ sprite
void drawArc(TFT_eSprite &s, int cx, int cy, int rOuter, int rInner, int a0, int a1, uint16_t col, uint16_t bg)
{
  for (int a = a0; a <= a1; a += 3)
  {
    float rad = a * 0.0174533f;
    int x0 = cx + (int)(rInner * cos(rad));
    int y0 = cy + (int)(rInner * sin(rad));
    int x1 = cx + (int)(rOuter * cos(rad));
    int y1 = cy + (int)(rOuter * sin(rad));
    s.drawLine(x0, y0, x1, y1, col);
  }
}
// แล้วแก้ใน drawNFCIcon / drawFingerprintIconModern ให้เรียก drawArc(s, ...)

void drawFingerIcon(int cx, int cy)
{
  // วงลายนิ้ว
  tft.drawCircle(cx, cy, 30, TFT_CYAN);
  tft.drawCircle(cx, cy, 24, TFT_CYAN);
  tft.drawCircle(cx, cy, 18, TFT_CYAN);
  tft.drawCircle(cx, cy, 12, TFT_CYAN);
  // โค้งชั้นๆ
  drawArc(cx, cy, 28, 27, 210, 330, TFT_CYAN, TFT_BLACK);
  drawArc(cx, cy, 22, 21, 200, 340, TFT_CYAN, TFT_BLACK);
  drawArc(cx, cy, 16, 15, 190, 350, TFT_CYAN, TFT_BLACK);
}

// --------- Arc helper (approx) ----------
void drawArc(int cx, int cy, int rOuter, int rInner, int a0, int a1, uint16_t col, uint16_t bg)
{
  // step 3° พอ
  for (int a = a0; a <= a1; a += 3)
  {
    float rad = a * 0.0174533f;
    int x0 = cx + (int)(rInner * cos(rad));
    int y0 = cy + (int)(rInner * sin(rad));
    int x1 = cx + (int)(rOuter * cos(rad));
    int y1 = cy + (int)(rOuter * sin(rad));
    tft.drawLine(x0, y0, x1, y1, col);
  }
}

// ----- Loading spinner -----
static bool ui_isLoading = false;
static uint32_t ui_loadStart = 0;

void uiSetLoading(bool on)
{
  ui_isLoading = on;
  if (on)
    ui_loadStart = millis();
}

// วาด spinner แบบกงล้อหมุน (non-blocking)
static void drawSpinner()
{
  const int cx = tft.width() / 2, cy = 160, r = 14;
  float t = (millis() - ui_loadStart) / 1000.0f; // วินาที
  // 12 แท่ง หมุนตามเวลา
  for (int i = 0; i < 12; i++)
  {
    float a = (i / 12.0f + fmodf(t, 1.0f)) * 2 * PI;
    int x0 = cx + (int)((r - 6) * cosf(a));
    int y0 = cy + (int)((r - 6) * sinf(a));
    int x1 = cx + (int)((r + 6) * cosf(a));
    int y1 = cy + (int)((r + 6) * sinf(a));
    uint16_t col = (i < 4) ? TFT_WHITE : ((i < 8) ? TFT_SILVER : TFT_DARKGREY);
    tft.drawLine(x0, y0, x1, y1, col);
  }
}

// --------- Scan border blink ----------
// --------- Modern scan border (glow + moving dash) ----------
static bool ui_isScanning = false;
static uint32_t ui_animStart = 0;

void uiSetScanning(bool on)
{
  ui_isScanning = on;
  if (on)
    ui_animStart = millis();
}

// วาดกรอบ dash รอบจอ โดยมี phase 0..1 เพื่อเลื่อน dash
static void drawFancyBorder(float phase)
{
  const int W = tft.width(), H = tft.height();
  // ความหนาและรัศมีมุม
  const int thick = 2;
  const int rad = 8;

  // pulse ความสว่าง 0.7..1.0
  float pulse = 0.7f + 0.3f * (0.5f - 0.5f * cosf(2.0f * 3.14159f * phase));
  // ผสมสีเหลือง-ขาวเบาๆ
  uint16_t col = (pulse > 0.85f) ? TFT_WHITE : TFT_YELLOW;

  // ลบขอบเก่าแบบเบาๆ: วาดกรอบใสก่อนเพื่อเคลียร์ (ใช้สีพื้น)
  tft.drawRoundRect(0, 0, W, H, rad, TFT_BLACK);
  tft.drawRoundRect(1, 1, W - 2, H - 2, rad, TFT_BLACK);

  // วาดเงาเรือง (outer glow) ชั้นนอกจางๆ
  tft.drawRoundRect(0, 0, W, H, rad, col);
  tft.drawRoundRect(1, 1, W - 2, H - 2, rad, col);

  // วาด dash เคลื่อนที่: เราจะแบ่ง per-seg แล้วเว้นช่อง
  const int seg = 8; // ยาวต่อ dash
  const int gap = 6; // ช่องว่าง
  const int per = seg + gap;

  auto drawDashedH = [&](int x0, int y, int len)
  {
    int offset = (int)(phase * per);
    for (int x = x0 - offset; x < x0 + len; x += per)
    {
      int x1 = max(x, x0);
      int x2 = min(x + seg, x0 + len);
      if (x2 > x1)
        tft.drawFastHLine(x1, y, x2 - x1, col);
    }
  };
  auto drawDashedV = [&](int x, int y0, int len)
  {
    int offset = (int)(phase * per);
    for (int y = y0 - offset; y < y0 + len; y += per)
    {
      int y1 = max(y, y0);
      int y2 = min(y + seg, y0 + len);
      if (y2 > y1)
        tft.drawFastVLine(x, y1, y2 - y1, col);
    }
  };

  // วาด dashed รอบกรอบด้านใน (เลี่ยงทับกับมุมโค้งมากเกินไป)
  int x = 2, y = 2, w = W - 4, h = H - 4;

  // ขอบบน/ล่าง
  drawDashedH(x + rad, y, w - 2 * rad);
  drawDashedH(x + rad, y + h - 1, w - 2 * rad);
  // ขอบซ้าย/ขวา
  drawDashedV(x, y + rad, h - 2 * rad);
  drawDashedV(x + w - 1, y + rad, h - 2 * rad);

  // เน้น “มุม” เล็กน้อย (จุดเล็กๆ)
  tft.fillCircle(x + rad, y + rad, 1, col);
  tft.fillCircle(x + w - 1 - rad, y + rad, 1, col);
  tft.fillCircle(x + rad, y + h - 1 - rad, 1, col);
  tft.fillCircle(x + w - 1 - rad, y + h - 1 - rad, 1, col);
}

void uiTick()
{
  // เส้นขอบวิ่งระหว่างสแกน
  if (ui_isScanning)
  {
    float t = (millis() - ui_animStart) / 600.0f;
    float phase = t - floorf(t);
    drawFancyBorder(phase);
  }
  // วาดสปินเนอร์ทับ (ถ้ามีโหลด)
  if (ui_isLoading)
  {
    drawSpinner();
  }
}

// ====== Modern vector icons (no SD needed) ======
void drawNFCIcon(TFT_eSprite &s, int cx, int cy, float scale = 1.0f)
{
  int w = int(110 * scale), h = int(70 * scale), r = int(14 * scale);
  // soft shadow
  s.fillRoundRect(cx - w / 2 + 3, cy - h / 2 + 5, w, h, r, TFT_DARKGREY);
  // card body
  s.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, TFT_WHITE);
  // top gradient bar
  for (int i = 0; i < int(18 * scale); ++i)
    s.drawFastHLine(cx - w / 2 + 6, cy - h / 2 + 10 + i, w - 12, TFT_NAVY + i);
  // chip
  int cw = int(22 * scale), ch = int(16 * scale), cr = int(4 * scale);
  s.fillRoundRect(cx - w / 2 + int(12 * scale), cy - int(h * 0.18f), cw, ch, cr, TFT_GOLD);
  s.drawRoundRect(cx - w / 2 + int(12 * scale), cy - int(h * 0.18f), cw, ch, cr, TFT_BROWN);
  // contactless waves
  uint16_t waveCol = TFT_CYAN;
  for (int k = 0; k < 3; k++)
  {
    int off = int(12 * scale + k * 8 * scale);
    drawArc(s, cx + int(w * 0.22f), cy - int(h * 0.02f),
            int(34 * scale) + off, int(34 * scale) + off - 2, 300, 60,
            waveCol, TFT_TRANSPARENT);
  }
}

void drawFingerprintIconModern(TFT_eSprite &s, int cx, int cy, float scale = 1.0f)
{
  int R = int(46 * scale);
  // glow
  s.drawCircle(cx, cy, R + 4, TFT_DARKCYAN);
  // rings
  uint16_t c = TFT_CYAN;
  s.drawCircle(cx, cy, R, c);
  s.drawCircle(cx, cy, int(R * 0.82f), c);
  s.drawCircle(cx, cy, int(R * 0.64f), c);
  s.drawCircle(cx, cy, int(R * 0.46f), c);
  // flowing arcs
  auto arc = [&](int ro, int ri, int a0, int a1)
  {
    drawArc(s, cx, cy, ro, ri, a0, a1, c, TFT_TRANSPARENT);
  };
  arc(R - 2, R - 3, 210, 330);
  arc(int(R * 0.78f), int(R * 0.78f) - 1, 195, 350);
  arc(int(R * 0.60f), int(R * 0.60f) - 1, 170, 10);
  arc(int(R * 0.42f), int(R * 0.42f) - 1, 150, 30);
}

// วาด background gradient + header + icon + badge ลง Sprite
void paintScreenToSprite(UIState s, const char *subtitle, bool popIcon = false, float popK = 1.0f)
{
  const int W = tft.width(), H = tft.height();
  spr.fillSprite(TFT_BLACK);

  // --- BG gradient (reuse fillGradientV แต่ลง sprite) ---
  // เราวาดเอง: เส้นแนวนอน ไล่สีเหมือนเดิม
  uint16_t c1, c2;
  switch (s)
  {
  case UI_BOOT:
  case UI_WAKE:
    c1 = TFT_DARKGREY;
    c2 = TFT_BLACK;
    break;
  case UI_READY:
  case UI_SCAN_CARD:
  case UI_SCAN_FINGER:
    c1 = TFT_NAVY;
    c2 = TFT_BLACK;
    break;
  case UI_CARD_OK:
  case UI_FINGER_OK:
  case UI_THANKS:
  case UI_CONFIRM:
    c1 = TFT_DARKGREEN;
    c2 = TFT_BLACK;
    break;
  case UI_CARD_FAIL:
  case UI_FINGER_FAIL:
  case UI_ERROR:
    c1 = TFT_MAROON;
    c2 = TFT_BLACK;
    break;
  case UI_SLEEP:
    c1 = TFT_DARKGREY;
    c2 = TFT_NAVY;
    break;
  case UI_WAIT_CHOICE:
  case UI_SELECTED:
  case UI_SENDING:
    c1 = TFT_NAVY;
    c2 = TFT_BLACK;
    break;
  }
  for (int y = 0; y < H; ++y)
  {
    float k = (float)y / (float)(H - 1);
    uint16_t r1 = ((c1 >> 11) & 0x1F), g1 = ((c1 >> 5) & 0x3F), b1 = (c1 & 0x1F);
    uint16_t r2 = ((c2 >> 11) & 0x1F), g2 = ((c2 >> 5) & 0x3F), b2 = (c2 & 0x1F);
    uint16_t r = r1 + (int)((r2 - r1) * k), g = g1 + (int)((g2 - g1) * k), b = b1 + (int)((b2 - b1) * k);
    uint16_t c = (r << 11) | (g << 5) | b;
    spr.drawFastHLine(0, y, W, c);
  }

  // --- Header (วาดแบบ “wipe” ขวา->ซ้ายเล็กน้อย) ---
  spr.fillRect(0, 0, W, 30, TFT_BLACK);
  spr.drawRect(0, 0, W, 30, TFT_WHITE);

  const char *hdr =
      (s == UI_BOOT) ? "ระบบกำลังเริ่มทำงาน" : (s == UI_WAKE)      ? "กำลังพร้อมใช้งาน"
                                        : (s == UI_READY)       ? "พร้อมให้บริการ"
                                        : (s == UI_SCAN_CARD)   ? "โปรดแตะบัตร"
                                        : (s == UI_CARD_OK)     ? "บัตรถูกต้อง"
                                        : (s == UI_CARD_FAIL)   ? "บัตรไม่ถูกต้อง"
                                        : (s == UI_SCAN_FINGER) ? "โปรดสแกนลายนิ้วมือ"
                                        : (s == UI_FINGER_OK)   ? "ยืนยันตัวตนสำเร็จ"
                                        : (s == UI_FINGER_FAIL) ? "ยืนยันตัวตนไม่ผ่าน"
                                        : (s == UI_CONFIRM)     ? "ยืนยันการทำรายการ"
                                        : (s == UI_THANKS)      ? "ขอบคุณ"
                                        : (s == UI_ERROR)       ? "ข้อผิดพลาด"
                                        : (s == UI_SLEEP)       ? "พักการทำงาน"
                                        : (s == UI_WAIT_CHOICE) ? "รอเลือกผู้สมัคร"
                                        : (s == UI_SELECTED)    ? "ยืนยันตัวเลือก"
                                        : (s == UI_SENDING)     ? "กำลังส่งข้อมูล"
                                                                : "";

  // wipe แถบขาวใต้โล่ (ความยาวสัมพันธ์ popK)
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(hdr, (W - spr.textWidth(hdr, 2)) / 2, 6, 2);
  int cx = W / 2;
  spr.fillTriangle(cx - 20, 34, cx + 20, 34, cx, 64, TFT_DARKGREY);
  spr.fillTriangle(cx - 16, 36, cx + 16, 36, cx, 60, TFT_NAVY);
  int wipeW = (int)(52 * popK);
  spr.fillRect(cx - 26, 70, wipeW, 3, TFT_WHITE);

  // --- Icon / Badge (pop-in scale) ---
  int icx = W / 2, icy = 150;
  // scale popK: 0.7 -> 1.0
  float scale = 0.7f + 0.3f * popK;

  // ไอคอนการ์ด/นิ้ว: เราจะเรียกของเดิมแต่เลื่อนพิกัด/สเกลง่าย ๆ
  auto drawCardScaled = [&](int cx, int cy, float s)
  {
    int w = (int)(96 * s), h = (int)(56 * s), r = (int)(8 * s);
    spr.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, TFT_DARKGREY);
    spr.fillRoundRect(cx - w / 2 + 2, cy - h / 2 + 2, w - 4, h - 4, r, TFT_WHITE);
    spr.fillRect(cx - w / 2 + 2, cy - (int)(h * 0.1f), w - 4, (int)(h * 0.25f), TFT_NAVY);
    spr.fillRect(cx - (int)(w * 0.35f), cy - (int)(h * 0.35f), (int)(w * 0.42f), (int)(h * 0.1f), TFT_LIGHTGREY);
  };
  auto drawFingerScaled = [&](int cx, int cy, float s)
  {
    int R = (int)(30 * s);
    spr.drawCircle(cx, cy, R, TFT_CYAN);
    spr.drawCircle(cx, cy, (int)(R * 0.8f), TFT_CYAN);
    spr.drawCircle(cx, cy, (int)(R * 0.6f), TFT_CYAN);
    spr.drawCircle(cx, cy, (int)(R * 0.4f), TFT_CYAN);
  };

  if (s == UI_SCAN_CARD)
    drawNFCIcon(spr, W / 2, 150, 1.0f * scale);
  else if (s == UI_SCAN_FINGER)
    drawFingerprintIconModern(spr, W / 2, 150, 1.0f * scale);

  // Badge OK/Fail
  auto drawCheck = [&](int cx, int cy, float s)
  {
    int R = (int)(34 * s);
    spr.fillCircle(cx, cy, R, TFT_DARKGREEN);
    spr.fillCircle(cx, cy, (int)(R * 0.88f), TFT_GREEN);
    spr.drawLine(cx - (int)(16 * s), cy, cx - (int)(6 * s), cy + (int)(12 * s), TFT_WHITE);
    spr.drawLine(cx - (int)(6 * s), cy + (int)(12 * s), cx + (int)(16 * s), cy - (int)(14 * s), TFT_WHITE);
    spr.drawLine(cx - (int)(17 * s), cy, cx - (int)(7 * s), cy + (int)(12 * s), TFT_WHITE);
    spr.drawLine(cx - (int)(7 * s), cy + (int)(12 * s), cx + (int)(17 * s), cy - (int)(14 * s), TFT_WHITE);
  };
  auto drawCross = [&](int cx, int cy, float s)
  {
    int R = (int)(34 * s);
    spr.fillCircle(cx, cy, R, TFT_MAROON);
    spr.fillCircle(cx, cy, (int)(R * 0.88f), TFT_RED);
    for (int i = -1; i <= 1; i++)
    {
      spr.drawLine(cx - (int)(16 * s), cy - (int)(16 * s) + i, cx + (int)(16 * s), cy + (int)(16 * s) + i, TFT_WHITE);
      spr.drawLine(cx - (int)(16 * s), cy + (int)(16 * s) + i, cx + (int)(16 * s), cy - (int)(16 * s) + i, TFT_WHITE);
    }
  };

  if (s == UI_CARD_OK || s == UI_FINGER_OK || s == UI_THANKS || s == UI_CONFIRM)
    drawCheck(icx, icy, scale);
  if (s == UI_CARD_FAIL || s == UI_FINGER_FAIL || s == UI_ERROR)
    drawCross(icx, icy, scale);

  // Big headline + subtitle
  const char *big =
      (s == UI_BOOT) ? "INITIALIZING" : (s == UI_WAKE)      ? "WAKE"
                                    : (s == UI_READY)       ? "READY"
                                    : (s == UI_SCAN_CARD)   ? "SCAN CARD"
                                    : (s == UI_CARD_OK)     ? "CARD OK"
                                    : (s == UI_CARD_FAIL)   ? "CARD REJECTED"
                                    : (s == UI_SCAN_FINGER) ? "SCAN FINGER"
                                    : (s == UI_FINGER_OK)   ? "FINGER OK"
                                    : (s == UI_FINGER_FAIL) ? "FINGER FAIL"
                                    : (s == UI_CONFIRM)     ? "CONFIRM"
                                    : (s == UI_THANKS)      ? "THANK YOU"
                                    : (s == UI_ERROR)       ? "ERROR"
                                    : (s == UI_SLEEP)       ? "SLEEP"
                                    : (s == UI_WAIT_CHOICE) ? "WAIT"
                                    : (s == UI_SELECTED)    ? "SELECTED"
                                    : (s == UI_SENDING)     ? "SENDING"
                                                            : "";

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(big, (W - spr.textWidth(big, 4)) / 2, 200, 4);
  if (subtitle && subtitle[0])
  {
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString(subtitle, (W - spr.textWidth(subtitle, 2)) / 2, 230, 2);
  }
}

// --------- Main painter ----------

void showUIx(UIState s, const char *subtitle = nullptr, UITrans tr = TR_SLIDE_L)
{
  if (isShowingPhoto)
    return;

  const String sub = subtitle ? String(subtitle) : String();
  const uint32_t now = millis();
  if (s == g_lastState && sub == g_lastSubtitle)
  {
    if (SAME_STATE_COOLDOWN_MS == 0 || (now - g_lastPaintMs) < SAME_STATE_COOLDOWN_MS)
      return;
  }
  g_lastState = s;
  g_lastSubtitle = sub;
  g_lastPaintMs = now;

  const int W = tft.width(), H = tft.height();

  spr.setTextDatum(TL_DATUM);

  // POP animation
  const int POP_FR = 8;
  for (int i = 0; i < POP_FR; ++i)
  {
    float k = easeInOutQuad((float)(i + 1) / POP_FR);
    spr.fillSprite(TFT_BLACK);
    paintScreenToSprite(s, subtitle, true, k);
    spr.pushSprite(0, 0);
    delay(12);
  }

  // เฟรมสุดท้าย + slide
  spr.fillSprite(TFT_BLACK);
  paintScreenToSprite(s, subtitle, false, 1.0f);

  if (tr == TR_NONE)
  {
    spr.pushSprite(0, 0);
  }
  else
  {
    const int FR = 12;
    for (int i = 0; i < FR; ++i)
    {
      float k = easeInOutQuad((float)(i + 1) / FR);
      int x = 0, y = 0;
      if (tr == TR_SLIDE_L)
        x = (int)((1.0f - k) * W);
      else if (tr == TR_SLIDE_R)
        x = (int)(-(1.0f - k) * W);
      else if (tr == TR_SLIDE_UP)
        y = (int)((1.0f - k) * H);
      else if (tr == TR_SLIDE_DOWN)
        y = (int)(-(1.0f - k) * H);
      spr.pushSprite(x, y);
      delay(14);
    }
  }

  uiSetScanning(s == UI_SCAN_CARD || s == UI_SCAN_FINGER);
}

// ใช้ GPIO35 เป็นขาปลุก (ต่อมาจาก ODROID PIN_33 ผ่าน R อนุกรม ~1k)
// *GPIO35 เป็นขา RTC input ได้ ปลุกด้วย ext1 ได้

// ==== forward declarations to satisfy compile order (ADD ONLY) ====
struct Rec;                   // ให้คอมไพเลอร์รู้จักชื่อ Rec ล่วงหน้า (ใช้กับ & ได้)
extern const int UID_HEX_MAX; // บอกว่าจะมีค่าคงที่ชื่อนี้ประกาศจริงด้านล่าง

// ===== [ADD] Ultrasonic (HC-SR04) for auto-sleep =====
const int TRIG_PIN = 4;
const int ECHO_PIN = 34; // ต้องลดเป็น 3.3V ก่อนเข้า ESP32

// เกณฑ์ “ใกล้”
volatile float NEAR_ON_CM = 25.0;  // เข้าสถานะ NEAR เมื่อ <= 25 cm
volatile float NEAR_OFF_CM = 35.0; // กลับ FAR เมื่อ >= 35 cm (ฮิสเทอรีส)

// รอบวัดและ timeout
const uint16_t US_INTERVAL_MS = 200;    // วัดทุก ~200ms
const unsigned long US_TIMEOUT = 25000; // pulseIn timeout ~25ms

// ===== [ADD] counters & confirm windows for noise filtering =====
static uint8_t nearConsec = 0;
static uint8_t farConsec = 0;
static const uint8_t NEAR_CONFIRM_N = 2; // ต้องเห็น NEAR 2 เฟรมติดถึงจะเปลี่ยนเป็น NEAR
static const uint8_t FAR_CONFIRM_N = 2;  // ต้องเห็น FAR  2 เฟรมติดถึงจะเปลี่ยนเป็น FAR

// จับเวลาเพื่อหลับ
const uint32_t NO_NEAR_SLEEP_MS = 30000; // FAR ต่อเนื่อง 30 วินาที -> หลับ

// ตัวแปรสถานะ
static bool nearState = false;
static uint32_t lastUSms = 0;
static uint32_t lastNearSeenMs = 0;

// [ADD] Debug logging for Ultrasonic
#define DEBUG_ULTRA 1
static uint32_t lastUltraLogMs = 0;

// (ออปชัน) ถ้าจะให้หลับเองเมื่อไม่มีเหตุการณ์นาน X ms ให้เปิดใช้ 2 บรรทัดนี้ได้ภายหลัง
// #define IDLE_SLEEP_MS 60000UL   // FAR/ไม่มีงาน 60s → หลับ
// static uint32_t lastActiveMs = 0;
// inline void noteActivity(){ lastActiveMs = millis(); }

#include "esp_system.h"
#include "driver/rtc_io.h"

void printBootAndWakeInfo()
{
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("Reset reason=%d (1=POWERON, 12=BROWNOUT, 5=DEEPSLEEP)\n", (int)rr);

  esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
  Serial.print("Wake cause=");
  switch (wc)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println("EXT0");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println("EXT1");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("TIMER");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    Serial.println("TOUCH");
    break;
  case ESP_SLEEP_WAKEUP_ULP:
    Serial.println("ULP");
    break;
  case ESP_SLEEP_WAKEUP_GPIO:
    Serial.println("GPIO");
    break;
  case ESP_SLEEP_WAKEUP_UNDEFINED:
  default:
    Serial.println("POWER-ON/RESET");
    break;
  }

  if (wc == ESP_SLEEP_WAKEUP_EXT1)
  {
    uint64_t m = esp_sleep_get_ext1_wakeup_status();
    Serial.printf("EXT1 mask=0x%016llX\n", (unsigned long long)m);
    if (m)
    {
      Serial.print("Pins HIGH: ");
      bool first = true;
      for (int g = 0; g <= 39; ++g)
        if (m & (1ULL << g))
        {
          Serial.print(first ? "" : " ,");
          Serial.print(g);
          first = false;
        }
      Serial.println();
    }
  }
}

// เข้าหลับทันที แล้วปลุกเมื่อ WAKE_PIN=HIGH จาก ODROID
void goDeepSleepNow()
{
  Serial.println("-> Deep-sleep now. Waiting for ODROID wake (GPIO HIGH)...");
  delay(30);

  // ปิด I/O ที่อาจดีดกลับ
  pinMode(12, INPUT);
  pinMode(4, INPUT);

  // เอา interrupt ของขาปลุกออกก่อน
  detachInterrupt(digitalPinToInterrupt(WAKE_PIN));

  // ตั้งค่าพินปลุกในสองโดเมนให้สะอาด
  rtc_gpio_hold_dis((gpio_num_t)WAKE_PIN);
  pinMode(WAKE_PIN, INPUT);              // digital
  rtc_gpio_deinit((gpio_num_t)WAKE_PIN); // RTC
  rtc_gpio_init((gpio_num_t)WAKE_PIN);
  rtc_gpio_set_direction((gpio_num_t)WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en((gpio_num_t)WAKE_PIN);
  rtc_gpio_pullup_dis((gpio_num_t)WAKE_PIN);

  // ถ้าขาปลุก HIGH อยู่แล้ว ให้ข้ามหลับ (กันเด้ง)
  if (rtc_gpio_get_level((gpio_num_t)WAKE_PIN) == 1)
  {
    Serial.println("[SLEEP] WAKE_PIN is HIGH already -> skip sleep");
    return;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext1_wakeup(1ULL << WAKE_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);

  // showUIx(UI_SLEEP, "พักการทำงาน");
  delay(200);
  esp_deep_sleep_start();
}

// ---------- Serial / UART ----------
HardwareSerial mySerial(2);     // UART2 : ใช้คุยกับบอร์ด/จออีกตัว ตามที่คุณใช้อยู่ (TX=17, RX=16 ด้านล่าง)
HardwareSerial FingerSerial(1); // UART1 : ใช้คุยกับโมดูลลายนิ้วมือ
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerSerial);

// ---------- RFID ----------
#define SS_PIN 5
#define RST_PIN 27
MFRC522 rfid(SS_PIN, RST_PIN);
// [ADD] ปล่อยทุก CS ให้ HIGH (กันชน)
// --- ปล่อยทุก CS ให้อยู่ HIGH เสมอ ---
inline void spi_idle_all()
{
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH); // RFID_CS
}

// --- ก่อนเรียกฟังก์ชันของ RC522: ปล่อยบัสจากตัวอื่น ไม่เปิด transaction ซ้อน ---
inline void rfid_bus_begin()
{
  // ปล่อยจอให้เลิกถือบัส (กรณี TFT_eSPI ยังอยู่ใน write mode)
  tft.endWrite(); // ปลอดภัย แม้จะยังไม่เริ่มวาด

  // ปล่อยอุปกรณ์อื่น
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);

  // ยก CS ของ RC522 ไว้ HIGH ก่อน ไลบรารี MFRC522 จะจัดการดึง LOW เอง
  digitalWrite(SS_PIN, HIGH);
}

// --- หลังจบงานกับ RC522 ---
inline void rfid_bus_end()
{
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
}

// ---------- I/O ----------
const int EEPROM_SIZE = 512;
const int buzzerPin = 12;
const int switchPin33 = 14; // สวิตช์ Register
const int switchPin32 = 32; // สวิตช์ Delete
// const int ledPin = 13;

// ---------- Finger UART Pins (ปรับให้ตรงบอร์ดคุณ) ----------
const int FINGER_RX = 26; // ESP32 RX1 pin to sensor TX
const int FINGER_TX = 25; // ESP32 TX1 pin to sensor RX

// ---------- Protocol between boards (คงรูปแบบเดิมของคุณ) ----------
/*
  mySerial.println("regis"); // โหมดลงทะเบียน
  mySerial.println("S");     // สแกนบัตรปกติ: สถานะกำลังอ่าน
  mySerial.println("W");     // บัตรผิด
  mySerial.println("OK");    // ยืนยันผ่าน (บัตร+นิ้วผ่าน)
*/

// ---------- Durable Storage Layout (EEPROM) ----------
/*
  Header (offset 0..15)
    0..3   : MAGIC 'VOTE' (0x56 0x4F 0x54 0x45)
    4      : VERSION = 1
    5..15  : reserved

  Records start at BASE = 16
  Each record = 20 bytes
    0..15 : UID_HEX (fixed 16 chars, ASCII hex, padded with 0x00)
    16    : FP_ID (0..199)  => id ในโมดูลลายนิ้วมือ
    17    : VOTED (0/1)
    18    : VALID (0xA5 = valid, 0xFF = empty)
    19    : reserved
*/
const uint32_t MAGIC = 0x564F5445UL; // 'VOTE'
const uint8_t VERSION = 1;
const int HDR_SIZE = 16;
const int RECORD_SIZE = 20;
const int BASE = HDR_SIZE;
const uint8_t VALID_FLAG = 0xA5;
const uint8_t EMPTY_FLAG = 0xFF;
const int MAX_RECORDS = (EEPROM_SIZE - BASE) / RECORD_SIZE; // ~= 24

// ---------- Utils ----------
struct Rec
{
  char uid[UID_HEX_MAX]; // ไม่รับ '\0' เสมอ ให้เก็บเป็น 16 ชาร์ (ถ้าน้อยกว่าก็ 0x00 padding)
  uint8_t fp_id;
  uint8_t voted; // 0/1
  uint8_t valid; // VALID_FLAG หรือ EMPTY_FLAG
  uint8_t reserved;
};

void eepromWriteBytes(int addr, const uint8_t *data, int len)
{
  for (int i = 0; i < len; ++i)
    EEPROM.write(addr + i, data[i]);
}

void eepromReadBytes(int addr, uint8_t *data, int len)
{
  for (int i = 0; i < len; ++i)
    data[i] = EEPROM.read(addr + i);
}

void writeHeader()
{
  uint8_t hdr[HDR_SIZE] = {0};
  hdr[0] = 'V';
  hdr[1] = 'O';
  hdr[2] = 'T';
  hdr[3] = 'E';
  hdr[4] = VERSION;
  // rest zero
  eepromWriteBytes(0, hdr, HDR_SIZE);
  EEPROM.commit();
}

bool headerOK()
{
  uint8_t h[5];
  for (int i = 0; i < 5; i++)
    h[i] = EEPROM.read(i);
  return (h[0] == 'V' && h[1] == 'O' && h[2] == 'T' && h[3] == 'E' && h[4] == VERSION);
}

int recAddr(int idx)
{
  return BASE + idx * RECORD_SIZE;
}

void readRec(int idx, Rec &r)
{
  uint8_t buf[RECORD_SIZE];
  eepromReadBytes(recAddr(idx), buf, RECORD_SIZE);
  for (int i = 0; i < UID_HEX_MAX; ++i)
    r.uid[i] = (char)buf[i];
  r.fp_id = buf[16];
  r.voted = buf[17];
  r.valid = buf[18];
  r.reserved = buf[19];
}

void writeRec(int idx, const Rec &r)
{
  uint8_t buf[RECORD_SIZE];
  for (int i = 0; i < UID_HEX_MAX; ++i)
    buf[i] = (uint8_t)r.uid[i];
  buf[16] = r.fp_id;
  buf[17] = r.voted;
  buf[18] = r.valid;
  buf[19] = r.reserved;
  eepromWriteBytes(recAddr(idx), buf, RECORD_SIZE);
  EEPROM.commit();
}

void clearRec(int idx)
{
  Rec r{};
  for (int i = 0; i < UID_HEX_MAX; ++i)
    r.uid[i] = 0x00;
  r.fp_id = 0;
  r.voted = 0;
  r.valid = EMPTY_FLAG;
  r.reserved = 0;
  writeRec(idx, r);
}

int findFreeSlot()
{
  for (int i = 0; i < MAX_RECORDS; ++i)
  {
    Rec r;
    readRec(i, r);
    if (r.valid != VALID_FLAG)
      return i;
  }
  return -1;
}

bool sameUID16(const char a[UID_HEX_MAX], const char b[UID_HEX_MAX])
{
  for (int i = 0; i < UID_HEX_MAX; ++i)
    if (a[i] != b[i])
      return false;
  return true;
}

void uidToFixed16(const String &uidHex, char out16[UID_HEX_MAX])
{
  // ตัด/แพดให้ยาว 16 ตัวอักษร
  // (UID 4 ไบต์ => 8 ตัวอักษร, UID 7/10 ไบต์ => 14/20 ตัวอักษร → เก็บ 16 ตัวอักษรแรกพอ)
  for (int i = 0; i < UID_HEX_MAX; i++)
  {
    out16[i] = (i < uidHex.length()) ? uidHex.charAt(i) : 0x00;
  }
}

int findByUID(const String &uidHex)
{
  char key[UID_HEX_MAX];
  uidToFixed16(uidHex, key);
  for (int i = 0; i < MAX_RECORDS; ++i)
  {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && sameUID16(r.uid, key))
      return i;
  }
  return -1;
}

int findByFPID(uint8_t fp)
{
  for (int i = 0; i < MAX_RECORDS; ++i)
  {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && r.fp_id == fp)
      return i;
  }
  return -1;
}

// สแกนนิ้วแบบเร็วเพื่อเช็กว่ามีนิ้วนี้อยู่ในฐานแล้วหรือไม่
int quickSearchFingerprint(uint32_t timeout_ms = 10000)
{
  unsigned long t0 = millis();
  while (millis() - t0 < timeout_ms)
  {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER)
    {
      delay(50);
      continue;
    }
    if (p != FINGERPRINT_OK)
    {
      delay(50);
      continue;
    }
    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK)
    {
      delay(50);
      continue;
    }
    p = finger.fingerFastSearch(); // ค้นหาในฐานของเซ็นเซอร์
    if (p == FINGERPRINT_OK)
      return finger.fingerID; // พบแล้ว → คืน fp_id เดิม
    else
      return -1; // ไม่พบ → นิ้วใหม่น่าจะยังไม่อยู่ในฐาน
  }
  return -1; // timeout
}

bool setVotedByIndex(int idx, uint8_t v)
{
  if (idx < 0 || idx >= MAX_RECORDS)
    return false;
  Rec r;
  readRec(idx, r);
  if (r.valid != VALID_FLAG)
    return false;
  r.voted = v ? 1 : 0;
  writeRec(idx, r);
  return true;
}

// ---------- Fingerprint helpers ----------
bool fingerBegin()
{
  // เริ่มพอร์ตกับโมดูลลายนิ้วมือ
  FingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  finger.begin(57600);
  delay(200);
  return finger.verifyPassword();
}

int enrollFingerprint(uint8_t fp_id)
{
  // ขั้นตอนย่อสไตล์ Adafruit: ขอภาพสองครั้ง, สร้างโมเดล, เก็บไว้ตำแหน่ง fp_id
  // คืน 0 = ok, อื่นๆ = code ผิดพลาด
  int p = -1;
  Serial.printf("Enroll FP id=%d : place finger\n", fp_id);

  // ภาพ 1
  while ((p = finger.getImage()) != FINGERPRINT_OK)
  {
    if (p == FINGERPRINT_NOFINGER)
    {
      delay(50);
      continue;
    }
    if (p == FINGERPRINT_PACKETRECIEVEERR)
      return p;
    if (p == FINGERPRINT_IMAGEFAIL)
      return p;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK)
    return p;

  Serial.println("Remove finger");
  while (finger.getImage() != FINGERPRINT_NOFINGER)
    delay(50);

  Serial.println("Place same finger again");
  while ((p = finger.getImage()) != FINGERPRINT_OK)
  {
    if (p == FINGERPRINT_NOFINGER)
    {
      delay(50);
      continue;
    }
    if (p == FINGERPRINT_PACKETRECIEVEERR)
      return p;
    if (p == FINGERPRINT_IMAGEFAIL)
      return p;
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK)
    return p;

  p = finger.createModel();
  if (p != FINGERPRINT_OK)
    return p;

  p = finger.storeModel(fp_id);
  return p; // FINGERPRINT_OK = 0x00
}

int matchFingerprint()
{
  // จับภาพ → แปลง → ค้นหา เร็ว
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK)
    return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)
    return -1;
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK)
    return -1;
  return finger.fingerID; // ตำแหน่งที่ match
}

// ---------- App Logic ----------
String readRFIDasHex()
{
  // คืนเป็นตัวอักษร hex (ไม่เว้นวรรค), ตัวพิมพ์ใหญ่, ยาวเท่าจำนวน uid.size*2 (สูงสุด ~20 chars)
  String ID = "";
  for (byte i = 0; i < rfid.uid.size; i++)
  {
    if (rfid.uid.uidByte[i] < 0x10)
      ID += "0";
    ID += String(rfid.uid.uidByte[i], HEX);
  }
  ID.toUpperCase();
  ID.replace(" ", "");
  return ID;
}

bool storeNewRecord(const String &uidHex, uint8_t fp_id)
{
  int slot = findFreeSlot();
  if (slot < 0)
    return false;
  Rec r{};
  uidToFixed16(uidHex, r.uid);
  r.fp_id = fp_id;
  r.voted = 0;
  r.valid = VALID_FLAG;
  r.reserved = 0;
  writeRec(slot, r);
  return true;
}

bool removeByUID(const String &uidHex)
{
  int idx = findByUID(uidHex);
  if (idx < 0)
    return false;
  Rec r;
  readRec(idx, r);
  // ลบในโมดูลลายนิ้วมือด้วย
  if (r.fp_id > 0)
  {
    finger.deleteModel(r.fp_id);
  }
  clearRec(idx);
  return true;
}

// ---- Compatibility wrappers (ให้โค้ดที่ยังเรียกชื่อเก่า compile ได้) ----
inline void bus_acquire_for_rfid(uint32_t /*hz*/ = 4000000)
{
  // เราใช้การคุม CS + endWrite() อยู่แล้ว ความเร็วจัดโดยไลบรารี/ESP32 SPI
  rfid_bus_begin();
}
inline void bus_release_after_rfid()
{
  rfid_bus_end();
}

// ---- RC522 hard reset ผ่านขา RST_PIN (27) ----
inline void rc522_hard_reset()
{
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(5); // หน่วงสั้น ๆ ให้รีเซ็ตจริง
  digitalWrite(RST_PIN, HIGH);
  delay(5);
}

// วางเหนือ registerCardAndFingerprint() / deleteCardFlow()
inline void exitPhotoMode()
{
  isShowingPhoto = false;
  uiSetScanning(true);
}

// ===== ESP32 tone() shim (no sound; compile-safe) =====
inline void tone(int /*pin*/, unsigned int /*freq*/, unsigned long /*duration*/ = 0) {}
inline void noTone(int /*pin*/) {}

// ---------- High-level flows ----------
void registerCardAndFingerprint()
{
  exitPhotoMode();
  mySerial.println("regis");
  Serial.println("Registration mode... Tap a new card");

  // UI: เริ่มโหมดลงทะเบียน → รอแตะบัตร
  showUIx(UI_SCAN_CARD, "แตะบัตรเพื่อเริ่มลงทะเบียน", TR_SLIDE_UP);

  // --- รอการ์ดแบบล็อคบัสทุกครั้ง ---
  while (true)
  {
    bool ok = false;
    bus_acquire_for_rfid();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
      ok = true;
    bus_release_after_rfid();
    if (ok)
      break;
    uiTick();
    delay(50);
  }

  // --- อ่าน UID แบบปลอดภัยบนบัส ---
  String uidHex;
  bus_acquire_for_rfid();
  uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  bus_release_after_rfid();

  // โชว์ “บัตรถูกต้อง” สั้นๆ ก่อนดำเนินการต่อ
  showUIx(UI_CARD_OK, "บัตรถูกต้อง", TR_FADE);
  showUIx(UI_SENDING, "เตรียมลงทะเบียนนิ้ว...", TR_FADE);
  delay(300);
  uiSetLoading(true);
  delay(500);
  uiSetLoading(false);
  showUIx(UI_SCAN_FINGER, "วางนิ้ว 2 ครั้งเพื่อลงทะเบียน", TR_SLIDE_L);
  tone(buzzerPin, 1200, 120);

  // --- การ์ดซ้ำ? ---
  if (findByUID(uidHex) >= 0)
  {
    Serial.println("This card is already registered.");
    showUIx(UI_CARD_FAIL, "บัตรนี้ลงทะเบียนแล้ว", TR_SLIDE_DOWN);
    tone(buzzerPin, 700, 300);
    delay(900);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // --- ตรวจนิ้วซ้ำก่อน Enroll ---
  Serial.println("Place finger to check duplication...");
  showUIx(UI_SCAN_FINGER, "ตรวจสอบลายนิ้วมือเดิม", TR_SLIDE_L);
  int existing_fp = quickSearchFingerprint(10000);
  if (existing_fp >= 0)
  {
    int idxExisting = findByFPID(existing_fp);
    if (idxExisting >= 0)
    {
      Rec rExist;
      readRec(idxExisting, rExist);
      Serial.printf("Duplicate finger detected! Already linked to another card (FP_ID=%d). Abort.\n", existing_fp);
      showUIx(UI_FINGER_FAIL, "ลายนิ้วมือนี้เชื่อมบัตรอื่นอยู่", TR_SLIDE_DOWN);
      tone(buzzerPin, 600, 400);
      delay(1000);
      showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
      return;
    }
    else
    {
      Serial.printf("Found stale FP template (id=%d) without EEPROM record. Deleting stale template.\n", existing_fp);
      finger.deleteModel(existing_fp);
      showUIx(UI_ERROR, "ล้างข้อมูลลายนิ้วมือที่ค้าง", TR_FADE);
      delay(400);
    }
  }

  // --- หา fp_id ว่าง 1..199 ---
  uint8_t chosen_fp_id = 1;
  bool used[200];
  for (int i = 0; i < 200; i++)
    used[i] = false;
  for (int i = 0; i < MAX_RECORDS; i++)
  {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && r.fp_id > 0 && r.fp_id < 200)
      used[r.fp_id] = true;
  }
  while (chosen_fp_id < 200 && used[chosen_fp_id])
    chosen_fp_id++;
  if (chosen_fp_id >= 200)
  {
    Serial.println("No free FP ID slot.");
    tone(buzzerPin, 800, 300);
    delay(1000);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // --- Enroll นิ้ว ---
  Serial.printf("Enroll fingerprint for this card (UID=%s) at FP_ID=%d\n", uidHex.c_str(), chosen_fp_id);
  showUIx(UI_SCAN_FINGER, "วางนิ้ว 2 ครั้งเพื่อลงทะเบียน", TR_SLIDE_L);
  int p = enrollFingerprint(chosen_fp_id);
  if (p != FINGERPRINT_OK)
  {
    Serial.printf("Enroll failed (code=%d). Abort.\n", p);
    showUIx(UI_FINGER_FAIL, "บันทึกลายนิ้วมือไม่สำเร็จ", TR_SLIDE_DOWN);
    tone(buzzerPin, 500, 500);
    delay(1000);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // --- เก็บเรคคอร์ด (UID + FP_ID) ลง EEPROM ---
  if (storeNewRecord(uidHex, chosen_fp_id))
  {
    Serial.println("Card+Fingerprint registered successfully.");
    mySerial.println("Card Registered!");
    showUIx(UI_FINGER_OK, "ลงทะเบียนสำเร็จ", TR_FADE);
    tone(buzzerPin, 1600, 120);
    delay(200);
    tone(buzzerPin, 1600, 120);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
  }
  else
  {
    Serial.println("EEPROM full. Cannot store new record.");
    showUIx(UI_ERROR, "หน่วยความจำเต็ม", TR_FADE);
    tone(buzzerPin, 500, 500);
    finger.deleteModel(chosen_fp_id); // roll back
    delay(1000);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
  }
}

void deleteCardFlow()
{
  exitPhotoMode();
  Serial.println("Delete mode... Tap a card to delete");
  showUIx(UI_SCAN_CARD, "แตะบัตรเพื่อลบข้อมูล", TR_SLIDE_UP);

  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial())
  {
    delay(50);
  }
  String uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  int idx = findByUID(uidHex);
  if (idx < 0)
  {
    Serial.println("Card not found");
    showUIx(UI_CARD_FAIL, "ไม่พบข้อมูลบัตรในระบบ", TR_SLIDE_DOWN);
    tone(buzzerPin, 600, 300);
    delay(900);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // การ์ดถูกต้อง
  showUIx(UI_CARD_OK, "บัตรถูกต้อง", TR_FADE);
  tone(buzzerPin, 1200, 150);
  delay(300);
  showUIx(UI_SENDING, "เตรียมยืนยันการลบ...", TR_FADE);
  uiSetLoading(true);
  delay(500);
  uiSetLoading(false);
  showUIx(UI_SCAN_FINGER, "วางนิ้วเพื่อยืนยันการลบ", TR_SLIDE_L);

  // โหลดเรคคอร์ดเพื่อรู้ fp_id ของเจ้าของบัตร
  Rec r;
  readRec(idx, r);

  // ✅ ขั้นตอน “ยืนยันลายนิ้วมือก่อนลบ”
  Serial.printf("Verify fingerprint to delete (expect FP_ID=%d)\n", r.fp_id);
  showUIx(UI_SCAN_FINGER, "วางนิ้วเพื่อยืนยันการลบ", TR_SLIDE_L);
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000)
  { // รอสูงสุด 15 วินาที
    matched = matchFingerprint();
    if (matched >= 0)
      break;
    uiTick();
    delay(50);
  }
  if (matched < 0 || matched != r.fp_id)
  {
    Serial.println("Fingerprint verify failed / timeout. Abort delete.");
    showUIx(UI_FINGER_FAIL, (matched < 0) ? "ไม่ตรวจพบลายนิ้ว" : "ลายนิ้วไม่ตรงเจ้าของบัตร", TR_SLIDE_DOWN);
    tone(buzzerPin, 600, 400);
    delay(1000);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // ลบ fingerprint template ในเซ็นเซอร์
  if (r.fp_id > 0)
  {
    uint8_t p = finger.deleteModel(r.fp_id);
    if (p != FINGERPRINT_OK)
    {
      Serial.printf("Delete template failed (code=%d). Continue to clear record.\n", p);
      showUIx(UI_ERROR, "ลบลายนิ้วในเซ็นเซอร์ไม่สำเร็จ", TR_FADE);
      delay(500);
      // ยังลบเรคคอร์ด EEPROM ต่อไปตามเดิม
    }
  }

  // ลบเรคคอร์ดบัตรใน EEPROM
  clearRec(idx);
  Serial.println("Card + Fingerprint deleted");

  showUIx(UI_FINGER_OK, "ลบข้อมูลสำเร็จ", TR_FADE);
  tone(buzzerPin, 1200, 150);
  delay(150);
  tone(buzzerPin, 1200, 150);
  delay(700);

  showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
}
void normalScanFlow()
{
  // เวอร์ชันเดิม + เติม UI อย่างเดียว (ไม่สลับลำดับ logic/protocol)
  // ขั้นตอน: ส่ง "S" → อ่าน UID → ถ้าไม่รู้จัก/ทำรายการแล้วให้แจ้งเตือน → ถ้ารู้จักให้สแกนนิ้วให้ตรง fp_id → OK และ mark voted

  Serial.println("Scan card...");
  mySerial.println("S"); // โปรโตคอลตามเดิม
  showUIx(UI_SCAN_CARD, "ยื่นบัตรใกล้เครื่องอ่าน", TR_SLIDE_UP);

  // --- อ่าน UID (คงสไตล์เดิม: อ่านเลย ไม่ยื้อรอ) ---
  bus_acquire_for_rfid();
  String uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  bus_release_after_rfid();

  // --- ตรวจว่าการ์ดอยู่ในระบบ? ---
  int idx = findByUID(uidHex);
  if (idx < 0)
  {
    Serial.println("Unknown card");
    showUIx(UI_CARD_FAIL, "บัตรนี้ไม่อยู่ในระบบ", TR_SLIDE_DOWN);
    tone(buzzerPin, 1000, 200);
    delay(200);
    tone(buzzerPin, 1000, 200);
    delay(500);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // การ์ด OK
  showUIx(UI_CARD_OK, "บัตรถูกต้อง", TR_FADE);
  tone(buzzerPin, 1200, 120);
  delay(250);

  Rec r;
  readRec(idx, r);

  // ถ้าโหมดโหวต: เคยทำรายการแล้วหรือยัง?
  if (r.voted == 1)
  {
    Serial.println("Already voted for this card holder.");
    mySerial.println("W");
    showUIx(UI_ERROR, "บัตรนี้ทำรายการแล้ว", TR_SLIDE_DOWN);
    tone(buzzerPin, 700, 300);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // --- ขอให้สแกนนิ้วให้ "ตรงกับ fp_id" ของบัตรนี้ ---
  Serial.printf("Card OK. Please verify fingerprint (expect FP_ID=%d)\n", r.fp_id);
  showUIx(UI_SCAN_FINGER, "วางนิ้วเพื่อยืนยันตัวตน", TR_SLIDE_L);

  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000)
  { // รอสูงสุด 15 วินาที
    matched = matchFingerprint();
    if (matched >= 0)
      break;
    uiTick(); // ให้กรอบกระพริบทำงานระหว่างรอ
    delay(50);
  }

  if (matched < 0)
  {
    Serial.println("Fingerprint not matched / timeout.");
    mySerial.println("W");
    showUIx(UI_FINGER_FAIL, "ไม่ตรวจพบลายนิ้วมือ", TR_SLIDE_DOWN);
    tone(buzzerPin, 600, 400);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  Serial.printf("Matched fingerID=%d\n", matched);
  if (matched != r.fp_id)
  {
    Serial.println("Fingerprint does not belong to this card.");
    mySerial.println("W");
    showUIx(UI_FINGER_FAIL, "ลายนิ้วมือต้องตรงกับผู้ถือบัตร", TR_SLIDE_DOWN);
    tone(buzzerPin, 600, 400);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }

  // --- ผ่านเงื่อนไข: บัตร+นิ้ว ตรงกัน → สำเร็จ ---
  // (ใส่จังหวะยืนยันสั้น ๆ แต่ไม่สลับลอจิกเดิม)
  // --- ผ่านเงื่อนไข: บัตร+นิ้ว ตรงกัน → สำเร็จ ---
  // --- ผ่านเงื่อนไข: บัตร+นิ้ว ตรงกัน → "รอเลือกผู้สมัคร" ---
  g_waitingChoice = true;
  g_selectedCandidate = -1;
  mySerial.println("AUTH_OK"); // แจ้งบอร์ดจอใหญ่ว่าอนุญาตแล้ว
  showUIx(UI_WAIT_CHOICE, "โปรดเลือกผู้สมัครที่หน้าจอใหญ่", TR_FADE);

  // วนรออีเวนต์: CF:xx / SENDING / VOTE:OK / VOTE:ERR (สูงสุด 20 วินาที)
  uint32_t tStart = millis();
  bool finished = false;
  uiSetLoading(false);

  while (!finished && millis() - tStart < 20000)
  {
    // อ่าน Serial2 ถ้ามี
    if (mySerial.available())
    {
      String line = mySerial.readStringUntil('\n');
      line.trim();

      if (line.startsWith("CF:"))
      {
        // ได้เบอร์ผู้สมัคร
        g_selectedCandidate = line.substring(3).toInt();
        String sub = "เลือกหมายเลข " + String(g_selectedCandidate);
        showUIx(UI_SELECTED, sub.c_str(), TR_FADE);
        // (ให้ผู้ใช้เห็นสักหน่อย)
        delay(400);
      }
      else if (line.equalsIgnoreCase("SENDING"))
      {
        // เริ่มส่ง/ประมวลผล -> หน้าโหลด
        uiSetLoading(true);
        showUIx(UI_SENDING, "กำลังส่งข้อมูล...", TR_NONE);
      }
      else if (line.equalsIgnoreCase("VOTE:OK"))
      {
        uiSetLoading(false);
        showUIx(UI_THANKS, "ทำรายการสำเร็จ", TR_FADE);
        setVotedByIndex(idx, 1); // ค่อย mark เมื่อได้ผลสำเร็จจริง
        finished = true;
      }
      else if (line.equalsIgnoreCase("VOTE:ERR"))
      {
        uiSetLoading(false);
        showUIx(UI_ERROR, "ส่งข้อมูลไม่สำเร็จ", TR_FADE);
        finished = true;
      }
      else if (line.equalsIgnoreCase("ABORT"))
      {
        uiSetLoading(false);
        showUIx(UI_ERROR, "ยกเลิกรายการ", TR_FADE);
        finished = true;
      }
    }

    uiTick(); // ให้กรอบ/สปินเนอร์วิ่ง
    delay(30);
  }

  // ถ้าหมดเวลาโดยยังไม่ finished
  if (!finished)
  {
    uiSetLoading(false);
    showUIx(UI_ERROR, "หมดเวลารอการเลือก", TR_FADE);
  }

  // ปิดช่วงรอ แล้วกลับหน้า READY
  g_waitingChoice = false;
  delay(800);
  showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
}

// วัด echo ครั้งเดียว (เวอร์ชันสั้น ใช้กับ measureDistanceCm)
inline unsigned long us_read_once()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return pulseIn(ECHO_PIN, HIGH, US_TIMEOUT); // microseconds
}

// [ADD] อ่าน 3 ครั้งเอามัธยฐาน เพื่อลดสไปค์
float measureDistanceCm()
{
  unsigned long a = us_read_once();
  delayMicroseconds(150);
  unsigned long b = us_read_once();
  delayMicroseconds(150);
  unsigned long c = us_read_once();
  // sort a<=b<=c
  if (a > b)
  {
    auto t = a;
    a = b;
    b = t;
  }
  if (b > c)
  {
    auto t = b;
    b = c;
    c = t;
  }
  if (a > b)
  {
    auto t = a;
    a = b;
    b = t;
  }
  unsigned long us = b;
  if (us == 0)
    return NAN;
  return (float)us / 58.0f; // cm
}

// ===== [ADD] Robust ultrasonic helpers =====
#ifndef PULSEIN_LONG_TIMEOUT_US
#define PULSEIN_LONG_TIMEOUT_US 50000UL // สำรอง ถ้าไลบรารีเก่า
#endif

// เกณฑ์กรองค่าที่เชื่อถือได้
static const float MIN_VALID_CM = 0.0f;
static const float MAX_VALID_CM = 300.0f;

// อ่าน echo แบบ robust: รอให้ ECHO เป็น LOW ก่อนทุกครั้ง, ใช้ pulseInLong
unsigned long us_read_echo_once_robust()
{
  // กันกรณี ECHO ยังค้าง HIGH จากรอบก่อน
  // รอให้ LOW ก่อน (แต่จำกัดเวลา)
  (void)pulseInLong(ECHO_PIN, LOW, 3000UL);

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // วัดช่วง HIGH ของ ECHO
  return pulseInLong(ECHO_PIN, HIGH, US_TIMEOUT);
}

// วัดหลายครั้ง → มัธยฐาน → คัดกรองช่วง valid
float measureDistanceCmRobust()
{
  unsigned long a = us_read_echo_once_robust();
  delayMicroseconds(150);
  unsigned long b = us_read_echo_once_robust();
  delayMicroseconds(150);
  unsigned long c = us_read_echo_once_robust();

  // sort a<=b<=c
  if (a > b)
  {
    auto t = a;
    a = b;
    b = t;
  }
  if (b > c)
  {
    auto t = b;
    b = c;
    c = t;
  }
  if (a > b)
  {
    auto t = a;
    a = b;
    b = t;
  }
  unsigned long us = b;
  if (us == 0)
    return NAN; // timeout → ไม่เชื่อถือ

  float cm = (float)us / 58.0f;
  if (cm < MIN_VALID_CM || cm > MAX_VALID_CM)
    return NAN; // กรองค่าหลอก
  return cm;
}

// [ADD] งานหลัก Ultrasonic: อัปเดต nearState + ตัดสินใจหลับ
// ===== [REPLACE CALL INSIDE YOUR TICK] =====
void ultrasonicTickForSleep()
{
  if (millis() - lastUSms < US_INTERVAL_MS)
    return;
  lastUSms = millis();

  float cm = measureDistanceCmRobust(); // <-- ใช้ตัว robust

  // ถ้าอ่านไม่ได้: นับ FAR ต่อ และพิมพ์ log เป็นครั้งคราว
  if (isnan(cm))
  {
    farConsec = min<uint8_t>(FAR_CONFIRM_N, farConsec + 1);
    nearConsec = 0;

    if (DEBUG_ULTRA && (millis() - lastUltraLogMs >= 1000))
    {
      Serial.println("[US] cm=NaN (treat FAR)");
      lastUltraLogMs = millis();
    }
  }
  else
  {
    // ตัดสินใจ newNear ด้วยฮิสเทอรีส
    bool wantNear = nearState;
    if (!nearState && cm <= NEAR_ON_CM)
      wantNear = true;
    if (nearState && cm >= NEAR_OFF_CM)
      wantNear = false;

    if (wantNear)
    {
      nearConsec = min<uint8_t>(NEAR_CONFIRM_N, nearConsec + 1);
      farConsec = 0;
    }
    else
    {
      farConsec = min<uint8_t>(FAR_CONFIRM_N, farConsec + 1);
      nearConsec = 0;
    }

    // เปลี่ยนสถานะเมื่อ “ยืนยัน” ครบ N เฟรม
    bool newNear = nearState;
    if (!nearState && nearConsec >= NEAR_CONFIRM_N)
      newNear = true;
    if (nearState && farConsec >= FAR_CONFIRM_N)
      newNear = false;

    // log ทุก 1s หรือเมื่อมีการสลับสถานะ
    if (DEBUG_ULTRA && (millis() - lastUltraLogMs >= 1000 || newNear != nearState))
    {
      Serial.print("[US] cm=");
      Serial.printf("%.1f", cm);
      Serial.print(" near=");
      Serial.println(newNear ? 1 : 0);
      lastUltraLogMs = millis();
    }

    if (newNear != nearState)
    {
      mySerial.println(newNear ? "NEAR" : "FAR"); // แจ้ง ODROID ถ้าต่อ UART
      nearState = newNear;
      if (newNear)
        lastNearSeenMs = millis(); // รีเฟรชเวลาเมื่อเห็นคน
    }
    else
    {
      if (newNear)
        lastNearSeenMs = millis(); // ยังเห็นคนอยู่
    }
  }

  // ไม่มี NEAR ต่อเนื่องครบ 5s → หลับ
  if (!nearState && (millis() - lastNearSeenMs >= NO_NEAR_SLEEP_MS))
  {
    Serial.println("No NEAR (valid) for 5s -> Deep-sleep");
    goDeepSleepNow();
  }
}

// ---------- Setup / Loop ----------
#include "driver/rtc_io.h"
#include "esp_system.h"

// แทนฟังก์ชัน tft_output เดิมทั้งหมด
// Callback ของ TJpgDec ที่รองรับการครอปทุกทิศ (x/y อาจติดลบได้)
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
  int16_t W = tft.width();
  int16_t H = tft.height();

  // ตัดแถวที่อยู่นอกจอด้านบน/ล่าง
  if (y >= H || (y + (int16_t)h) <= 0)
    return true;

  // แถวต่อแถว (เพื่อคลิปซ้าย/ขวาได้ละเอียดยิ่งขึ้น)
  for (int16_t row = 0; row < (int16_t)h; row++)
  {
    int16_t yy = y + row;
    if (yy < 0 || yy >= H)
      continue; // นอกจอแนวตั้ง ข้าม

    int16_t xx = x;
    int16_t ww = (int16_t)w;
    uint16_t *src = bitmap + row * w;

    // คลิปลบซ้าย
    if (xx < 0)
    {
      int16_t skip = -xx;
      if (skip >= ww)
        continue; // ทั้งแถวอยู่นอกจอ
      xx = 0;
      ww -= skip;
      src += skip;
    }
    // คลิปล้นขวา
    if (xx + ww > W)
    {
      int16_t keep = W - xx;
      if (keep <= 0)
        continue;
      ww = keep;
    }

    if (ww > 0)
      tft.pushImage(xx, yy, (uint16_t)ww, 1, src);
  }
  return true;
}

// วาด JPEG พอดีจอ เริ่มที่ (0,0) โดยไม่จัดกึ่งกลาง/ไม่ครอบ
bool drawJpgExactFromSD(const String &path)
{
  uint16_t jw, jh;
  if (!TJpgDec.getJpgSize(&jw, &jh, path.c_str()))
  {
    // แจ้งบนจอว่าไม่รองรับ (มักเป็น Progressive JPEG)
    digitalWrite(SD_CS, HIGH);
    digitalWrite(SS_PIN, HIGH);
    digitalWrite(TFT_CS, LOW);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Unsupported JPG", 10, 10, 2);
    tft.drawString("Likely Progressive", 10, 28, 2);
    tft.drawString(path, 10, 46, 2);
    digitalWrite(TFT_CS, HIGH);
    return false;
  }

  TJpgDec.setJpgScale(1);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(TFT_CS, LOW);
  tft.fillScreen(TFT_BLACK);
  bool ok = TJpgDec.drawSdJpg(0, 0, path.c_str());
  digitalWrite(TFT_CS, HIGH);
  return ok;
}

// [ADD] วาดรูปให้พอดีกลางจอ
// วาด JPEG ให้ "เต็มจอ" แบบครอบ (cover) ด้วยการ downscale 1/2/4/8 แล้วเลื่อนศูนย์กลาง
bool drawJpgCoverFromSD(const String &path)
{
  uint16_t jw, jh;
  if (!TJpgDec.getJpgSize(&jw, &jh, path.c_str()))
    return false;

  uint16_t sw = tft.width(), sh = tft.height();

  // เลือก scale (1/2/4/8) ที่ทำให้รูปหลังสเกล >= จอ ทั้งสองมิติ (เพื่อ cover)
  uint8_t candidates[4] = {1, 2, 4, 8};
  uint8_t scale = 1;
  bool ok = false;
  for (uint8_t i = 0; i < 4; i++)
  {
    uint8_t s = candidates[i];
    uint16_t dw = jw / s;
    uint16_t dh = jh / s;
    if (dw >= sw && dh >= sh)
    {
      scale = s;
      ok = true;
      break;
    }
  }
  if (!ok)
  {
    // ไม่มีสเกลที่ครอบเต็ม (เช่นไฟล์เล็กกว่าจอมาก) -> ใช้ scale=1 (จะไม่เต็มเป๊ะ)
    scale = 1;
  }
  TJpgDec.setJpgScale(scale);

  uint16_t dw = jw / scale, dh = jh / scale;

  // origin ติดลบเมื่อ dw/dh > จอ → ให้ครอบกลางจอ
  int16_t ox = (int16_t)((int32_t)sw - (int32_t)dw) / 2;
  int16_t oy = (int16_t)((int32_t)sh - (int32_t)dh) / 2;

  // ปล่อยบัสอื่นก่อนใช้จอ
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(TFT_CS, LOW);

  tft.fillScreen(TFT_BLACK);
  bool res = TJpgDec.drawSdJpg(ox, oy, path.c_str()); // ox/oy อาจติดลบได้ (เราคลิปใน callback แล้ว)

  digitalWrite(TFT_CS, HIGH);
  return res;
}

// [ADD] ช่วยแสดงรูปตามหมายเลข (รองรับ .jpg/.JPG)
void showCandidateJpg(uint8_t n)
{
  String p_plain = "/" + String(n) + ".jpg";
  String p_plainU = "/" + String(n) + ".JPG";
  char buf[16];
  snprintf(buf, sizeof(buf), "/%02u.jpg", n);
  String p_pad = String(buf);
  snprintf(buf, sizeof(buf), "/%02u.JPG", n);
  String p_padU = String(buf);

  String path;
  if (SD.exists(p_plain))
    path = p_plain;
  else if (SD.exists(p_plainU))
    path = p_plainU;
  else if (SD.exists(p_pad))
    path = p_pad;
  else if (SD.exists(p_padU))
    path = p_padU;

  if (path.length() == 0)
  {
    digitalWrite(SD_CS, HIGH);
    digitalWrite(SS_PIN, HIGH);
    digitalWrite(TFT_CS, LOW);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Missing:", 8, 96, 2);
    tft.drawString("/" + String(n) + ".jpg", 8, 114, 2);
    char miss[16];
    snprintf(miss, sizeof(miss), "/%02u.jpg", n);
    tft.drawString(String("or ") + miss, 8, 132, 2);
    digitalWrite(TFT_CS, HIGH);
    return;
  }
  drawJpgExactFromSD(path);
}

void showIdleScreen(const char *msg = "Ready")
{
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(TFT_CS, LOW);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(msg, 10, 10, 2);
  digitalWrite(TFT_CS, HIGH);
}

void setup()
{
  // --- Wake pin / IRQ ---
  rtc_gpio_hold_dis((gpio_num_t)WAKE_PIN);
  pinMode(WAKE_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(WAKE_PIN), WAKE_isr, CHANGE);

  // --- Serial / I2C / UART2 ---
  Serial.begin(115200);
  Serial.setTimeout(200);
  mySerial.setTimeout(200);
  Wire.begin();

  // UART2: RX=16, TX=17 (บอร์ดลูก/ODROID)
  mySerial.begin(9600, SERIAL_8N1, 16, 17);

  // --- Ultrasonic pins ---
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT); // GPIO34 input-only
  digitalWrite(TRIG_PIN, LOW);
  lastNearSeenMs = millis();

  // --- EEPROM header/init ---
  EEPROM.begin(EEPROM_SIZE);
  if (!headerOK())
  {
    Serial.println("Init header...");
    writeHeader();
    for (int i = 0; i < MAX_RECORDS; i++)
      clearRec(i);
  }

  // --- SPI / Bus guard ---
  // VSPI: SCK=18, MISO=19, MOSI=23 — เราคุมทุก CS เอง
  SPI.begin(18, 19, 23, SD_CS);
  spi_idle_all(); // ดันทุก CS = HIGH

  // --- SD Card: ลอง 10 MHz -> 4 MHz ---
  // --- SD Card: เริ่มแบบปลอดภัย ไม่ดึง CS ลงเอง ---
  bool sdOK = false;
  {
    // ให้แน่ใจว่า CS ทุกตัวเป็น OUTPUT และ HIGH
    pinMode(SD_CS, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    pinMode(SS_PIN, OUTPUT);
    digitalWrite(SD_CS, HIGH); // <-- สำคัญ: ปล่อย HIGH
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(SS_PIN, HIGH);

    // เริ่มที่ความถี่ต่ำก่อน (เสถียรสุด) แล้วค่อยเพิ่ม
    if (SD.begin(SD_CS, SPI, 1000000))
    { // 1 MHz
      sdOK = (SD.cardType() != CARD_NONE);
      if (!sdOK)
        SD.end();
    }
    if (!sdOK)
    {
      if (SD.begin(SD_CS, SPI, 4000000))
      { // 4 MHz
        sdOK = (SD.cardType() != CARD_NONE);
        if (!sdOK)
          SD.end();
      }
    }
    if (!sdOK)
    {
      if (SD.begin(SD_CS, SPI, 10000000))
      { // 10 MHz (ถ้าการ์ดดี)
        sdOK = (SD.cardType() != CARD_NONE);
        if (!sdOK)
          SD.end();
      }
    }

    if (sdOK)
    {
      Serial.printf("SD OK, type=%u, size=%llu MB\n",
                    (unsigned)SD.cardType(),
                    (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
    }
    else
    {
      Serial.println("SD mount failed (tried 1/4/10 MHz)");
    }
  }

  // --- TFT + TJpg callback ---
  tft.init();
  tft.setSwapBytes(true);
  tft.setRotation(0); // แนวนอน 320x240
  TJpgDec.setCallback(tft_output);

  if (!spr.created())
  {
    spr.setColorDepth(8);
    if (!spr.createSprite(tft.width(), tft.height()))
    {
      Serial.println("[UI] createSprite(8bpp) failed, retry 4bpp");
      spr.setColorDepth(4);
      if (!spr.createSprite(tft.width(), tft.height()))
      {
        Serial.println("[UI] createSprite failed.");
      }
    }
  }
  showIdleScreen(sdOK ? "SD OK" : "No SD");

  showUIx(UI_BOOT, "กำลังตรวจสอบระบบ", TR_FADE);
  delay(600);
  showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);

  // --- RC522 init (ปล่อยบัสจริง + รีเซ็ต RST ก่อน) ---
  Serial.println("Init RC522...");
  rfid_bus_begin();
  rc522_hard_reset();
  rfid.PCD_Init();
  byte rc522v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  rfid_bus_end();
  Serial.printf("RC522 Version=0x%02X\n", rc522v);
  if (rc522v == 0x00 || rc522v == 0xFF)
  {
    Serial.println("[RC522] Bad version (0x00/0xFF) -> ตรวจ CS/MISO/MOSI/SCK และว่ามี CS อื่นค้าง LOW ไหม");
  }

  // --- I/O อื่น ๆ ---
  pinMode(buzzerPin, OUTPUT);
  pinMode(switchPin33, INPUT_PULLUP);
  pinMode(switchPin32, INPUT_PULLUP);
  // ถ้าใช้ LED เพิ่มค่อยเปิด
  // pinMode(ledPin, OUTPUT); digitalWrite(ledPin, LOW);

  // --- Fingerprint module ---
  if (!fingerBegin())
  {
    Serial.println("Fingerprint module not found. Check wiring.");
  }
  else
  {
    Serial.println("Fingerprint module ready.");
  }

  // --- Info / wake-pin debug ---
  Serial.printf("MAX_RECORDS=%d, RECORD_SIZE=%d\n", MAX_RECORDS, RECORD_SIZE);
  printBootAndWakeInfo();

  pinMode(WAKE_PIN, INPUT_PULLDOWN); // กันลอยซ้ำ
  attachInterrupt(digitalPinToInterrupt(WAKE_PIN), WAKE_isr, CHANGE);
  dbgPrintWakePin("boot");

  lastUltraLogMs = millis();

  isShowingPhoto = false;
  uiSetScanning(true);

  Serial.println("setup() done.");
}

// [ADD] ฟังก์ชันรับคำสั่งจากบอร์ดลูกโซ่
void handleU2Line(const String &raw)
{
  String m = raw;
  m.trim();
  if (m.startsWith("SEL:"))
  {
    if (m.equalsIgnoreCase("SEL:CLEAR"))
    {
      isShowingPhoto = false;                                 // ปลดล็อก
      uiSetScanning(true);                                    // จะให้กรอบสแกนทำงานต่อก็ได้
      showUIx(UI_SCAN_CARD, "ยื่นบัตรใกล้เครื่องอ่าน", TR_SLIDE_UP); // กลับไปหน้าหลัก
    }
    else
    {
      int n = m.substring(4).toInt(); // หลัง "SEL:"
      if (n >= 0 && n <= 99)
      {
        isShowingPhoto = true; // ล็อกไม่ให้ UI ทับ
        uiSetScanning(false);  // ปิดกรอบแอนิเมชันบนหน้ารูป
        showCandidateJpg((uint8_t)n);
      }
      else
      {
        isShowingPhoto = true;
        uiSetScanning(false);
        showIdleScreen("Bad SEL");
      }
    }
    return;
  }
  if (m.startsWith("CF:"))
  {
    int n = m.substring(3).toInt();
    g_selectedCandidate = n;
    // ถ้าอยู่ช่วงรอ ให้แสดงผลชัดเจน
    if (g_waitingChoice)
    {
      String sub = "เลือกหมายเลข " + String(n);
      uiSetLoading(false);
      showUIx(UI_SELECTED, sub.c_str(), TR_FADE);
    }
    return;
  }
  else if (m.equalsIgnoreCase("SENDING"))
  {
    uiSetLoading(true);
    showUIx(UI_SENDING, "กำลังส่งข้อมูล...", TR_NONE);
    return;
  }
  else if (m.equalsIgnoreCase("VOTE:OK"))
  {
    uiSetLoading(false);
    showUIx(UI_THANKS, "ทำรายการสำเร็จ", TR_FADE);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }
  else if (m.equalsIgnoreCase("VOTE:ERR"))
  {
    uiSetLoading(false);
    showUIx(UI_ERROR, "ส่งข้อมูลไม่สำเร็จ", TR_FADE);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_SLIDE_R);
    return;
  }
  Serial.println(raw);
}

// ===== วางฟังก์ชันนี้ "ถัดจาก" ปิดวงเล็บของ setup() =====
void loop()
{
  // ===== ปุ่มโหมด =====
  int switchReg = digitalRead(switchPin33);
  int switchDel = digitalRead(switchPin32);

  if (switchReg == LOW)
  {
    showUIx(UI_CONFIRM, "โหมดลงทะเบียน", TR_SLIDE_UP);
    delay(500); // ให้ผู้ใช้เห็น
    while (digitalRead(switchPin33) == LOW)
      delay(10);
    registerCardAndFingerprint();
    uiShownScanCard = false;
    delay(300);
    return;
  }
  else if (switchDel == LOW)
  {
    showUIx(UI_ERROR, "โหมดลบข้อมูล", TR_SLIDE_UP);
    delay(500);
    while (digitalRead(switchPin32) == LOW)
      delay(10);
    deleteCardFlow();
    uiShownScanCard = false;
    delay(300);
    return;
  }

  // ===== แตะการ์ด (ล็อคบัส RC522 เสมอ) =====

  // NEW: แสดง "สแกนบัตร" 1 ครั้ง เมื่อเข้าลูปว่างครั้งแรก
  if (!uiShownScanCard)
  {
    showUIx(UI_SCAN_CARD, "ยื่นบัตรใกล้เครื่องอ่าน", TR_SLIDE_UP);
    uiShownScanCard = true;
    uiScanCardShownAt = millis();
  }

  bool cardReady = false;
  rfid_bus_begin();
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
  {
    cardReady = true;
  }
  rfid_bus_end();

  if (cardReady)
  {
    // NEW: reset flag เพื่อให้รอบถัดไปขึ้น "สแกนบัตร" ใหม่อีกครั้ง หลังจบ flow
    uiShownScanCard = false;
    normalScanFlow();
  }

  // ===== รับคำสั่งจากบอร์ดลูก (UART2) =====
  if (mySerial.available())
  {
    String msg = mySerial.readStringUntil('\n');
    msg.trim();

    if (msg.equalsIgnoreCase("SLEEP!"))
    {
      mySerial.println("OK SLEEP");
      delay(30);
      goDeepSleepNow(); // ไม่กลับจากฟังก์ชันนี้
    }

    handleU2Line(msg);

    // log debug จากบอร์ดลูก
    Serial.println(msg);
  }

  // ===== อัลตราโซนิก: auto-sleep =====
  ultrasonicTickForSleep();

  // ===== คำสั่งผ่าน USB Serial (debug) =====
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.equalsIgnoreCase("ULTRA?") || cmd.equalsIgnoreCase("U"))
    {
      float cm = measureDistanceCm();
      bool ns = nearState;
      if (!isnan(cm))
      {
        if (!ns && cm <= NEAR_ON_CM)
          ns = true;
        if (ns && cm >= NEAR_OFF_CM)
          ns = false;
      }
      Serial.print("[US:NOW] cm=");
      if (isnan(cm))
        Serial.print("NaN");
      else
        Serial.printf("%.1f", cm);
      Serial.print(" near=");
      Serial.println(ns ? 1 : 0);
    }
    else if (cmd.equalsIgnoreCase("W?"))
    {
      dbgPrintWakePin("now");
    }
    else if (cmd.equalsIgnoreCase("WTEST"))
    {
      uint32_t t0 = millis();
      while (millis() - t0 < 10000)
      {
        dbgPrintWakePin("probe");
        delay(300);
      }
    }
  }
  uiTick();
}