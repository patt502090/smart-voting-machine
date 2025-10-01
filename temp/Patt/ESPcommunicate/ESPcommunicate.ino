/* Fill-in information from Blynk Device Info here */
#define BLYNK_TEMPLATE_ID "TMPL6G6KsJzqK"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN    "RUBdFFrRrLJ99YHyTgYN5rew8gfkPzaH"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

HardwareSerial mySerial(2); // UART2
//ใช้วงจรแบ่งแรงดัน 1k กับ 2k ต่อ gnd





/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial



// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "@Ps2311-WiFi";
char pass[] = "99682455";

BlynkTimer timer;

// This function is called every time the Virtual Pin 0 state changes
BLYNK_WRITE(V0)
{
  // Set incoming value from pin V0 to a variable
  int value = param.asInt();

  // Update state
  Blynk.virtualWrite(V1, value);
}

// This function is called every time the device is connected to the Blynk.Cloud
BLYNK_CONNECTED()
{
  // Change Web Link Button message to "Congratulations!"
  Blynk.setProperty(V3, "offImageUrl", "https://static-image.nyc3.cdn.digitaloceanspaces.com/general/fte/congratulations.png");
  Blynk.setProperty(V3, "onImageUrl",  "https://static-image.nyc3.cdn.digitaloceanspaces.com/general/fte/congratulations_pressed.png");
  Blynk.setProperty(V3, "url", "https://docs.blynk.io/en/getting-started/what-do-i-need-to-blynk/how-quickstart-device-was-made");
}

// This function sends Arduino's uptime every second to Virtual Pin 2.
void myTimerEvent()
{
  // You can send any value at any time.
  // Please don't send more that 10 values per second.
  Blynk.virtualWrite(V2, millis() / 1000);
}



void setup() {
  Serial.begin(115200);           
  mySerial.begin(115200, SERIAL_8N1, 16, 17); 
    // Debug console


  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  // You can also specify server:
  //Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 80);
  //Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, IPAddress(192,168,1,100), 8080);

  // Setup a function to be called every second
  timer.setInterval(1000L, myTimerEvent);
}

void loop() {
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');
    Serial.println("From Arduino: " + msg);
  }
  mySerial.println("Hello from ESP32");
  delay(1000);
  //Blynk.run();
  //timer.run();
}

