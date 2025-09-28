#define BLYNK_TEMPLATE_ID "TMPL6G6KsJzqK"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "RUBdFFrRrLJ99YHyTgYN5rew8gfkPzaH"

const char *WIFI_SSID = "CoEIoT";
const char *WIFI_PASS = "iot.coe.psu.ac.th";

#define WAKE_PIN 14
#include <algorithm>

// ==== must be the very first lines ====
const int UID_HEX_MAX = 16;
struct Rec;

void readRec(int idx, Rec &r);         // tell IDE not to autogenerate wrong prototypes
void writeRec(int idx, const Rec &r);  // uses incomplete type by reference (OK)

static int g_selectedCandidate = -1;
static bool g_waitingChoice = false;  // อยู่ช่วงรอผู้ใช้เลือก

static bool g_votePosted = false;  // กันยิงซ้ำในหนึ่งรอบเลือก
static int g_idxPending = -1;      // เก็บ index ของบัตรที่จะ mark voted

#define SD_CS 13
#define TFT_CS 15
#define SS_PIN 5

int mjoy = 35;
int valmjoy = 0;

// ===== Analog Keypad Configuration =====
// ค่า analog ที่อ่านได้จาก keypad (2 ปุ่ม จากการทดสอบจริง)
const int KEY_NONE = 4095;    // ไม่กดปุ่ม (HIGH)
const int KEY_REGISTER = 0;   // ปุ่มลงทะเบียน (0V)
const int KEY_DELETE = 1950;  // ปุ่มลบ (~1950)
const int KEY_SCORE = 350;    // ปุ่มเช็ค score (300-400) - ส่ง T ไป Arduino

const int KEY_TOLERANCE = 150;  // ความคลาดเคลื่อนที่ยอมรับได้ (เพิ่มเป็น 150)

// ตัวแปรสำหรับ debounce และ hold detection
static int lastKeyValue = KEY_NONE;
static uint32_t lastKeyTime = 0;
static uint32_t keyPressStartTime = 0;
static int currentPressedKey = -1;
static const uint32_t KEY_DEBOUNCE_MS = 100;    // ลดเป็น 100ms
static const uint32_t KEY_HOLD_TIME_MS = 2000;  // กดค้าง 3 วินาที
static uint32_t lastKeyPollTime = 0;
static const uint32_t KEY_POLL_INTERVAL = 50;  // polling ทุก 50ms

// ตัวแปรสำหรับ mode management
static bool inRegisterMode = false;
static bool inDeleteMode = false;
static bool inScoreMode = false;
static bool testModeEnabled = false;  // ควบคุมโหมดทดสอบ (เปิด/ปิด)
static bool waitingForPassword = false;  // รอการยืนยัน password จาก Arduino

#include "driver/rtc_io.h"  // สำหรับ rtc_gpio_get_level()
#include "esp_system.h"

// ประกาศล่วงหน้าค่าคงที่ที่ struct ใช้ (ถ้าคุณมีเวอร์ชันเป็น #define อยู่แล้ว ข้ามได้)
#if 0  // DISABLE: duplicates UID_HEX_MAX (we already #define it at top)
const int      UID_HEX_MAX = 16;
#endif

// ต้อง "นิยาม" struct Rec ให้เสร็จก่อนฟังก์ชัน readRec()/writeRec()
// (forward declare เฉยๆ ไม่พอ เพราะฟังก์ชันแตะฟิลด์ใน struct)
#if 0  // DISABLE: duplicate struct Rec (already defined at top)
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

#include <HTTPClient.h>

// ===== Web API (FastAPI) =====
static const char *API_SCHEME = "http";         // ถ้าใช้ HTTPS ดูหมายเหตุท้าย
static const char *API_HOST = "172.30.81.175";  // IP/โดเมนของเซิร์ฟเวอร์
static const uint16_t API_PORT = 8001;          // พอร์ต FastAPI
static const char *API_TOKEN = "mysecret";      // ต้องตรงกับ API_TOKEN ฝั่ง FastAPI

// ===== SPI bus guard & CS helpers =====
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static portMUX_TYPE spiMux = portMUX_INITIALIZER_UNLOCKED;

inline void spi_guard_begin() {
  portENTER_CRITICAL(&spiMux);
}
inline void spi_guard_end() {
  portEXIT_CRITICAL(&spiMux);
}

inline void spi_deselect_all() {
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);  // RC522 CS
  delayMicroseconds(50);       // หน่วงนานขึ้น
}

inline void spi_select_tft() {
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(TFT_CS, LOW);
}

inline void spi_select_sd() {
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(SD_CS, LOW);
}

inline void spi_select_rc522() {
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SS_PIN, LOW);
}

// ===== UI (no-image) =====
enum UIState {
  UI_BOOT,
  UI_READY,
  UI_SCAN_CARD,
  UI_CARD_OK,
  UI_CARD_FAIL,
  UI_CARD_DUPLICATE,      // บัตรซ้ำ (ลงทะเบียนแล้ว)
  UI_CARD_NOT_FOUND,      // บัตรไม่อยู่ในระบบ
  UI_CARD_ALREADY_VOTED,  // บัตรใช้งานแล้ว (โหวตไปแล้ว)
  UI_SCAN_FINGER,
  UI_FINGER_OK,
  UI_FINGER_FAIL,
  UI_FINGER_LIFT,  // ยกนิ้วขึ้น
  UI_CONFIRM,
  UI_THANKS,
  UI_ERROR,
  UI_SLEEP,
  UI_WAKE,
  UI_WAIT_CHOICE,    // รอผู้ใช้เลือกผู้สมัคร
  UI_SELECTED,       // แสดงว่าผู้ใช้เลือกหมายเลขอะไรแล้ว
  UI_SENDING,        // ขณะกำลังส่ง/รอผล
  UI_SD_CHECK,       // กำลังตรวจสอบ SD Card
  UI_SD_FAIL,        // SD Card ไม่ทำงาน
  UI_SD_RETRY,       // กำลังลอง SD Card ใหม่
  UI_MODE_REGISTER,  // โหมดลงทะเบียน
  UI_MODE_DELETE,    // โหมดลบข้อมูล
  UI_REGISTER_SCAN,  // สแกนบัตรในโหมดลงทะเบียน
  UI_DELETE_SCAN,    // สแกนบัตรในโหมดลบ
  UI_WAIT_PASSWORD,  // รอการยืนยัน password จาก Arduino
  UI_PASSWORD_OK     // ยืนยัน password สำเร็จ
};
static bool uiShownScanCard = false;
static uint32_t uiScanCardShownAt = 0;

UIState g_lastState = UI_READY;
static String g_lastSubtitle;                        // จำ subtitle ล่าสุด
static uint32_t g_lastPaintMs = 0;                   // ไว้คุมคูลดาวน์ (ถ้าต้องการ)
static const uint16_t SAME_STATE_COOLDOWN_MS = 350;  // กันสั่น (ปรับได้/จะปิดก็ได้)
static bool isShowingPhoto = false;

// [ADD] จอ + SD + JPG decoder
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <SD.h>

#include <functional>

// RFID_CS = SS_PIN (=5) มีอยู่แล้วจากโค้ดคุณ

// [ADD] สร้างอ็อบเจ็กต์จอ
TFT_eSPI tft;
// ===== Modern UI Transition =====
TFT_eSprite spr(&tft);

// วาดส่วนโค้งลง "จอจริง" (tft)
void drawArcSprite(int cx, int cy, int rOuter, int rInner, int a0, int a1,
                   uint16_t col, uint16_t /*bg*/) {
  // step ประมาณ 3 องศา
  for (int a = a0; a <= a1; a += 3) {
    float rad = a * 0.01745329252f;
    int x0 = cx + (int)(rInner * cosf(rad));
    int y0 = cy + (int)(rInner * sinf(rad));
    int x1 = cx + (int)(rOuter * cosf(rad));
    int y1 = cy + (int)(rOuter * sinf(rad));
    tft.drawLine(x0, y0, x1, y1, col);
  }
}

// วาดส่วนโค้งลง "สไปรท์" (spr)
void drawArcSprite(TFT_eSprite &s, int cx, int cy, int rOuter, int rInner, int a0, int a1,
                   uint16_t col, uint16_t /*bg*/) {
  for (int a = a0; a <= a1; a += 3) {
    float rad = a * 0.01745329252f;
    int x0 = cx + (int)(rInner * cosf(rad));
    int y0 = cy + (int)(rInner * sinf(rad));
    int x1 = cx + (int)(rOuter * cosf(rad));
    int y1 = cy + (int)(rOuter * sinf(rad));
    s.drawLine(x0, y0, x1, y1, col);
  }
}

enum UITrans {
  TR_NONE,
  TR_SLIDE_L,
  TR_SLIDE_R,
  TR_SLIDE_UP,
  TR_SLIDE_DOWN,
  TR_FADE
};

// ==== Time-based progress bar (loop) ====
static bool ui_isScanning = false;    // ต้องมาก่อน barStart()
static uint32_t ui_animStart = 0;     // ต้องมาก่อน barStart()
static bool ui_isBusyBorder = false;  // ใช้แทน progress bar ระหว่างกำลังประมวลผล/รอ
static bool g_barOn = false;
static uint32_t g_barStart = 0;
static uint32_t g_barPeriod = 3000;  // ระยะเวลาเติมเต็มหนึ่งรอบ (ms)
static String g_barLabel;

void barStart(uint32_t period_ms = 1000, const String &label = "") {
  // เปลี่ยนจากแถบด้านล่างเป็น "กรอบกระพริบ" เพื่อสื่อว่าระบบกำลังทำงาน
  // ยกเลิกการใช้ progress bar เดิม
  g_barOn = false;
  g_barStart = millis();
  g_barPeriod = (period_ms == 0 ? 1000 : period_ms);
  g_barLabel = label;
  // เปิดโหมดกรอบกระพริบ (busy)
  ui_isBusyBorder = true;
  ui_animStart = millis();
}
void barStop() {
  g_barOn = false;
  // ปิดโหมดกรอบกระพริบ (busy)
  ui_isBusyBorder = false;
}

// วาดหลอดเรียบๆ ด้านล่างจอ วิ่งเต็มตามเวลาแล้ววน
static void drawTimedBarOverlay() {
  if (!g_barOn)
    return;

  // ปล่อยบัสอื่นก่อนทำงานกับ TFT
  spi_deselect_all();
  delayMicroseconds(10);

  int W = tft.width(), H = tft.height();
  int bw = W - 60, bh = 12;
  int x = (W - bw) / 2, y = H - 32;

  // คำนวณเฟส 0..1 จากเวลาที่ผ่านไป
  float t = float((millis() - g_barStart) % g_barPeriod) / float(g_barPeriod);  // 0..1
  float phase = (t <= 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);                   // ไป-กลับ 0→1→0
  int fillw = int(bw * phase);

  // กรอบ
  tft.drawRoundRect(x - 2, y - 2, bw + 4, bh + 4, 4, TFT_WHITE);
  // แถบ (เติมจากซ้ายไปขวา)
  if (fillw > 0) {
    int seg = bw / 4;           // ความยาวก้อน
    int ofs = int(bw * phase);  // ตำแหน่งวิ่ง 0..bw
    int x1 = x + ofs - seg / 2;
    if (x1 < x)
      x1 = x;
    int x2 = x + ofs + seg / 2;
    if (x2 > x + bw)
      x2 = x + bw;
    if (x2 > x1)
      tft.fillRoundRect(x1, y, x2 - x1, bh, 3, TFT_CYAN);
  }
  // ป้ายเล็กๆ (ออปชัน)
  if (g_barLabel.length()) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(g_barLabel, (W - tft.textWidth(g_barLabel, 2)) / 2, y - 18, 2);
  }
}

// Easing นุ่มๆ (0..1 -> 0..1)
static inline float easeInOutQuad(float x) {
  return (x < 0.5f) ? 2 * x * x : 1 - powf(-2 * x + 2, 2) / 2;
}

// ===== Added: Deep-sleep support =====
#include "esp_sleep.h"

#include <math.h>

bool postVoteToServer(int option) {
  if (option < 0 || option > 9) {
    Serial.printf("[API] invalid option=%d\n", option);
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] WiFi not connected");
    return false;
  }

  HTTPClient http;
  String url = String(API_SCHEME) + "://" + API_HOST + ":" + String(API_PORT) + "/vote";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-KEY", API_TOKEN);
  http.setTimeout(3000);  // 3s

  String body = String("{\"option\":\"") + option + "\"}";
  int code = http.POST(body);
  Serial.printf("[API] POST %s -> %d\n", url.c_str(), code);
  if (code > 0) {
    String resp = http.getString();
    Serial.printf("[API] resp: %s\n", resp.c_str());
  }
  http.end();
  return code == 200;
}

// ==== [ADD] Wake-pin debug helpers (no change to existing code) ====
volatile uint32_t WAKE_edges = 0;
volatile uint32_t WAKE_lastMs = 0;

IRAM_ATTR void WAKE_isr() {
  // นับทุกครั้งที่มีขอบขึ้น/ลง
  WAKE_edges++;
  WAKE_lastMs = millis();
}

// อ่านระดับจากทั้ง digital และ RTC domain
int wake_digital() {
  return digitalRead(WAKE_PIN);
}
int wake_rtc() {
  return rtc_gpio_get_level((gpio_num_t)WAKE_PIN);
}

