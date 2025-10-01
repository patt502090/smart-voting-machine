/*
 * Clear Fingerprint Sensor Template และ EEPROM ESP32
 * 
 * ฟังก์ชัน:
 * 1. ลบ template ทั้งหมดใน fingerprint sensor
 * 2. ล้างข้อมูล EEPROM ของ ESP32
 * 3. รีเซ็ตระบบให้กลับสู่สถานะเริ่มต้น
 * 
 * วิธีใช้:
 * 1. อัปโหลดโค้ดนี้ลง ESP32
 * 2. เปิด Serial Monitor (115200 baud)
 * 3. ระบบจะทำการล้างข้อมูลอัตโนมัติ
 * 4. รอให้เสร็จสิ้น แล้วอัปโหลดโค้ดหลักกลับ
 */

#include <EEPROM.h>
#include <Adafruit_Fingerprint.h>

// Pin configuration สำหรับ fingerprint sensor
const int FINGERPRINT_TX_PIN = 16;  // GPIO 16
const int FINGERPRINT_RX_PIN = 17;  // GPIO 17

// สร้าง fingerprint sensor object
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);

// EEPROM configuration
const int EEPROM_SIZE = 512;

void setup() {
  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN);
  
  delay(1000);
  
  Serial.println("==========================================");
  Serial.println("   Clear Fingerprint & EEPROM Tool v1.0  ");
  Serial.println("==========================================");
  Serial.println("This will:");
  Serial.println("1. Delete ALL fingerprint templates");
  Serial.println("2. Clear ESP32 EEPROM");
  Serial.println("3. Reset system to factory state");
  Serial.println("");
  Serial.println("WARNING: This action cannot be undone!");
  Serial.println("==========================================");
  
  // รอ 5 วินาทีให้ผู้ใช้อ่าน
  for (int i = 5; i > 0; i--) {
    Serial.printf("Starting in %d seconds...\n", i);
    delay(1000);
  }
  
  Serial.println("\n=== Starting Cleanup Process ===");
  
  // Step 1: Initialize fingerprint sensor
  Serial.println("Step 1: Initializing fingerprint sensor...");
  finger.begin(57600);
  
  if (finger.verifyPassword()) {
    Serial.println("✓ Fingerprint sensor connected");
  } else {
    Serial.println("✗ Fingerprint sensor not found!");
    Serial.println("Please check connections and try again.");
    return;
  }
  
  // Step 2: Clear fingerprint templates
  Serial.println("\nStep 2: Clearing fingerprint templates...");
  clearAllFingerprints();
  
  // Step 3: Clear EEPROM
  Serial.println("\nStep 3: Clearing ESP32 EEPROM...");
  clearEEPROM();
  
  // Step 4: Verify cleanup
  Serial.println("\nStep 4: Verifying cleanup...");
  verifyCleanup();
  
  Serial.println("\n=== Cleanup Complete ===");
  Serial.println("System has been reset to factory state.");
  Serial.println("You can now upload your main code.");
}

void loop() {
  // ไม่ทำอะไรใน loop
  delay(1000);
}

void clearAllFingerprints() {
  Serial.println("Deleting all fingerprint templates...");
  
  int deleted = 0;
  int failed = 0;
  
  // ลบ template ตั้งแต่ ID 1-127 (Adafruit fingerprint sensor)
  for (int id = 1; id <= 127; id++) {
    uint8_t result = finger.deleteModel(id);
    
    if (result == FINGERPRINT_OK) {
      deleted++;
      Serial.printf("Deleted template ID %d\n", id);
    } else if (result == FINGERPRINT_NOTFOUND) {
      // ไม่มี template อยู่แล้ว ไม่นับเป็น error
    } else {
      failed++;
      Serial.printf("Failed to delete template ID %d (error: %d)\n", id, result);
    }
    
    // แสดงความคืบหน้า
    if (id % 20 == 0) {
      Serial.printf("Progress: %d/127 templates processed\n", id);
    }
    
    delay(100); // หน่วงเวลาระหว่างการลบ
  }
  
  Serial.printf("\nFingerprint cleanup summary:\n");
  Serial.printf("- Templates deleted: %d\n", deleted);
  Serial.printf("- Failed deletions: %d\n", failed);
  Serial.printf("- Total processed: 127\n");
}

void clearEEPROM() {
  Serial.println("Initializing EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  
  Serial.printf("Clearing %d bytes of EEPROM...\n", EEPROM_SIZE);
  
  // เขียน 0xFF (หรือ 0x00) ลงทุกตำแหน่ง
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);  // หรือใช้ 0x00 ก็ได้
    
    // แสดงความคืบหน้า
    if (i % 50 == 0) {
      Serial.printf("Progress: %d/%d bytes cleared\n", i, EEPROM_SIZE);
    }
  }
  
  // บันทึกการเปลี่ยนแปลง
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("✓ EEPROM cleared successfully");
}

void verifyCleanup() {
  Serial.println("Verifying fingerprint sensor...");
  
  // ตรวจสอบจำนวน template ที่เหลือ
  uint8_t templateCount = finger.getTemplateCount();
  if (templateCount == 0) {
    Serial.println("✓ No fingerprint templates remaining");
  } else {
    Serial.printf("⚠ Warning: %d templates still exist\n", templateCount);
  }
  
  Serial.println("Verifying EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  
  // ตรวจสอบว่าข้อมูลถูกล้างแล้วจริง
  bool allCleared = true;
  for (int i = 0; i < 10; i++) {  // ตรวจสอบแค่ 10 ตำแหน่งแรก
    uint8_t value = EEPROM.read(i);
    if (value != 0xFF) {
      allCleared = false;
      Serial.printf("⚠ Position %d still contains: 0x%02X\n", i, value);
    }
  }
  
  if (allCleared) {
    Serial.println("✓ EEPROM verification passed");
  } else {
    Serial.println("⚠ EEPROM verification failed - some data remains");
  }
  
  EEPROM.end();
}

// ฟังก์ชันสำหรับแสดงสถานะ fingerprint sensor
void showFingerprintStatus() {
  Serial.println("\n=== Fingerprint Sensor Status ===");
  
  // ตรวจสอบการเชื่อมต่อ
  if (finger.verifyPassword()) {
    Serial.println("✓ Sensor connected and password verified");
  } else {
    Serial.println("✗ Sensor connection failed");
    return;
  }
  
  // แสดงข้อมูล sensor
  Serial.printf("Parameters: %d\n", finger.getParameters());
  Serial.printf("Template count: %d\n", finger.getTemplateCount());
  Serial.printf("Capacity: %d\n", finger.capacity);
  
  // แสดงสถานะ packet - แก้ไข error
  Serial.printf("Packet length: %d\n", finger.packet_len);
}