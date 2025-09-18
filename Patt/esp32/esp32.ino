// ESP32 Smart Voting Dashboard (stable)
// - WebServer serves index HTML from PROGMEM (low heap)
// - /cfg provides candidate names + photo URLs
// - /tally provides counts (updated every 700ms on page)
// - UART2 RX=GPIO16 reads "CF:X" lines from UNO

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

//////// Wi-Fi ////////
const char* STA_SSID = "iPhone";      // <-- เปลี่ยนให้ตรงของคุณ
const char* STA_PASS = "abcdefgh";    // <-- เปลี่ยนให้ตรงของคุณ
const char* AP_SSID  = "SVM-ESP32";
const char* AP_PASS  = "12345678";

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

//////// UART ////////
HardwareSerial VOTE(2);
#define RX2_PIN    16
#define TX2_PIN    -1
#define UART_BAUD  115200

//////// State ////////
volatile int tally[10] = {0};
volatile int totalVotes = 0;
String lineBuf;

WebServer server(80);

//////// Candidate config (แก้ชื่อ/รูปตามต้องการ) ////////
const char* NAME[10] = {
  "งดออกเสียง", "ผู้สมัคร 1", "ผู้สมัคร 2", "ผู้สมัคร 3",
  "ผู้สมัคร 4", "ผู้สมัคร 5", "ผู้สมัคร 6", "ผู้สมัคร 7",
  "ผู้สมัคร 8", "ผู้สมัคร 9"
};

// ใช้รูปจากลิงก์ (ขอแบบสี่เหลี่ยม) — แก้ให้เป็นของจริงได้เลย
// ทดลอง: ใช้ placeholder สีต่าง ๆ จาก dummyimage.com
const char* PHOTO[10] = {
  "https://mercyinvestmentservices.org/wp-content/uploads/AdobeStock_70618933-scaled.jpeg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/e/e9/Field_Marshal_Plaek_Phibunsongkhram.jpg/330px-Field_Marshal_Plaek_Phibunsongkhram.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/6/64/Anutin_Charnvirakul_in_September_2025_%28cropped%29.png/330px-Anutin_Charnvirakul_in_September_2025_%28cropped%29.png",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/7/7b/Anand_Panyarachun_%28cropped%29.jpg/250px-Anand_Panyarachun_%28cropped%29.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/5/58/Chuan_Likphai_1993_cropped.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/b/b6/The_Prime_Minister_of_Thailand%2C_Mr._Somchai_Wangsawat_meeting_the_Prime_Minister%2C_Dr._Manmohan_Singh%2C_in_New_Delhi_on_November_13%2C_2008_%28cropped%29_%28cropped%29.jpg/250px-The_Prime_Minister_of_Thailand%2C_Mr._Somchai_Wangsawat_meeting_the_Prime_Minister%2C_Dr._Manmohan_Singh%2C_in_New_Delhi_on_November_13%2C_2008_%28cropped%29_%28cropped%29.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/2/28/Abhisit_Vejjajiva_2010.jpg/250px-Abhisit_Vejjajiva_2010.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/2/2b/Prawit_Wongsuwan_%282018%29_cropped.jpg/250px-Prawit_Wongsuwan_%282018%29_cropped.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/d/d4/Phumtham_Wechayachai_30_May_2025.jpg/250px-Phumtham_Wechayachai_30_May_2025.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/4/4d/Kriangsak_Chomanan_1976_%28cropped%29.jpg/250px-Kriangsak_Chomanan_1976_%28cropped%29.jpg"
};

