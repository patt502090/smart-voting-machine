/*
 * Debug Keypad Test - สำหรับแก้ปัญหา keypad ที่ค่าไม่เสถียร
 */

const int ANALOG_PIN = 35;

// ลดค่า tolerance ลง
const int TIGHT_TOLERANCE = 30;

// Variables for voltage monitoring
float supplyVoltage = 3.3; // ESP32 reference voltage

void setup() {
  Serial.begin(115200);
  pinMode(ANALOG_PIN, INPUT);
  
  Serial.println("=== Debug Keypad Test ===");
  Serial.println("Commands:");
  Serial.println("v - Show voltage readings");
  Serial.println("r - Raw value monitoring");
  Serial.println("c - Continuous monitoring");
  Serial.println("s - Statistics (100 samples)");
  Serial.println("t - Tight tolerance test");
  Serial.println("=====================================");
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case 'v':
        showVoltageReadings();
        break;
      case 'r':
        rawValueMonitoring();
        break;
      case 'c':
        continuousMonitoring();
        break;
      case 's':
        statisticsTest();
        break;
      case 't':
        tightToleranceTest();
        break;
    }
  }
  
  delay(100);
}

void showVoltageReadings() {
  Serial.println("\n=== Voltage Readings ===");
  Serial.println("Press each key and observe voltage:");
  
  for (int i = 0; i < 20; i++) {
    int raw = analogRead(ANALOG_PIN);
    float voltage = (raw / 4095.0) * supplyVoltage;
    
    Serial.printf("Sample %2d: Raw=%4d, Voltage=%.3fV\n", i+1, raw, voltage);
    delay(500);
  }
  
  Serial.println("Voltage test complete.\n");
}

void rawValueMonitoring() {
  Serial.println("\n=== Raw Value Monitoring ===");
  Serial.println("Press any key to stop...");
  
  unsigned long lastPrint = 0;
  int minVal = 4095, maxVal = 0;
  long sum = 0;
  int count = 0;
  
  while (!Serial.available()) {
    int raw = analogRead(ANALOG_PIN);
    
    if (raw < minVal) minVal = raw;
    if (raw > maxVal) maxVal = raw;
    sum += raw;
    count++;
    
    if (millis() - lastPrint > 100) {
      lastPrint = millis();
      float avg = (float)sum / count;
      
      Serial.printf("Raw: %4d | Min: %4d | Max: %4d | Avg: %6.1f | Range: %4d | Noise: %6.1f%%\n", 
                   raw, minVal, maxVal, avg, maxVal-minVal, ((maxVal-minVal)/avg)*100);
    }
    
    delay(10);
  }
  
  // Clear serial buffer
  while (Serial.available()) Serial.read();
  Serial.println("Raw monitoring stopped.\n");
}

void continuousMonitoring() {
  Serial.println("\n=== Continuous Monitoring ===");
  Serial.println("Showing all values. Press any key to stop...");
  
  unsigned long lastPrint = 0;
  
  while (!Serial.available()) {
    if (millis() - lastPrint > 200) {
      lastPrint = millis();
      
      int raw = analogRead(ANALOG_PIN);
      float voltage = (raw / 4095.0) * supplyVoltage;
      
      Serial.printf("Time: %8lu | Raw: %4d | Voltage: %.3fV | ", millis(), raw, voltage);
      
      // Try to detect key with tight tolerance
      String keyDetected = detectKeyTight(raw);
      Serial.printf("Key: %s\n", keyDetected.c_str());
    }
    
    delay(10);
  }
  
  while (Serial.available()) Serial.read();
  Serial.println("Continuous monitoring stopped.\n");
}

