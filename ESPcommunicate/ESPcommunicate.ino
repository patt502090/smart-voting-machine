HardwareSerial mySerial(2); // UART2
//ใช้วงจรแบ่งแรงดัน 1k กับ 2k ต่อ gnd
void setup() {
  Serial.begin(115200);           
  mySerial.begin(115200, SERIAL_8N1, 16, 17); 
}

void loop() {
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');
    Serial.println("From Arduino: " + msg);
  }
  mySerial.println("Hello from ESP32");
  delay(1000);
}