//////// Index page in PROGMEM (ไม่กิน heap) ////////
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="th"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Voting — Live</title>
<style>
:root{
  --bg:#0b1220; --fg:#e5e7eb; --muted:#94a3b8;
  --card:#0f172a; --line:#1f2937; --grad:linear-gradient(90deg,#22d3ee,#a78bfa);
}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--fg);font:15px system-ui,Segoe UI,Roboto,Arial}
.wrap{max-width:1250px;margin:0 auto;padding:18px 18px 0;display:flex;flex-direction:column;height:100%}
.top{display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap}
h2{margin:0;font-weight:800;letter-spacing:.2px}
.badge{display:inline-block;padding:6px 10px;border-radius:10px;background:#0b1730;border:1px solid var(--line);color:var(--muted);font-size:12px}
.btn{padding:9px 14px;border-radius:10px;background:#22d3ee;color:#08131a;text-decoration:none;font-weight:800}
.grid{display:grid;grid-template-columns:repeat(5,1fr);gap:12px;margin-top:14px;flex:1;min-height:0}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:10px;display:flex;flex-direction:column}
.photo{width:100%;aspect-ratio:1/1;border-radius:10px;object-fit:cover;border:1px solid #0b1220}
.name{margin:8px 0 2px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.muted{color:var(--muted);font-size:12px}
.big{font-size:30px;font-weight:900}
.bar{height:10px;background:var(--line);border-radius:7px;overflow:hidden}
.fill{height:100%;background:var(--grad);width:0;transition:width .35s ease}
.toprow{display:grid;grid-template-columns:2fr 1fr;gap:12px;margin-top:14px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-left:8px}
.ok{background:#22c55e}.bad{background:#ef4444}.warn{background:#eab308}
@media (max-width:1100px){ .grid{grid-template-columns:repeat(4,1fr)} }
@media (max-width:900px){ .grid{grid-template-columns:repeat(3,1fr)} }
</style>
</head>
<body><div class="wrap">
  <div class="top">
    <div>
      <h2>Smart Voting — Live</h2>
      <span id="net" class="badge">Connecting… <span id="dot" class="dot warn"></span></span>
    </div>
    <div>
      <a class="btn" href="/reset" onclick="return confirm('Reset all counts?')">Reset</a>
      <a class="btn" href="/ping" target="_blank">Ping</a>
    </div>
  </div>

  <div class="toprow">
    <div class="tile">
      <div class="muted">Total votes</div>
      <div id="total" class="big">0</div>
    </div>
    <div class="tile">
      <div class="muted">IP info</div>
      <div id="ipinfo">—</div>
    </div>
  </div>

  <div id="grid" class="grid"></div>
</div>
<script>
let cfg=null;
function el(tag,cls){const e=document.createElement(tag); if(cls) e.className=cls; return e;}

async function loadCfg(){
  const r=await fetch('/cfg',{cache:'no-store'}); cfg=await r.json();
  // render cards
  const g=document.getElementById('grid'); g.innerHTML='';
  for(let i=0;i<10;i++){
    const card=el('div','card');
    const img=el('img','photo'); img.id='img'+i; img.src=cfg.photos[i]; card.appendChild(img);
    const nm=el('div','name'); nm.textContent=`${cfg.names[i]} (${i})`; card.appendChild(nm);
    const val=el('div','big'); val.id='v'+i; val.textContent='0'; card.appendChild(val);
    const m=el('div','muted'); m.textContent='count'; card.appendChild(m);
    const bar=el('div','bar'); const fill=el('div','fill'); fill.id='b'+i; bar.appendChild(fill); card.appendChild(bar);
    g.appendChild(card);
  }
  document.getElementById('ipinfo').textContent=`STA ${cfg.sta}  |  AP ${cfg.ap}`;
}

async function tick(){
  try{
    const r=await fetch('/tally',{cache:'no-store'});
    if(!r.ok) throw 0;
    const j=await r.json();
    document.getElementById('total').textContent=j.total;
    let max=j.max||1;
    for(let i=0;i<10;i++){
      const v=j.counts[i]||0;
      document.getElementById('v'+i).textContent=v;
      document.getElementById('b'+i).style.width=(100*v/max)+'%';
    }
    document.getElementById('dot').className='dot ok';
    document.getElementById('net').textContent = `Online`;
    document.getElementById('net').appendChild(document.getElementById('dot'));
  }catch(e){
    document.getElementById('dot').className='dot bad';
    document.getElementById('net').textContent = `Offline`;
    document.getElementById('net').appendChild(document.getElementById('dot'));
  }
}

loadCfg().then(()=>{ tick(); setInterval(tick,700); });
</script>
</body></html>)HTML";

//////// Handlers ////////
void sendIndex(){ server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }

void sendCfg(){
  // JSON ขนาดเล็ก -> สร้างด้วย String ได้สบาย
  String s; s.reserve(1800);
  s += F("{\"sta\":\""); s += WiFi.localIP().toString(); s += F("\",\"ap\":\""); s += WiFi.softAPIP().toString(); s += F("\",");
  s += F("\"names\":[");
  for(int i=0;i<10;i++){ if(i) s+=','; s += '\"'; s += NAME[i]; s += '\"'; }
  s += F("],\"photos\":[");
  for(int i=0;i<10;i++){ if(i) s+=','; s += '\"'; s += PHOTO[i]; s += '\"'; }
  s += F("]}");
  server.send(200,"application/json",s);
}

void sendTally(){
  int maxv = 1; for(int i=0;i<10;i++) if(tally[i]>maxv) maxv = tally[i];
  String j; j.reserve(220);
  j += F("{\"total\":"); j += totalVotes; j += F(",\"counts\":[");
  for(int i=0;i<10;i++){ if(i) j+=','; j+=tally[i]; }
  j += F("],\"max\":"); j += maxv; j += F("}");
  server.send(200,"application/json",j);
}

void handleReset(){ for(int i=0;i<10;i++) tally[i]=0; totalVotes=0; server.sendHeader("Location","/"); server.send(303); }
void handlePing(){ server.send(200,"text/plain","ok"); }
void handleFav(){ server.send(204); }
void handle404(){ server.send(404,"text/plain","404"); }

//////// Parse CF:X ////////
void handleVoteDigit(int d){
  if(d<0||d>9) return;
  tally[d]++; totalVotes++;
  Serial.printf("[VOTE] CF:%d | total=%d\n", d, totalVotes);
}
void tryParseLine(const String& s){
  if(s.length()>=4 && s.startsWith("CF:")){
    char c=s.charAt(3);
    if(c>='0' && c<='9'){ handleVoteDigit(c-'0'); return; }
  }
  Serial.printf("[PASS] %s\n", s.c_str());
}

//////// Setup ////////
void setup(){
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW);

  // UART
  VOTE.begin(UART_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);
  Serial.println("[UART] Listen UNO on GPIO16");

  // Wi-Fi STA + AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.printf("STA -> %s", STA_SSID);
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<8000){ delay(250); Serial.print("."); }
  Serial.println();
  if(WiFi.status()==WL_CONNECTED) Serial.printf("STA IP: %s\n", WiFi.localIP().toString().c_str());
  else                            Serial.println("STA failed.");
  bool apok = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP %s  IP: %s  (%s)\n", AP_SSID, WiFi.softAPIP().toString().c_str(), apok?"OK":"FAIL");

  // Routes
  server.on("/", sendIndex);
  server.on("/cfg",   sendCfg);
  server.on("/tally", sendTally);
  server.on("/reset", handleReset);
  server.on("/ping",  handlePing);
  server.on("/favicon.ico", handleFav);
  server.onNotFound(handle404);
  server.begin();
  Serial.println("[WEB] Ready. Open http://<STA-IP>/  or  http://192.168.4.1/");
}

//////// Loop ////////
void loop(){
  // UART non-blocking
  uint8_t guard=0;
  while(VOTE.available() && guard++<80){
    char c=(char)VOTE.read();
    if(c=='\n'){ lineBuf.trim(); if(lineBuf.length()) tryParseLine(lineBuf); lineBuf=""; }
    else if(c!='\r'){ if(c>=32 && c<=126) lineBuf+=c; if(lineBuf.length()>64) lineBuf=""; }
  }
  server.handleClient();

  // ทดสอบจาก USB Serial: พิมพ์ "CF:5" แล้ว Enter
  if(Serial.available()){
    String s=Serial.readStringUntil('\n'); s.trim();
    if(s.length()) tryParseLine(s);
  }
}