//#include <SoftwareSerial.h>

// กำหนด RX, TX ของ SoftwareSerial
//SoftwareSerial mySerial(8, 7); // RX, TX

void setup() {
  Serial.begin(9600);       // Serial Monitor
  //mySerial.begin(115200);   // สื่อสารกับ ESP32
}

void loop() {
  // อ่านข้อมูลจาก ESP32
  if (Serial.available()) {
    String msg =  Serial.readStringUntil('\n');
    Serial.println("From ESP32: " + msg);
  }

  // ส่งข้อมูลไป ESP32
  Serial.println("Hello from Arduino");
  delay(1000);
}