void dbgPrintWakePin(const char *tag) {
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
void fillGradientV(uint16_t c1, uint16_t c2) {
  // ไล่สีแนวตั้งทั้งจอ
  int W = tft.width(), H = tft.height();
  for (int y = 0; y < H; ++y) {
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

int16_t centerX(const String &s, int font) {
  int w = tft.textWidth(s, font);
  return (tft.width() - w) / 2;
}
void drawCenter(const String &s, int y, int font, uint16_t fg, uint16_t bg = TFT_TRANSPARENT) {
  tft.setTextColor(fg, bg);
  tft.drawString(s, centerX(s, font), y, font);
}

// --------- Badge / Icons (vector-ish) ----------
void drawShieldHeader(const char *title) {
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

void drawCheckBadge(int cx, int cy) {
  tft.fillCircle(cx, cy, 34, TFT_DARKGREEN);
  tft.fillCircle(cx, cy, 30, TFT_GREEN);
  // เช็ค
  tft.drawLine(cx - 16, cy, cx - 6, cy + 12, TFT_WHITE);
  tft.drawLine(cx - 6, cy + 12, cx + 16, cy - 14, TFT_WHITE);
  tft.drawLine(cx - 17, cy, cx - 7, cy + 12, TFT_WHITE);
  tft.drawLine(cx - 7, cy + 12, cx + 17, cy - 14, TFT_WHITE);
}

void drawCrossBadge(int cx, int cy) {
  tft.fillCircle(cx, cy, 34, TFT_MAROON);
  tft.fillCircle(cx, cy, 30, TFT_RED);
  // กากบาท
  for (int i = -1; i <= 1; i++) {
    tft.drawLine(cx - 16, cy - 16 + i, cx + 16, cy + 16 + i, TFT_WHITE);
    tft.drawLine(cx - 16, cy + 16 + i, cx + 16, cy - 16 + i, TFT_WHITE);
  }
}

void drawCardIcon(int cx, int cy) {
  // การ์ดสี่เหลี่ยม + แถบแม่เหล็ก
  tft.fillRoundRect(cx - 48, cy - 28, 96, 56, 8, TFT_DARKGREY);
  tft.fillRoundRect(cx - 46, cy - 26, 92, 52, 8, TFT_WHITE);
  tft.fillRect(cx - 46, cy - 6, 92, 14, TFT_NAVY);
  tft.fillRect(cx - 40, cy - 20, 40, 6, TFT_LIGHTGREY);
}

// overload สำหรับ sprite
// void drawArc(TFT_eSprite &s, int cx, int cy, int rOuter, int rInner, int a0, int a1, uint16_t col, uint16_t bg)
// {
//   for (int a = a0; a <= a1; a += 3)
//   {
//     float rad = a * 0.0174533f;
//     int x0 = cx + (int)(rInner * cos(rad));
//     int y0 = cy + (int)(rInner * sin(rad));
//     int x1 = cx + (int)(rOuter * cos(rad));
//     int y1 = cy + (int)(rOuter * sin(rad));
//     s.drawLine(x0, y0, x1, y1, col);
//   }
// }
// แล้วแก้ใน drawNFCIcon / drawFingerprintIconModern ให้เรียก drawArc(s, ...)
// void drawArc(int cx, int cy, int rOuter, int rInner, int a0, int a1, uint16_t col, uint16_t bg);
void drawFingerIcon(int cx, int cy) {
  // วงลายนิ้ว
  tft.drawCircle(cx, cy, 30, TFT_CYAN);
  tft.drawCircle(cx, cy, 24, TFT_CYAN);
  tft.drawCircle(cx, cy, 18, TFT_CYAN);
  tft.drawCircle(cx, cy, 12, TFT_CYAN);
  // โค้งชั้นๆ
  drawArcSprite(cx, cy, 28, 27, 210, 330, TFT_CYAN, TFT_BLACK);
  drawArcSprite(cx, cy, 22, 21, 200, 340, TFT_CYAN, TFT_BLACK);
  drawArcSprite(cx, cy, 16, 15, 190, 350, TFT_CYAN, TFT_BLACK);
}

// --------- Arc helper (approx) ----------
// void drawArc(int cx, int cy, int rOuter, int rInner, int a0, int a1, uint16_t col, uint16_t bg)
// {
//   // step 3° พอ
//   for (int a = a0; a <= a1; a += 3)
//   {
//     float rad = a * 0.0174533f;
//     int x0 = cx + (int)(rInner * cos(rad));
//     int y0 = cy + (int)(rInner * sin(rad));
//     int x1 = cx + (int)(rOuter * cos(rad));
//     int y1 = cy + (int)(rOuter * sin(rad));
//     tft.drawLine(x0, y0, x1, y1, col);
//   }
// }

// ----- Loading spinner -----
static bool ui_isLoading = false;
static uint32_t ui_loadStart = 0;

void uiSetLoading(bool on) {
  ui_isLoading = on;
  if (on)
    ui_loadStart = millis();
}

// วาด spinner แบบกงล้อหมุน (non-blocking)
static void drawSpinner() {
  // ปล่อยบัสอื่นก่อนทำงานกับ TFT
  spi_deselect_all();
  delayMicroseconds(10);

  const int cx = tft.width() / 2, cy = 160, r = 14;
  float t = (millis() - ui_loadStart) / 1000.0f;  // วินาที
  // 12 แท่ง หมุนตามเวลา
  for (int i = 0; i < 12; i++) {
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

void uiSetScanning(bool on) {
  ui_isScanning = on;
  if (on)
    ui_animStart = millis();
}

// วาดกรอบ dash รอบจอ โดยมี phase 0..1 เพื่อเลื่อน dash
static void drawFancyBorder(float phase) {
  // ปล่อยบัสอื่นก่อนทำงานกับ TFT
  spi_deselect_all();
  delayMicroseconds(10);

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
  const int seg = 8;  // ยาวต่อ dash
  const int gap = 6;  // ช่องว่าง
  const int per = seg + gap;

  auto drawDashedH = [&](int x0, int y, int len) {
    int offset = (int)(phase * per);
    for (int x = x0 - offset; x < x0 + len; x += per) {
      int x1 = max(x, x0);
      int x2 = min(x + seg, x0 + len);
      if (x2 > x1)
        tft.drawFastHLine(x1, y, x2 - x1, col);
    }
  };
  auto drawDashedV = [&](int x, int y0, int len) {
    int offset = (int)(phase * per);
    for (int y = y0 - offset; y < y0 + len; y += per) {
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

  // เน้น "มุม" เล็กน้อย (จุดเล็กๆ)
  tft.fillCircle(x + rad, y + rad, 1, col);
  tft.fillCircle(x + w - 1 - rad, y + rad, 1, col);
  tft.fillCircle(x + rad, y + h - 1 - rad, 1, col);
  tft.fillCircle(x + w - 1 - rad, y + h - 1 - rad, 1, col);
}

// Forward declaration for painter used by uiTick animation refresh
void paintScreenToSprite(UIState s, const char *subtitle, bool popIcon, float popK);
void showUIxFallback(UIState s, const char *subtitle);

void uiTick() {
  bool painted = false;
  static uint32_t g_lastIconAnimMs = 0;  // รีเฟรชไอคอนแบบเป็นช่วง ๆ

  // ปล่อยบัสอื่นก่อนทำงานกับ TFT
  spi_deselect_all();
  delayMicroseconds(10);

  // รีเฟรชการ์ดที่หน้า Scan Card และ Wait Choice ให้ขยับตลอด (ประมาณ ~8 FPS)
  if (!isShowingPhoto && (g_lastState == UI_SCAN_CARD || g_lastState == UI_WAIT_CHOICE)) {
    uint32_t now = millis();
    if (now - g_lastIconAnimMs > 120) {  // ~8.3 fps
      g_lastIconAnimMs = now;
      // วาดทั้งเฟรมลง sprite แล้ว push ออกจอ
      spr.fillSprite(TFT_BLACK);
      paintScreenToSprite(g_lastState, g_lastSubtitle.length() ? g_lastSubtitle.c_str() : "", false, 1.0f);
      tft.endWrite();
      spr.pushSprite(0, 0);
      painted = true;
    }
  }

  if (ui_isScanning || ui_isBusyBorder) {
    float t = (millis() - ui_animStart) / 600.0f;
    float phase = t - floorf(t);
    drawFancyBorder(phase);
    painted = true;
  }
  if (ui_isLoading) {
    drawSpinner();
    painted = true;
  }
  if (g_barOn) {
    drawTimedBarOverlay();
    painted = true;
  }

  // อัปเดต timestamp ว่าเพิ่ง "วาด" ไปจริง ๆ
  if (painted)
    g_lastPaintMs = millis();
}

// ====== Modern vector icons (no SD needed) ======
void drawNFCIcon(TFT_eSprite &s, int cx, int cy, float scale = 1.0f, float animPhase = 0.0f) {
  int w = int(110 * scale), h = int(70 * scale), r = int(14 * scale);

  // Animation: subtle bounce and slight rotation
  float bounce = sinf(animPhase * 2.0f * PI) * 2.0f;  // ±2 pixels bounce
  float tilt = sinf(animPhase * 1.5f * PI) * 0.05f;   // ±0.05 radians tilt
  int offsetY = int(bounce);

  // Apply tilt by adjusting corners slightly
  int tiltOffset = int(tilt * h * 0.3f);

  // soft shadow (with animation offset)
  s.fillRoundRect(cx - w / 2 + 3, cy - h / 2 + 5 + offsetY, w, h, r, TFT_DARKGREY);

  // card body (with animation offset)
  s.fillRoundRect(cx - w / 2, cy - h / 2 + offsetY, w, h, r, TFT_WHITE);

  // top gradient bar (with animation offset)
  for (int i = 0; i < int(18 * scale); ++i)
    s.drawFastHLine(cx - w / 2 + 6, cy - h / 2 + 10 + i + offsetY, w - 12, TFT_NAVY + i);

  // chip (with animation offset)
  int cw = int(22 * scale), ch = int(16 * scale), cr = int(4 * scale);
  s.fillRoundRect(cx - w / 2 + int(12 * scale), cy - int(h * 0.18f) + offsetY, cw, ch, cr, TFT_GOLD);
  s.drawRoundRect(cx - w / 2 + int(12 * scale), cy - int(h * 0.18f) + offsetY, cw, ch, cr, TFT_BROWN);

  // contactless waves (with animation offset and pulsing effect)
  uint16_t waveCol = TFT_CYAN;
  float waveIntensity = 0.7f + 0.3f * sinf(animPhase * 3.0f * PI);  // pulsing intensity
  for (int k = 0; k < 3; k++) {
    int off = int((12 * scale + k * 8 * scale) * waveIntensity);
    drawArcSprite(s, cx + int(w * 0.22f), cy - int(h * 0.02f) + offsetY,
                  int(34 * scale) + off, int(34 * scale) + off - 2, 300, 60,
                  waveCol, TFT_TRANSPARENT);
  }
}

void drawFingerprintIconModern(TFT_eSprite &s, int cx, int cy, float scale = 1.0f) {
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
  auto arc = [&](int ro, int ri, int a0, int a1) {
    drawArcSprite(s, cx, cy, ro, ri, a0, a1, c, TFT_TRANSPARENT);
  };
  arc(R - 2, R - 3, 210, 330);
  arc(int(R * 0.78f), int(R * 0.78f) - 1, 195, 350);
  arc(int(R * 0.60f), int(R * 0.60f) - 1, 170, 10);
  arc(int(R * 0.42f), int(R * 0.42f) - 1, 150, 30);
}

// วาด background gradient + header + icon + badge ลง Sprite
void paintScreenToSprite(UIState s, const char *subtitle, bool popIcon = false, float popK = 1.0f) {
  const int W = tft.width(), H = tft.height();
  spr.fillSprite(TFT_BLACK);

  // --- BG gradient (reuse fillGradientV แต่ลง sprite) ---
  // เราวาดเอง: เส้นแนวนอน ไล่สีเหมือนเดิม
  uint16_t c1, c2;
  switch (s) {
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
    case UI_CARD_DUPLICATE:
      c1 = TFT_ORANGE;  // สีส้มสำหรับบัตรซ้ำ
      c2 = TFT_BLACK;
      break;
    case UI_CARD_NOT_FOUND:
      c1 = TFT_MAROON;  // สีแดงสำหรับไม่พบบัตร
      c2 = TFT_BLACK;
      break;
    case UI_CARD_ALREADY_VOTED:
      c1 = TFT_PURPLE;  // สีม่วงสำหรับใช้งานแล้ว
      c2 = TFT_BLACK;
      break;
    case UI_FINGER_LIFT:
      c1 = TFT_PURPLE;  // สีม่วงสำหรับยกนิ้ว
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
    case UI_SD_CHECK:
    case UI_SD_RETRY:
      c1 = TFT_ORANGE;
      c2 = TFT_BLACK;
      break;
    case UI_SD_FAIL:
      c1 = TFT_MAROON;
      c2 = TFT_BLACK;
      break;
    case UI_MODE_REGISTER:
    case UI_REGISTER_SCAN:
      c1 = TFT_DARKGREEN;
      c2 = TFT_BLACK;
      break;
    case UI_MODE_DELETE:
    case UI_DELETE_SCAN:
      c1 = TFT_MAROON;
      c2 = TFT_BLACK;
      break;
  }
  for (int y = 0; y < H; ++y) {
    float k = (float)y / (float)(H - 1);
    uint16_t r1 = ((c1 >> 11) & 0x1F), g1 = ((c1 >> 5) & 0x3F), b1 = (c1 & 0x1F);
    uint16_t r2 = ((c2 >> 11) & 0x1F), g2 = ((c2 >> 5) & 0x3F), b2 = (c2 & 0x1F);
    uint16_t r = r1 + (int)((r2 - r1) * k), g = g1 + (int)((g2 - g1) * k), b = b1 + (int)((b2 - b1) * k);
    uint16_t c = (r << 11) | (g << 5) | b;
    spr.drawFastHLine(0, y, W, c);
  }

  // --- Header (วาดแบบ "wipe" ขวา->ซ้ายเล็กน้อย) ---
  spr.fillRect(0, 0, W, 30, TFT_BLACK);
  spr.drawRect(0, 0, W, 30, TFT_WHITE);

  const char *hdr =
    (s == UI_BOOT) ? "ระบบกำลังเริ่มทำงาน" : (s == UI_WAKE)               ? "กำลังพร้อมใช้งาน"
                                        : (s == UI_READY)              ? "พร้อมให้บริการ"
                                        : (s == UI_SCAN_CARD)          ? "โปรดแตะบัตร"
                                        : (s == UI_CARD_OK)            ? "บัตรถูกต้อง"
                                        : (s == UI_CARD_FAIL)          ? "บัตรไม่ถูกต้อง"
                                        : (s == UI_CARD_DUPLICATE)     ? "บัตรลงทะเบียนแล้ว"
                                        : (s == UI_CARD_NOT_FOUND)     ? "บัตรไม่อยู่ในระบบ"
                                        : (s == UI_CARD_ALREADY_VOTED) ? "บัตรใช้งานแล้ว"
                                        : (s == UI_SCAN_FINGER)        ? "โปรดสแกนลายนิ้วมือ"
                                        : (s == UI_FINGER_OK)          ? "ยืนยันตัวตนสำเร็จ"
                                        : (s == UI_FINGER_FAIL)        ? "ยืนยันตัวตนไม่ผ่าน"
                                        : (s == UI_FINGER_LIFT)        ? "โปรดยกนิ้วขึ้น"
                                        : (s == UI_CONFIRM)            ? "ยืนยันการทำรายการ"
                                        : (s == UI_THANKS)             ? "ขอบคุณ"
                                        : (s == UI_ERROR)              ? "ข้อผิดพลาด"
                                        : (s == UI_SLEEP)              ? "พักการทำงาน"
                                        : (s == UI_WAIT_CHOICE)        ? "รอเลือกผู้สมัคร"
                                        : (s == UI_SELECTED)           ? "ยืนยันตัวเลือก"
                                        : (s == UI_SENDING)            ? "กำลังส่งข้อมูล"
                                        : (s == UI_SD_CHECK)           ? "กำลังตรวจสอบ SD Card"
                                        : (s == UI_SD_FAIL)            ? "SD Card ไม่ทำงาน"
                                        : (s == UI_SD_RETRY)           ? "กำลังลอง SD Card ใหม่"
                                        : (s == UI_MODE_REGISTER)      ? "โหมดลงทะเบียน"
                                        : (s == UI_MODE_DELETE)        ? "โหมดลบข้อมูล"
                                        : (s == UI_REGISTER_SCAN)      ? "แตะบัตรเพื่อลงทะเบียน"
                                        : (s == UI_DELETE_SCAN)        ? "แตะบัตรเพื่อลบข้อมูล"
                                        : (s == UI_WAIT_PASSWORD)      ? "รอการยืนยันจากผู้ดูแล"
                                        : (s == UI_PASSWORD_OK)        ? "ยืนยันสำเร็จ"
                                                                       : "";

  // Header text
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(hdr, (W - spr.textWidth(hdr, 2)) / 2, 6, 2);

  // --- Icon / Badge (pop-in scale) ---
  int icx = W / 2, icy = 150;
  // scale popK: 0.7 -> 1.0
  float scale = 0.7f + 0.3f * popK;

  // ไอคอนการ์ด/นิ้ว: เราจะเรียกของเดิมแต่เลื่อนพิกัด/สเกลง่าย ๆ
  auto drawCardScaled = [&](int cx, int cy, float s) {
    int w = (int)(96 * s), h = (int)(56 * s), r = (int)(8 * s);
    spr.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, TFT_DARKGREY);
    spr.fillRoundRect(cx - w / 2 + 2, cy - h / 2 + 2, w - 4, h - 4, r, TFT_WHITE);
    spr.fillRect(cx - w / 2 + 2, cy - (int)(h * 0.1f), w - 4, (int)(h * 0.25f), TFT_NAVY);
    spr.fillRect(cx - (int)(w * 0.35f), cy - (int)(h * 0.35f), (int)(w * 0.42f), (int)(h * 0.1f), TFT_LIGHTGREY);
  };
  auto drawFingerScaled = [&](int cx, int cy, float s) {
    int R = (int)(30 * s);
    spr.drawCircle(cx, cy, R, TFT_CYAN);
    spr.drawCircle(cx, cy, (int)(R * 0.8f), TFT_CYAN);
    spr.drawCircle(cx, cy, (int)(R * 0.6f), TFT_CYAN);
    spr.drawCircle(cx, cy, (int)(R * 0.4f), TFT_CYAN);
  };

  if (s == UI_SCAN_CARD) {
    // Add animation phase for card bouncing effect
    float animPhase = (millis() % 2000) / 2000.0f;  // 2-second cycle
    drawNFCIcon(spr, W / 2, 150, 1.0f * scale, animPhase);

    // Draw animated pulsing circles around the card
    int cx = W / 2, cy = 150;
    float pulsePhase = (millis() % 3000) / 3000.0f;  // 3-second cycle
    uint16_t pulseCol = TFT_CYAN;

    // Draw 3 concentric pulsing circles
    for (int i = 0; i < 3; i++) {
      float phase = fmod(pulsePhase + i * 0.33f, 1.0f);
      float pulse = (sinf(phase * 2.0f * PI) + 1.0f) * 0.5f;  // 0..1
      int radius = 80 + int(pulse * 20) + (i * 15);           // 80-100, 95-115, 110-130
      int alpha = int(255 * (1.0f - pulse * 0.7f));           // fade out as it grows

      // Draw circle with fading effect
      for (int r = radius - 2; r <= radius + 2; r++) {
        spr.drawCircle(cx, cy, r, pulseCol);
      }
    }

    // Draw floating particles around the card
    int particleCount = 8;
    for (int i = 0; i < particleCount; i++) {
      float angle = (millis() / 2000.0f + i * 0.785f) * 2.0f * PI;  // 8 particles, rotating
      int distance = 60 + int(sinf(millis() / 1500.0f + i) * 15);   // varying distance
      int px = cx + int(cosf(angle) * distance);
      int py = cy + int(sinf(angle) * distance);

      // Draw small glowing particle
      spr.fillCircle(px, py, 2, TFT_WHITE);
      spr.fillCircle(px, py, 1, pulseCol);
    }
  } else if (s == UI_SCAN_FINGER)
    drawFingerprintIconModern(spr, W / 2, 150, 1.0f * scale);
  else if (s == UI_WAIT_CHOICE) {
    // Animated waiting indicator - pulsing dots
    int cx = W / 2, cy = 150;
    float animPhase = (millis() % 1500) / 1500.0f;  // 1.5-second cycle
    uint16_t dotCol = TFT_CYAN;

    // Draw 3 pulsing dots
    for (int i = 0; i < 3; i++) {
      float phase = fmod(animPhase + i * 0.33f, 1.0f);
      float pulse = (sinf(phase * 2.0f * PI) + 1.0f) * 0.5f;  // 0..1
      int radius = 4 + int(pulse * 6);                        // 4-10 pixels
      int x = cx - 20 + i * 20;
      spr.fillCircle(x, cy, radius, dotCol);
    }
  }

  // Badge OK/Fail
  auto drawCheck = [&](int cx, int cy, float s) {
    int R = (int)(34 * s);
    spr.fillCircle(cx, cy, R, TFT_DARKGREEN);
    spr.fillCircle(cx, cy, (int)(R * 0.88f), TFT_GREEN);
    spr.drawLine(cx - (int)(16 * s), cy, cx - (int)(6 * s), cy + (int)(12 * s), TFT_WHITE);
    spr.drawLine(cx - (int)(6 * s), cy + (int)(12 * s), cx + (int)(16 * s), cy - (int)(14 * s), TFT_WHITE);
    spr.drawLine(cx - (int)(17 * s), cy, cx - (int)(7 * s), cy + (int)(12 * s), TFT_WHITE);
    spr.drawLine(cx - (int)(7 * s), cy + (int)(12 * s), cx + (int)(17 * s), cy - (int)(14 * s), TFT_WHITE);
  };
  auto drawCross = [&](int cx, int cy, float s) {
    int R = (int)(34 * s);
    spr.fillCircle(cx, cy, R, TFT_MAROON);
    spr.fillCircle(cx, cy, (int)(R * 0.88f), TFT_RED);
    for (int i = -1; i <= 1; i++) {
      spr.drawLine(cx - (int)(16 * s), cy - (int)(16 * s) + i, cx + (int)(16 * s), cy + (int)(16 * s) + i, TFT_WHITE);
      spr.drawLine(cx - (int)(16 * s), cy + (int)(16 * s) + i, cx + (int)(16 * s), cy - (int)(16 * s) + i, TFT_WHITE);
    }
  };

  if (s == UI_CARD_OK || s == UI_FINGER_OK || s == UI_THANKS || s == UI_CONFIRM)
    drawCheck(icx, icy, scale);
  if (s == UI_CARD_FAIL || s == UI_FINGER_FAIL || s == UI_ERROR)
    drawCross(icx, icy, scale);
  if (s == UI_CARD_DUPLICATE) {
    // วาดไอคอนบัตรซ้ำ (การ์ด + เครื่องหมายซ้ำ)
    drawCardScaled(icx, icy - 20, scale * 0.8f);
    // วาดเครื่องหมาย = (ซ้ำ)
    int lineW = 20 * scale;
    spr.drawLine(icx - lineW / 2, icy + 15, icx + lineW / 2, icy + 15, TFT_ORANGE);
    spr.drawLine(icx - lineW / 2, icy + 25, icx + lineW / 2, icy + 25, TFT_ORANGE);
  }
  if (s == UI_CARD_NOT_FOUND) {
    // วาดไอคอนไม่พบบัตร (การ์ด + เครื่องหมาย?)
    drawCardScaled(icx, icy - 10, scale * 0.8f);
    // วาดเครื่องหมาย ?
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("?", icx - 8, icy + 20, 4);
  }
  if (s == UI_CARD_ALREADY_VOTED) {
    // วาดไอคอนบัตรใช้งานแล้ว (การ์ด + เครื่องหมายถูก)
    drawCardScaled(icx, icy - 15, scale * 0.8f);
    // วาดเครื่องหมายถูก
    int checkSize = 25 * scale;
    spr.drawLine(icx - checkSize / 2, icy + 10, icx - 5, icy + 20, TFT_GREEN);
    spr.drawLine(icx - 5, icy + 20, icx + checkSize / 2, icy - 5, TFT_GREEN);
    spr.drawLine(icx - checkSize / 2 + 1, icy + 10, icx - 4, icy + 20, TFT_GREEN);
    spr.drawLine(icx - 4, icy + 20, icx + checkSize / 2 + 1, icy - 5, TFT_GREEN);
  }
  if (s == UI_FINGER_LIFT) {
    // วาดไอคอนยกนิ้ว (รูปมือ + ลูกศรขึ้น)
    drawFingerScaled(icx, icy + 10, scale * 0.8f);
    // วาดลูกศรขึ้น
    int arrowH = 30 * scale;
    int arrowW = 15 * scale;
    spr.drawLine(icx, icy - arrowH, icx, icy - 5, TFT_WHITE);                         // เส้นตรง
    spr.drawLine(icx, icy - arrowH, icx - arrowW / 2, icy - arrowH + 10, TFT_WHITE);  // ซ้าย
    spr.drawLine(icx, icy - arrowH, icx + arrowW / 2, icy - arrowH + 10, TFT_WHITE);  // ขวา
  }

  // SD Card icons
  if (s == UI_SD_CHECK || s == UI_SD_RETRY) {
    // วาดไอคอน SD Card พร้อม spinner
    int cx = W / 2, cy = 150;
    int w = 60, h = 40, r = 8;

    // SD Card body
    spr.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, TFT_WHITE);
    spr.fillRoundRect(cx - w / 2 + 2, cy - h / 2 + 2, w - 4, h - 4, r, TFT_ORANGE);

    // SD text
    spr.setTextColor(TFT_WHITE, TFT_ORANGE);
    spr.drawString("SD", cx - 8, cy - 8, 2);

    // Spinner dots around the card
    float animPhase = (millis() % 2000) / 2000.0f;
    for (int i = 0; i < 8; i++) {
      float angle = (i / 8.0f + animPhase) * 2.0f * PI;
      int x = cx + int(cosf(angle) * 50);
      int y = cy + int(sinf(angle) * 50);
      spr.fillCircle(x, y, 2, TFT_WHITE);
    }
  }

  if (s == UI_SD_FAIL) {
    // วาดไอคอน SD Card พร้อม X
    int cx = W / 2, cy = 150;
    int w = 60, h = 40, r = 8;

    // SD Card body
    spr.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, TFT_WHITE);
    spr.fillRoundRect(cx - w / 2 + 2, cy - h / 2 + 2, w - 4, h - 4, r, TFT_RED);

    // SD text
    spr.setTextColor(TFT_WHITE, TFT_RED);
    spr.drawString("SD", cx - 8, cy - 8, 2);

    // X mark
    spr.drawLine(cx - 15, cy - 15, cx + 15, cy + 15, TFT_WHITE);
    spr.drawLine(cx - 15, cy + 15, cx + 15, cy - 15, TFT_WHITE);
  }

  // Register Mode Icons
  if (s == UI_MODE_REGISTER || s == UI_REGISTER_SCAN) {
    int cx = W / 2, cy = 150;

    // วาดไอคอน + (บวก) ใหญ่
    int size = 50;
    spr.drawLine(cx - size / 2, cy, cx + size / 2, cy, TFT_WHITE);  // แนวนอน
    spr.drawLine(cx, cy - size / 2, cx, cy + size / 2, TFT_WHITE);  // แนวตั้ง

    // วาดวงกลมรอบ
    spr.drawCircle(cx, cy, size / 2 + 10, TFT_WHITE);

    // วาดการ์ดเล็กๆ ด้านล่าง
    int cardW = 30, cardH = 20;
    spr.fillRoundRect(cx - cardW / 2, cy + 40, cardW, cardH, 4, TFT_WHITE);
    spr.fillRoundRect(cx - cardW / 2 + 2, cy + 42, cardW - 4, cardH - 4, 4, TFT_DARKGREEN);

    // วาดนิ้วมือเล็กๆ ด้านบน
    int fingerR = 15;
    spr.drawCircle(cx, cy - 40, fingerR, TFT_WHITE);
    spr.drawCircle(cx, cy - 40, fingerR - 3, TFT_WHITE);
    spr.drawCircle(cx, cy - 40, fingerR - 6, TFT_WHITE);
  }

  // Delete Mode Icons
  if (s == UI_MODE_DELETE || s == UI_DELETE_SCAN) {
    int cx = W / 2, cy = 150;

    // วาดไอคอน - (ลบ) ใหญ่
    int size = 50;
    spr.drawLine(cx - size / 2, cy, cx + size / 2, cy, TFT_WHITE);  // แนวนอน

    // วาดวงกลมรอบ
    spr.drawCircle(cx, cy, size / 2 + 10, TFT_WHITE);

    // วาดการ์ดเล็กๆ ด้านล่าง
    int cardW = 30, cardH = 20;
    spr.fillRoundRect(cx - cardW / 2, cy + 40, cardW, cardH, 4, TFT_WHITE);
    spr.fillRoundRect(cx - cardW / 2 + 2, cy + 42, cardW - 4, cardH - 4, 4, TFT_RED);

    // วาด X ด้านบน
    int xSize = 20;
    spr.drawLine(cx - xSize / 2, cy - 40 - xSize / 2, cx + xSize / 2, cy - 40 + xSize / 2, TFT_WHITE);
    spr.drawLine(cx - xSize / 2, cy - 40 + xSize / 2, cx + xSize / 2, cy - 40 - xSize / 2, TFT_WHITE);
  }

  // Password Wait Icons
  if (s == UI_WAIT_PASSWORD) {
    int cx = W / 2, cy = 150;

    // วาดไอคอนกุญแจ + นาฬิกา
    int keyW = 40, keyH = 30;
    spr.fillRoundRect(cx - keyW / 2, cy - keyH / 2, keyW, keyH, 4, TFT_WHITE);
    spr.fillRoundRect(cx - keyW / 2 + 5, cy - keyH / 2 - 8, 8, 12, 2, TFT_WHITE);
    spr.fillRoundRect(cx - keyW / 2 + 10, cy - keyH / 2 + 5, 20, 8, 2, TFT_BLACK);

    // วาดนาฬิกา
    int clockR = 25;
    spr.drawCircle(cx + 50, cy, clockR, TFT_WHITE);
    spr.drawLine(cx + 50, cy, cx + 50 + 15, cy - 10, TFT_WHITE);  // เข็มชั่วโมง
    spr.drawLine(cx + 50, cy, cx + 50 + 20, cy + 5, TFT_WHITE);   // เข็มนาที
  }

  if (s == UI_PASSWORD_OK) {
    int cx = W / 2, cy = 150;

    // วาดไอคอนกุญแจ + เครื่องหมายถูก
    int keyW = 40, keyH = 30;
    spr.fillRoundRect(cx - keyW / 2, cy - keyH / 2, keyW, keyH, 4, TFT_GREEN);
    spr.fillRoundRect(cx - keyW / 2 + 5, cy - keyH / 2 - 8, 8, 12, 2, TFT_GREEN);
    spr.fillRoundRect(cx - keyW / 2 + 10, cy - keyH / 2 + 5, 20, 8, 2, TFT_WHITE);

    // วาดเครื่องหมายถูก
    int checkSize = 30;
    spr.drawLine(cx + 50 - checkSize / 2, cy + 10, cx + 50 - 5, cy + 20, TFT_GREEN);
    spr.drawLine(cx + 50 - 5, cy + 20, cx + 50 + checkSize / 2, cy - 5, TFT_GREEN);
    spr.drawLine(cx + 50 - checkSize / 2 + 1, cy + 10, cx + 50 - 4, cy + 20, TFT_GREEN);
    spr.drawLine(cx + 50 - 4, cy + 20, cx + 50 + checkSize / 2 + 1, cy - 5, TFT_GREEN);
  }

  // Big headline + subtitle
  const char *big =
    (s == UI_BOOT) ? "INITIALIZING" : (s == UI_WAKE)               ? "WAKE"
                                    : (s == UI_READY)              ? "READY"
                                    : (s == UI_SCAN_CARD)          ? "SCAN CARD"
                                    : (s == UI_CARD_OK)            ? "CARD OK"
                                    : (s == UI_CARD_FAIL)          ? "CARD REJECTED"
                                    : (s == UI_CARD_DUPLICATE)     ? "CARD EXISTS"
                                    : (s == UI_CARD_NOT_FOUND)     ? "CARD UNKNOWN"
                                    : (s == UI_CARD_ALREADY_VOTED) ? "ALREADY VOTED"
                                    : (s == UI_SCAN_FINGER)        ? "SCAN FINGER"
                                    : (s == UI_FINGER_OK)          ? "FINGER OK"
                                    : (s == UI_FINGER_FAIL)        ? "FINGER FAIL"
                                    : (s == UI_FINGER_LIFT)        ? "LIFT FINGER"
                                    : (s == UI_CONFIRM)            ? "CONFIRM"
                                    : (s == UI_THANKS)             ? "THANK YOU"
                                    : (s == UI_ERROR)              ? "ERROR"
                                    : (s == UI_SLEEP)              ? "SLEEP"
                                    : (s == UI_WAIT_CHOICE)        ? "WAIT"
                                    : (s == UI_SELECTED)           ? "SELECTED"
                                    : (s == UI_SENDING)            ? "SENDING"
                                    : (s == UI_SD_CHECK)           ? "SD CHECK"
                                    : (s == UI_SD_FAIL)            ? "SD FAIL"
                                    : (s == UI_SD_RETRY)           ? "SD RETRY"
                                    : (s == UI_MODE_REGISTER)      ? "REGISTER MODE"
                                    : (s == UI_MODE_DELETE)        ? "DELETE MODE"
                                    : (s == UI_REGISTER_SCAN)      ? "REGISTER CARD"
                                    : (s == UI_DELETE_SCAN)        ? "DELETE CARD"
                                    : (s == UI_WAIT_PASSWORD)      ? "WAIT PASSWORD"
                                    : (s == UI_PASSWORD_OK)        ? "PASSWORD OK"
                                                                   : "";

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(big, (W - spr.textWidth(big, 4)) / 2, 200, 4);
  if (subtitle && subtitle[0]) {
    // Pulse subtitle color on scan card to show liveness
    if (s == UI_SCAN_CARD) {
      float p = (sinf((millis() % 1600) / 1600.0f * 2.0f * PI) + 1.0f) * 0.5f;  // 0..1
      uint16_t col = (p > 0.5f) ? TFT_YELLOW : TFT_WHITE;
      spr.setTextColor(col, TFT_BLACK);
    } else {
      spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    }
    spr.drawString(subtitle, (W - spr.textWidth(subtitle, 2)) / 2, 230, 2);
  }
}

// --------- Main painter ----------

void showUIx(UIState s, const char *subtitle = nullptr, UITrans tr = TR_SLIDE_L) {
  if (isShowingPhoto)
    return;

  const String sub = subtitle ? String(subtitle) : String();
  const uint32_t now = millis();
  if (s == g_lastState && sub == g_lastSubtitle) {
    if (SAME_STATE_COOLDOWN_MS == 0 || (now - g_lastPaintMs) < SAME_STATE_COOLDOWN_MS)
      return;
  }
  g_lastState = s;
  g_lastSubtitle = sub;
  g_lastPaintMs = now;

  Serial.printf("[UI] showUIx: state=%d, subtitle='%s'\n", (int)s, subtitle ? subtitle : "");

  // ปล่อยบัสอื่นก่อนทำงานกับ TFT
  spi_deselect_all();
  delay(10);

  const int W = tft.width(), H = tft.height();

  // ตรวจสอบว่า sprite ใช้งานได้หรือไม่
  if (!spr.created()) {
    Serial.println("[UI] Sprite not created, using fallback mode");
    showUIxFallback(s, subtitle);
    return;
  }

  spr.setTextDatum(TL_DATUM);

  // POP animation
  const int POP_FR = 8;
  for (int i = 0; i < POP_FR; ++i) {
    float k = easeInOutQuad((float)(i + 1) / POP_FR);
    spr.fillSprite(TFT_BLACK);
    paintScreenToSprite(s, subtitle, true, k);
    tft.endWrite();
    spr.pushSprite(0, 0);
    delay(12);
  }

  // เฟรมสุดท้าย + slide
  spr.fillSprite(TFT_BLACK);
  paintScreenToSprite(s, subtitle, false, 1.0f);

  if (tr == TR_NONE) {
    tft.endWrite();
    spr.pushSprite(0, 0);
  } else {
    const int FR = 12;
    for (int i = 0; i < FR; ++i) {
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
      tft.endWrite();
      spr.pushSprite(x, y);
      delay(14);
    }
  }

  uiSetScanning(s == UI_SCAN_CARD || s == UI_SCAN_FINGER || s == UI_FINGER_LIFT || s == UI_SENDING || s == UI_WAIT_CHOICE);
}

// Fallback UI function สำหรับกรณีที่ sprite ไม่ทำงาน
void showUIxFallback(UIState s, const char *subtitle = nullptr) {
  Serial.println("[UI] Using fallback mode (direct TFT)");

  spi_deselect_all();
  delay(10);
  spi_select_tft();

  // เคลียร์จอ
  tft.fillScreen(TFT_BLACK);

  // กำหนดสีตาม state
  uint16_t headerColor = TFT_WHITE;
  uint16_t bgColor = TFT_BLACK;

  switch (s) {
    case UI_CARD_OK:
    case UI_FINGER_OK:
    case UI_THANKS:
      headerColor = TFT_GREEN;
      break;
    case UI_CARD_FAIL:
    case UI_FINGER_FAIL:
    case UI_ERROR:
    case UI_CARD_NOT_FOUND:
      headerColor = TFT_RED;
      break;
    case UI_CARD_DUPLICATE:
    case UI_CARD_ALREADY_VOTED:
    case UI_SD_CHECK:
    case UI_SD_RETRY:
      headerColor = TFT_ORANGE;
      break;
    case UI_MODE_REGISTER:
    case UI_REGISTER_SCAN:
      headerColor = TFT_GREEN;
      break;
    case UI_MODE_DELETE:
    case UI_DELETE_SCAN:
      headerColor = TFT_RED;
      break;
    default:
      headerColor = TFT_CYAN;
      break;
  }

  // วาดกรอบ
  tft.drawRect(0, 0, tft.width(), tft.height(), headerColor);
  tft.drawRect(1, 1, tft.width() - 2, tft.height() - 2, headerColor);

  // Header text
  const char *hdr =
    (s == UI_READY)                ? "พร้อมให้บริการ"
    : (s == UI_SCAN_CARD)          ? "โปรดแตะบัตร"
    : (s == UI_CARD_OK)            ? "บัตรถูกต้อง"
    : (s == UI_CARD_FAIL)          ? "บัตรไม่ถูกต้อง"
    : (s == UI_CARD_DUPLICATE)     ? "บัตรลงทะเบียนแล้ว"
    : (s == UI_CARD_NOT_FOUND)     ? "บัตรไม่อยู่ในระบบ"
    : (s == UI_CARD_ALREADY_VOTED) ? "บัตรใช้งานแล้ว"
    : (s == UI_SCAN_FINGER)        ? "โปรดสแกนลายนิ้วมือ"
    : (s == UI_FINGER_OK)          ? "ยืนยันตัวตนสำเร็จ"
    : (s == UI_FINGER_FAIL)        ? "ยืนยันตัวตนไม่ผ่าน"
    : (s == UI_FINGER_LIFT)        ? "โปรดยกนิ้วขึ้น"
    : (s == UI_SD_CHECK)           ? "กำลังตรวจสอบ SD Card"
    : (s == UI_SD_FAIL)            ? "SD Card ไม่ทำงาน"
    : (s == UI_SD_RETRY)           ? "กำลังลอง SD Card ใหม่"
    : (s == UI_MODE_REGISTER)      ? "โหมดลงทะเบียน"
    : (s == UI_MODE_DELETE)        ? "โหมดลบข้อมูล"
    : (s == UI_BOOT)               ? "ระบบกำลังเริ่มทำงาน"
                                   : "ระบบทำงาน";

  // วาด header
  tft.fillRect(5, 5, tft.width() - 10, 30, headerColor);
  tft.setTextColor(TFT_BLACK, headerColor);
  tft.drawString(hdr, 10, 10, 2);

  // วาด main text
  const char *mainText =
    (s == UI_READY)                ? "READY"
    : (s == UI_SCAN_CARD)          ? "SCAN CARD"
    : (s == UI_CARD_OK)            ? "CARD OK"
    : (s == UI_CARD_FAIL)          ? "CARD FAIL"
    : (s == UI_CARD_DUPLICATE)     ? "CARD EXISTS"
    : (s == UI_CARD_NOT_FOUND)     ? "CARD UNKNOWN"
    : (s == UI_CARD_ALREADY_VOTED) ? "ALREADY VOTED"
    : (s == UI_SCAN_FINGER)        ? "SCAN FINGER"
    : (s == UI_FINGER_OK)          ? "FINGER OK"
    : (s == UI_FINGER_FAIL)        ? "FINGER FAIL"
    : (s == UI_FINGER_LIFT)        ? "LIFT FINGER"
    : (s == UI_SD_CHECK)           ? "SD CHECK"
    : (s == UI_SD_FAIL)            ? "SD FAIL"
    : (s == UI_SD_RETRY)           ? "SD RETRY"
    : (s == UI_MODE_REGISTER)      ? "REGISTER"
    : (s == UI_MODE_DELETE)        ? "DELETE"
    : (s == UI_BOOT)               ? "BOOT"
                                   : "SYSTEM";

  tft.setTextColor(headerColor, TFT_BLACK);
  int textX = (tft.width() - tft.textWidth(mainText, 4)) / 2;
  tft.drawString(mainText, textX, 80, 4);

  // วาด subtitle
  if (subtitle && subtitle[0]) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    int subX = (tft.width() - tft.textWidth(subtitle, 2)) / 2;
    tft.drawString(subtitle, subX, 120, 2);
  }

  // วาดสถานะที่ด้านล่าง
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Fallback Mode", 10, tft.height() - 25, 1);

  spi_deselect_all();
  Serial.println("[UI] Fallback UI completed");
}

// ใช้ GPIO35 เป็นขาปลุก (ต่อมาจาก ODROID PIN_33 ผ่าน R อนุกรม ~1k)
// *GPIO35 เป็นขา RTC input ได้ ปลุกด้วย ext1 ได้

// ==== forward declarations to satisfy compile order (ADD ONLY) ====
struct Rec;  // ให้คอมไพเลอร์รู้จักชื่อ Rec ล่วงหน้า (ใช้กับ & ได้)
// extern const int UID_HEX_MAX;  // บอกว่าจะมีค่าคงที่ชื่อนี้ประกาศจริงด้านล่าง

// ===== [ADD] Ultrasonic (HC-SR04) for auto-sleep =====
const int TRIG_PIN = 4;
const int ECHO_PIN = 34;  // ต้องลดเป็น 3.3V ก่อนเข้า ESP32

// เกณฑ์ "ใกล้"
volatile float NEAR_ON_CM = 25.0;   // เข้าสถานะ NEAR เมื่อ <= 25 cm
volatile float NEAR_OFF_CM = 35.0;  // กลับ FAR เมื่อ >= 35 cm (ฮิสเทอรีส)

// รอบวัดและ timeout
const uint16_t US_INTERVAL_MS = 100;     // วัดทุก ~100ms (เร็วขึ้น)
const unsigned long US_TIMEOUT = 25000;  // pulseIn timeout ~25ms

// ===== [ADD] counters & confirm windows for noise filtering =====
static uint8_t nearConsec = 0;
static uint8_t farConsec = 0;
static const uint8_t NEAR_CONFIRM_N = 2;  // ต้องเห็น NEAR 2 เฟรมติดถึงจะเปลี่ยนเป็น NEAR
static const uint8_t FAR_CONFIRM_N = 2;   // ต้องเห็น FAR  2 เฟรมติดถึงจะเปลี่ยนเป็น FAR

// จับเวลาเพื่อหลับ
const uint32_t NO_NEAR_SLEEP_MS = 30000;  // FAR ต่อเนื่อง 30 วินาที -> หลับ

// ตัวแปรสถานะ
static bool nearState = false;
static uint32_t lastUSms = 0;
static uint32_t lastNearSeenMs = 0;

// [ADD] Debug logging for Ultrasonic
// DEBUG_ULTRA is defined below with other debug flags
static uint32_t lastUltraLogMs = 0;

// (ออปชัน) ถ้าจะให้หลับเองเมื่อไม่มีเหตุการณ์นาน X ms ให้เปิดใช้ 2 บรรทัดนี้ได้ภายหลัง
// #define IDLE_SLEEP_MS 60000UL   // FAR/ไม่มีงาน 60s → หลับ
// static uint32_t lastActiveMs = 0;
// inline void noteActivity(){ lastActiveMs = millis(); }

#include "esp_system.h"
#include "driver/rtc_io.h"

void printBootAndWakeInfo() {
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("Reset reason=%d (1=POWERON, 12=BROWNOUT, 5=DEEPSLEEP)\n", (int)rr);

  esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
  Serial.print("Wake cause=");
  switch (wc) {
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

  if (wc == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t m = esp_sleep_get_ext1_wakeup_status();
    Serial.printf("EXT1 mask=0x%016llX\n", (unsigned long long)m);
    if (m) {
      Serial.print("Pins HIGH: ");
      bool first = true;
      for (int g = 0; g <= 39; ++g)
        if (m & (1ULL << g)) {
          Serial.print(first ? "" : " ,");
          Serial.print(g);
          first = false;
        }
      Serial.println();
    }
  }
}

// เข้าหลับทันที แล้วปลุกเมื่อ WAKE_PIN=HIGH จาก ODROID
void goDeepSleepNow() {
  detachInterrupt(digitalPinToInterrupt(WAKE_PIN));

  // ตั้งเป็น RTC input เฉพาะตอนหลับ
  rtc_gpio_deinit((gpio_num_t)WAKE_PIN);
  rtc_gpio_init((gpio_num_t)WAKE_PIN);
  rtc_gpio_set_direction((gpio_num_t)WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);

  // เลือกอย่างใดอย่างหนึ่งตามข้อ 2:
  // --- ทาง A: ปลุกเมื่อ HIGH ---
  rtc_gpio_pulldown_en((gpio_num_t)WAKE_PIN);
  rtc_gpio_pullup_dis((gpio_num_t)WAKE_PIN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext1_wakeup(1ULL << WAKE_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);

  // --- ทาง B: ปลุกเมื่อ LOW ---
  // rtc_gpio_pullup_en((gpio_num_t)WAKE_PIN);
  // rtc_gpio_pulldown_dis((gpio_num_t)WAKE_PIN);
  // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // esp_sleep_enable_ext1_wakeup(1ULL << WAKE_PIN, ESP_EXT1_WAKEUP_ALL_LOW);

  // กันเด้ง: ถ้าตอนนี้อยู่ในระดับที่จะปลุก ให้ "ไม่หลับ"
  if (rtc_gpio_get_level((gpio_num_t)WAKE_PIN) == 1 /* ถ้าใช้ ANY_HIGH ให้เท่ากับ 1; ถ้าใช้ ALL_LOW ให้ 0 */) {
    Serial.println("[SLEEP] Wake pin already at trigger level -> skip sleep");
    return;
  }

  // แสดง UI sleep ก่อน
  Serial.println("[SLEEP] Showing sleep UI...");
  showUIx(UI_SLEEP, "กำลังพักการทำงาน", TR_NONE);
  delay(1500);  // แสดง UI สัก 1.5 วินาที

  // ปิดหน้าจอให้ดำสนิท
  Serial.println("[SLEEP] Turning off display...");
  spi_deselect_all();
  delay(100);
  spi_select_tft();
  tft.fillScreen(TFT_BLACK);  // หน้าจอดำสนิท
  tft.setRotation(0);
  tft.setTextColor(TFT_BLACK, TFT_BLACK);  // ข้อความดำบนพื้นดำ
  delay(200);
  spi_deselect_all();

  Serial.println("[SLEEP] Display turned off, entering deep sleep...");
  delay(100);
  esp_deep_sleep_start();
}

// ---------- Serial / UART ----------
HardwareSerial mySerial(2);      // UART2 : ใช้คุยกับบอร์ด/จออีกตัว ตามที่คุณใช้อยู่ (TX=17, RX=16 ด้านล่าง)
HardwareSerial FingerSerial(1);  // UART1 : ใช้คุยกับโมดูลลายนิ้วมือ
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerSerial);

// ฟังก์ชันรวมสำหรับกลับโหมดปกติ (ส่งเสียงและแสดง UI)
void returnToNormalMode(const char *message = "พร้อมให้บริการ", bool playSound = false) {
  showUIx(UI_SCAN_CARD, "ยื่นบัตรใกล้เครื่องอ่าน", TR_NONE);
}

// ---------- RFID ----------
#define RST_PIN 27
MFRC522 rfid(SS_PIN, RST_PIN);
// [ADD] ปล่อยทุก CS ให้ HIGH (กันชน)
// --- ปล่อยทุก CS ให้อยู่ HIGH เสมอ ---
inline void spi_idle_all() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);  // RFID_CS
}

// --- ก่อนเรียกฟังก์ชันของ RC522: ปล่อยบัสจากตัวอื่น ไม่เปิด transaction ซ้อน ---
inline void rfid_bus_begin() {
  // ปล่อยอุปกรณ์อื่นแน่นอน
  tft.endWrite();
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  // CS ของ RC522 ปล่อย HIGH ไว้ ให้ไลบรารีจัดการเอง
  digitalWrite(SS_PIN, HIGH);
  // (ออปชัน) หน่วงสั้นๆ ให้บัสนิ่ง
  delayMicroseconds(50);
}

// --- หลังจบงานกับ RC522 ---
inline void rfid_bus_end() {
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
}

// ---------- I/O ----------
// EEPROM_SIZE moved to conditional section below

// const int switchPin33 = 99; // สวิตช์ Register
// const int switchPin32 =99; // สวิตช์ Delete
// const int ledPin = 13;

// ---------- Finger UART Pins (ปรับให้ตรงบอร์ดคุณ) ----------
const int FINGER_RX = 26;  // ESP32 RX1 pin to sensor TX
const int FINGER_TX = 25;  // ESP32 TX1 pin to sensor RX

// ===== EEPROM Configuration =====
#include <EEPROM.h>

// Debug controls
#define DEBUG_RFID_DETAIL 0
#define DEBUG_KEYPAD_DETAIL 0
#define DEBUG_ULTRA 1  // เปิด ultrasonic debug logging

// ESP32 Internal EEPROM Configuration
const int EEPROM_SIZE = 512;
const int MAX_RECORDS = (EEPROM_SIZE - 16) / 20;  // ~= 24 records

// ---------- Durable Storage Layout (ESP32 Internal EEPROM) ----------
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
const uint32_t MAGIC = 0x564F5445UL;  // 'VOTE'
const uint8_t VERSION = 1;
const int HDR_SIZE = 16;
const int RECORD_SIZE = 20;
const int BASE = HDR_SIZE;
const uint8_t VALID_FLAG = 0xA5;
const uint8_t EMPTY_FLAG = 0xFF;
// Record storage configuration

// ---------- Utils ----------
struct Rec {
  char uid[UID_HEX_MAX];  // ไม่รับ '\0' เสมอ ให้เก็บเป็น 16 ชาร์ (ถ้าน้อยกว่าก็ 0x00 padding)
  uint8_t fp_id;
  uint8_t voted;  // 0/1
  uint8_t valid;  // VALID_FLAG หรือ EMPTY_FLAG
  uint8_t reserved;
};

// ===== EEPROM Functions =====
// ESP32 Internal EEPROM Functions Only
void eepromWriteBytes(int addr, const uint8_t *data, int len) {
  for (int i = 0; i < len; ++i)
    EEPROM.write(addr + i, data[i]);
}

void eepromReadBytes(int addr, uint8_t *data, int len) {
  for (int i = 0; i < len; ++i)
    data[i] = EEPROM.read(addr + i);
}

void writeHeader() {
  uint8_t hdr[HDR_SIZE] = { 0 };
  hdr[0] = 'V';
  hdr[1] = 'O';
  hdr[2] = 'T';
  hdr[3] = 'E';
  hdr[4] = VERSION;
  // rest zero
  eepromWriteBytes(0, hdr, HDR_SIZE);
  EEPROM.commit();
  Serial.println("Header written");
}

bool headerOK() {
  uint8_t h[5];
  eepromReadBytes(0, h, 5);
  return (h[0] == 'V' && h[1] == 'O' && h[2] == 'T' && h[3] == 'E' && h[4] == VERSION);
}

int recAddr(int idx) {
  return BASE + idx * RECORD_SIZE;
}

void readRec(int idx, Rec &r) {
  uint8_t buf[RECORD_SIZE];
  eepromReadBytes(recAddr(idx), buf, RECORD_SIZE);
  for (int i = 0; i < UID_HEX_MAX; ++i)
    r.uid[i] = (char)buf[i];
  r.fp_id = buf[16];
  r.voted = buf[17];
  r.valid = buf[18];
  r.reserved = buf[19];
}

void writeRec(int idx, const Rec &r) {
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

void clearRec(int idx) {
  Rec r{};
  for (int i = 0; i < UID_HEX_MAX; ++i)
    r.uid[i] = 0x00;
  r.fp_id = 0;
  r.voted = 0;
  r.valid = EMPTY_FLAG;
  r.reserved = 0;
  writeRec(idx, r);
}

int findFreeSlot() {
  for (int i = 0; i < MAX_RECORDS; ++i) {
    Rec r;
    readRec(i, r);
    if (r.valid != VALID_FLAG)
      return i;
  }
  return -1;
}

bool sameUID16(const char a[UID_HEX_MAX], const char b[UID_HEX_MAX]) {
  for (int i = 0; i < UID_HEX_MAX; ++i)
    if (a[i] != b[i])
      return false;
  return true;
}

void uidToFixed16(const String &uidHex, char out16[UID_HEX_MAX]) {
  // ตัด/แพดให้ยาว 16 ตัวอักษร
  // (UID 4 ไบต์ => 8 ตัวอักษร, UID 7/10 ไบต์ => 14/20 ตัวอักษร → เก็บ 16 ตัวอักษรแรกพอ)
  for (int i = 0; i < UID_HEX_MAX; i++) {
    out16[i] = (i < uidHex.length()) ? uidHex.charAt(i) : 0x00;
  }
}

int findByUID(const String &uidHex) {
  char key[UID_HEX_MAX];
  uidToFixed16(uidHex, key);

  if (DEBUG_RFID_DETAIL) {
    Serial.printf("[DEBUG] findByUID: searching for UID='%s'\n", uidHex.c_str());
    Serial.printf("[DEBUG] findByUID: key array: ");
    for (int j = 0; j < UID_HEX_MAX; j++) {
      Serial.printf("%02X ", (uint8_t)key[j]);
    }
    Serial.println();
  }

  for (int i = 0; i < MAX_RECORDS; ++i) {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG) {
      if (DEBUG_RFID_DETAIL) {
        Serial.printf("[DEBUG] Record[%d]: UID=", i);
        for (int j = 0; j < UID_HEX_MAX; j++) {
          Serial.printf("%02X ", (uint8_t)r.uid[j]);
        }
        Serial.printf("(valid=0x%02X)\n", r.valid);
      }

      if (sameUID16(r.uid, key)) {
        if (DEBUG_RFID_DETAIL) {
          Serial.printf("[DEBUG] Found match at index %d\n", i);
        }
        return i;
      }
    }
  }
  if (DEBUG_RFID_DETAIL) {
    Serial.println("[DEBUG] No match found");
  }
  return -1;
}

int findByFPID(uint8_t fp) {
  for (int i = 0; i < MAX_RECORDS; ++i) {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && r.fp_id == fp)
      return i;
  }
  return -1;
}

// สแกนนิ้วแบบเร็วเพื่อเช็กว่ามีนิ้วนี้อยู่ในฐานแล้วหรือไม่
int quickSearchFingerprint(uint32_t timeout_ms = 10000) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeout_ms) {
    // ตรวจสอบว่ายังอยู่ในโหมดลงทะเบียนหรือไม่
    if (!inRegisterMode) {
      Serial.println("[QUICK_SEARCH] Exiting register mode - stopping fingerprint search");
      return -1;  // exit early
    }

    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      delay(50);
      continue;
    }
    if (p != FINGERPRINT_OK) {
      delay(50);
      continue;
    }
    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) {
      delay(50);
      continue;
    }
    p = finger.fingerFastSearch();  // ค้นหาในฐานของเซ็นเซอร์
    if (p == FINGERPRINT_OK)
      return finger.fingerID;  // พบแล้ว → คืน fp_id เดิม
    else
      return -1;  // ไม่พบ → นิ้วใหม่น่าจะยังไม่อยู่ในฐาน
  }
  return -1;  // timeout
}

