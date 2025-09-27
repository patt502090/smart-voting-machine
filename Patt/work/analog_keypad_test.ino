    /*
    * Analog Keypad Test - สำหรับทดสอบ AD Keyboard module 5 ปุ่ม
    * ใช้ GPIO 35 สำหรับอ่านค่า analog
    * 
    * วิธีใช้:
    * 1. อัปโหลดโค้ดนี้ลง ESP32
    * 2. เปิด Serial Monitor (115200 baud)
    * 3. กดปุ่มแต่ละปุ่มเพื่อดูค่า analog
    * 4. จดค่าที่ได้แล้วนำไปปรับใน main code
    */

    // Pin configuration
    const int ANALOG_PIN = 35;  // GPIO 35 สำหรับอ่าน analog keypad

    // Expected values (อาจต้องปรับตามโมดูลจริง)
    const int KEY_1_EXPECTED = 0;      // ปุ่ม 1 (0V)
    const int KEY_2_EXPECTED = 1024;   // ปุ่ม 2 (1.2V)
    const int KEY_3_EXPECTED = 2048;   // ปุ่ม 3 (2.4V)
    const int KEY_4_EXPECTED = 3072;   // ปุ่ม 4 (3.6V)
    const int KEY_5_EXPECTED = 4064;   // ปุ่ม 5 (4.8V)
    const int KEY_NONE_EXPECTED = 4095; // ไม่กดปุ่ม (5V)

    const int TOLERANCE = 100;  // ความคลาดเคลื่อนที่ยอมรับได้

    // Variables for smoothing readings
    const int NUM_READINGS = 10;
    int readings[NUM_READINGS];
    int readIndex = 0;
    int total = 0;
    int average = 0;

    // Variables for change detection
    int lastValue = -1;
    unsigned long lastChangeTime = 0;
    const unsigned long DEBOUNCE_TIME = 200; // ms

    void setup() {
    Serial.begin(115200);
    Serial.println("=== Analog Keypad Test ===");
    Serial.println("ESP32 Analog Keypad Tester v1.0");
    Serial.println("Pin: GPIO 35");
    Serial.println("Expecting 5-button analog keypad");
    Serial.println("");
    
    // Initialize analog pin
    pinMode(ANALOG_PIN, INPUT);
    
    // Initialize readings array
    for (int i = 0; i < NUM_READINGS; i++) {
        readings[i] = 0;
    }
    
    Serial.println("Expected values:");
    Serial.println("KEY_1 (0V):     ~0");
    Serial.println("KEY_2 (1.2V): ~1024");
    Serial.println("KEY_3 (2.4V): ~2048");
    Serial.println("KEY_4 (3.6V): ~3072");
    Serial.println("KEY_5 (4.8V): ~4064");
    Serial.println("NONE (5V):    ~4095");
    Serial.println("");
    Serial.println("Press keys to see actual values...");
    Serial.println("=====================================");
    }

    void loop() {
    // Read and smooth the analog value
    int rawValue = analogRead(ANALOG_PIN);
    
    // Remove oldest reading
    total = total - readings[readIndex];
    // Add new reading
    readings[readIndex] = rawValue;
    total = total + readings[readIndex];
    // Advance to next position
    readIndex = (readIndex + 1) % NUM_READINGS;
    
    // Calculate average
    average = total / NUM_READINGS;
    
    // Check for significant change
    if (abs(average - lastValue) > 50) {
        unsigned long currentTime = millis();
        
        // Debounce check
        if (currentTime - lastChangeTime > DEBOUNCE_TIME) {
        lastChangeTime = currentTime;
        lastValue = average;
        
        // Display reading with interpretation
        Serial.printf("Raw: %4d | Smooth: %4d | ", rawValue, average);
        
        // Determine which key was pressed
        String keyName = "UNKNOWN";
        int expectedValue = -1;
        int error = 9999;
        
        if (abs(average - KEY_1_EXPECTED) <= TOLERANCE) {
            keyName = "KEY_1";
            expectedValue = KEY_1_EXPECTED;
            error = average - KEY_1_EXPECTED;
        } else if (abs(average - KEY_2_EXPECTED) <= TOLERANCE) {
            keyName = "KEY_2";
            expectedValue = KEY_2_EXPECTED;
            error = average - KEY_2_EXPECTED;
        } else if (abs(average - KEY_3_EXPECTED) <= TOLERANCE) {
            keyName = "KEY_3";
            expectedValue = KEY_3_EXPECTED;
            error = average - KEY_3_EXPECTED;
        } else if (abs(average - KEY_4_EXPECTED) <= TOLERANCE) {
            keyName = "KEY_4";
            expectedValue = KEY_4_EXPECTED;
            error = average - KEY_4_EXPECTED;
        } else if (abs(average - KEY_5_EXPECTED) <= TOLERANCE) {
            keyName = "KEY_5";
            expectedValue = KEY_5_EXPECTED;
            error = average - KEY_5_EXPECTED;
        } else if (abs(average - KEY_NONE_EXPECTED) <= TOLERANCE) {
            keyName = "NONE";
            expectedValue = KEY_NONE_EXPECTED;
            error = average - KEY_NONE_EXPECTED;
        }
        
        if (expectedValue != -1) {
            Serial.printf("Key: %-6s | Expected: %4d | Error: %+4d", 
                        keyName.c_str(), expectedValue, error);
        } else {
            Serial.printf("Key: %-6s | Closest: ?", keyName.c_str());
            
            // Find closest expected value
            int minDiff = 9999;
            String closestKey = "";
            int closestExpected = 0;
            
            int expectations[] = {KEY_1_EXPECTED, KEY_2_EXPECTED, KEY_3_EXPECTED, 
                                KEY_4_EXPECTED, KEY_5_EXPECTED, KEY_NONE_EXPECTED};
            String keyNames[] = {"KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5", "NONE"};
            
            for (int i = 0; i < 6; i++) {
            int diff = abs(average - expectations[i]);
            if (diff < minDiff) {
                minDiff = diff;
                closestKey = keyNames[i];
                closestExpected = expectations[i];
            }
            }
            
            Serial.printf(" | Closest: %s (%d, diff: %d)", 
                        closestKey.c_str(), closestExpected, minDiff);
        }
        
        Serial.println();
        }
    }
    
    delay(10); // Small delay for stability
    }

    /*
    * Additional test functions - uncomment to use
    */

    // Continuous monitoring mode
    void continuousMode() {
    Serial.println("\n=== Continuous Monitoring Mode ===");
    Serial.println("Showing real-time values every 500ms");
    Serial.println("Send any character to stop");
    
    while (!Serial.available()) {
        int rawValue = analogRead(ANALOG_PIN);
        Serial.printf("Time: %8lu | Raw: %4d\n", millis(), rawValue);
        delay(500);
    }
    
    // Clear serial buffer
    while (Serial.available()) {
        Serial.read();
    }
    }

    // Calibration helper
    void calibrationMode() {
    Serial.println("\n=== Calibration Mode ===");
    Serial.println("Press and hold each key when prompted:");
    
    String keyNames[] = {"KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5", "NONE"};
    int calibratedValues[6];
    
    for (int i = 0; i < 6; i++) {
        Serial.printf("\nPress and hold %s, then press Enter...", keyNames[i].c_str());
        
        // Wait for Enter
        while (!Serial.available()) {
        delay(10);
        }
        while (Serial.available()) {
        Serial.read(); // Clear buffer
        }
        
        // Take multiple readings
        int sum = 0;
        int numSamples = 50;
        
        Serial.print("Reading");
        for (int j = 0; j < numSamples; j++) {
        sum += analogRead(ANALOG_PIN);
        if (j % 10 == 0) Serial.print(".");
        delay(20);
        }
        
        calibratedValues[i] = sum / numSamples;
        Serial.printf(" %s = %d\n", keyNames[i].c_str(), calibratedValues[i]);
    }
    
    Serial.println("\n=== Calibration Results ===");
    Serial.println("Copy these values to your main code:");
    for (int i = 0; i < 6; i++) {
        Serial.printf("const int %s = %d;\n", keyNames[i].c_str(), calibratedValues[i]);
    }
    }

    // Statistics mode
    void statisticsMode() {
    Serial.println("\n=== Statistics Mode ===");
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
    
    int avgVal = sum / 1000;
    
    // Calculate standard deviation
    long sumSquaredDiffs = 0;
    for (int i = 0; i < 1000; i++) {
        int diff = samples[i] - avgVal;
        sumSquaredDiffs += diff * diff;
    }
    
    int stdDev = sqrt(sumSquaredDiffs / 1000);
    
    Serial.printf("Min: %d\n", minVal);
    Serial.printf("Max: %d\n", maxVal);
    Serial.printf("Average: %d\n", avgVal);
    Serial.printf("Std Dev: %d\n", stdDev);
    Serial.printf("Range: %d\n", maxVal - minVal);
    }
