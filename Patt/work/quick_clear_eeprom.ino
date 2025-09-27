/*
 * Quick Clear EEPROM Only
 * สำหรับล้างเฉพาะ EEPROM ESP32 อย่างรวดเร็ว
 */

#include <EEPROM.h>

const int EEPROM_SIZE = 512;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== Quick EEPROM Clear ===");
  Serial.println("Clearing ESP32 EEPROM...");
  
  EEPROM.begin(EEPROM_SIZE);
  
  // เขียน 0xFF ลงทุกตำแหน่ง
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("✓ EEPROM cleared!");
  Serial.println("Upload your main code now.");
}

void loop() {
  delay(1000);
}
