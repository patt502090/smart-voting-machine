/*
  SmartVoting — Arduino Board (MEGA2560 recommended)
  --------------------------------------------------
  Role: Vote booth controller (all peripherals) + TX-only uplink to ESP32.

  Peripherals (typical):
   - MFRC522 (SPI) RFID
   - Fingerprint Sensor (AS608/R305) on Serial2
   - LCD 16x2 I2C (addr 0x27)
   - 8 candidate buttons (INPUT_PULLUP)
   - Reed switch (tamper), IR-gates (ballot drop), HX711(loadcell) optional
   - Optional: Buzzer, WS2812
   - Serial1 (TX only) to ESP32 RX2 (through optocoupler if data-diode)

  Compile:
   - Board: Arduino Mega or Uno (works with pin changes & SoftwareSerial for FP)
   - Libraries: MFRC522, Adafruit Fingerprint Sensor, LiquidCrystal_I2C, HX711 (optional)

  Security:
   - No commands are read from Serial1; TX-only wire recommended.
*/
#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include "Protocol.h"
// Optional (uncomment if used)
// #include "HX711.h"

using namespace SVM;

// ---------------- PIN MAP ----------------
#if defined(ARDUINO_AVR_MEGA2560)
// MEGA2560
#define PIN_RC522_SS   53
#define PIN_RC522_RST  49
#define SER_FP         Serial2   // Fingerprint
#define SER_UPLINK     Serial1   // TX-only uplink to ESP32
#else
// UNO fallback (adjust as needed; FP via SoftwareSerial etc.)
#include <SoftwareSerial.h>
SoftwareSerial FingerSS(10,11); // RX,TX
#define SER_FP FingerSS
#define SER_UPLINK Serial
#define PIN_RC522_SS   10
#define PIN_RC522_RST  9
#endif

// Buttons & sensors
const uint8_t BTN[8] = {22,23,24,25,26,27,28,29}; // MEGA digital pins
const uint8_t PIN_REED = 30;   // tamper
const uint8_t PIN_IR_L = 31;   // IR gate left
const uint8_t PIN_IR_R = 32;   // IR gate right
const int BUZZER_PIN = 8;      // optional

// LCD
LiquidCrystal_I2C lcd(0x27,16,2);

// RFID
MFRC522 rfid(PIN_RC522_SS, PIN_RC522_RST);

// Fingerprint
Adafruit_Fingerprint finger(&SER_FP);

// Uplink
FrameWriter uplink(SER_UPLINK);

// Voting mode
bool MULTI_MODE = false;  // false = pick 1, true = pick up to MAX_SELECT
const uint8_t MAX_SELECT = 5;

uint32_t tally[8] = {0};
uint32_t lastHeartbeat = 0;
uint8_t fwMajor=1, fwMinor=0;

// Debounce
uint32_t lastPressMs[8] = {0};
const uint16_t DB_MS = 60;

// Voter DB (example)
struct Voter { uint8_t uid[4]; uint16_t fpId; bool hasVoted; };
Voter voters[] = {
  {{0xDE,0xAD,0xBE,0xEF}, 1, false},
  {{0xA1,0xB2,0xC3,0xD4}, 2, false}
};
const int NUM_VOTERS = sizeof(voters)/sizeof(voters[0]);

int findVoter(byte* uid, byte len){
  for(int i=0;i<NUM_VOTERS;i++){
    bool ok=true;
    for(byte j=0;j<4 && j<len;j++){
      if(voters[i].uid[j]!=uid[j]){ ok=false; break; }
    }
    if(ok) return i;
  }
  return -1;
}

void beep(uint16_t ms=60){ if(BUZZER_PIN>=0){ tone(BUZZER_PIN, 2000, ms); } }

void lcdMsg(const String& a, const String& b=""){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(a.substring(0,16));
  lcd.setCursor(0,1); lcd.print(b.substring(0,16));
}

// Pack & send frames
void sendHeartbeat(){
  uint8_t p[1+1+1+1+4]; // uptime32 | mode8 | maxSel8 | fwMajor | fwMinor
  be_write32(p, millis());
  p[4] = MULTI_MODE ? 1:0;
  p[5] = MAX_SELECT;
  p[6] = fwMajor;
  p[7] = fwMinor;
  uplink.writeFrame(HEARTBEAT, p, 8);
}

void sendTally(){
  uint8_t p[32];
  for(int i=0;i<8;i++) be_write32(p+4*i, tally[i]);
  uplink.writeFrame(TALLY_SNAP, p, 32);
}

void sendVoteCast(uint16_t voterId, uint8_t candMask){
  uint8_t p[2+1+1+4];
  be_write16(p, voterId);
  p[2] = MULTI_MODE ? 1:0;
  p[3] = candMask;
  be_write32(p+4, millis());
  uplink.writeFrame(VOTE_CAST, p, sizeof(p));
}

void sendTamper(uint8_t code, uint16_t val){
  uint8_t p[1+2+4];
  p[0]=code; be_write16(p+1, val); be_write32(p+3, millis());
  uplink.writeFrame(TAMPER_EVT, p, sizeof(p));
}