bool setVotedByIndex(int idx, uint8_t v) {
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
bool fingerBegin() {
  // เริ่มพอร์ตกับโมดูลลายนิ้วมือ
  FingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  finger.begin(57600);
  delay(200);
  return finger.verifyPassword();
}

int enrollFingerprint(uint8_t fp_id) {
  // ขั้นตอนย่อสไตล์ Adafruit: ขอภาพสองครั้ง, สร้างโมเดล, เก็บไว้ตำแหน่ง fp_id
  // คืน 0 = ok, อื่นๆ = code ผิดพลาด
  int p = -1;
  Serial.printf("Enroll FP id=%d : place finger\n", fp_id);

  // ภาพ 1
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    // ตรวจสอบว่ายังอยู่ในโหมดลงทะเบียนหรือไม่
    if (!inRegisterMode) {
      Serial.println("[ENROLL] Exiting register mode - stopping fingerprint enroll");
      return FINGERPRINT_ENROLLMISMATCH;  // return error code
    }

    if (p == FINGERPRINT_NOFINGER) {
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
  // for (int ii = 0; ii < 5; ii++)
  mySerial.println("LLLLLLLLLLLLLLLLLLLLLLL");
  showUIx(UI_FINGER_LIFT, "โปรดยกนิ้วขึ้น", TR_NONE);
  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    // ตรวจสอบว่ายังอยู่ในโหมดลงทะเบียนหรือไม่
    if (!inRegisterMode) {
      Serial.println("[ENROLL] Exiting register mode - stopping finger lift wait");
      return FINGERPRINT_ENROLLMISMATCH;  // return error code
    }
    delay(50);
  }

  Serial.println("Place same finger again");
  // for (int ii = 0; ii < 5; ii++)
  mySerial.println("PPPPPPPPPPPPPPPPPPPPPP");
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    // ตรวจสอบว่ายังอยู่ในโหมดลงทะเบียนหรือไม่
    if (!inRegisterMode) {
      Serial.println("[ENROLL] Exiting register mode - stopping second image capture");
      return FINGERPRINT_ENROLLMISMATCH;  // return error code
    }

    if (p == FINGERPRINT_NOFINGER) {
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
  return p;  // FINGERPRINT_OK = 0x00
}

int matchFingerprint() {
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
  return finger.fingerID;  // ตำแหน่งที่ match
}

// ---------- App Logic ----------
String readRFIDasHex() {
  // คืนเป็นตัวอักษร hex (ไม่เว้นวรรค), ตัวพิมพ์ใหญ่, ยาวเท่าจำนวน uid.size*2 (สูงสุด ~20 chars)
  String ID = "";

  if (DEBUG_RFID_DETAIL) {
    Serial.printf("[DEBUG] RFID UID size: %d bytes\n", rfid.uid.size);
    Serial.printf("[DEBUG] RFID UID raw: ");
  }

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (DEBUG_RFID_DETAIL) {
      Serial.printf("%02X ", rfid.uid.uidByte[i]);
    }
    if (rfid.uid.uidByte[i] < 0x10)
      ID += "0";
    ID += String(rfid.uid.uidByte[i], HEX);
  }

  if (DEBUG_RFID_DETAIL) {
    Serial.println();
  }

  ID.toUpperCase();
  ID.replace(" ", "");

  if (DEBUG_RFID_DETAIL) {
    Serial.printf("[DEBUG] RFID UID hex string: '%s'\n", ID.c_str());
  }
  return ID;
}

bool storeNewRecord(const String &uidHex, uint8_t fp_id) {
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

bool removeByUID(const String &uidHex) {
  int idx = findByUID(uidHex);
  if (idx < 0)
    return false;
  Rec r;
  readRec(idx, r);
  // ลบในโมดูลลายนิ้วมือด้วย
  if (r.fp_id > 0) {
    finger.deleteModel(r.fp_id);
  }
  clearRec(idx);
  return true;
}

// ---- Compatibility wrappers (ให้โค้ดที่ยังเรียกชื่อเก่า compile ได้) ----
inline void bus_acquire_for_rfid(uint32_t /*hz*/ = 4000000) {
  // เราใช้การคุม CS + endWrite() อยู่แล้ว ความเร็วจัดโดยไลบรารี/ESP32 SPI
  rfid_bus_begin();
}
inline void bus_release_after_rfid() {
  rfid_bus_end();
}

// ---- RC522 hard reset ผ่านขา RST_PIN (27) ----
inline void rc522_hard_reset() {
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(5);  // หน่วงสั้น ๆ ให้รีเซ็ตจริง
  digitalWrite(RST_PIN, HIGH);
  delay(5);
}

// ตรวจสอบสถานะ RFID โดยอ่าน version register
bool checkRFIDHealth() {
  bus_acquire_for_rfid();
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  bus_release_after_rfid();
  
  Serial.printf("[RFID] Version register: 0x%02X\n", version);
  
  // RC522 มี version register เป็น 0x91 หรือ 0x92
  return (version == 0x91 || version == 0x92);
}

// ฟังก์ชันส่งเสียงยืนยันตัวตนสำเร็จ - สำคัญมาก!
void sendAuthenticationSuccessSound() {
  Serial.println("[AUDIO] Sending CRITICAL authentication success sound (O)...");
  
  // ส่งซ้ำ 2 ครั้งเพื่อให้แน่ใจ
  for (int i = 0; i < 2; i++) {
    mySerial.println("OOOOOOOOOOOOOOOOOOOOOO");
    mySerial.flush();  // บังคับส่งข้อมูลออกไปทันที
    delay(30);  // หน่วงสั้นๆ ระหว่างการส่ง
  }
  
  Serial.println("[AUDIO] Authentication success sound sent 3 times - CRITICAL");
}

// วางเหนือ registerCardAndFingerprint() / deleteCardFlow()
inline void exitPhotoMode() {
  isShowingPhoto = false;
  uiSetScanning(true);
}

// ===== ESP32 tone() shim (no sound; compile-safe) =====
inline void tone(int /*pin*/, unsigned int /*freq*/, unsigned long /*duration*/ = 0) {}
inline void noTone(int /*pin*/) {}

// ---------- High-level flows ----------
void registerCardAndFingerprint() {
  // ตรวจสอบว่าได้ยืนยัน password แล้วหรือยัง
  if (waitingForPassword) {
    Serial.println("[REGISTER] Still waiting for password confirmation");
    return;
  }

  exitPhotoMode();
  Serial.println("Registration mode... Tap a new card");

  // เพิ่ม: Reset และ re-init RFID เพื่อให้แน่ใจว่าทำงาน
  Serial.println("[REGISTER] Resetting RFID...");
  rc522_hard_reset();
  delay(100);

  Serial.println("[REGISTER] Reinitializing RFID...");
  bus_acquire_for_rfid();
  rfid.PCD_Init();
  bus_release_after_rfid();
  delay(50);
  Serial.println("[REGISTER] RFID reinitialized");

  // UI: เริ่มโหมดลงทะเบียน → รอแตะบัตร
  showUIx(UI_REGISTER_SCAN, "แตะบัตรเพื่อเริ่มลงทะเบียน", TR_NONE);

  // --- รอการ์ดแบบล็อคบัสทุกครั้ง ---
  Serial.println("[REGISTER] Starting card detection loop");
  int noCardCount = 0;  // นับจำนวนครั้งที่ไม่พบการ์ด
  
  while (true) {
    // ตรวจสอบว่ายังอยู่ในโหมดลงทะเบียนหรือไม่
    if (!inRegisterMode) {
      Serial.println("[REGISTER] Exiting register mode - stopping card wait");
      return;
    }

    bool ok = false;
    bus_acquire_for_rfid();
    Serial.println("[REGISTER] Bus acquired, checking for card...");
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.println("[REGISTER] Card detected!");
      ok = true;
      noCardCount = 0;  // รีเซ็ตตัวนับ
    } else {
      Serial.println("[REGISTER] No card detected");
      noCardCount++;
      
      // ถ้าไม่พบการ์ด 40 ครั้งติดต่อกัน (2 วินาที) ให้ลอง reset RFID
      if (noCardCount >= 40) {
        Serial.println("[REGISTER] Too many failed attempts - checking RFID health...");
        bus_release_after_rfid();  // ปล่อย bus ก่อน reset
        
        // ตรวจสอบสถานะ RFID
        if (!checkRFIDHealth()) {
          Serial.println("[REGISTER] RFID health check failed - performing hard reset...");
          
          // Hard reset RFID
          rc522_hard_reset();
          delay(100);
          
          // Re-init RFID
          bus_acquire_for_rfid();
          rfid.PCD_Init();
          bus_release_after_rfid();
          delay(50);
          
          // ตรวจสอบอีกครั้งหลัง reset
          if (checkRFIDHealth()) {
            Serial.println("[REGISTER] RFID recovery successful");
          } else {
            Serial.println("[REGISTER] RFID recovery failed - may need hardware check");
          }
        } else {
          Serial.println("[REGISTER] RFID health OK - continuing...");
        }
        
        noCardCount = 0;  // รีเซ็ตตัวนับ
        continue;  // ข้ามการ release bus เพราะเราทำไปแล้ว
      }
    }
    bus_release_after_rfid();
    
    if (ok) {
      // ส่งสัญญาณให้ Arduino เล่นเสียง "กำลังอ่านบัตร"
      mySerial.println("SSSSSSSSSSSSSSSSSSSSSSSSSSSS");
      break;
    }
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

  // โชว์ "บัตรถูกต้อง" สั้นๆ ก่อนดำเนินการต่อ
  showUIx(UI_CARD_OK, "บัตรถูกต้อง", TR_NONE);
  showUIx(UI_SENDING, "เตรียมลงทะเบียนนิ้ว...", TR_NONE);
  delay(300);
  uiSetLoading(true);
  delay(500);
  uiSetLoading(false);
  showUIx(UI_SCAN_FINGER, "วางนิ้ว 2 ครั้งเพื่อลงทะเบียน", TR_NONE);

  // ตรวจสอบว่ายังอยู่ในโหมดลงทะเบียนหรือไม่
  if (!inRegisterMode) {
    Serial.println("[REGISTER] Exiting register mode - stopping after card read");
    return;
  }

  // --- การ์ดซ้ำ? ---
  if (findByUID(uidHex) >= 0) {
    Serial.println("This card is already registered.");
    
    // ส่งสัญญาณให้ Arduino เล่นเสียง "ลายนิ้วมือผิด/error"
    mySerial.println("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    
    showUIx(UI_CARD_DUPLICATE, "บัตรนี้ลงทะเบียนแล้ว", TR_NONE);

    delay(900);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // --- ตรวจนิ้วซ้ำก่อน Enroll ---
  Serial.println("Place finger to check duplication...");
  // for (int ii = 0; ii < 5; ii++)
  mySerial.println("JJJJJJJJJJJJJJJJJJJJJJJJJJ");
  showUIx(UI_SCAN_FINGER, "ตรวจสอบลายนิ้วมือเดิม", TR_NONE);
  int existing_fp = quickSearchFingerprint(10000);
  if (existing_fp >= 0) {
    int idxExisting = findByFPID(existing_fp);
    if (idxExisting >= 0) {
      Rec rExist;
      readRec(idxExisting, rExist);
      Serial.printf("Duplicate finger detected! Already linked to another card (FP_ID=%d). Abort.\n", existing_fp);
      
      // ส่งสัญญาณให้ Arduino เล่นเสียง "ลายนิ้วมือผิด"
      mySerial.println("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
      
      showUIx(UI_FINGER_FAIL, "ลายนิ้วมือนี้เชื่อมบัตรอื่นอยู่", TR_NONE);

      delay(1000);
      showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
      return;
    } else {
      Serial.printf("Found stale FP template (id=%d) without EEPROM record. Deleting stale template.\n", existing_fp);
      finger.deleteModel(existing_fp);
      showUIx(UI_ERROR, "ล้างข้อมูลลายนิ้วมือที่ค้าง", TR_NONE);
      delay(400);
    }
  }

  // --- หา fp_id ว่าง 1..199 ---
  uint8_t chosen_fp_id = 1;
  bool used[200];
  for (int i = 0; i < 200; i++)
    used[i] = false;
  for (int i = 0; i < MAX_RECORDS; i++) {
    Rec r;
    readRec(i, r);
    if (r.valid == VALID_FLAG && r.fp_id > 0 && r.fp_id < 200)
      used[r.fp_id] = true;
  }
  while (chosen_fp_id < 200 && used[chosen_fp_id])
    chosen_fp_id++;
  if (chosen_fp_id >= 200) {
    Serial.println("No free FP ID slot.");
    
    // ส่งสัญญาณให้ Arduino เล่นเสียง "error"
    mySerial.println("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    
    showUIx(UI_ERROR, "ที่เก็บลายนิ้วมือเต็ม", TR_NONE);

    delay(1000);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // ตรวจสอบว่ายังอยู่ในโหมดลงทะเบียนหรือไม่
  if (!inRegisterMode) {
    Serial.println("[REGISTER] Exiting register mode - stopping before fingerprint enroll");
    return;
  }

  // --- Enroll นิ้ว ---
  Serial.printf("Enroll fingerprint for this card (UID=%s) at FP_ID=%d\n", uidHex.c_str(), chosen_fp_id);
  showUIx(UI_SCAN_FINGER, "วางนิ้ว 2 ครั้งเพื่อลงทะเบียน", TR_NONE);
  int p = enrollFingerprint(chosen_fp_id);
  if (p != FINGERPRINT_OK) {
    Serial.printf("Enroll failed (code=%d). Abort.\n", p);
    
    // ส่งสัญญาณให้ Arduino เล่นเสียง "ลายนิ้วมือผิด"
    mySerial.println("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    
    showUIx(UI_FINGER_FAIL, "บันทึกลายนิ้วมือไม่สำเร็จ", TR_NONE);

    delay(1000);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // --- เก็บเรคคอร์ด (UID + FP_ID) ลง EEPROM ---
  if (storeNewRecord(uidHex, chosen_fp_id)) {
    Serial.println("Card+Fingerprint registered successfully.");
    // for (int ii = 0; ii < 5; ii++)
    mySerial.println("GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG");
    showUIx(UI_FINGER_OK, "ลงทะเบียนสำเร็จ", TR_NONE);

    delay(200);

    delay(700);
    returnToNormalMode("พร้อมให้บริการ", true);  // กลับโหมดปกติ + เสียง M (เฉพาะตอนออกจากโหมด)
  } else {
    Serial.println("EEPROM full. Cannot store new record.");
    
    // ส่งสัญญาณให้ Arduino เล่นเสียง "error"
    mySerial.println("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    
    showUIx(UI_ERROR, "หน่วยความจำเต็ม", TR_NONE);

    finger.deleteModel(chosen_fp_id);  // roll back
    delay(1000);
    returnToNormalMode("พร้อมให้บริการ", true);  // กลับโหมดปกติ + เสียง M (เฉพาะตอนออกจากโหมดลงทะเบียน)
  }
}

void deleteCardFlow() {
  exitPhotoMode();
  Serial.println("Delete mode... Tap a card to delete");

  // ส่งสัญญาณให้ Arduino เล่นเสียง "เข้าโหมดลบ"
  mySerial.println("DDDDDDDDDDDDDDDDDDDDDDDD");

  // เพิ่ม: Reset และ re-init RFID เพื่อให้แน่ใจว่าทำงาน
  Serial.println("[DELETE] Resetting RFID...");
  rc522_hard_reset();
  delay(100);

  Serial.println("[DELETE] Reinitializing RFID...");
  bus_acquire_for_rfid();
  rfid.PCD_Init();
  bus_release_after_rfid();
  delay(50);
  Serial.println("[DELETE] RFID reinitialized");

  showUIx(UI_DELETE_SCAN, "แตะบัตรเพื่อลบข้อมูล", TR_NONE);

  // แก้ไขการใช้ bus management
  int noCardCount = 0;  // นับจำนวนครั้งที่ไม่พบการ์ด
  
  while (inDeleteMode) {
    bool cardPresent = false;
    bus_acquire_for_rfid();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      cardPresent = true;
      noCardCount = 0;  // รีเซ็ตตัวนับ
    } else {
      noCardCount++;
      
      // ถ้าไม่พบการ์ด 40 ครั้งติดต่อกัน (2 วินาที) ให้ลอง reset RFID
      if (noCardCount >= 40) {
        Serial.println("[DELETE] Too many failed attempts - checking RFID health...");
        bus_release_after_rfid();  // ปล่อย bus ก่อน reset
        
        // ตรวจสอบสถานะ RFID
        if (!checkRFIDHealth()) {
          Serial.println("[DELETE] RFID health check failed - performing hard reset...");
          
          // Hard reset RFID
          rc522_hard_reset();
          delay(100);
          
          // Re-init RFID
          bus_acquire_for_rfid();
          rfid.PCD_Init();
          bus_release_after_rfid();
          delay(50);
          
          // ตรวจสอบอีกครั้งหลัง reset
          if (checkRFIDHealth()) {
            Serial.println("[DELETE] RFID recovery successful");
          } else {
            Serial.println("[DELETE] RFID recovery failed - may need hardware check");
          }
        }
        
        noCardCount = 0;  // รีเซ็ตตัวนับ
        continue;  // ข้ามการ release bus เพราะเราทำไปแล้ว
      }
    }
    bus_release_after_rfid();

    if (cardPresent) {
      break;
    }
    delay(50);
  }

  // ถ้าออกจากโหมดลบแล้ว ให้ return ทันที
  if (!inDeleteMode) {
    Serial.println("[DELETE] Exiting delete mode - stopping card wait");
    returnToNormalMode();
    return;
  }

  // อ่าน UID แบบปลอดภัยบนบัส
  String uidHex;
  bus_acquire_for_rfid();
  uidHex = readRFIDasHex();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  bus_release_after_rfid();

  int idx = findByUID(uidHex);
  if (idx < 0) {
    Serial.println("Card not found");
    showUIx(UI_CARD_NOT_FOUND, "ไม่พบข้อมูลบัตรในระบบ", TR_NONE);

    delay(900);
    returnToNormalMode();  // กลับโหมดปกติ + เสียง M
    return;
  }

  // การ์ดถูกต้อง
  showUIx(UI_CARD_OK, "บัตรถูกต้อง", TR_NONE);

  delay(300);
  showUIx(UI_SENDING, "เตรียมยืนยันการลบ...", TR_NONE);
  uiSetLoading(true);
  delay(500);
  uiSetLoading(false);
  showUIx(UI_SCAN_FINGER, "วางนิ้วเพื่อยืนยันการลบ", TR_NONE);

  // โหลดเรคคอร์ดเพื่อรู้ fp_id ของเจ้าของบัตร
  Rec r;
  readRec(idx, r);

  // ✅ ขั้นตอน "ยืนยันลายนิ้วมือก่อนลบ"
  Serial.printf("Verify fingerprint to delete (expect FP_ID=%d)\n", r.fp_id);
  showUIx(UI_SCAN_FINGER, "วางนิ้วเพื่อยืนยันการลบ", TR_NONE);
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000 && inDeleteMode) {  // รอสูงสุด 15 วินาที + เช็คโหมดลบ
    matched = matchFingerprint();
    if (matched >= 0)
      break;
    uiTick();
    delay(50);
  }

  // ถ้าออกจากโหมดลบแล้ว ให้ return ทันที
  if (!inDeleteMode) {
    Serial.println("[DELETE] Exiting delete mode - stopping fingerprint verification");
    returnToNormalMode();
    return;
  }

  if (matched < 0 || matched != r.fp_id) {
    Serial.println("Fingerprint verify failed / timeout. Abort delete.");
    showUIx(UI_FINGER_FAIL, (matched < 0) ? "ไม่ตรวจพบลายนิ้ว" : "ลายนิ้วไม่ตรงเจ้าของบัตร", TR_NONE);

    delay(1000);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // ส่งเสียงยืนยันตัวตนสำเร็จ - ใช้ฟังก์ชันเฉพาะ
  sendAuthenticationSuccessSound();

  /*
  // ลบ fingerprint template ในเซ็นเซอร์
  if (r.fp_id > 0) {
    uint8_t p = finger.deleteModel(r.fp_id);
    if (p != FINGERPRINT_OK) {
      Serial.printf("Delete template failed (code=%d). Continue to clear record.\n", p);
      showUIx(UI_ERROR, "ลบลายนิ้วในเซ็นเซอร์ไม่สำเร็จ", TR_NONE);
      delay(500);
      // ยังลบเรคคอร์ด EEPROM ต่อไปตามเดิม
    }
  }

  // ลบเรคคอร์ดบัตรใน EEPROM
  clearRec(idx);
  Serial.println("Card + Fingerprint deleted");
*/

  // ===== ลบ fingerprint template ก่อน แล้วค่อยลบ EEPROM ถ้าสำเร็จ =====
  bool okToClear = true;

  if (r.fp_id > 0) {
    uint8_t p = finger.deleteModel(r.fp_id);
    if (p != FINGERPRINT_OK) {
      okToClear = false;
      Serial.printf("Delete template failed (code=%d). Abort clearing EEPROM.\n", p);
      showUIx(UI_ERROR, "ลบลายนิ้วในเซ็นเซอร์ไม่สำเร็จ", TR_NONE);
      delay(700);
    }
  }

  if (okToClear) {
    clearRec(idx);
    Serial.println("Card + Fingerprint deleted");
    showUIx(UI_FINGER_OK, "ลบข้อมูลสำเร็จ", TR_NONE);
    delay(150);
    returnToNormalMode();  // กลับโหมดปกติ + เสียง M
  } else {
    // ถ้าอยากให้ย้อนกลับหน้าพร้อมใช้งาน
    returnToNormalMode();  // กลับโหมดปกติ + เสียง M
  }

  // showUIx(UI_FINGER_OK, "ลบข้อมูลสำเร็จ", TR_NONE);
  // delay(150);
  // delay(700);
  // showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
}

// ฟังก์ชันรวมสำหรับเข้าโหมดรอเลือก (ใช้ร่วมกันระหว่าง real vote และ test mode)
void startVotingMode(int idx = -1, bool isTestMode = false) {
  exitPhotoMode();
  g_waitingChoice = true;
  g_selectedCandidate = -1;
  g_votePosted = false;
  g_idxPending = idx;

  barStart(1500, "รอการเลือก");
  if (isTestMode) {
    showUIx(UI_WAIT_CHOICE, "โหมดทดสอบ: รอ CF:x ทาง USB", TR_NONE);
    Serial.println("[TEST] Test voting mode started");
  } else {
    showUIx(UI_WAIT_CHOICE, "โปรดเลือกผู้สมัครที่หน้าจอใหญ่", TR_NONE);
  }
}

void normalScanFlow() {
  exitPhotoMode();
  // เวอร์ชันเดิม + เติม UI อย่างเดียว (ไม่สลับลำดับ logic/protocol)
  // ขั้นตอน: ส่ง "S" → อ่าน UID → ถ้าไม่รู้จัก/ทำรายการแล้วให้แจ้งเตือน → ถ้ารู้จักให้สแกนนิ้วให้ตรง fp_id → OK และ mark voted

  Serial.println("Scan card...");
  // for (int ii = 0; ii < 5; ii++)
  mySerial.println("SSSSSSSSSSSSSSSSSSSSSSSSSSSS");  // โปรโตคอลตามเดิม
  showUIx(UI_SCAN_CARD, "ยื่นบัตรใกล้เครื่องอ่าน", TR_NONE);

  // --- อ่าน UID อย่างปลอดภัย (รอการ์ดใน flow ด้วย) ---
  uint32_t tWait = millis();
  bool got = false;
  String uidHex;

  while (millis() - tWait < 2000) {  // รอสูงสุด 2 วินาที
    bus_acquire_for_rfid();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      uidHex = readRFIDasHex();
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      got = true;
      bus_release_after_rfid();
      break;
    }
    bus_release_after_rfid();
    uiTick();
    delay(20);
  }
  if (!got || uidHex.length() == 0) {
    showUIx(UI_CARD_FAIL, "อ่านบัตรไม่สำเร็จ", TR_NONE);
    delay(600);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // --- ตรวจว่าการ์ดอยู่ในระบบ? ---
  if (DEBUG_RFID_DETAIL) {
    Serial.printf("[DEBUG] Card UID: %s\n", uidHex.c_str());
  }
  int idx = findByUID(uidHex);
  if (DEBUG_RFID_DETAIL) {
    Serial.printf("[DEBUG] Card index: %d\n", idx);
  }

  if (idx < 0) {
    Serial.println("Unknown card");
    delay(2000);
    // for(int ii=0;ii<5;ii++)
    mySerial.println("WWWWWWWWWWWWWWWWWWWWWWW");
    showUIx(UI_CARD_NOT_FOUND, "บัตรนี้ไม่อยู่ในระบบ", TR_NONE);

    delay(200);

    delay(500);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // การ์ด OK
  showUIx(UI_CARD_OK, "บัตรถูกต้อง", TR_NONE);

  delay(250);

  Rec r;
  readRec(idx, r);

  Serial.printf("[DEBUG] Card record - FP_ID: %d, Voted: %d, Valid: 0x%02X\n",
                r.fp_id, r.voted, r.valid);

  // ถ้าโหมดโหวต: เคยทำรายการแล้วหรือยัง?
  if (r.voted == 1) {
    Serial.println("[DEBUG] Already voted for this card holder.");
    // mySerial.println("W");
    showUIx(UI_CARD_ALREADY_VOTED, "บัตรนี้ใช้งานแล้ว (โหวตไปแล้ว)", TR_NONE);

    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // --- ขอให้สแกนนิ้วให้ "ตรงกับ fp_id" ของบัตรนี้ ---
  Serial.printf("Card OK. Please verify fingerprint (expect FP_ID=%d)\n", r.fp_id);
  showUIx(UI_SCAN_FINGER, "วางนิ้วเพื่อยืนยันตัวตน", TR_NONE);
  // for (int ii = 0; ii < 10; ii++)
  mySerial.println("JJJJJJJJJJJJJJJJJJJJJJJJJJ");
  unsigned long t0 = millis();
  int matched = -1;
  while (millis() - t0 < 15000) {  // รอสูงสุด 15 วินาที
    matched = matchFingerprint();
    if (matched >= 0)
      break;
    uiTick();  // ให้กรอบกระพริบทำงานระหว่างรอ
    delay(50);
  }

  if (matched < 0) {
    Serial.println("Fingerprint not matched / timeout.");
    // mySerial.println("W");
    showUIx(UI_FINGER_FAIL, "ไม่ตรวจพบลายนิ้วมือ", TR_NONE);

    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  Serial.printf("Matched fingerID=%d\n", matched);
  if (matched != r.fp_id) {
    Serial.println("Fingerprint does not belong to this card.");

    // ส่งสัญญาณให้ Arduino เล่นเสียง "ลายนิ้วมือผิด"
    mySerial.println("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");

    showUIx(UI_FINGER_FAIL, "ลายนิ้วมือต้องตรงกับผู้ถือบัตร", TR_NONE);

    delay(700);

    // กลับโหมดปกติ → ส่งเสียงโหมดเลือกตั้ง
    returnToNormalMode();
    return;
  }

  // ส่งเสียงยืนยันตัวตนสำเร็จ - ใช้ฟังก์ชันเฉพาะ
  sendAuthenticationSuccessSound();

  // --- ผ่านเงื่อนไข: บัตร+นิ้ว ตรงกัน → สำเร็จ ---
  // (ใส่จังหวะยืนยันสั้น ๆ แต่ไม่สลับลอจิกเดิม)
  // --- ผ่านเงื่อนไข: บัตร+นิ้ว ตรงกัน → สำเร็จ ---
  // --- ผ่านเงื่อนไข: บัตร+นิ้ว ตรงกัน → "รอเลือกผู้สมัคร" ---
  // เข้าโหมดรอเลือก (real voting)
  startVotingMode(idx, false);

  // ล้างข้อความขยะใน UART buffer ก่อนเริ่มรอ
  while (mySerial.available()) {
    String trash = mySerial.readStringUntil('\n');
    Serial.printf("[CLEANUP] Flushed: '%s'\n", trash.c_str());
  }

  // วนรออีเวนต์: CF:xx / SEL:xx / SENDING / VOTE:OK / VOTE:ERR (สูงสุด 20 วินาที)
  uint32_t tStart = millis();
  bool finished = false;

  while (!finished && millis() - tStart < 20000 && g_waitingChoice) {
    // จัดการ UART2 messages ระหว่างรอ (ล้างทุกข้อความที่มี)
    while (mySerial.available()) {
      String line = mySerial.readStringUntil('\n');
      line.trim();

      // ล้าง hidden characters เพิ่มเติม (carriage return, etc.)
      line.replace("\r", "");
      line.replace("\0", "");

      // ข้าม ข้อความขยะ/ไม่สมบูรณ์
      if (line.length() < 3 || line.startsWith("ESP")) {
        Serial.printf("[UART2] Ignoring: '%s'\n", line.c_str());
        continue;
      }

      Serial.printf("[UART2] Received during wait: '%s' (len=%d)\n", line.c_str(), line.length());

      // Debug: แสดง hex characters
      Serial.print("[DEBUG] Hex: ");
      for (int i = 0; i < line.length(); i++) {
        Serial.printf("%02X ", line[i]);
      }
      Serial.println();

      // จัดการ SEL: commands ระหว่างรอ (ทันที - ไม่ต้องรอ)
      if (line.startsWith("SEL:")) {
        Serial.printf("[URGENT] Processing SEL immediately: '%s'\n", line.c_str());

        // ยุติ voting loop ทันที
        g_waitingChoice = false;
        barStop();

        // แสดงรูปโดยตรง
        int n = line.substring(4).toInt();
        if (n >= 0 && n <= 99) {
          Serial.printf("[URGENT] Switching to candidate %d immediately\n", n);
          isShowingPhoto = true;
          uiSetScanning(false);
          showCandidateJpg(n);
        }
        break;  // ออกจาก voting loop ทันที
      }

      if (line.startsWith("CF:") || line.startsWith(" CF:")) {
        // ได้เบอร์ผู้สมัคร → ถือว่ายืนยันแล้ว
        g_selectedCandidate = line.substring(3).toInt();
        barStop();

        // UI: โชว์หมายเลขที่เลือก แล้วเข้าส่งทันที
        {
          String sub = "เลือกหมายเลข " + String(g_selectedCandidate);
          showUIx(UI_SELECTED, sub.c_str(), TR_NONE);
          delay(400);
        }

        if (!g_votePosted && g_selectedCandidate >= 0 && g_selectedCandidate <= 9) {
          showUIx(UI_SENDING, "กำลังส่งข้อมูล...", TR_NONE);

          bool sent = postVoteToServer(g_selectedCandidate);
          Serial.printf("[API] CF post option=%d -> %s\n",
                        g_selectedCandidate, sent ? "OK" : "FAIL");

          if (sent) {
            if (g_idxPending >= 0)
              setVotedByIndex(g_idxPending, 1);
            g_votePosted = true;
            g_waitingChoice = false;
            isShowingPhoto = false;  // รีเซ็ตโหมดแสดงภาพเมื่อโหวตสำเร็จ

            showUIx(UI_THANKS, "โหวตเสร็จสิ้น", TR_NONE);
            delay(5000);  // แสดง 5 วินาที
            showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
            finished = true;
          } else {
            showUIx(UI_ERROR, "ส่งข้อมูลไม่สำเร็จ", TR_NONE);
            delay(700);
            showUIx(UI_WAIT_CHOICE, "โปรดเลือกใหม่หรือลองอีกครั้ง", TR_NONE);
            // finished คงไว้เป็น false เพื่อรอ CF ใหม่ได้
          }
        }
      } else if (line.equalsIgnoreCase("SENDING")) {
        barStart(1200, "กำลังส่ง");
        showUIx(UI_SENDING, "กำลังส่งข้อมูล...", TR_NONE);
      } else if (line.equalsIgnoreCase("VOTE:OK")) {
        // กรณีอนาคตถ้ามีส่ง VOTE:OK ก็เคลียร์ให้จบเหมือนกัน
        showUIx(UI_THANKS, "ทำรายการสำเร็จ", TR_NONE);
        if (g_idxPending >= 0)
          setVotedByIndex(g_idxPending, 1);
        isShowingPhoto = false;  // รีเซ็ตโหมดแสดงภาพ
        finished = true;
      } else if (line.equalsIgnoreCase("VOTE:ERR")) {
        showUIx(UI_ERROR, "ส่งข้อมูลไม่สำเร็จ", TR_NONE);
        // ให้ผู้ใช้เลือกใหม่
      } else if (line.equalsIgnoreCase("ABORT")) {
        showUIx(UI_ERROR, "ยกเลิกรายการ", TR_NONE);
        isShowingPhoto = false;  // รีเซ็ตโหมดแสดงภาพ
        finished = true;
      }
    }

    uiTick();
    delay(30);
  }

  if (!finished) {
    barStop();
    showUIx(UI_ERROR, "หมดเวลารอการเลือก", TR_NONE);
  }

  g_waitingChoice = false;
  isShowingPhoto = false;  // รีเซ็ตโหมดแสดงภาพเมื่อ timeout หรือเสร็จสิ้น
  barStop();
  delay(800);
  showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
}
// วัด echo ครั้งเดียว (เวอร์ชันสั้น ใช้กับ measureDistanceCm)
inline unsigned long us_read_once() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return pulseIn(ECHO_PIN, HIGH, US_TIMEOUT);  // microseconds
}

// [ADD] อ่าน 3 ครั้งเอามัธยฐาน เพื่อลดสไปค์
float measureDistanceCm() {
  unsigned long a = us_read_once();
  delayMicroseconds(150);
  unsigned long b = us_read_once();
  delayMicroseconds(150);
  unsigned long c = us_read_once();
  // sort a<=b<=c
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    auto t = b;
    b = c;
    c = t;
  }
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  unsigned long us = b;
  if (us == 0)
    return NAN;
  return (float)us / 58.0f;  // cm
}

// ===== [ADD] Robust ultrasonic helpers =====
#ifndef PULSEIN_LONG_TIMEOUT_US
#define PULSEIN_LONG_TIMEOUT_US 50000UL  // สำรอง ถ้าไลบรารีเก่า
#endif

// เกณฑ์กรองค่าที่เชื่อถือได้
static const float MIN_VALID_CM = 0.0f;
static const float MAX_VALID_CM = 300.0f;

// อ่าน echo แบบ robust: รอให้ ECHO เป็น LOW ก่อนทุกครั้ง, ใช้ pulseInLong
unsigned long us_read_echo_once_robust() {
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
float measureDistanceCmRobust() {
  unsigned long a = us_read_echo_once_robust();
  delayMicroseconds(150);
  unsigned long b = us_read_echo_once_robust();
  delayMicroseconds(150);
  unsigned long c = us_read_echo_once_robust();

  // sort a<=b<=c
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    auto t = b;
    b = c;
    c = t;
  }
  if (a > b) {
    auto t = a;
    a = b;
    b = t;
  }
  unsigned long us = b;
  if (us == 0)
    return NAN;  // timeout → ไม่เชื่อถือ

  float cm = (float)us / 58.0f;
  if (cm < MIN_VALID_CM || cm > MAX_VALID_CM)
    return NAN;  // กรองค่าหลอก
  return cm;
}

// [ADD] งานหลัก Ultrasonic: อัปเดต nearState + ตัดสินใจหลับ
// ===== [REPLACE CALL INSIDE YOUR TICK] =====
void ultrasonicTickForSleep() {
  if (millis() - lastUSms < US_INTERVAL_MS)
    return;
  lastUSms = millis();

  float cm = measureDistanceCmRobust();  // <-- ใช้ตัว robust

  // ถ้าอ่านไม่ได้: นับ FAR ต่อ และพิมพ์ log เป็นครั้งคราว
  if (isnan(cm)) {
    farConsec = min<uint8_t>(FAR_CONFIRM_N, farConsec + 1);
    nearConsec = 0;

    if (DEBUG_ULTRA && (millis() - lastUltraLogMs >= 1000)) {
      uint32_t timeSinceNear = millis() - lastNearSeenMs;
      uint32_t timeToSleep = 0;
      if (timeSinceNear < NO_NEAR_SLEEP_MS) {
        timeToSleep = NO_NEAR_SLEEP_MS - timeSinceNear;
      }
      Serial.printf("[US] cm=NaN (treat FAR, sleep in %u.%us)\n",
                    timeToSleep / 1000, (timeToSleep % 1000) / 100);
      lastUltraLogMs = millis();
    }
  } else {
    // ตัดสินใจ newNear ด้วยฮิสเทอรีส
    bool wantNear = nearState;
    if (!nearState && cm <= NEAR_ON_CM)
      wantNear = true;
    if (nearState && cm >= NEAR_OFF_CM)
      wantNear = false;

    if (wantNear) {
      nearConsec = min<uint8_t>(NEAR_CONFIRM_N, nearConsec + 1);
      farConsec = 0;
    } else {
      farConsec = min<uint8_t>(FAR_CONFIRM_N, farConsec + 1);
      nearConsec = 0;
    }

    // เปลี่ยนสถานะเมื่อ "ยืนยัน" ครบ N เฟรม
    bool newNear = nearState;
    if (!nearState && nearConsec >= NEAR_CONFIRM_N)
      newNear = true;
    if (nearState && farConsec >= FAR_CONFIRM_N)
      newNear = false;

    // log เฉพาะเมื่อเปลี่ยนสถานะ หรือทุก ๆ 1 วินาที
    bool timeToLog = (millis() - lastUltraLogMs >= 1000);
    if (DEBUG_ULTRA && (newNear != nearState || timeToLog)) {
      uint32_t timeSinceNear = millis() - lastNearSeenMs;
      uint32_t timeToSleep = 0;
      if (!newNear && timeSinceNear < NO_NEAR_SLEEP_MS) {
        timeToSleep = NO_NEAR_SLEEP_MS - timeSinceNear;
      }

      if (newNear) {
        Serial.printf("[US] cm=%.1f near=%d (person detected)\n", cm, newNear ? 1 : 0);
      } else {
        Serial.printf("[US] cm=%.1f near=%d (sleep in %u.%us)\n",
                      cm, newNear ? 1 : 0, timeToSleep / 1000, (timeToSleep % 1000) / 100);
      }
      lastUltraLogMs = millis();
    }

    if (newNear != nearState) {
      nearState = newNear;
      if (newNear)
        lastNearSeenMs = millis();  // รีเฟรชเวลาเมื่อเห็นคน
    } else {
      if (newNear)
        lastNearSeenMs = millis();  // ยังเห็นคนอยู่
    }
  }

  // ไม่มี NEAR ต่อเนื่องครบ 30s → หลับ
  if (!nearState && (millis() - lastNearSeenMs >= NO_NEAR_SLEEP_MS)) {
    Serial.println("No NEAR for 30s -> Deep-sleep");
    goDeepSleepNow();
  }

  // แจ้งเตือน 10 วินาทีก่อน sleep
  static bool warningShown = false;
  if (!nearState && (millis() - lastNearSeenMs >= (NO_NEAR_SLEEP_MS - 10000))) {
    if (!warningShown) {
      Serial.println("Warning: Will sleep in 10 seconds if no person detected");
      showUIx(UI_SLEEP, "กำลังจะพักการทำงาน", TR_NONE);
      warningShown = true;
    }
  } else if (nearState) {
    // Reset warning when person detected
    warningShown = false;
  }
}

// Callback ของ TJpgDec แบบง่ายๆ
static volatile bool g_jpgAnyScanline = false;  // set true when at least 1 scanline drawn

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  // ปล่อยบัสอื่นก่อน และเลือก TFT เฉพาะช่วง pushImage
  spi_deselect_all();
  delayMicroseconds(4);

  int16_t W = tft.width();
  int16_t H = tft.height();

  // ตัดแถวที่อยู่นอกจอด้านบน/ล่าง
  if (y >= H || (y + (int16_t)h) <= 0) {
    return true;
  }

  // แถวต่อแถว
  for (int16_t row = 0; row < (int16_t)h; row++) {
    int16_t yy = y + row;
    if (yy < 0 || yy >= H)
      continue;

    int16_t xx = x;
    int16_t ww = (int16_t)w;
    uint16_t *src = bitmap + row * w;

    if (xx < 0) {
      int16_t skip = -xx;
      if (skip >= ww)
        continue;
      xx = 0;
      ww -= skip;
      src += skip;
    }
    if (xx + ww > W) {
      int16_t keep = W - xx;
      if (keep <= 0)
        continue;
      ww = keep;
    }

    if (ww > 0) {
      spi_select_tft();
      tft.pushImage(xx, yy, (uint16_t)ww, 1, src);
      spi_deselect_all();
      g_jpgAnyScanline = true;
    }
  }
  return true;
}

// วาด JPEG พอดีจอ เริ่มที่ (0,0) โดยไม่จัดกึ่งกลาง/ไม่ครอบ
bool drawJpgExactFromSD(const String &path) {

  // ปล่อยบัสอื่นก่อน
  spi_deselect_all();
  delay(50);

  // ตรวจสอบว่าไฟล์มีอยู่จริง
  if (!SD.exists(path)) {
    spi_select_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("File not found", 10, 10, 2);
    tft.drawString(path, 10, 28, 2);
    spi_deselect_all();
    return false;
  }

  // ตรวจสอบขนาดไฟล์
  uint16_t jw, jh;
  if (!TJpgDec.getJpgSize(&jw, &jh, path.c_str())) {
    spi_select_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Unsupported JPG", 10, 10, 2);
    tft.drawString("Likely Progressive", 10, 28, 2);
    tft.drawString(path, 10, 46, 2);
    spi_deselect_all();
    return false;
  }

  Serial.printf("[JPG] Image size: %dx%d\n", jw, jh);

  // หยุด UI effect ชั่วคราวระหว่างวาดรูป เพื่อลดการชนบัส
  bool prevScan = ui_isScanning;
  ui_isScanning = false;
  bool prevLoad = ui_isLoading;
  ui_isLoading = false;
  bool prevBar = g_barOn;
  g_barOn = false;

  // เคลียร์จอก่อนวาดรูป
  spi_select_tft();
  tft.fillScreen(TFT_BLACK);
  spi_deselect_all();
  delay(20);

  // วาดรูป (รีเซ็ตแฟล็ก และ retry 1 ครั้งถ้าล้มเหลวและยังไม่มี scanline วาด)
  g_jpgAnyScanline = false;
  bool ok = TJpgDec.drawSdJpg(0, 0, path.c_str());
  if (!ok && !g_jpgAnyScanline) {
    // sd_reinit();
    g_jpgAnyScanline = false;
    ok = TJpgDec.drawSdJpg(0, 0, path.c_str());
  }

  if (ok) {
    Serial.printf("[JPG] Successfully drew: %s\n", path.c_str());
  } else {
    // ถ้า fail แต่มีบางส่วนถูกวาดแล้ว ให้ถือว่าสำเร็จ (อย่าเขียนทับด้วย error)
    if (g_jpgAnyScanline) {
      Serial.printf("[JPG] Partial draw occurred, suppressing error overlay for: %s\n", path.c_str());
      ok = true;
    } else {
      Serial.printf("[JPG] Failed to draw: %s\n", path.c_str());
      spi_select_tft();
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("Display failed", 10, 10, 2);
      tft.drawString(path, 10, 28, 2);
      spi_deselect_all();
    }
  }

  // คืนค่า UI effect
  ui_isScanning = prevScan;
  ui_isLoading = prevLoad;
  g_barOn = prevBar;

  return ok;
}
// [ADD] วาดรูปให้พอดีกลางจอ
// วาด JPEG ให้ "เต็มจอ" แบบครอบ (cover) ด้วยการ downscale 1/2/4/8 แล้วเลื่อนศูนย์กลาง
bool drawJpgCoverFromSD(const String &path) {
  uint16_t jw, jh;
  if (!TJpgDec.getJpgSize(&jw, &jh, path.c_str()))
    return false;

  uint16_t sw = tft.width(), sh = tft.height();

  // เลือก scale (1/2/4/8) ที่ทำให้รูปหลังสเกล >= จอ ทั้งสองมิติ (เพื่อ cover)
  uint8_t candidates[4] = { 1, 2, 4, 8 };
  uint8_t scale = 1;
  bool ok = false;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t s = candidates[i];
    uint16_t dw = jw / s;
    uint16_t dh = jh / s;
    if (dw >= sw && dh >= sh) {
      scale = s;
      ok = true;
      break;
    }
  }
  if (!ok) {
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
  bool res = TJpgDec.drawSdJpg(ox, oy, path.c_str());  // ox/oy อาจติดลบได้ (เราคลิปใน callback แล้ว)

  digitalWrite(TFT_CS, HIGH);
  return res;
}

// ===== SD re-init + retry helpers =====
bool sd_reinit(uint32_t hz) {
  Serial.println("[SD] Ending previous SD session...");
  SD.end();
  delay(10);

  // ปล่อยทุก CS = HIGH
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  delay(10);

  Serial.println("[SD] Trying 1MHz...");
  // ลองความเร็วต่ำก่อน (1 MHz) แล้วค่อยเพิ่ม
  if (SD.begin(SD_CS, SPI, 1000000)) {
    if (SD.cardType() != CARD_NONE) {
      Serial.println("[SD] 1MHz successful!");
      return true;
    }
  }
  SD.end();
  delay(10);

  Serial.printf("[SD] Trying %dHz...\n", hz);
  if (SD.begin(SD_CS, SPI, hz)) {
    if (SD.cardType() != CARD_NONE) {
      Serial.printf("[SD] %dHz successful!\n", hz);
      return true;
    }
  }
  SD.end();
  delay(10);

  Serial.println("[SD] All frequencies failed");
  return false;
}

// ===== SD Card Check with UI =====
bool checkSDCardWithUI() {
  Serial.println("[SD] Starting SD Card check...");
  showUIx(UI_SD_CHECK, "กำลังตรวจสอบ SD Card", TR_NONE);
  uiTick();    // Force UI refresh
  delay(500);  // ให้เวลา UI แสดง

  // ตรวจสอบ SD Card
  if (SD.cardType() != CARD_NONE) {
    Serial.println("[SD] SD Card detected and working");
    return true;
  }

  // SD Card ไม่ทำงาน - แสดง UI และลองใหม่
  Serial.println("[SD] SD Card not working, showing retry UI");
  showUIx(UI_SD_FAIL, "SD Card ไม่ทำงาน", TR_NONE);
  uiTick();  // Force UI refresh
  delay(2000);

  // ลองใหม่ 3 ครั้ง
  for (int retry = 0; retry < 3; retry++) {
    Serial.printf("[SD] Retry attempt %d/3\n", retry + 1);

    // สร้างข้อความ retry แบบ static
    char retryMsg[64];
    snprintf(retryMsg, sizeof(retryMsg), "กำลังลอง SD Card ใหม่... (%d/3)", retry + 1);
    showUIx(UI_SD_RETRY, retryMsg, TR_NONE);

    // บังคับให้ TFT update และรอให้ UI แสดงออกมาก่อน
    uiTick();  // Force UI refresh
    delay(200);

    // แสดงข้อความเพิ่มเติมระหว่าง retry
    char statusMsg[64];
    snprintf(statusMsg, sizeof(statusMsg), "กำลังทดสอบความเร็ว SD Card...");
    showUIx(UI_SD_RETRY, statusMsg, TR_NONE);
    uiTick();  // Force UI refresh
    delay(300);

    // ลองเริ่ม SD Card ใหม่
    if (sd_reinit(4000000)) {
      Serial.println("[SD] SD Card recovered!");
      showUIx(UI_SD_CHECK, "SD Card ทำงานได้แล้ว", TR_NONE);
      delay(1000);
      return true;
    }

    // แสดงผลลัพธ์การลองแต่ละครั้ง
    if (retry < 2) {  // ไม่ใช่ครั้งสุดท้าย
      char failMsg[64];
      snprintf(failMsg, sizeof(failMsg), "ลองครั้งที่ %d ไม่สำเร็จ", retry + 1);
      showUIx(UI_SD_FAIL, failMsg, TR_NONE);
      uiTick();  // Force UI refresh
      delay(1000);
    }
  }

  // ล้มเหลวทั้งหมด
  Serial.println("[SD] All retry attempts failed");
  showUIx(UI_SD_FAIL, "SD Card ไม่สามารถใช้งานได้", TR_NONE);
  uiTick();  // Force UI refresh
  return false;
}

int getKeyPressed();
void waitForKeyRelease();
int readKeypadStable();

// ฟังก์ชันตรวจสอบว่ากดปุ่มอะไร พร้อม hold detection
int getKeyPressed() {
  uint32_t currentTime = millis();

  // Polling control - อ่านทุก 50ms เท่านั้น
  if (currentTime - lastKeyPollTime < KEY_POLL_INTERVAL) {
    return -1;
  }
  lastKeyPollTime = currentTime;

  int currentValue = readKeypadStable();

  // ตรวจสอบว่ากำลังกดปุ่มอะไรอยู่
  int pressedKey = -1;
  if (abs(currentValue - KEY_REGISTER) <= KEY_TOLERANCE) {
    pressedKey = KEY_REGISTER;
  } else if (abs(currentValue - KEY_DELETE) <= KEY_TOLERANCE) {
    pressedKey = KEY_DELETE;
  } else if (abs(currentValue - KEY_SCORE) <= KEY_TOLERANCE) {
    pressedKey = KEY_SCORE;
  } else if (currentValue >= KEY_NONE - KEY_TOLERANCE) {
    pressedKey = KEY_NONE;
  }

  // Hold detection logic
  if (pressedKey != KEY_NONE && pressedKey != -1) {
    // มีการกดปุ่ม
    if (currentPressedKey != pressedKey) {
      // เริ่มกดปุ่มใหม่
      currentPressedKey = pressedKey;
      keyPressStartTime = currentTime;

      if (DEBUG_KEYPAD_DETAIL) {
        Serial.printf("[KEYPAD] Key press started: %d (value: %d)\n", pressedKey, currentValue);
      }
    } else {
      // กดปุ่มเดิมต่อ - เช็คว่าครบ 3 วินาทีหรือยัง
      if (currentTime - keyPressStartTime >= KEY_HOLD_TIME_MS) {
        // กดครบ 3 วินาทีแล้ว
        Serial.printf("[KEYPAD] Key held for 3 seconds: %d\n", pressedKey);

        // รีเซ็ต hold detection เพื่อป้องกันการเรียกซ้ำ
        keyPressStartTime = currentTime;  // รีเซ็ตเวลาเริ่มต้น
        currentPressedKey = -1;           // รีเซ็ตปุ่มที่กด

        return pressedKey;  // คืนค่าปุ่มที่กดค้าง
      }
    }
  } else {
    // ไม่มีการกดปุ่ม หรือปล่อยปุ่มแล้ว
    if (currentPressedKey != -1) {
      uint32_t holdDuration = currentTime - keyPressStartTime;

      Serial.printf("[KEYPAD] Key released: %d (held for %d ms)\n", currentPressedKey, holdDuration);

      // รีเซ็ต
      currentPressedKey = -1;
      keyPressStartTime = 0;
    }
  }

  // อัปเดตค่าสำหรับการตรวจสอบการเปลี่ยนแปลง
  if (abs(currentValue - lastKeyValue) >= KEY_TOLERANCE) {
    lastKeyValue = currentValue;
    lastKeyTime = currentTime;
  }

  return -1;  // ไม่มีการกดค้างครบ 3 วินาที
}

// ===== SD Card Wait Loop =====
void waitForSDCard() {
  Serial.println("[SD] Waiting for SD Card to be ready...");

  while (true) {
    if (checkSDCardWithUI()) {
      Serial.println("[SD] SD Card is ready, continuing...");
      break;
    }

    // แสดงข้อความรอ
    showUIx(UI_SD_FAIL, "กรุณาใส่ SD Card หรือตรวจสอบการเชื่อมต่อ", TR_NONE);
    delay(3000);

    // ตรวจสอบว่าผู้ใช้กดปุ่มเพื่อข้าม
    int keyPressed = getKeyPressed();
    if (keyPressed == KEY_REGISTER || keyPressed == KEY_DELETE) {
      Serial.println("[SD] User pressed key to skip SD Card check");
      waitForKeyRelease();
      showUIx(UI_SD_FAIL, "ข้ามการตรวจสอบ SD Card", TR_NONE);
      delay(1000);
      break;
    }
  }
}

bool sd_retry_wrap(std::function<bool()> io, int retries = 2) {
  for (int i = 0; i <= retries; ++i) {
    if (io())
      return true;
    // ถ้า fail: re-init แล้วลองใหม่
    // if (!sd_reinit()) delay(10);
  }
  return false;
}

// [ADD] ช่วยแสดงรูปตามหมายเลข (รองรับ .jpg/.JPG)
void showCandidateJpg(uint8_t n) {
  Serial.printf("[JPG] Looking for candidate %d\n", n);

  String p_plain = "/" + String(n) + ".jpg";
  String p_plainU = "/" + String(n) + ".JPG";
  char buf[16];
  snprintf(buf, sizeof(buf), "/%02u.jpg", n);
  String p_pad = String(buf);
  snprintf(buf, sizeof(buf), "/%02u.JPG", n);
  String p_padU = String(buf);

  String path;

  if (SD.exists(p_plain)) {
    path = p_plain;
  } else if (SD.exists(p_plainU)) {
    path = p_plainU;
  } else if (SD.exists(p_pad)) {
    path = p_pad;
  } else if (SD.exists(p_padU)) {
    path = p_padU;
  }

  if (path.length() == 0) {
    Serial.printf("[JPG] File not found for candidate %d\n", n);
    spi_deselect_all();
    spi_select_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Missing:", 8, 96, 2);
    tft.drawString("/" + String(n) + ".jpg", 8, 114, 2);
    char miss[16];
    snprintf(miss, sizeof(miss), "/%02u.jpg", n);
    tft.drawString(String("or ") + miss, 8, 132, 2);
    spi_deselect_all();
    return;
  }

  // แสดงรูปโดยใช้วิธีที่เสถียร
  Serial.printf("[JPG] Displaying: %s\n", path.c_str());
  bool ok = drawJpgExactFromSD(path);
  if (!ok) {
    Serial.printf("[JPG] Failed to display: %s\n", path.c_str());
    spi_deselect_all();
    spi_select_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Display failed", 8, 96, 2);
    tft.drawString(path, 8, 114, 2);
    spi_deselect_all();
  } else {
    Serial.printf("[JPG] Successfully displayed: %s\n", path.c_str());
  }
}

void showIdleScreen(const char *msg = "Ready") {
  Serial.printf("[UI] showIdleScreen: %s\n", msg);

  spi_deselect_all();
  delay(10);
  spi_select_tft();

  // ทดสอบ TFT ด้วยการเขียนสีทั้งจอก่อน
  tft.fillScreen(TFT_BLACK);
  delay(100);

  // เขียนข้อความ
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(msg, 10, 10, 2);
  tft.drawString("System Starting...", 10, 30, 2);

  // เขียนกรอบเพื่อทดสอบว่า TFT ทำงาน
  tft.drawRect(5, 5, tft.width() - 10, tft.height() - 10, TFT_WHITE);

  spi_deselect_all();
  Serial.println("[UI] showIdleScreen completed");
}

// ฟังก์ชัน debug TFT
void debugTFT() {
  Serial.println("=== TFT Debug ===");

  // ตรวจสอบ CS pins
  Serial.printf("CS Pins - SD:%d TFT:%d RC522:%d\n",
                digitalRead(SD_CS), digitalRead(TFT_CS), digitalRead(SS_PIN));

  // ตรวจสอบการเชื่อมต่อ SPI
  Serial.println("Testing SPI...");
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(TFT_CS, LOW);

  // ทดสอบ SPI โดยตรง
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) {
    SPI.transfer(0x00);
  }
  SPI.endTransaction();
  Serial.println("SPI test completed");

  // ทดสอบการเขียนสีแบบละเอียด
  Serial.println("Testing colors...");

  // ทดสอบ 1: สีแดง
  tft.fillScreen(TFT_RED);
  delay(1000);
  Serial.println("Red test");

  // ทดสอบ 2: สีเขียว
  tft.fillScreen(TFT_GREEN);
  delay(1000);
  Serial.println("Green test");

  // ทดสอบ 3: สีน้ำเงิน
  tft.fillScreen(TFT_BLUE);
  delay(1000);
  Serial.println("Blue test");

  // ทดสอบ 4: สีขาว
  tft.fillScreen(TFT_WHITE);
  delay(1000);
  Serial.println("White test");

  // ทดสอบ 5: สีดำ
  tft.fillScreen(TFT_BLACK);
  delay(500);
  Serial.println("Black test");

  // ทดสอบข้อความ
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TFT OK", 10, 10, 2);
  tft.drawString("Test", 10, 30, 2);
  tft.drawString("Debug", 10, 50, 2);
  delay(1000);
  Serial.println("Text test");

  // ทดสอบการวาดรูปทรง
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(10, 10, 100, 50, TFT_WHITE);
  tft.fillCircle(50, 50, 20, TFT_RED);
  delay(1000);
  Serial.println("Shape test");

  digitalWrite(TFT_CS, HIGH);
  Serial.println("TFT debug completed");
}

// ฟังก์ชันทดสอบ TFT แบบพื้นฐาน
void testTFTBasic() {
  Serial.println("=== TFT Basic Test ===");

  // ปล่อยบัสอื่น
  digitalWrite(SD_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);
  digitalWrite(TFT_CS, HIGH);
  delay(100);

  // เริ่ม TFT ใหม่
  tft.init();
  tft.endWrite();

  // ตั้งค่าพื้นฐาน
  tft.setSwapBytes(false);
  tft.setRotation(0);

  // ทดสอบการเขียน
  digitalWrite(TFT_CS, LOW);
  tft.fillScreen(TFT_RED);
  delay(2000);
  tft.fillScreen(TFT_GREEN);
  delay(2000);
  tft.fillScreen(TFT_BLUE);
  delay(2000);
  tft.fillScreen(TFT_BLACK);

  // ทดสอบข้อความ
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TFT WORKING", 10, 10, 2);
  tft.drawString("Basic Test OK", 10, 30, 2);

  digitalWrite(TFT_CS, HIGH);
  Serial.println("TFT basic test completed");
}

// ===== Forward Declarations =====

void setup() {
  // --- Wake pin / IRQ ---
  pinMode(mjoy, INPUT);
  rtc_gpio_hold_dis((gpio_num_t)WAKE_PIN);
  pinMode(WAKE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(WAKE_PIN), WAKE_isr, CHANGE);

  // --- Serial / I2C / UART2 ---
  Serial.begin(115200);
  Serial.setTimeout(200);
  mySerial.setTimeout(200);

  // Initialize ESP32 Internal EEPROM
  EEPROM.begin(EEPROM_SIZE);
  Serial.printf("[EEPROM] ESP32 Internal EEPROM initialized (%d bytes)\n", EEPROM_SIZE);

  // --- Make all SPI CS pins OUTPUT & HIGH as early as possible ---
  pinMode(SD_CS, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);

  // หน่วงให้ CS pins settle
  delay(100);

  // เริ่มบัส SPI หลังจากทุก CS ถูกปล่อยแล้ว
  SPI.begin(18, 19, 23, SD_CS);

  // หน่วงให้ SPI settle
  delay(100);

  // ตรวจสอบการเชื่อมต่อ SPI
  Serial.println("Testing SPI communication...");
  Serial.printf("SPI Settings: SCK=%d, MISO=%d, MOSI=%d, CS=%d\n", 18, 19, 23, SD_CS);

  // ทดสอบการเขียน SPI โดยตรง
  digitalWrite(TFT_CS, LOW);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0x00);  // ส่งคำสั่ง dummy
  SPI.endTransaction();
  digitalWrite(TFT_CS, HIGH);
  Serial.println("SPI basic test completed");

  // UART2: RX=16, TX=17 (บอร์ดลูก/ODROID)
  mySerial.begin(9600, SERIAL_8N1, 16, 17);

  // --- WiFi ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connecting to %s", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect FAILED");
  }

  // --- Ultrasonic pins ---
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);  // GPIO34 input-only
  digitalWrite(TRIG_PIN, LOW);
  lastNearSeenMs = millis();

  // --- EEPROM header/init ---
  Serial.println("[EEPROM] Checking ESP32 Internal EEPROM...");

  if (!headerOK()) {
    Serial.println("Init header...");
    writeHeader();
    for (int i = 0; i < MAX_RECORDS; i++)
      clearRec(i);
    Serial.printf("Initialized %d records\n", MAX_RECORDS);
  } else {
    Serial.println("Header OK");
  }

  // --- SPI / Bus guard ---
  // VSPI: SCK=18, MISO=19, MOSI=23 — เราคุมทุก CS เอง
  SPI.begin(18, 19, 23, SD_CS);
  spi_idle_all();  // ดันทุก CS = HIGH

  // --- SD Card: เริ่มแบบปลอดภัย ---
  // ให้แน่ใจว่า CS ทุกตัวเป็น OUTPUT และ HIGH
  pinMode(SD_CS, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SD_CS, HIGH);  // <-- สำคัญ: ปล่อย HIGH
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SS_PIN, HIGH);

  // เริ่มที่ความถี่ต่ำก่อน (เสถียรสุด) แล้วค่อยเพิ่ม
  bool sdOK = false;
  if (SD.begin(SD_CS, SPI, 1000000)) {  // 1 MHz
    sdOK = (SD.cardType() != CARD_NONE);
    if (!sdOK)
      SD.end();
  }
  if (!sdOK) {
    if (SD.begin(SD_CS, SPI, 4000000)) {  // 4 MHz
      sdOK = (SD.cardType() != CARD_NONE);
      if (!sdOK)
        SD.end();
    }
  }
  if (!sdOK) {
    if (SD.begin(SD_CS, SPI, 10000000)) {  // 10 MHz (ถ้าการ์ดดี)
      sdOK = (SD.cardType() != CARD_NONE);
      if (!sdOK)
        SD.end();
    }
  }

  if (sdOK) {
    Serial.printf("SD OK, type=%u, size=%llu MB\n",
                  (unsigned)SD.cardType(),
                  (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
  } else {
    Serial.println("SD mount failed (tried 1/4/10 MHz)");
    // ใช้ UI เพื่อรอ SD Card
    waitForSDCard();
  }

  // --- TFT + TJpg callback ---
  Serial.println("Initializing TFT...");

  // ปล่อยบัสอื่นก่อน init TFT
  spi_deselect_all();
  delay(200);

  // เริ่ม TFT ด้วยการตั้งค่าที่ชัดเจน
  tft.init();
  tft.endWrite();

  // ตั้งค่าพื้นฐานที่เสถียร
  tft.setSwapBytes(true);  // ใช้ true สำหรับ TFT_eSPI
  tft.setRotation(0);

  // ทดสอบการเขียนพื้นฐาน
  Serial.println("Testing TFT communication...");

  // ทดสอบการเขียนสี
  spi_select_tft();
  tft.fillScreen(TFT_BLACK);
  delay(100);
  tft.fillScreen(TFT_RED);
  delay(200);
  tft.fillScreen(TFT_GREEN);
  delay(200);
  tft.fillScreen(TFT_BLUE);
  delay(200);
  tft.fillScreen(TFT_BLACK);
  delay(100);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TFT Ready", 10, 10, 2);
  tft.drawString("System OK", 10, 30, 2);
  tft.drawString("Color Test Done", 10, 50, 2);

  // วาดกรอบทดสอบ
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);

  spi_deselect_all();

  // ตั้งค่า TJpgDec
  TJpgDec.setCallback(tft_output);

  Serial.println("TFT initialization completed");

  // สร้าง sprite สำหรับ UI
  Serial.println("[UI] Creating sprite...");
  if (!spr.created()) {
    spr.setColorDepth(8);
    if (!spr.createSprite(tft.width(), tft.height())) {
      Serial.println("[UI] createSprite(8bpp) failed, retry 4bpp");
      spr.setColorDepth(4);
      if (!spr.createSprite(tft.width(), tft.height())) {
        Serial.println("[UI] createSprite(4bpp) failed, retry 1bpp");
        spr.setColorDepth(1);
        if (!spr.createSprite(tft.width(), tft.height())) {
          Serial.println("[UI] createSprite failed completely!");
        } else {
          Serial.println("[UI] Sprite created with 1bpp");
        }
      } else {
        Serial.println("[UI] Sprite created with 4bpp");
      }
    } else {
      Serial.println("[UI] Sprite created with 8bpp");
    }
  } else {
    Serial.println("[UI] Sprite already exists");
  }

  // แสดงข้อมูล TFT
  Serial.printf("[TFT] Width: %d, Height: %d\n", tft.width(), tft.height());
  Serial.printf("[SPRITE] Created: %s, ColorDepth: %d\n", spr.created() ? "YES" : "NO", spr.getColorDepth());

  // ทดสอบการแสดงผลพื้นฐาน
  Serial.println("[UI] Testing basic display...");
  showIdleScreen(sdOK ? "SD OK" : "No SD");
  delay(1000);

  // แสดง UI แรก
  Serial.println("[UI] Showing boot screen...");
  showUIx(UI_BOOT, "กำลังตรวจสอบระบบ", TR_NONE);
  delay(600);

  Serial.println("[UI] Showing ready screen...");
  showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);

  // --- RC522 init (ปล่อยบัสจริง + รีเซ็ต RST ก่อน) ---
  Serial.println("Init RC522...");
  rfid_bus_begin();
  rc522_hard_reset();
  rfid.PCD_Init();
  byte rc522v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  rfid_bus_end();
  Serial.printf("RC522 Version=0x%02X\n", rc522v);
  if (rc522v == 0x00 || rc522v == 0xFF) {
    Serial.println("[RC522] Bad version (0x00/0xFF) -> ตรวจ CS/MISO/MOSI/SCK และว่ามี CS อื่นค้าง LOW ไหม");
  }

  // --- I/O อื่น ๆ ---

  // pinMode(switchPin33, INPUT_PULLUP);
  // pinMode(switchPin32, INPUT_PULLUP);
  // ถ้าใช้ LED เพิ่มค่อยเปิด
  // pinMode(ledPin, OUTPUT); digitalWrite(ledPin, LOW);

  // --- Fingerprint module ---
  if (!fingerBegin()) {
    Serial.println("Fingerprint module not found. Check wiring.");
  } else {
    Serial.println("Fingerprint module ready.");
  }

  // --- Info / wake-pin debug ---
  Serial.printf("MAX_RECORDS=%d, RECORD_SIZE=%d\n", MAX_RECORDS, RECORD_SIZE);
  printBootAndWakeInfo();
  dbgPrintWakePin("boot");

  lastUltraLogMs = millis();

  isShowingPhoto = false;
  uiSetScanning(true);

  // แสดงสถานะโหมดต่างๆ
  Serial.printf("=== SYSTEM STATUS ===\n");
  Serial.printf("Test Mode: %s (Use 'TESTMODE' command to toggle)\n", testModeEnabled ? "ENABLED" : "DISABLED");
  Serial.printf("Register Mode: %s\n", inRegisterMode ? "ON" : "OFF");
  Serial.printf("Delete Mode: %s\n", inDeleteMode ? "ON" : "OFF");
  Serial.printf("====================\n");

  Serial.println("setup() done.");
}

// [ADD] ฟังก์ชันรับคำสั่งจากบอร์ดลูกโซ่
void handleU2Line(const String &raw) {
  String m = raw;
  m.trim();

  // ทำความสะอาดข้อความเพิ่มเติม - เอาเฉพาะตัวอักษรที่พิมพ์ได้
  String cleaned = "";
  for (int i = 0; i < m.length(); i++) {
    char c = m.charAt(i);
    if (c >= 32 && c <= 126) {  // ASCII printable characters
      cleaned += c;
    }
  }
  m = cleaned;
  m.trim();  // trim อีกครั้งหลังทำความสะอาด

  Serial.printf("[HANDLE] Processing: '%s' (length=%d)\n", m.c_str(), m.length());

  // Debug: แสดง hex ของข้อความ
  Serial.print("[HANDLE] Hex: ");
  for (int i = 0; i < m.length(); i++) {
    Serial.printf("%02X ", (unsigned char)m.charAt(i));
  }
  Serial.println();

  if (m.equalsIgnoreCase("AUTHOK")) {
    // รับ AUTHOK จาก Arduino → เข้าโหมดรอเลือก (test mode)
    Serial.println("[HANDLE] Received AUTHOK from Arduino");
    startVotingMode(-1, true);  // idx=-1 = test mode, isTestMode=true
    return;
  } else if (m.startsWith("SEL:")) {
    Serial.printf("[HANDLE] SEL command detected: '%s'\n", m.c_str());

    // ยกเลิกโหมดรอเลือกทันที (ถ้ามี)
    barStop();

    // ยุติ voting loop ถ้ากำลังรอ
    if (g_waitingChoice) {
      g_waitingChoice = false;
      Serial.println("[SEL] Terminating voting loop");
    }

    if (m.equalsIgnoreCase("SEL:CLEAR")) {
      Serial.println("[SEL] Clearing photo mode");
      isShowingPhoto = false;
      uiSetScanning(true);
      showUIx(UI_SCAN_CARD, "ยื่นบัตรใกล้เครื่องอ่าน", TR_NONE);
    } else {
      int n = m.substring(4).toInt();  // หลัง "SEL:"
      Serial.printf("[SEL] Extracted number: %d from '%s'\n", n, m.c_str());
      if (n >= 0 && n <= 99) {
        Serial.printf("[SEL] Setting photo mode, showing candidate %d\n", n);

        // เข้าสู่ voting mode หากยังไม่ได้อยู่ในโหมดใดๆ
        if (!g_waitingChoice && !inRegisterMode && !inDeleteMode) {
          if (testModeEnabled) {
            Serial.println("[SEL] Starting new voting mode for photo display - TEST MODE");
            startVotingMode(-1, true);  // test mode สำหรับแสดงภาพ
          } else {
            Serial.println("[SEL] Test mode disabled - only showing photo, no voting mode");
            // แค่แสดงภาพโดยไม่เข้า voting mode
          }
        }

        isShowingPhoto = true;
        uiSetScanning(false);

        // ตรวจสอบ SD card ก่อนแสดงรูป
        if (!SD.cardType()) {
          Serial.println("[SEL] ERROR: SD card not mounted!");
          showIdleScreen("SD Error");
          return;
        }

        Serial.printf("[SEL] About to call showCandidateJpg(%d)\n", n);
        showCandidateJpg((uint8_t)n);
        Serial.printf("[SEL] showCandidateJpg(%d) completed\n", n);
      } else {
        Serial.printf("[SEL] Invalid number: %d (must be 0-99)\n", n);
        isShowingPhoto = true;
        uiSetScanning(false);
        showIdleScreen("Bad SEL");
      }
    }
    return;
  } else if (m.startsWith("CF:")) {
    int n = m.substring(3).toInt();
    g_selectedCandidate = n;

    if (g_waitingChoice) {
      barStop();
      String sub = "เลือกหมายเลข " + String(n);
      showUIx(UI_SELECTED, sub.c_str(), TR_NONE);
      delay(400);
    }

    // จบโหวตจาก CF: เช่นกัน (กันยิงซ้ำ)
    if (g_waitingChoice && !g_votePosted && n >= 0 && n <= 9) {
      showUIx(UI_SENDING, "กำลังส่งข้อมูล...", TR_NONE);

      bool sent = postVoteToServer(n);
      Serial.printf("[API] CF post option=%d -> %s\n", n, sent ? "OK" : "FAIL");

      if (sent) {
        if (g_idxPending >= 0)
          setVotedByIndex(g_idxPending, 1);
        g_votePosted = true;
        g_waitingChoice = false;

        showUIx(UI_THANKS, "โหวตเสร็จสิ้น", TR_NONE);
        delay(5000);  // 5 วิ ตามที่ขอ
        showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
      } else {
        showUIx(UI_ERROR, "ส่งข้อมูลไม่สำเร็จ", TR_NONE);
        delay(700);
        showUIx(UI_WAIT_CHOICE, "โปรดเลือกใหม่หรือลองอีกครั้ง", TR_NONE);
      }
    }
    return;
  } else if (m.equalsIgnoreCase("SENDING")) {
    Serial.println("[HANDLE] Received SENDING");
    barStart(1200, "กำลังส่ง");
    showUIx(UI_SENDING, "กำลังส่งข้อมูล...", TR_NONE);
    return;
  } else if (m.equalsIgnoreCase("VOTE:OK")) {
    Serial.println("[HANDLE] Received VOTE:OK");
    uiSetLoading(false);
    showUIx(UI_THANKS, "ทำรายการสำเร็จ", TR_NONE);
    if (g_idxPending >= 0)
      setVotedByIndex(g_idxPending, 1);
    g_waitingChoice = false;
    g_votePosted = true;
    return;
  } else if (m.equalsIgnoreCase("VOTE:ERR")) {
    Serial.println("[HANDLE] Received VOTE:ERR");
    uiSetLoading(false);
    showUIx(UI_ERROR, "ส่งข้อมูลไม่สำเร็จ", TR_NONE);
    delay(700);
    showUIx(UI_WAIT_CHOICE, "โปรดลองอีกครั้ง", TR_NONE);
    return;
  } else if (m.equalsIgnoreCase("ESP S")) {
    // รับ ESP S จาก Arduino - ไม่ต้องทำอะไร
    Serial.println("[HANDLE] Received ESP S - ignoring");
    return;
  } else if (m.equalsIgnoreCase("ESP J")) {
    // รับ ESP J จาก Arduino - ไม่ต้องทำอะไร
    Serial.println("[HANDLE] Received ESP J - ignoring");
    return;
  } else if (m.equalsIgnoreCase("ESP G")) {
    // รับ ESP G จาก Arduino - ไม่ต้องทำอะไร
    Serial.println("[HANDLE] Received ESP G - ignoring");
    return;
  } else if (m.equalsIgnoreCase("ESP")) {
    // รับ ESP จาก Arduino - ไม่ต้องทำอะไร
    Serial.println("[HANDLE] Received ESP - ignoring");
    return;
  } else if (m.length() == 0) {
    // รับ empty string - ไม่ต้องทำอะไร
    Serial.println("[HANDLE] Received empty string - ignoring");
    return;
  } else if (m.equalsIgnoreCase("R")) {
    // รับ PS (Password Success) จาก Arduino
    Serial.println("[HANDLE] Received PS - Password confirmed");

    if (waitingForPassword && inRegisterMode) {
      waitingForPassword = false;

      // แสดง UI ยืนยันสำเร็จ
      showUIx(UI_PASSWORD_OK, "ยืนยันสำเร็จ", TR_NONE);
      delay(1500);

      // เปลี่ยนเป็นโหมดลงทะเบียนจริง
      showUIx(UI_REGISTER_SCAN, "แตะบัตรเพื่อลงทะเบียน", TR_NONE);
      Serial.println("[HANDLE] Entering actual register mode");
    } else {
      Serial.println("[HANDLE] PS received but not waiting for password");
    }
    return;
  } else if (m.equalsIgnoreCase("SENDING")) {
    // เปลี่ยนมาเป็นหลอดโหมดส่ง (จะวนทุก 1.2 วินาที)
    barStart(1200, "กำลังส่ง");
    showUIx(UI_SENDING, "กำลังส่งข้อมูล...", TR_NONE);
    return;
  } else if (m.equalsIgnoreCase("VOTE:OK")) {
    barStop();
    showUIx(UI_THANKS, "ทำรายการสำเร็จ", TR_NONE);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  } else if (m.equalsIgnoreCase("VOTE:ERR")) {
    barStop();
    showUIx(UI_ERROR, "ส่งข้อมูลไม่สำเร็จ", TR_NONE);
    delay(700);
    showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
    return;
  }

  // default: unknown command
  Serial.printf("[HANDLE] Unknown command: '%s'\n", m.c_str());
}

void tftSoftRecoverIfBlank() {
  static uint32_t lastTry = 0;
  const uint32_t NOW = millis();

  // ถ้ามี animation/โหลด/แถบวิ่งอยู่ หรือเพิ่งวาดไม่นาน ให้ข้ามไปเลย
  if (ui_isScanning || ui_isLoading || g_barOn || isShowingPhoto)
    return;

  // ขยาย margin ให้ยาวขึ้นอีกหน่อย เช่น 120 วินาที
  if (NOW - g_lastPaintMs < 120000)
    return;

  if (NOW - lastTry < 30000)
    return;  // กันสั่น 30 วินาที
  lastTry = NOW;

  Serial.println("[TFT] Attempting soft recovery...");
  spi_deselect_all();
  delay(100);
  tft.endWrite();
  tft.init();
  tft.setSwapBytes(true);
  tft.setRotation(0);

  // ทดสอบการเขียนหลัง recovery
  spi_select_tft();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TFT Recovered", 10, 10, 2);
  spi_deselect_all();
}

// ===== วางฟังก์ชันนี้ "ถัดจาก" ปิดวงเล็บของ setup() =====
void loop() {
  // ===== ปุ่มโหมด (กดค้าง 3 วินาที) =====
  int keyPressed = getKeyPressed();

  if (keyPressed == KEY_REGISTER) {
    Serial.printf("[KEYPAD] KEY_REGISTER detected - inRegisterMode:%s\n",
                  inRegisterMode ? "true" : "false");
  }

  if (keyPressed == KEY_REGISTER) {  // Register mode
    if (!inRegisterMode) {
      // เข้าโหมดลงทะเบียน - ส่ง PASSWORD และรอ PS
      Serial.println("[KEYPAD] REGISTER key held for 3 seconds - entering register mode");
      Serial.printf("[KEYPAD] Before - inRegisterMode:%s, waitingForPassword:%s\n",
                    inRegisterMode ? "true" : "false", waitingForPassword ? "true" : "false");
      inRegisterMode = true;
      waitingForPassword = true;
      Serial.printf("[KEYPAD] After - inRegisterMode:%s, waitingForPassword:%s\n",
                    inRegisterMode ? "true" : "false", waitingForPassword ? "true" : "false");

      // ส่งสัญญาณให้ Arduino เล่นเสียง "เข้าโหมดลงทะเบียน"
      mySerial.println("BBBBBBBBBBBBBBBBBBBBBBBB");

      // แสดง UI รอการยืนยัน
      showUIx(UI_WAIT_PASSWORD, "รอการยืนยันจากผู้ดูแล", TR_NONE);

      // ส่ง PASSWORD ไป Arduino
      mySerial.println("RRRRRRRRRRRRRRRRRRRRRRRRRRRRRR");
      Serial.println("[UART2] Sent: PASSWORD");

      waitForKeyRelease();  // รอให้ปล่อยปุ่ม

      // รอ PS จาก Arduino (จะถูกจัดการใน handleU2Line)
    } else {
      // ออกจากโหมดลงทะเบียน - ออกได้ทุกขั้นตอน
      Serial.println("[KEYPAD] REGISTER key held for 3 seconds - exiting register mode");
      Serial.printf("[KEYPAD] Current state - inRegisterMode:%s, waitingForPassword:%s\n",
                    inRegisterMode ? "true" : "false", waitingForPassword ? "true" : "false");

      // ส่ง R เพื่อบอก Arduino ว่าออกจากโหมดลงทะเบียน
      mySerial.println("RRRRRRRRRRRRRRRRRRRRRRRRRRRRRR");
      Serial.println("[UART2] Sent: R (exit register mode)");

      // รีเซ็ตสถานะทั้งหมด
      inRegisterMode = false;
      waitingForPassword = false;

      showUIx(UI_READY, "ยกเลิกโหมดลงทะเบียน", TR_NONE);
      delay(1000);

      // กลับโหมดปกติ → ส่งเสียงโหมดเลือกตั้ง (เฉพาะตอนออกจากโหมด)
      returnToNormalMode("ยื่นบัตรใกล้เครื่องอ่าน", true);

      waitForKeyRelease();
    }
    return;
  }

  if (keyPressed == KEY_DELETE) {  // Delete mode
    if (!inDeleteMode) {
      // เข้าโหมดลบ
      Serial.println("[KEYPAD] DELETE key held for 3 seconds - entering delete mode");
      inDeleteMode = true;
      showUIx(UI_MODE_DELETE, "โหมดลบข้อมูล", TR_NONE);
      waitForKeyRelease();  // รอให้ปล่อยปุ่ม
      deleteCardFlow();
      uiShownScanCard = false;
      inDeleteMode = false;  // รีเซ็ตหลังจากเสร็จสิ้น
    } else {
      // ออกจากโหมดลบ
      Serial.println("[KEYPAD] DELETE key held for 3 seconds - exiting delete mode");
      inDeleteMode = false;
      showUIx(UI_READY, "ยกเลิกโหมดลบข้อมูล", TR_NONE);
      delay(1000);

      // กลับโหมดปกติ → ส่งเสียงโหมดเลือกตั้ง (เฉพาะตอนออกจากโหมด)
      returnToNormalMode("ยื่นบัตรใกล้เครื่องอ่าน", true);

      waitForKeyRelease();
    }
    return;
  }

  if (keyPressed == KEY_SCORE) {  // Score check mode - ส่ง T ไป Arduino
    if (!inScoreMode) {
      // เข้าโหมดเช็ค score
      Serial.println("[KEYPAD] SCORE key held for 3 seconds - entering score mode");
      inScoreMode = true;
      showUIx(UI_SENDING, "โหมดเช็ค Score", TR_NONE);
      // for (int ii = 0; ii < 5; ii++)
      mySerial.println("TTTTTTTTTTTTTTTTTTTTTTTT");  // ส่ง T ไปยัง Arduino ผ่าน UART2
      Serial.println("[UART2] Sent: T");
      delay(2000);  // แสดงโหมด 2 วินาที
      showUIx(UI_READY, "พร้อมให้บริการ", TR_NONE);
      inScoreMode = false;  // รีเซ็ตทันที
      waitForKeyRelease();
    } else {
      // ออกจากโหมดเช็ค score (ไม่ค่อยมีประโยชน์ แต่ให้ครบ)
      Serial.println("[KEYPAD] SCORE key held for 3 seconds - exiting score mode");
      inScoreMode = false;
      showUIx(UI_READY, "ยกเลิกโหมดเช็ค Score", TR_NONE);
      delay(1000);

      // กลับโหมดปกติ → ส่งเสียงโหมดเลือกตั้ง
      returnToNormalMode("ยื่นบัตรใกล้เครื่องอ่าน");

      waitForKeyRelease();
    }
    return;
  }

  // int switchReg = digitalRead(switchPin33);
  // int switchDel = digitalRead(switchPin32);

  // if (switchReg == LOW)
  // {
  //   showUIx(UI_CONFIRM, "โหมดลงทะเบียน", TR_SLIDE_UP);
  //   delay(500); // ให้ผู้ใช้เห็น
  //   while (digitalRead(switchPin33) == LOW)
  //     delay(10);
  //   registerCardAndFingerprint();
  //   uiShownScanCard = false;
  //   delay(300);
  //   return;
  // }
  // else if (switchDel == LOW)
  // {
  //   showUIx(UI_ERROR, "โหมดลบข้อมูล", TR_SLIDE_UP);
  //   delay(500);
  //   while (digitalRead(switchPin32) == LOW)
  //     delay(10);
  //   deleteCardFlow();
  //   uiShownScanCard = false;
  //   delay(300);
  //   return;
  // }

  // ===== แตะการ์ด (ล็อคบัส RC522 เสมอ) =====

  // NEW: แสดง "สแกนบัตร" 1 ครั้ง เมื่อเข้าลูปว่างครั้งแรก
  if (!uiShownScanCard) {
    showUIx(UI_SCAN_CARD, "ยื่นบัตรใกล้เครื่องอ่าน", TR_NONE);
    uiShownScanCard = true;
    uiScanCardShownAt = millis();
  }

  // เรียก uiTick() เพื่อให้ animation ทำงานตลอดเวลา
  uiTick();

  bool cardReady = false;
  rfid_bus_begin();
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    cardReady = true;
  }
  rfid_bus_end();

  if (cardReady) {
    // NEW: reset flag เพื่อให้รอบถัดไปขึ้น "สแกนบัตร" ใหม่อีกครั้ง หลังจบ flow
    uiShownScanCard = false;

    // ตรวจสอบว่าอยู่ในโหมดลงทะเบียนหรือไม่
    if (inRegisterMode && !waitingForPassword) {
      registerCardAndFingerprint();
    } else {
      normalScanFlow();
    }
  }

  // ===== รับคำสั่งจากบอร์ดลูก (UART2) =====
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');
    msg.trim();

    Serial.printf("[UART2] Received: '%s'\n", msg.c_str());

    if (msg.equalsIgnoreCase("SLEEP!")) {
      mySerial.println("OK SLEEP");
      delay(30);
      goDeepSleepNow();  // ไม่กลับจากฟังก์ชันนี้
    }

    // จัดการ commands ทันที
    handleU2Line(msg);

    // ถ้าอยู่ในโหมดรอการเลือก ให้จบ normalScanFlow() ด้วย
    if (g_waitingChoice && (msg.startsWith("CF:") || msg.equalsIgnoreCase("VOTE:OK") || msg.equalsIgnoreCase("VOTE:ERR"))) {
      Serial.println("[MAIN] Vote completed - ending waiting choice");
      g_waitingChoice = false;
    }
  }

  // ===== อัลตราโซนิก: auto-sleep =====
  ultrasonicTickForSleep();

  // ===== อัปเดต UI animation =====
  uiTick();

  // ===== ทดสอบ TFT ทุก 30 วินาที (ลดความถี่) =====
  static uint32_t lastTFTTest = 0;
  if (millis() - lastTFTTest > 30000) {
    lastTFTTest = millis();
    // ทดสอบการเขียน TFT เฉพาะเมื่อไม่มีการแสดงผลอื่น
    if (!isShowingPhoto && !ui_isScanning && !ui_isLoading && !g_barOn && !ui_isBusyBorder) {
      // ไม่แสดงข้อความบนจอเพื่อไม่ให้รบกวนผู้ใช้
      // แค่ทดสอบการเขียนแบบเงียบๆ
      spi_deselect_all();
      delay(10);
      spi_select_tft();
      tft.fillScreen(TFT_BLACK);
      spi_deselect_all();
    }
  }

  // ===== คำสั่งผ่าน USB Serial (debug) =====
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.equalsIgnoreCase("ULTRA?") || cmd.equalsIgnoreCase("U")) {
      float cm = measureDistanceCm();
      bool ns = nearState;
      if (!isnan(cm)) {
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
    } else if (cmd.equalsIgnoreCase("W?")) {
      dbgPrintWakePin("now");
    } else if (cmd.equalsIgnoreCase("WTEST")) {
      uint32_t t0 = millis();
      while (millis() - t0 < 10000) {
        dbgPrintWakePin("probe");
        delay(300);
      }
    } else if (cmd.equalsIgnoreCase("AUTHOK")) {
      // จำลองว่า auth ผ่านแล้ว → เข้าโหมดรอเลือก (test mode)
      startVotingMode(-1, true);  // idx=-1 = test mode, isTestMode=true
    } else if (cmd.equalsIgnoreCase("TFTDEBUG") || cmd.equalsIgnoreCase("TFT")) {
      debugTFT();
    } else if (cmd.equalsIgnoreCase("TFTBASIC") || cmd.equalsIgnoreCase("TFTB")) {
      testTFTBasic();
    } else if (cmd.equalsIgnoreCase("TFTINIT")) {
      // เริ่ม TFT ใหม่
      spi_deselect_all();
      delay(100);
      tft.init();
      tft.endWrite();
      tft.setSwapBytes(true);
      tft.setRotation(0);
      Serial.println("TFT re-initialized");
    } else if (cmd.equalsIgnoreCase("TFTTEST")) {
      // ทดสอบ TFT แบบง่าย
      spi_deselect_all();
      spi_select_tft();
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("TFT Test OK", 10, 10, 2);
      tft.drawString("Time: " + String(millis()), 10, 30, 2);
      spi_deselect_all();
      Serial.println("TFT test completed");
    } else if (cmd.equalsIgnoreCase("SDTEST")) {
      // ทดสอบ SD Card
      if (SD.cardType() != CARD_NONE) {
        Serial.println("SD Card detected");
        File root = SD.open("/");
        if (root) {
          Serial.println("SD Card files:");
          while (true) {
            File entry = root.openNextFile();
            if (!entry)
              break;
            Serial.println(entry.name());
            entry.close();
          }
          root.close();
        }
      } else {
        Serial.println("No SD Card");
      }
    } else if (cmd.equalsIgnoreCase("SDCHECK")) {
      // ตรวจสอบ SD Card พร้อม UI
      checkSDCardWithUI();
    } else if (cmd.equalsIgnoreCase("SDWAIT")) {
      // รอ SD Card พร้อม UI
      waitForSDCard();
    } else if (cmd.equalsIgnoreCase("JPGTEST")) {
      // ทดสอบการแสดงรูป JPG
      Serial.println("Testing JPG display...");
      showCandidateJpg(2);
    } else if (cmd.equalsIgnoreCase("TESTMODE")) {
      // สลับโหมดทดสอบ
      testModeEnabled = !testModeEnabled;
      Serial.printf("Test mode is now: %s\n", testModeEnabled ? "ENABLED" : "DISABLED");
      Serial.println("Test mode controls whether SEL commands after timeout can enter voting mode");
    } else if (cmd.equalsIgnoreCase("SDFILES") || cmd.equalsIgnoreCase("LIST")) {
      // แสดงรายการไฟล์ใน SD card
      Serial.println("=== SD Card Files ===");
      if (SD.cardType() == CARD_NONE) {
        Serial.println("SD Card not mounted!");
      } else {
        File root = SD.open("/");
        if (!root) {
          Serial.println("Failed to open root directory");
        } else {
          File file = root.openNextFile();
          while (file) {
            Serial.printf("File: %s (Size: %d bytes)\n", file.name(), file.size());
            file = root.openNextFile();
          }
          root.close();
        }
      }
    } else if (cmd.equalsIgnoreCase("SDREINIT")) {
      // เริ่มต้น SD Card ใหม่
      Serial.println("Reinitializing SD Card...");
      SD.end();
      delay(100);
      if (SD.begin(SD_CS, SPI, 4000000)) {
        Serial.println("SD Card reinitialized successfully");
      } else {
        Serial.println("SD Card reinitialization failed");
      }
    } else if (cmd.equalsIgnoreCase("DBGEEPROM") || cmd.equalsIgnoreCase("DBG")) {
      // แสดงข้อมูล EEPROM ทั้งหมด
      Serial.println("=== EEPROM Debug ===");
      Serial.printf("Header OK: %s\n", headerOK() ? "YES" : "NO");
      Serial.printf("MAX_RECORDS: %d\n", MAX_RECORDS);

      for (int i = 0; i < MAX_RECORDS; i++) {
        Rec r;
        readRec(i, r);
        if (r.valid == VALID_FLAG) {
          String uidStr = String(r.uid, UID_HEX_MAX);
          Serial.printf("Record[%d]: UID=%s, FP_ID=%d, Voted=%d, Valid=0x%02X\n",
                        i, uidStr.c_str(), r.fp_id, r.voted, r.valid);
        } else {
          Serial.printf("Record[%d]: EMPTY (Valid=0x%02X)\n", i, r.valid);
        }
      }
    } else if (cmd.equalsIgnoreCase("CLEAREEPROM") || cmd.equalsIgnoreCase("CLEAR")) {
      // ล้าง EEPROM ทั้งหมด
      Serial.println("Clearing all EEPROM records...");
      for (int i = 0; i < MAX_RECORDS; i++) {
        clearRec(i);
      }
      Serial.println("EEPROM cleared!");
    } else if (cmd.equalsIgnoreCase("TESTCARD")) {
      // ทดสอบการอ่านบัตร
      Serial.println("Testing card read...");
      rfid_bus_begin();
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        String uidHex = readRFIDasHex();
        Serial.printf("Card detected: %s\n", uidHex.c_str());
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
      } else {
        Serial.println("No card detected");
      }
      rfid_bus_end();
    } else if (cmd.equalsIgnoreCase("KEYPAD") || cmd.equalsIgnoreCase("KEY")) {
      // ทดสอบ keypad
      Serial.println("=== Keypad Test ===");
      Serial.println("Press keys to see values (press any key to exit)...");
      uint32_t startTime = millis();

      while (millis() - startTime < 10000) {  // ทดสอบ 10 วินาที
        int rawValue = analogRead(mjoy);
        int stableValue = readKeypadStable();
        int keyPressed = getKeyPressed();

        Serial.printf("Raw: %d, Stable: %d, Key: ", rawValue, stableValue);
        if (keyPressed == KEY_REGISTER) {
          Serial.println("KEY_REGISTER (HELD 3s)");
        } else if (keyPressed == KEY_DELETE) {
          Serial.println("KEY_DELETE (HELD 3s)");
        } else if (keyPressed == KEY_SCORE) {
          Serial.println("KEY_SCORE (HELD 3s) - Send T to Arduino");
        } else if (keyPressed == KEY_NONE) {
          Serial.println("NONE");
        } else if (keyPressed > 0) {
          Serial.printf("DETECTED(%d)\n", keyPressed);
        } else {
          Serial.println("NO_CHANGE");
        }

        // แสดงสถานะโหมด
        Serial.printf("Mode Status - Register:%s Delete:%s Score:%s Test:%s Password:%s\n",
                      inRegisterMode ? "ON" : "OFF",
                      inDeleteMode ? "ON" : "OFF",
                      inScoreMode ? "ON" : "OFF",
                      testModeEnabled ? "ON" : "OFF",
                      waitingForPassword ? "WAITING" : "OK");

        delay(200);
      }
      Serial.println("Keypad test completed");
    } else if (cmd.equalsIgnoreCase("KEYPADRAW") || cmd.equalsIgnoreCase("KEYRAW")) {
      // แสดงค่า raw ของ keypad
      int rawValue = analogRead(mjoy);
      int stableValue = readKeypadStable();
      Serial.printf("Keypad Raw: %d, Stable: %d\n", rawValue, stableValue);
    } else if (cmd.equalsIgnoreCase("KEYCAL") || cmd.equalsIgnoreCase("CALIBRATE")) {
      // Calibrate keypad - อ่านค่าต่อเนื่อง
      Serial.println("=== Keypad Calibration ===");
      Serial.println("Press each key and observe values:");
      Serial.println("KEY_REGISTER should be ~0");
      Serial.println("KEY_DELETE should be ~1950");
      Serial.println("KEY_SCORE should be ~350 (sends T to Arduino for score check)");
      Serial.println("NONE should be ~4095");
      Serial.println("Press any key for 30 seconds...");

      uint32_t startTime = millis();
      while (millis() - startTime < 30000) {  // 30 วินาที
        int rawValue = analogRead(mjoy);
        int stableValue = readKeypadStable();

        // แสดงค่าเมื่อมีการเปลี่ยนแปลง
        static int lastDisplayValue = -1;
        if (abs(stableValue - lastDisplayValue) > 50) {
          Serial.printf("Value: %d (Raw: %d)\n", stableValue, rawValue);
          lastDisplayValue = stableValue;
        }

        delay(50);
      }
      Serial.println("Calibration completed");
    } else if (cmd.equalsIgnoreCase("24C32TEST")) {
      // ทดสอบ 24C32 EEPROM
      Serial.println("=== 24C32 EEPROM Test ===");

      // Test write and read
      uint8_t testData[16] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                               0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };
      uint8_t readData[16];

      Serial.println("Writing test data...");
      eepromWriteBytes(0x100, testData, 16);
      EEPROM.commit();
      Serial.println("Write successful");

      delay(10);

      Serial.println("Reading test data...");
      eepromReadBytes(0x100, readData, 16);
      Serial.println("Read successful");

      bool match = true;
      for (int i = 0; i < 16; i++) {
        if (testData[i] != readData[i]) {
          match = false;
          break;
        }
      }

      if (match) {
        Serial.println("ESP32 EEPROM test PASSED!");
      } else {
        Serial.println("ESP32 EEPROM test FAILED - data mismatch");
      }
    } else if (cmd.equalsIgnoreCase("24C32SIZE") || cmd.equalsIgnoreCase("EEPROMSIZE")) {
      // แสดงขนาด EEPROM
      Serial.printf("ESP32 EEPROM Size: %d bytes (%d KB)\n", EEPROM_SIZE, EEPROM_SIZE / 1024);
      Serial.printf("Max records: %d\n", MAX_RECORDS);
    } else if (cmd.startsWith("CF:") || cmd.startsWith("SEL:") || cmd.equalsIgnoreCase("SENDING") || cmd.equalsIgnoreCase("VOTE:OK") || cmd.equalsIgnoreCase("VOTE:ERR")) {
      // ส่งต่อข้อความจาก USB Serial ให้ใช้ logic เดียวกับ UART2
      Serial.printf("[USB] Processing command: %s\n", cmd.c_str());
      handleU2Line(cmd);
    }
    // ปล่อยบัสและจัดการ TFT
    spi_deselect_all();
    tft.endWrite();
    uiTick();
    tftSoftRecoverIfBlank();
  }
}

