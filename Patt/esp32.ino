#include <Arduino.h>
#include <HardwareSerial.h>

// ===== UART2 จาก UNO =====
HardwareSerial VOTE(2);      // UART2 on classic ESP32
#define RX_PIN 16
#define TX_PIN 17
#define BAUD   115200

// ===== LED แจ้งสถานะ (ถ้าไม่มี LED_BUILTIN ให้ใช้ GPIO2) =====
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ===== ตัวนับคะแนน =====
int tally[10] = {0};
int totalVotes = 0;

// ===== บัฟเฟอร์รับบรรทัด =====
String lineBuf;

// ===== จัดจังหวะพิมพ์สรุปทุก ๆ interval =====
unsigned long lastDash = 0;
const unsigned long DASH_MS = 2000;

// ===== ยูทิล =====
void blinkLED(uint16_t ms=40) {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(ms);
  digitalWrite(LED_BUILTIN, LOW);
}

void printHelp() {
  Serial.println(F("\n--- Smart Voting ESP32 (Serial Dashboard) ---"));
  Serial.println(F("คำสั่ง:"));
  Serial.println(F("  h / help      = แสดงคำสั่ง"));
  Serial.println(F("  s / stats     = แสดงสรุปคะแนน"));
  Serial.println(F("  r / reset     = รีเซ็ตตัวนับทั้งหมดเป็นศูนย์"));
  Serial.println(F("  csv           = พิมพ์หัว csv + แถวคะแนนปัจจุบัน"));
  Serial.println(F("  demo N        = สุ่มโหวต N ครั้ง (สำหรับทดสอบ)"));
  Serial.println(F("หมายเหตุ: ตั้ง Serial Monitor -> 115200 และส่ง Newline (\\n)\n"));
}

void printDashboard() {
  Serial.println(F("\n======= LIVE TALLY ======="));
  Serial.println(F("Choice : Count    Bar"));
  Serial.println(F("--------------------------"));
  int maxv = 1;
  for (int i=0;i<10;i++) if (tally[i] > maxv) maxv = tally[i];

  for (int i=0;i<10;i++) {
    Serial.print("   ");
    Serial.print(i);
    Serial.print("   : ");
    if (tally[i] < 10) Serial.print(' ');
    Serial.print(tally[i]);
    Serial.print("      ");
    // วาดแท่งสัดส่วนความยาวสูงสุด 40
    int bar = (maxv == 0) ? 0 : ( (long)tally[i] * 40 / maxv );
    for (int b=0;b<bar;b++) Serial.print('#');
    Serial.println();
  }
  Serial.println(F("--------------------------"));
  Serial.print (F("TOTAL = "));
  Serial.println(totalVotes);
  Serial.println(F("==========================\n"));
}

void printCSVHeader() {
  Serial.print(F("timestamp"));
  for (int i=0;i<10;i++) { Serial.print(F(",\"")); Serial.print(i); Serial.print(F("\"")); }
  Serial.println(F(",\"total\""));
}

void printCSVRow() {
  Serial.print(millis());
  for (int i=0;i<10;i++) { Serial.print(','); Serial.print(tally[i]); }
  Serial.print(','); Serial.println(totalVotes);
}

void handleVoteDigit(int d) {
  if (d < 0 || d > 9) return;
  tally[d]++; totalVotes++;
  blinkLED(20);
  Serial.print(F("[VOTE] CF:")); Serial.print(d);
  Serial.print(F("  | total=")); Serial.println(totalVotes);
}

void handleLineFromUNO(const String& s) {
  // รองรับรูปแบบ: CF:X
  if (s.startsWith("CF:") && s.length() >= 4) {
    char c = s.charAt(3);
    if (c >= '0' && c <= '9') {
      handleVoteDigit(c - '0');
      return;
    }
  }
  // อย่างอื่นพิมพ์ผ่านเฉย ๆ (ดีบัก)
  Serial.print(F("[PASS] ")); Serial.println(s);
}

void handleHostCommand(const String& cmd) {
  // ตัดช่องว่างหัว/ท้าย
  String s = cmd; s.trim();
  if (s == "h" || s == "help" || s == "?") { printHelp(); return; }
  if (s == "s" || s == "stats") { printDashboard(); return; }
  if (s == "r" || s == "reset") {
    for (int i=0;i<10;i++) tally[i]=0; totalVotes=0;
    Serial.println(F("[OK] reset counters"));
    printDashboard();
    return;
  }
  if (s == "csv") { printCSVHeader(); printCSVRow(); return; }
  if (s.startsWith("demo")) {
    // demo N
    int sp = s.indexOf(' ');
    int N = (sp>0) ? s.substring(sp+1).toInt() : 20;
    if (N <= 0) N = 20;
    Serial.print(F("[DEMO] generating ")); Serial.print(N); Serial.println(F(" votes..."));
    for (int i=0;i<N;i++) { handleVoteDigit(random(0,10)); delay(10); }
    printDashboard();
    return;
  }
  Serial.println(F("[ERR] unknown command (พิมพ์ help เพื่อดูคำสั่ง)"));
}

void setup() {
  Serial.begin(BAUD);
  delay(50);
  VOTE.begin(BAUD, SERIAL_8N1, RX_PIN, TX_PIN);  // รับจาก UNO ทาง UART2
  pinMode(LED_BUILTIN, OUTPUT);
  randomSeed(esp_random());

  Serial.println(F("ESP32 READY. Waiting CF:X from UNO on UART2 (GPIO16 RX)..."));
  printHelp();
}

void loop() {
  // อ่านจาก UNO (UART2)
  while (VOTE.available()) {
    char c = (char)VOTE.read();
    if (c == '\n') {
      lineBuf.trim();
      if (lineBuf.length()) handleLineFromUNO(lineBuf);
      lineBuf = "";
    } else if (c != '\r') {
      lineBuf += c;
      if (lineBuf.length() > 120) lineBuf.remove(0); // กันบัฟเฟอร์ล้น
    }
  }

  // อ่านคำสั่งจากคอม (USB Serial)
  static String hostBuf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      hostBuf.trim();
      if (hostBuf.length()) handleHostCommand(hostBuf);
      hostBuf = "";
    } else if (c != '\r') {
      hostBuf += c;
      if (hostBuf.length() > 120) hostBuf.remove(0);
    }
  }

  // พิมพ์แดชบอร์ดอัตโนมัติทุก 2 วินาที (ถ้ามีโหวตเข้ามา)
  static int lastTotalShown = -1;
  unsigned long now = millis();
  if ((now - lastDash >= DASH_MS) && (totalVotes != lastTotalShown)) {
    lastDash = now;
    lastTotalShown = totalVotes;
    printDashboard();
  }
}