// Voting helpers
uint8_t waitSinglePick(){
  while(true){
    for(int i=0;i<8;i++){
      if(digitalRead(BTN[i])==LOW){
        uint32_t now=millis();
        if(now-lastPressMs[i]>DB_MS){
          lastPressMs[i]=now;
          while(digitalRead(BTN[i])==LOW) delay(5);
          beep();
          return (uint8_t)i;
        }
      }
    }
    delay(2);
  }
}

uint8_t waitMultiPick(){
  bool sel[8]={0}; uint8_t cnt=0, mask=0;
  lcdMsg("Pick up to "+String(MAX_SELECT),"Sel: 0/"+String(MAX_SELECT));
  while(cnt<MAX_SELECT){
    for(int i=0;i<8;i++){
      if(digitalRead(BTN[i])==LOW && !sel[i]){
        uint32_t now=millis();
        if(now-lastPressMs[i]>DB_MS){
          lastPressMs[i]=now;
          while(digitalRead(BTN[i])==LOW) delay(5);
          sel[i]=true; cnt++; mask |= (1<<i);
          tally[i]++; beep();
          lcdMsg("Picked #"+String(i+1),"Sel: "+String(cnt)+"/"+String(MAX_SELECT));
          if(cnt>=MAX_SELECT) break;
        }
      }
    }
    delay(2);
  }
  return mask;
}

// Setup
void setup(){
  // Serials
#if defined(ARDUINO_AVR_MEGA2560)
  Serial.begin(115200);
  SER_UPLINK.begin(115200); // TX-only
  SER_FP.begin(57600);
#else
  Serial.begin(9600);
  SER_UPLINK.begin(115200);
  SER_FP.begin(57600);
#endif

  // LCD
  lcd.init(); lcd.backlight(); lcdMsg("SmartVoting","Booting...");
  // Buttons
  for(int i=0;i<8;i++){ pinMode(BTN[i], INPUT_PULLUP); }
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_IR_L, INPUT_PULLUP);
  pinMode(PIN_IR_R, INPUT_PULLUP);
  if(BUZZER_PIN>=0) pinMode(BUZZER_PIN, OUTPUT);

  // RFID
  SPI.begin();
  rfid.PCD_Init();

  // Fingerprint
  finger.begin(57600);
  lcdMsg("Init FP...", finger.verifyPassword()?"FP OK":"FP FAIL");

  delay(800);
  lcdMsg("Ready", MULTI_MODE? "Mode: MULTI":"Mode: SINGLE");
}

// Loop FSM
void loop(){
  // Heartbeat every second
  if(millis()-lastHeartbeat>1000){
    lastHeartbeat=millis();
    sendHeartbeat();
  }

  // Tamper monitoring
  static bool prevReed = !digitalRead(PIN_REED);
  bool nowReed = !digitalRead(PIN_REED); // closed? (depends on reed wiring)
  if(nowReed!=prevReed){
    prevReed=nowReed;
    sendTamper( (nowReed?1:2), 0 ); // 1=closed,2=open
  }

  // Wait RFID
  if(!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()){
    delay(5); return;
  }
  byte* uid = rfid.uid.uidByte;
  int vidx = findVoter(uid, rfid.uid.size);
  if(vidx<0){
    lcdMsg("Unauthorized","RFID not found");
    beep(200); delay(1200);
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    lcdMsg("Ready", MULTI_MODE? "Mode: MULTI":"Mode: SINGLE");
    return;
  }
  if(voters[vidx].hasVoted){
    lcdMsg("Denied","Already voted");
    beep(200); delay(1200);
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    lcdMsg("Ready", MULTI_MODE? "Mode: MULTI":"Mode: SINGLE");
    return;
  }

  // Ask fingerprint
  lcdMsg("RFID OK","#"+String(vidx)+" Scan finger");
  rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();

  bool ok=false;
  for(int tries=0;tries<6;tries++){
    if(finger.getImage()==FINGERPRINT_OK){
      if(finger.image2Tz()==FINGERPRINT_OK){
        if(finger.fingerSearch()==FINGERPRINT_OK){
          if(finger.fingerID==voters[vidx].fpId){ ok=true; break; }
        }
      }
    }
    delay(300);
  }
  if(!ok){
    lcdMsg("FP mismatch","Try again");
    beep(200); delay(1200);
    lcdMsg("Ready", MULTI_MODE? "Mode: MULTI":"Mode: SINGLE");
    return;
  }
  beep(120);
  lcdMsg("Auth OK", MULTI_MODE? "Pick up to 5":"Pick ONE");

  // Vote
  uint8_t candMask=0;
  if(MULTI_MODE){
    candMask = waitMultiPick();
  }else{
    uint8_t idx = waitSinglePick();
    tally[idx]++; candMask = (1<<idx);
  }

  // Mark voted
  voters[vidx].hasVoted = true;

  // Send vote cast + tally snapshot
  sendVoteCast(voters[vidx].fpId, candMask);
  sendTally();

  // UI done
  lcdMsg("Vote recorded!","Thank you");
  delay(1000);
  lcdMsg("Ready", MULTI_MODE? "Mode: MULTI":"Mode: SINGLE");
}
