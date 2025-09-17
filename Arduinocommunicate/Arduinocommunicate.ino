#include <SoftwareSerial.h>

// กำหนด RX, TX ของ SoftwareSerial
SoftwareSerial mySerial(8, 7); // RX, TX

void setup() {
  Serial.begin(9600);       // Serial Monitor
  mySerial.begin(115200);   // สื่อสารกับ ESP32
}

void loop() {
  // อ่านข้อมูลจาก ESP32
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');
    Serial.println("From ESP32: " + msg);
  }

  // ส่งข้อมูลไป ESP32
  //mySerial.println("Hello from Arduino");
  delay(1000);
}
