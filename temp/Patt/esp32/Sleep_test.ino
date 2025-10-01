#include <Arduino.h>
#include "esp_sleep.h"
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nBoot");
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  Serial.printf("Wake cause=%d (6=EXT1, 2=TIMER, 0=POWERON)\n", (int)c);

  // ตั้งนอน 10 วิ ไม่มีแหล่งปลุกอื่น
  esp_sleep_enable_timer_wakeup(10000000ULL);
  Serial.println("Sleep 10s...");
  Serial.flush();
  delay(50);
  esp_deep_sleep_start();
}
void loop(){}