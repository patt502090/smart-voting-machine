/*
 * Advanced Analog Keypad Test v2.0
 * สำหรับทดสอบ AD Keyboard module 5 ปุ่มแบบละเอียด
 * 
 * Features:
 * - Real-time monitoring
 * - Auto calibration
 * - Statistics analysis
 * - Noise analysis
 * - Interactive menu
 */

#include <math.h>

// Pin configuration
const int ANALOG_PIN = 35;  // GPIO 35 สำหรับอ่าน analog keypad

// Current keypad configuration (can be updated via calibration)
int KEY_VALUES[6] = {0, 1024, 2048, 3072, 4064, 4095};
String KEY_NAMES[6] = {"KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5", "NONE"};
int TOLERANCE = 100;

// Smoothing variables
const int SMOOTH_SAMPLES = 10;
int readings[SMOOTH_SAMPLES];
int readIndex = 0;
int total = 0;
bool smoothingInitialized = false;

// Change detection
int lastDetectedKey = -1;
unsigned long lastChangeTime = 0;
const unsigned long DEBOUNCE_TIME = 200;

// Menu state
bool inMenu = true;
bool continuousMode = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=========================================");
  Serial.println("   Advanced Analog Keypad Test v2.0    ");
  Serial.println("=========================================");
  Serial.println("ESP32 GPIO 35 - 5-Button Analog Keypad");
  Serial.println("");
  
  pinMode(ANALOG_PIN, INPUT);
  initializeSmoothing();
  
  showMenu();
}

void loop() {
  if (inMenu) {
    handleMenu();
  } else if (continuousMode) {
    handleContinuousMode();
  } else {
    handleNormalMode();
  }
  
  delay(10);
}

void initializeSmoothing() {
  int initialValue = analogRead(ANALOG_PIN);
  for (int i = 0; i < SMOOTH_SAMPLES; i++) {
    readings[i] = initialValue;
  }
  total = initialValue * SMOOTH_SAMPLES;
  smoothingInitialized = true;
}

int getSmoothedReading() {
  int rawValue = analogRead(ANALOG_PIN);
  
  // Update smoothing array
  total = total - readings[readIndex];
  readings[readIndex] = rawValue;
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % SMOOTH_SAMPLES;
  
  return total / SMOOTH_SAMPLES;
}

int detectKey(int value) {
  int bestMatch = -1;
  int minDiff = 9999;
  
  for (int i = 0; i < 6; i++) {
    int diff = abs(value - KEY_VALUES[i]);
    if (diff <= TOLERANCE && diff < minDiff) {
      minDiff = diff;
      bestMatch = i;
    }
  }
  
  return bestMatch;
}

void showMenu() {
  Serial.println("\n========== MENU ==========");
  Serial.println("1. Normal monitoring");
  Serial.println("2. Continuous mode");
  Serial.println("3. Auto calibration");
  Serial.println("4. Manual calibration");
  Serial.println("5. Statistics analysis");
  Serial.println("6. Noise analysis");
  Serial.println("7. Current configuration");
  Serial.println("8. Test all keys");
  Serial.println("9. Export configuration");
  Serial.println("r. Reset to defaults");
  Serial.println("h. Show this help");
  Serial.println("==========================");
  Serial.println("Enter your choice:");
}

void handleMenu() {
  if (Serial.available()) {
    char choice = Serial.read();
    
    // Clear buffer
    while (Serial.available()) {
      Serial.read();
    }
    
    switch (choice) {
      case '1':
        inMenu = false;
        continuousMode = false;
        Serial.println("\n=== Normal Monitoring Mode ===");
        Serial.println("Press keys to see detection. Type 'm' to return to menu.");
        break;
        
      case '2':
        inMenu = false;
        continuousMode = true;
        Serial.println("\n=== Continuous Mode ===");
        Serial.println("Real-time values. Type 'm' to return to menu.");
        break;
        
      case '3':
        autoCalibration();
        break;
        
      case '4':
        manualCalibration();
        break;
        
      case '5':
        statisticsAnalysis();
        break;
        
      case '6':
        noiseAnalysis();
        break;
        
      case '7':
        showCurrentConfig();
        break;
        
      case '8':
        testAllKeys();
        break;
        
      case '9':
        exportConfiguration();
        break;
        
      case 'r':
      case 'R':
        resetToDefaults();
        break;
        
      case 'h':
      case 'H':
        showMenu();
        break;
        
      default:
        Serial.println("Invalid choice. Type 'h' for help.");
        break;
    }
  }
}