void statisticsTest() {
  Serial.println("\n=== Statistics Test ===");
  Serial.println("Hold a key stable and press Enter...");
  
  // Wait for Enter
  while (!Serial.available()) delay(10);
  while (Serial.available()) Serial.read();
  
  Serial.println("Collecting 100 samples...");
  
  int samples[100];
  for (int i = 0; i < 100; i++) {
    samples[i] = analogRead(ANALOG_PIN);
    delay(20);
  }
  
  // Calculate statistics
  int minVal = samples[0];
  int maxVal = samples[0];
  long sum = 0;
  
  for (int i = 0; i < 100; i++) {
    if (samples[i] < minVal) minVal = samples[i];
    if (samples[i] > maxVal) maxVal = samples[i];
    sum += samples[i];
  }
  
  float average = (float)sum / 100.0;
  
  // Calculate standard deviation
  float sumSquaredDiffs = 0;
  for (int i = 0; i < 100; i++) {
    float diff = samples[i] - average;
    sumSquaredDiffs += diff * diff;
  }
  
  float stdDev = sqrt(sumSquaredDiffs / 100.0);
  
  Serial.printf("Results:\n");
  Serial.printf("Min: %d\n", minVal);
  Serial.printf("Max: %d\n", maxVal);
  Serial.printf("Average: %.2f\n", average);
  Serial.printf("Std Dev: %.2f\n", stdDev);
  Serial.printf("Range: %d\n", maxVal - minVal);
  Serial.printf("Stability: %.2f%% (lower is better)\n", (stdDev/average)*100);
  
  if (stdDev > 20) {
    Serial.println("WARNING: High noise detected! Check wiring.");
  } else if (stdDev > 10) {
    Serial.println("CAUTION: Moderate noise. Consider filtering.");
  } else {
    Serial.println("GOOD: Low noise signal.");
  }
  
  Serial.println();
}

void tightToleranceTest() {
  Serial.println("\n=== Tight Tolerance Test ===");
  Serial.println("Testing with tolerance = 30");
  Serial.println("Press keys to test detection...");
  
  unsigned long testStart = millis();
  int lastDetectedValue = -1;
  
  while (millis() - testStart < 30000) { // 30 second test
    if (Serial.available()) break;
    
    int raw = analogRead(ANALOG_PIN);
    
    if (abs(raw - lastDetectedValue) > 20) { // Only show changes
      lastDetectedValue = raw;
      
      String key = detectKeyTight(raw);
      Serial.printf("Raw: %4d | Key: %s\n", raw, key.c_str());
    }
    
    delay(50);
  }
  
  while (Serial.available()) Serial.read();
  Serial.println("Tight tolerance test complete.\n");
}

String detectKeyTight(int value) {
  // Try with much tighter ranges based on your readings
  if (value < 100) {
    return "KEY_1 (0V)";
  } else if (value > 300 && value < 600) {
    return "MAYBE_KEY_2";
  } else if (value > 800 && value < 1300) {
    return "MAYBE_KEY_2 (1.2V)";
  } else if (value > 1800 && value < 2300) {
    return "MAYBE_KEY_3 (2.4V)";
  } else if (value > 2800 && value < 3300) {
    return "MAYBE_KEY_4 (3.6V)";
  } else if (value > 3800 && value < 4200) {
    if (value > 4000) {
      return "NONE (5V)";
    } else {
      return "MAYBE_KEY_5 (4.8V)";
    }
  } else {
    return "UNKNOWN";
  }
}

// Hardware diagnostic function
void hardwareDiagnostic() {
  Serial.println("\n=== Hardware Diagnostic ===");
  
  // Test if the pin is working at all
  Serial.println("Testing GPIO 35...");
  
  // Read multiple times rapidly
  int readings[10];
  for (int i = 0; i < 10; i++) {
    readings[i] = analogRead(ANALOG_PIN);
    delayMicroseconds(100);
  }
  
  // Check if readings are consistent
  bool consistent = true;
  for (int i = 1; i < 10; i++) {
    if (abs(readings[i] - readings[0]) > 100) {
      consistent = false;
      break;
    }
  }
  
  if (consistent) {
    Serial.println("✓ GPIO 35 appears stable");
  } else {
    Serial.println("✗ GPIO 35 shows rapid fluctuation - possible noise/wiring issue");
  }
  
  // Test voltage reference
  float avgVoltage = 0;
  for (int i = 0; i < 10; i++) {
    avgVoltage += (analogRead(ANALOG_PIN) / 4095.0) * 3.3;
  }
  avgVoltage /= 10;
  
  Serial.printf("Average voltage: %.3fV\n", avgVoltage);
  
  if (avgVoltage < 0.1) {
    Serial.println("✗ Voltage too low - check VCC connection");
  } else if (avgVoltage > 3.2) {
    Serial.println("✗ Voltage too high - check if keypad needs 5V");
  } else {
    Serial.println("✓ Voltage in reasonable range");
  }
  
  Serial.println();
}