// ===== Analog Keypad Functions =====
// ฟังก์ชันอ่านค่า keypad แบบเสถียร
int readKeypadStable() {
  static int readings[5] = { 0 };  // เก็บค่าล่าสุด 5 ครั้ง
  static int index = 0;
  static bool initialized = false;
  static uint32_t lastLogTime = 0;

  // อ่านค่าใหม่
  int newReading = analogRead(mjoy);

  // เก็บค่าใน array
  readings[index] = newReading;
  index = (index + 1) % 5;

  if (!initialized) {
    // เติมค่าให้เต็ม array ก่อน
    for (int i = 0; i < 5; i++) {
      readings[i] = newReading;
    }
    initialized = true;
  }

  // คำนวณค่าเฉลี่ย
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += readings[i];
  }
  int average = sum / 5;

  // Log ค่า raw ทุก 10 วินาที (ลด logging)
  if (DEBUG_KEYPAD_DETAIL) {
    uint32_t now = millis();
    if (now - lastLogTime > 10000) {
      Serial.printf("[KEYPAD] Raw: %d, Stable: %d\n", newReading, average);
      lastLogTime = now;
    }
  }

  return average;
}

// ฟังก์ชันรอให้ปล่อยปุ่ม
void waitForKeyRelease() {
  uint32_t startTime = millis();
  int stableCount = 0;
  int lastVal = -1;

  Serial.println("[KEYPAD] Waiting for key release...");

  while (stableCount < 3) {  // ต้องอ่านค่าติดต่อกัน 10 ครั้ง
    int val = readKeypadStable();

    // Log ค่าเมื่อเปลี่ยนแปลง
    if (val != lastVal) {
      lastVal = val;
    }

    if (val >= KEY_NONE - KEY_TOLERANCE) {
      stableCount++;
    } else {
      stableCount = 0;
    }

    delay(20);  // หน่วง 20ms

    // กัน infinite loop
    if (millis() - startTime > 3000) {
      Serial.println("[KEYPAD] Timeout waiting for key release");
      break;
    }
  }

  Serial.printf("[KEYPAD] Key released after %d ms\n", millis() - startTime);
}