void handleNormalMode() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'm' || cmd == 'M') {
      inMenu = true;
      showMenu();
      return;
    }
  }
  
  int smoothed = getSmoothedReading();
  int detectedKey = detectKey(smoothed);
  
  if (detectedKey != lastDetectedKey) {
    unsigned long currentTime = millis();
    if (currentTime - lastChangeTime > DEBOUNCE_TIME) {
      lastChangeTime = currentTime;
      lastDetectedKey = detectedKey;
      
      Serial.printf("Value: %4d | ", smoothed);
      if (detectedKey >= 0) {
        int error = smoothed - KEY_VALUES[detectedKey];
        Serial.printf("Key: %-6s | Expected: %4d | Error: %+4d\n", 
                     KEY_NAMES[detectedKey].c_str(), KEY_VALUES[detectedKey], error);
      } else {
        Serial.println("Key: UNKNOWN");
      }
    }
  }
}

void handleContinuousMode() {
  static unsigned long lastPrint = 0;
  
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'm' || cmd == 'M') {
      inMenu = true;
      showMenu();
      return;
    }
  }
  
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    
    int raw = analogRead(ANALOG_PIN);
    int smoothed = getSmoothedReading();
    int detectedKey = detectKey(smoothed);
    
    Serial.printf("Time: %8lu | Raw: %4d | Smooth: %4d | Key: ", 
                 millis(), raw, smoothed);
    
    if (detectedKey >= 0) {
      Serial.printf("%-6s", KEY_NAMES[detectedKey].c_str());
    } else {
      Serial.printf("UNKNOWN");
    }
    Serial.println();
  }
}

void autoCalibration() {
  Serial.println("\n=== Auto Calibration ===");
  Serial.println("This will automatically detect key values.");
  Serial.println("Make sure NO keys are pressed. Press Enter to start...");
  
  waitForEnter();
  
  // Step 1: Detect NONE value
  Serial.println("Step 1: Detecting NONE value (no keys pressed)...");
  int noneValue = collectSamples(100, 50);
  KEY_VALUES[5] = noneValue;
  Serial.printf("NONE value detected: %d\n", noneValue);
  
  // Step 2: Detect other keys
  for (int i = 0; i < 5; i++) {
    Serial.printf("\nStep %d: Press and hold %s, then press Enter...", 
                 i + 2, KEY_NAMES[i].c_str());
    waitForEnter();
    
    Serial.printf("Detecting %s value...", KEY_NAMES[i].c_str());
    int keyValue = collectSamples(100, 50);
    KEY_VALUES[i] = keyValue;
    Serial.printf(" %d\n", keyValue);
  }
  
  // Calculate optimal tolerance
  calculateOptimalTolerance();
  
  Serial.println("\n=== Auto Calibration Complete ===");
  showCurrentConfig();
  
  Serial.println("Press Enter to return to menu...");
  waitForEnter();
}

void manualCalibration() {
  Serial.println("\n=== Manual Calibration ===");
  
  for (int i = 0; i < 6; i++) {
    Serial.printf("Current %s value: %d\n", KEY_NAMES[i].c_str(), KEY_VALUES[i]);
    Serial.printf("Enter new value (or press Enter to keep current): ");
    
    String input = readLine();
    if (input.length() > 0) {
      int newValue = input.toInt();
      if (newValue >= 0 && newValue <= 4095) {
        KEY_VALUES[i] = newValue;
        Serial.printf("Updated %s to %d\n", KEY_NAMES[i].c_str(), newValue);
      } else {
        Serial.println("Invalid value. Keeping current.");
      }
    }
  }
  
  Serial.printf("\nCurrent tolerance: %d\n", TOLERANCE);
  Serial.printf("Enter new tolerance (or press Enter to keep current): ");
  String input = readLine();
  if (input.length() > 0) {
    int newTolerance = input.toInt();
    if (newTolerance > 0 && newTolerance <= 500) {
      TOLERANCE = newTolerance;
      Serial.printf("Updated tolerance to %d\n", TOLERANCE);
    }
  }
  
  Serial.println("Manual calibration complete.");
}

