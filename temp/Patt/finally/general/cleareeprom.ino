#include <EEPROM.h>

const size_t EEPROM_SIZE = 4096;   // เลือกให้ครอบคลุมที่เคยใช้ (สูงสุด ~4096 บน ESP32)

void wipeEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  for (size_t i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);         // ค่าแฟลชตอนถูกล้างปกติคือ 0xFF (จะใช้ 0 ก็ได้)
  }
  EEPROM.commit();                 // สำคัญมาก ต้อง commit ถึงจะเขียนจริง
  EEPROM.end();
}

void setup() { wipeEEPROM(); }
void loop() {}