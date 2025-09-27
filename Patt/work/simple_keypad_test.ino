/*
 * Simple Analog Keypad Test
 * เวอร์ชันง่าย ๆ สำหรับทดสอบเร็ว ๆ
 */

const int ANALOG_PIN = 35;

void setup() {
  Serial.begin(115200);
  pinMode(ANALOG_PIN, INPUT);
  
  Serial.println("=== Simple Keypad Test ===");
  Serial.println("GPIO 35 - Press keys to see values");
  Serial.println("===============================");
}

void loop() {
  static int lastValue = -1;
  static unsigned long lastPrint = 0;
  
  int currentValue = analogRead(ANALOG_PIN);
  
  // Print only when value changes significantly or every 2 seconds
  if (abs(currentValue - lastValue) > 50 || millis() - lastPrint > 2000) {
    lastValue = currentValue;
    lastPrint = millis();
    
    Serial.printf("Value: %4d | ", currentValue);
    
    // Simple key detection
    if (currentValue < 100) {
      Serial.println("KEY_1 (0V)");
    } else if (currentValue > 900 && currentValue < 1100) {
      Serial.println("KEY_2 (1.2V)");
    } else if (currentValue > 1900 && currentValue < 2200) {
      Serial.println("KEY_3 (2.4V)");
    } else if (currentValue > 2900 && currentValue < 3200) {
      Serial.println("KEY_4 (3.6V)");
    } else if (currentValue > 3900 && currentValue < 4200) {
      if (currentValue > 4000) {
        Serial.println("NONE (5V)");
      } else {
        Serial.println("KEY_5 (4.8V)");
      }
    } else {
      Serial.println("UNKNOWN");
    }
  }
  
  delay(50);
}