void statisticsAnalysis() {
  Serial.println("\n=== Statistics Analysis ===");
  Serial.println("Press and hold a key, then press Enter to analyze...");
  
  waitForEnter();
  
  Serial.println("Collecting 1000 samples...");
  
  int samples[1000];
  for (int i = 0; i < 1000; i++) {
    samples[i] = analogRead(ANALOG_PIN);
    if (i % 100 == 0) {
      Serial.printf("Progress: %d%%\n", i/10);
    }
    delay(5);
  }
  
  // Calculate statistics
  int minVal = samples[0];
  int maxVal = samples[0];
  long sum = 0;
  
  for (int i = 0; i < 1000; i++) {
    if (samples[i] < minVal) minVal = samples[i];
    if (samples[i] > maxVal) maxVal = samples[i];
    sum += samples[i];
  }
  
  double average = (double)sum / 1000.0;
  
  // Calculate standard deviation
  double sumSquaredDiffs = 0;
  for (int i = 0; i < 1000; i++) {
    double diff = samples[i] - average;
    sumSquaredDiffs += diff * diff;
  }
  
  double stdDev = sqrt(sumSquaredDiffs / 1000.0);
  
  Serial.println("\n=== Results ===");
  Serial.printf("Samples: 1000\n");
  Serial.printf("Min: %d\n", minVal);
  Serial.printf("Max: %d\n", maxVal);
  Serial.printf("Average: %.2f\n", average);
  Serial.printf("Std Dev: %.2f\n", stdDev);
  Serial.printf("Range: %d\n", maxVal - minVal);
  Serial.printf("Noise Level: %.2f%%\n", (stdDev / average) * 100);
  
  Serial.println("\nPress Enter to continue...");
  waitForEnter();
}

void noiseAnalysis() {
  Serial.println("\n=== Noise Analysis ===");
  Serial.println("This will analyze noise for each key position.");
  
  for (int keyIdx = 0; keyIdx < 6; keyIdx++) {
    if (keyIdx == 5) {
      Serial.printf("\nAnalyzing %s (don't press any key)...", KEY_NAMES[keyIdx].c_str());
    } else {
      Serial.printf("\nPress and hold %s...", KEY_NAMES[keyIdx].c_str());
    }
    Serial.println(" Press Enter when ready.");
    
    waitForEnter();
    
    Serial.println("Collecting 200 samples...");
    
    int samples[200];
    for (int i = 0; i < 200; i++) {
      samples[i] = analogRead(ANALOG_PIN);
      delay(10);
    }
    
    // Calculate statistics for this key
    int minVal = samples[0];
    int maxVal = samples[0];
    long sum = 0;
    
    for (int i = 0; i < 200; i++) {
      if (samples[i] < minVal) minVal = samples[i];
      if (samples[i] > maxVal) maxVal = samples[i];
      sum += samples[i];
    }
    
    double average = (double)sum / 200.0;
    
    double sumSquaredDiffs = 0;
    for (int i = 0; i < 200; i++) {
      double diff = samples[i] - average;
      sumSquaredDiffs += diff * diff;
    }
    
    double stdDev = sqrt(sumSquaredDiffs / 200.0);
    
    Serial.printf("%s: Avg=%.1f, StdDev=%.2f, Range=%d\n", 
                 KEY_NAMES[keyIdx].c_str(), average, stdDev, maxVal - minVal);
  }
  
  Serial.println("\nNoise analysis complete. Press Enter to continue...");
  waitForEnter();
}

