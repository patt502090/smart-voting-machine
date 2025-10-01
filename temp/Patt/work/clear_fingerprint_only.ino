/*
 * Clear Fingerprint Templates Only
 * สำหรับล้างเฉพาะ fingerprint templates
 */

#include <Adafruit_Fingerprint.h>

// Pin configuration
const int FINGERPRINT_TX_PIN = 16;
const int FINGERPRINT_RX_PIN = 17;

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);

void setup() {
  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN);
  
  delay(1000);
  
  Serial.println("=== Clear Fingerprint Templates ===");
  
  finger.begin(57600);
  
  if (finger.verifyPassword()) {
    Serial.println("✓ Fingerprint sensor connected");
  } else {
    Serial.println("✗ Fingerprint sensor not found!");
    return;
  }
  
  Serial.println("Deleting all templates...");
  
  int deleted = 0;
  for (int id = 1; id <= 127; id++) {
    uint8_t result = finger.deleteModel(id);
    
    if (result == FINGERPRINT_OK) {
      deleted++;
      Serial.printf("Deleted ID %d\n", id);
    }
    
    delay(50);
  }
  
  Serial.printf("✓ Deleted %d templates\n", deleted);
  Serial.println("Done!");
}

void loop() {
  delay(1000);
}
