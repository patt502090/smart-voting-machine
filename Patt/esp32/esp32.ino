// ESP32 (30-pin) — UART2 RX=GPIO16 <-- UNO TX  |  WiFi + MQTT Bridge (stable)
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

HardwareSerial VOTE(2);
#define RX2_PIN 16
#define TX2_PIN -1
#define UART_BAUD 115200

// ===== WIFI/MQTT =====
const char* WIFI_SSID = "pattESP";
const char* WIFI_PASS = "502090";
const char* MQTT_HOST = "172.30.90.36";   // เปลี่ยนเป็น IP เครื่องที่รัน mosquitto
const uint16_t MQTT_PORT = 1883;

WiFiClient wifi;
PubSubClient mqtt(wifi);
String deviceId;

int tally[10] = {0};
int totalVotes = 0;

String lineBuf;
unsigned long lastPush = 0;
const unsigned long PUSH_MS = 1500;

void wifiEnsure(){
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);     // ลดกำลังส่ง ลดกระชากไฟ
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) { delay(200); }
}

void publishTally(){
  if (!mqtt.connected()) return;
  String s; s.reserve(128);
  s += "{\"id\":\""; s += deviceId; s += "\",\"total\":"; s += totalVotes; s += ",\"counts\":[";
  for(int i=0;i<10;i++){ if(i) s += ','; s += tally[i]; }
  s += "],\"ts\":"; s += millis(); s += "}";
  mqtt.publish("svm/tally", s.c_str(), true);
}

void sendLog(int d){
  if (!mqtt.connected()) return;
  String s; s.reserve(96);
  s += "{\"id\":\""; s += deviceId; s += "\",\"choice\":"; s += d;
  s += ",\"ts\":"; s += millis(); s += "}";
  mqtt.publish("svm/log", s.c_str(), false);
}

void onMqtt(char* topic, byte* payload, unsigned int len){
  // ตัวอย่าง: รองรับ reset ผ่าน "svm/cmd" = "reset"
  String t(topic); String msg; msg.reserve(len);
  for(unsigned i=0;i<len;i++) msg += (char)payload[i];
  msg.trim();
  if(t == "svm/cmd" && msg == "reset"){
    for(int i=0;i<10;i++) tally[i]=0; totalVotes=0;
    publishTally();
  }
}

void mqttEnsure(){
  if (mqtt.connected()) return;
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.connect(deviceId.c_str());
  if (mqtt.connected()){
    mqtt.publish("svm/status", "online", true);
    mqtt.subscribe("svm/cmd");
    publishTally();
  }
}

void handleVoteDigit(int d){
  if (d < 0 || d > 9) return;
  tally[d]++; totalVotes++;
  sendLog(d);
}

void tryParseLine(const String& s){
  // รับ "CF:X"
  if (s.length()>=4 && s[0]=='C' && s[1]=='F' && s[2]==':'){
    char c = s[3];
    if (c>='0' && c<='9') handleVoteDigit(c-'0');
  }
}

void setup(){
  Serial.begin(UART_BAUD);
  VOTE.begin(UART_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  // สร้าง deviceId จาก MAC
  uint8_t mac[6]; WiFi.macAddress(mac);
  char idbuf[32]; snprintf(idbuf, sizeof(idbuf), "esp32-%02X%02X%02X", mac[3], mac[4], mac[5]);
  deviceId = idbuf;

  wifiEnsure();
  mqttEnsure();
}

void loop(){
  // รักษาเน็ต
  static uint32_t lastNet = 0;
  if (millis() - lastNet > 1200) { lastNet = millis(); wifiEnsure(); mqttEnsure(); }
  if (mqtt.connected()) mqtt.loop();

  // อ่านจาก UNO
  while (VOTE.available()){
    char c = (char)VOTE.read();
    if (c == '\n'){ lineBuf.trim(); if(lineBuf.length()) tryParseLine(lineBuf); lineBuf = ""; }
    else if (c != '\r'){ if (c>=32 && c<=126) lineBuf += c; if(lineBuf.length()>32) lineBuf = ""; }
  }

  // push tally เป็นระยะ
  if (millis() - lastPush > PUSH_MS){ lastPush = millis(); publishTally(); }
}