void testAllKeys() {
  Serial.println("\n=== Test All Keys ===");
  Serial.println("Press each key when prompted to verify detection:");
  
  for (int i = 0; i < 6; i++) {
    if (i == 5) {
      Serial.printf("\nTest %s: Don't press any key, then press Enter...", KEY_NAMES[i].c_str());
    } else {
      Serial.printf("\nTest %s: Press and hold key %d, then press Enter...", KEY_NAMES[i].c_str(), i + 1);
    }
    
    waitForEnter();
    
    int reading = getSmoothedReading();
    int detectedKey = detectKey(reading);
    
    Serial.printf("Reading: %d | Expected: %d | ", reading, KEY_VALUES[i]);
    
    if (detectedKey == i) {
      Serial.printf("✓ PASS - Detected %s\n", KEY_NAMES[i].c_str());
    } else if (detectedKey >= 0) {
      Serial.printf("✗ FAIL - Detected %s instead\n", KEY_NAMES[detectedKey].c_str());
    } else {
      Serial.printf("✗ FAIL - No key detected\n");
    }
  }
  
  Serial.println("\nTest complete. Press Enter to continue...");
  waitForEnter();
}

void showCurrentConfig() {
  Serial.println("\n=== Current Configuration ===");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%-6s = %4d\n", KEY_NAMES[i].c_str(), KEY_VALUES[i]);
  }
  Serial.printf("TOLERANCE = %d\n", TOLERANCE);
}

void exportConfiguration() {
  Serial.println("\n=== Export Configuration ===");
  Serial.println("Copy these lines to your main code:");
  Serial.println();
  
  for (int i = 0; i < 5; i++) {
    Serial.printf("const int KEY_%d = %d;\n", i + 1, KEY_VALUES[i]);
  }
  Serial.printf("const int KEY_NONE = %d;\n", KEY_VALUES[5]);
  Serial.printf("const int KEY_TOLERANCE = %d;\n", TOLERANCE);
  
  Serial.println();
  Serial.println("Or use these assignments:");
  for (int i = 0; i < 5; i++) {
    Serial.printf("KEY_%d = %d;\n", i + 1, KEY_VALUES[i]);
  }
  Serial.printf("KEY_NONE = %d;\n", KEY_VALUES[5]);
  Serial.printf("KEY_TOLERANCE = %d;\n", TOLERANCE);
  
  Serial.println("\nPress Enter to continue...");
  waitForEnter();
}

void resetToDefaults() {
  Serial.println("\n=== Reset to Defaults ===");
  KEY_VALUES[0] = 0;     // KEY_1
  KEY_VALUES[1] = 1024;  // KEY_2
  KEY_VALUES[2] = 2048;  // KEY_3
  KEY_VALUES[3] = 3072;  // KEY_4
  KEY_VALUES[4] = 4064;  // KEY_5
  KEY_VALUES[5] = 4095;  // KEY_NONE
  TOLERANCE = 100;
  
  Serial.println("Configuration reset to defaults.");
  showCurrentConfig();
}

int collectSamples(int numSamples, int delayMs) {
  long sum = 0;
  
  for (int i = 0; i < numSamples; i++) {
    sum += analogRead(ANALOG_PIN);
    delay(delayMs);
  }
  
  return sum / numSamples;
}

void calculateOptimalTolerance() {
  // Find minimum distance between any two key values
  int minDistance = 4095;
  
  for (int i = 0; i < 6; i++) {
    for (int j = i + 1; j < 6; j++) {
      int distance = abs(KEY_VALUES[i] - KEY_VALUES[j]);
      if (distance < minDistance) {
        minDistance = distance;
      }
    }
  }
  
  // Set tolerance to 1/3 of minimum distance, but not less than 20 or more than 200
  TOLERANCE = minDistance / 3;
  if (TOLERANCE < 20) TOLERANCE = 20;
  if (TOLERANCE > 200) TOLERANCE = 200;
  
  Serial.printf("Optimal tolerance calculated: %d (min distance: %d)\n", TOLERANCE, minDistance);
}

void waitForEnter() {
  while (!Serial.available()) {
    delay(10);
  }
  while (Serial.available()) {
    Serial.read();
  }
}

String readLine() {
  String input = "";
  
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        break;
      }
      input += c;
    }
    delay(10);
  }
  
  return input;
}
