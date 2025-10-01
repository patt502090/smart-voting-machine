/*
  SmartVoting — ESP32 Board
  -------------------------
  Role: Receive framed TX (Serial2 RX-only) from Arduino, verify CRC, update in-RAM tally,
        and host a Wi‑Fi dashboard + JSON APIs.

  - Serial2: RX2=GPIO16 (TX not used). Keep TX-only wiring for data-diode security.
  - Wi‑Fi: serves a simple HTML dashboard with auto-refresh (AJAX).
  - JSON Endpoints:
      GET /counts   -> {"counts":[...],"updated":"..."}
      GET /events   -> ring buffer of last 100 events
  - Best-practice: robust frame parser with state machine + CRC16-CCITT.

  Libraries: WiFi.h, WebServer.h
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "Protocol.h"

using namespace SVM;

// ---------------- CONFIG ----------------
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Serial2 pins: RX=GPIO16, TX=GPIO17 (unused)
HardwareSerial& UPLINK = Serial2;

// Data
uint32_t tally[8] = {0};
char updated[32] = "never";

// Events ring buffer
struct Event { uint32_t ts; uint8_t type; uint8_t a; uint16_t b; uint8_t mask; };
Event events[100]; int evHead=0, evCount=0;

WebServer srv(80);

// HTML (minimal)
const char* INDEX_HTML = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<title>Smart Voting — ESP32 Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;margin:24px;background:#f6f7fb;color:#222}
h1{margin:0 0 10px}
.card{background:#fff;border:1px solid #ddd;border-radius:12px;padding:16px;margin:14px 0}
.bar{display:flex;align-items:center;margin:6px 0}
.bar .label{width:130px}
.bar .track{flex:1;height:18px;background:#eee;border-radius:10px;overflow:hidden}
.bar .fill{height:100%;background:#4aa3ff}
small{color:#666}
code{background:#f1f1f4;padding:2px 6px;border-radius:6px}
</style></head><body>
<h1>Smart Voting — ESP32 Dashboard</h1>
<div class="card">
  <div id="bars"></div>
  <small id="meta"></small>
</div>
<div class="card">
  <b>Recent events</b>
  <div id="ev"></div>
</div>
<script>
async function refresh(){
  const r1=await fetch('/counts'); const j1=await r1.json();
  const max=Math.max(1,...j1.counts);
  document.getElementById('bars').innerHTML = j1.counts.map((v,i)=>`
   <div class="bar"><div class="label">Candidate #${i+1}</div>
   <div class="track"><div class="fill" style="width:${Math.round(v*100/max)}%"></div></div>
   <div style="width:60px;text-align:right;margin-left:8px">${v}</div></div>`).join('');
  document.getElementById('meta').innerText = 'Updated: '+(j1.updated||'-');
  const r2=await fetch('/events'); const j2=await r2.json();
  document.getElementById('ev').innerHTML = j2.events.map(e=>`<div>
   <code>${e.ts}</code> — <b>${e.type}</b> a=${e.a} b=${e.b} mask=${e.mask}</div>`).join('');
}
refresh(); setInterval(refresh, 1200);
</script>
</body></html>
)HTML";

String jsonCounts(){
  String s="{\"counts\":[";
  for(int i=0;i<8;i++){ s+=String((unsigned long)tally[i]); if(i<7) s+=','; }
  s+="],\"updated\":\""; s+=updated; s+="\"}";
  return s;
}

String jsonEvents(){
  String s="{\"events\":[";
  for(int i=0;i<evCount;i++){
    int idx=(evHead - 1 - i + 100) % 100;
    const Event& e = events[idx];
    s+="{\"ts\":"+String(e.ts)+",\"type\":"+String(e.type)+",\"a\":"+String(e.a)+",\"b\":"+String(e.b)+",\"mask\":"+String(e.mask)+"}";
    if(i<evCount-1) s+=',';
  }
  s+="]}"; return s;
}

void pushEvent(uint8_t type, uint8_t a, uint16_t b, uint8_t mask){
  events[evHead] = { (uint32_t)millis(), type, a, b, mask };
  evHead = (evHead+1)%100; if(evCount<100) evCount++;
  snprintf(updated, sizeof(updated), "%lu ms", (unsigned long)millis());
}

// ---------------- Frame Parser ----------------
enum RState { S_IDLE, S_HDR, S_PAY, S_CRC, S_ETX };
RState state = S_IDLE;
uint8_t ver=0, type=0, len=0;
uint8_t payload[255];
uint8_t idx=0;
uint16_t crc=0;

void resetParser(){ state=S_IDLE; ver=type=len=idx=0; crc=0; }

void applyFrame(uint8_t type, const uint8_t* p, uint8_t n){
  if(type==HEARTBEAT){
    // p: uptime32 | mode8 | maxSel8 | fwMajor | fwMinor
    pushEvent(type, p[4], p[5], 0);
  }else if(type==TALLY_SNAP && n==32){
    for(int i=0;i<8;i++){
      uint32_t v = (uint32_t)p[4*i]<<24 | (uint32_t)p[4*i+1]<<16 | (uint32_t)p[4*i+2]<<8 | (uint32_t)p[4*i+3];
      tally[i]=v;
    }
    pushEvent(type, 0, 0, 0);
  }else if(type==VOTE_CAST && n>=8){
    uint16_t voterId = ((uint16_t)p[0]<<8) | p[1];
    uint8_t mode = p[2];
    uint8_t mask = p[3];
    // Update local tally also (already updated by Arduino, but keep in sync)
    for(int i=0;i<8;i++) if(mask & (1<<i)) tally[i]++;
    pushEvent(type, mode, voterId, mask);
  }else if(type==TAMPER_EVT && n>=7){
    pushEvent(type, p[0], ((uint16_t)p[1]<<8)|p[2], 0);
  }else{
    pushEvent(0xFF, type, n, 0); // unknown
  }
}

void parseByte(uint8_t b){
  switch(state){
    case S_IDLE:
      if(b==STX) state = S_HDR;
      break;
    case S_HDR:
      if(idx==0){ ver=b; idx++; }
      else if(idx==1){ type=b; idx++; }
      else if(idx==2){ len=b; idx=0; crc=0; state=S_PAY; }
      break;
    case S_PAY:
      payload[idx++]=b;
      if(idx>=len){ state=S_CRC; idx=0; }
      break;
    case S_CRC:
      if(idx==0){ crc = ((uint16_t)b<<8); idx++; }
      else { crc |= b; state=S_ETX; }
      break;
    case S_ETX:
      if(b==ETX){
        // verify crc
        uint8_t buf[3+255]; buf[0]=ver; buf[1]=type; buf[2]=len;
        memcpy(buf+3, payload, len);
        uint16_t calc = crc16_ccitt(buf, 3+len);
        if(calc==crc && ver==VER){
          applyFrame(type, payload, len);
        } // else drop silently
      }
      resetParser();
      break;
  }
}

// ---------------- Setup/Loop ----------------
void setup(){
  Serial.begin(115200);
  UPLINK.begin(115200, SERIAL_8N1, 16, 17); // RX2=16, TX2=17 (unused)

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi...");
  for(int i=0;i<60 && WiFi.status()!=WL_CONNECTED;i++){ delay(250); Serial.print("."); }
  Serial.println(); Serial.println(WiFi.localIP());

  srv.on("/", [](){ srv.send(200,"text/html", INDEX_HTML); });
  srv.on("/counts", [](){ srv.send(200, "application/json", jsonCounts()); });
  srv.on("/events", [](){ srv.send(200, "application/json", jsonEvents()); });
  srv.begin();
}

void loop(){
  // Read uplink
  while(UPLINK.available()) parseByte( (uint8_t)UPLINK.read() );
  srv.handleClient();
}
