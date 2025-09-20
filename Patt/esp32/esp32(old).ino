// ESP32 Smart Voting + Web Dashboard + Multi-upload (/up)
// - ใช้ได้กับ LittleFS หรือ SPIFFS (เลือกอัตโนมัติจาก define ด้านล่าง)
// - ใส่รูปผ่านเว็บครั้งเดียวหลายไฟล์ได้ (multiple)
// - หน้า /cfg ดึงชื่อ/รูปจาก FS path: /img/0.jpg ... /img/9.jpg

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>

// ====== เลือก FS ======
// ถ้าใช้ LittleFS ให้ uncomment บรรทัดด้านล่าง แล้วเพิ่มไลบรารี LittleFS ของ ESP32 ด้วย
// #include <LittleFS.h>
// #define FILESYS LittleFS

// ถ้ายังไม่ติดตั้ง LittleFS หรือ Partition เป็น SPIFFS อยู่ ใช้ SPIFFS ไปก่อน:
#include <SPIFFS.h>
#define FILESYS SPIFFS

// ====== Wi-Fi ======xx`
const char* STA_SSID = "iPhone";      // แก้เป็น Wi-Fi ของคุณ
const char* STA_PASS = "abcdefgh";
const char* AP_SSID  = "SVM-ESP32";
const char* AP_PASS  = "12345678";

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ====== UART (UNO -> ESP32) ======
HardwareSerial VOTE(2);
#define RX2_PIN    16
#define TX2_PIN    -1
#define UART_BAUD  115200

// ====== State ======
volatile int tally[10] = {0};
volatile int totalVotes = 0;
String lineBuf;

WebServer server(80);

// ====== Candidate names (แก้ชื่อที่นี่) ======
const char* NAME[10] = {
  "งดออกเสียง","ผู้สมัคร 1","ผู้สมัคร 2","ผู้สมัคร 3","ผู้สมัคร 4",
  "ผู้สมัคร 5","ผู้สมัคร 6","ผู้สมัคร 7","ผู้สมัคร 8","ผู้สมัคร 9"
};

// ====== หน้า index (ธีมทางการของรัฐบาล) ======
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="th"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ระบบลงคะแนนเสียงอัจฉริยะ - สำนักงานคณะกรรมการการเลือกตั้ง</title>
<style>
:root{
  --primary:#1e40af; --primary-dark:#1e3a8a; --secondary:#dc2626; --accent:#f59e0b;
  --bg:#f8fafc; --card:#ffffff; --text:#1e293b; --text-muted:#64748b;
  --border:#e2e8f0; --success:#059669; --warning:#d97706; --danger:#dc2626;
  --gradient:linear-gradient(135deg,#1e40af,#dc2626); --shadow:0 4px 6px -1px rgba(0,0,0,0.1);
}
*{box-sizing:border-box; margin:0; padding:0}
html,body{height:100%; font-family:'Sarabun','Kanit',system-ui,Segoe UI,Roboto,Arial; background:var(--bg); color:var(--text)}
body{font-size:16px; line-height:1.6}

/* Government Header */
.gov-header{background:var(--gradient); color:white; padding:15px 0; margin-bottom:20px; box-shadow:var(--shadow)}
.gov-header .wrap{max-width:1000px; margin:0 auto; padding:0 15px}
.gov-logo{display:flex; align-items:center; gap:12px; margin-bottom:8px}
.logo-container{width:45px; height:45px; display:flex; align-items:center; justify-content:center}
.logo-circle{width:40px; height:40px; background:white; border-radius:50%; display:flex; align-items:center; justify-content:center; box-shadow:0 2px 6px rgba(0,0,0,0.1)}
.logo-inner{width:32px; height:32px; background:var(--gradient); border-radius:50%; display:flex; align-items:center; justify-content:center}
.logo-text{color:white; font-size:12px; font-weight:800; font-family:'Sarabun','Kanit',system-ui}
.gov-title{font-size:18px; font-weight:700}
.gov-subtitle{font-size:11px; opacity:0.9}
.gov-nav{display:flex; gap:15px; margin-top:10px; flex-wrap:wrap}
.gov-nav a{color:white; text-decoration:none; padding:6px 12px; border-radius:4px; transition:background 0.2s; font-size:12px}
.gov-nav a:hover{background:rgba(255,255,255,0.1)}

/* Layout */
.wrap{max-width:1000px; margin:0 auto; padding:0 15px}
.main-content{background:var(--card); border-radius:8px; padding:20px; margin-bottom:20px; box-shadow:var(--shadow); border:1px solid var(--border)}

/* Status Bar */
.status-bar{display:flex; justify-content:space-between; align-items:center; background:var(--card); padding:10px 15px; border-radius:6px; border:1px solid var(--border); margin-bottom:20px}
.status-info{display:flex; align-items:center; gap:10px}
.status-badge{display:flex; align-items:center; gap:6px; padding:4px 8px; background:#f1f5f9; border-radius:15px; font-size:12px; font-weight:500}
.status-dot{width:6px; height:6px; border-radius:50%; background:var(--warning)}
.status-dot.online{background:var(--success)}
.status-dot.offline{background:var(--danger)}
.current-time{font-size:12px; color:var(--text-muted)}

/* Stats Row */
.stats-row{display:grid; grid-template-columns:repeat(2,1fr); gap:15px; margin-bottom:20px}
.stat-card{background:var(--card); border:2px solid var(--border); border-radius:8px; padding:15px; text-align:center; transition:all 0.2s}
.stat-card:hover{border-color:var(--primary); transform:translateY(-1px)}
.stat-label{color:var(--text-muted); font-size:12px; margin-bottom:6px; font-weight:500}
.stat-value{font-size:24px; font-weight:800; color:var(--primary); margin-bottom:2px}
.stat-sub{font-size:10px; color:var(--text-muted)}

/* Candidates Grid */
.candidates-section{margin-bottom:25px}
.section-title{font-size:16px; font-weight:700; margin-bottom:15px; color:var(--text); text-align:center; padding-bottom:8px; border-bottom:2px solid var(--primary)}
.candidates-grid{display:grid; grid-template-columns:repeat(5,1fr); gap:10px; max-width:800px; margin:0 auto}
.candidate-card{background:var(--card); border:2px solid var(--border); border-radius:8px; padding:10px; text-align:center; transition:all 0.2s; position:relative}
.candidate-card:hover{border-color:var(--primary); transform:translateY(-1px); box-shadow:0 4px 12px rgba(30,64,175,0.15)}
.candidate-card.leading{border-color:var(--success); background:linear-gradient(135deg,#f0fdf4,#dcfce7)}
.candidate-photo{width:100%; height:80px; object-fit:cover; border-radius:6px; margin-bottom:8px; border:1px solid var(--border)}
.candidate-name{font-size:11px; font-weight:600; margin-bottom:6px; color:var(--text)}
.candidate-votes{font-size:16px; font-weight:800; color:var(--primary); margin-bottom:4px}
.candidate-percentage{font-size:9px; color:var(--text-muted); margin-bottom:6px}
.progress-bar{height:4px; background:#f1f5f9; border-radius:2px; overflow:hidden}
.progress-fill{height:100%; background:var(--gradient); transition:width 0.5s ease; border-radius:2px}

/* Buttons */
.btn{display:inline-flex; align-items:center; gap:6px; padding:8px 12px; background:var(--primary); color:white; text-decoration:none; border-radius:6px; font-weight:600; transition:all 0.2s; border:none; cursor:pointer; font-size:11px}
.btn:hover{background:var(--primary-dark); transform:translateY(-1px); box-shadow:0 2px 8px rgba(30,64,175,0.3)}
.btn-secondary{background:var(--card); color:var(--text); border:1px solid var(--border)}
.btn-secondary:hover{background:#f8fafc; border-color:var(--primary)}

/* Responsive */
@media (max-width:768px){ 
  .candidates-grid{grid-template-columns:repeat(3,1fr)}
  .stats-row{grid-template-columns:1fr}
  .gov-nav{flex-direction:column; gap:8px}
}
@media (max-width:480px){ 
  .candidates-grid{grid-template-columns:repeat(2,1fr)}
  .wrap{padding:0 10px}
}
</style></head>
<body>
  <!-- Government Header -->
  <div class="gov-header">
    <div class="wrap">
      <div class="gov-logo">
        <div class="logo-container">
          <div class="logo-circle">
            <div class="logo-inner">
              <div class="logo-text">กกต</div>
            </div>
          </div>
        </div>
        <div>
          <div class="gov-title">สำนักงานคณะกรรมการการเลือกตั้ง</div>
          <div class="gov-subtitle">ELECTION COMMISSION OF THAILAND</div>
        </div>
      </div>
      <div class="gov-nav">
        <a href="#dashboard">หน้าหลัก</a>
        <a href="#results">ผลการเลือกตั้ง</a>
        <a href="#statistics">สถิติ</a>
      </div>
    </div>
  </div>

  <div class="wrap">
    <!-- Status Bar -->
    <div class="status-bar">
      <div class="status-info">
        <div class="status-badge">
          <span id="net">กำลังเชื่อมต่อ...</span>
          <div id="dot" class="status-dot"></div>
        </div>
        <div class="current-time" id="current-time"></div>
      </div>
      <div style="font-size:12px; color:var(--text-muted)">
        ระบบลงคะแนนเสียงอัจฉริยะ
      </div>
    </div>

    <!-- Main Content -->
    <div class="main-content">
      <!-- Stats Row -->
      <div class="stats-row">
        <div class="stat-card">
          <div class="stat-label">รวมคะแนนทั้งหมด</div>
          <div id="total" class="stat-value">0</div>
          <div class="stat-sub">Total Votes</div>
        </div>
        <div class="stat-card">
          <div class="stat-label">สถานะระบบ</div>
          <div id="ipinfo" class="stat-value">—</div>
          <div class="stat-sub">System Status</div>
        </div>
      </div>

      <!-- Candidates Section -->
      <div class="candidates-section">
        <h2 class="section-title">ผลการลงคะแนนเสียงผู้สมัคร</h2>
        <div id="grid" class="candidates-grid"></div>
      </div>

      <!-- Action Buttons -->
      <div style="text-align:center; margin-top:20px; padding-top:15px; border-top:1px solid var(--border)">
        <a href="/up" class="btn" target="_blank">📤 อัปโหลดรูป</a>
        <a href="/reset" class="btn btn-secondary" onclick="return confirm('รีเซ็ตคะแนนทั้งหมด?')">🔄 รีเซ็ต</a>
        <a href="/ping" class="btn btn-secondary" target="_blank">📡 Ping</a>
      </div>
    </div>
  </div>
</div>
<script>
let cfg=null;
function el(t,c){const e=document.createElement(t); if(c) e.className=c; return e;}
function updateCurrentTime(){
  const now=new Date();
  const timeString=now.toLocaleString('th-TH',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
  const timeEl=document.getElementById('current-time');
  if(timeEl) timeEl.textContent=timeString;
}
async function loadCfg(){
  const r=await fetch('/cfg',{cache:'no-store'}); cfg=await r.json();
  const g=document.getElementById('grid'); g.innerHTML='';
  for(let i=0;i<10;i++){
    const card=el('div','candidate-card');
    const img=el('img','candidate-photo'); img.id='img'+i; img.src=cfg.photos[i]; img.onerror=function(){this.style.display='none';}; card.appendChild(img);
    const nm=el('div','candidate-name'); nm.textContent=`${cfg.names[i]} (${i})`; card.appendChild(nm);
    const val=el('div','candidate-votes'); val.id='v'+i; val.textContent='0'; card.appendChild(val);
    const pct=el('div','candidate-percentage'); pct.id='p'+i; pct.textContent='0% ของคะแนนสูงสุด'; card.appendChild(pct);
    const bar=el('div','progress-bar'); const fill=el('div','progress-fill'); fill.id='b'+i; bar.appendChild(fill); card.appendChild(bar);
    g.appendChild(card);
  }
  document.getElementById('ipinfo').textContent=`STA ${cfg.sta} | AP ${cfg.ap}`;
}
async function tick(){
  try{
    const r=await fetch('/tally',{cache:'no-store'}); if(!r.ok) throw 0;
    const j=await r.json();
    document.getElementById('total').textContent=j.total;
    let max=j.max||1;
    let leadingCandidate=null;
    let maxVotes=0;
    for(let i=0;i<10;i++){
      const v=j.counts[i]||0;
      const percentage=Math.round((v*100)/max);
      document.getElementById('v'+i).textContent=v;
      document.getElementById('p'+i).textContent=`${percentage}% ของคะแนนสูงสุด`;
      document.getElementById('b'+i).style.width=percentage+'%';
      const card=document.querySelector(`#grid .candidate-card:nth-child(${i+1})`);
      if(card) card.classList.remove('leading');
      if(v>maxVotes){maxVotes=v; leadingCandidate=i;}
    }
    if(leadingCandidate!==null && maxVotes>0){
      const leadingCard=document.querySelector(`#grid .candidate-card:nth-child(${leadingCandidate+1})`);
      if(leadingCard) leadingCard.classList.add('leading');
    }
    document.getElementById('dot').className='status-dot online';
    document.getElementById('net').textContent='ออนไลน์';
  }catch(e){
    document.getElementById('dot').className='status-dot offline';
    document.getElementById('net').textContent='ออฟไลน์';
  }
}
loadCfg().then(()=>{
  updateCurrentTime();
  setInterval(updateCurrentTime,1000);
  tick();
  setInterval(tick,1000);
});
</script></body></html>)HTML";

// ====== หน้าอัปโหลด (multiple) ======
static const char UP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Upload images</title>
<style>body{font-family:system-ui;max-width:700px;margin:24px auto} .box{border:1px solid #ccc;padding:16px;border-radius:10px}</style>
</head><body>
<h3>อัปโหลดรูปผู้สมัคร (เลือกได้หลายไฟล์)</h3>
<div class="box">
<form method="POST" action="/up" enctype="multipart/form-data">
<input type="file" name="files" accept=".jpg,.jpeg,.png" multiple>
<button type="submit">Upload</button>
<p>ชื่อไฟล์ที่แนะนำ: <code>0.jpg ... 9.jpg</code> (ระบบจะเก็บที่ <code>/img/</code>)</p>
</form>
</div>
<p><a href="/">⬅ กลับหน้า Dashboard</a></p>
</body></html>)HTML";

// ====== Handlers ======
void sendIndex(){ server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }
void sendUpPage(){ server.send_P(200, "text/html; charset=utf-8", UP_HTML); }

String urlFor(int i){ return String("/img/") + i + ".jpg"; }

void sendCfg(){
  String s; s.reserve(1600);
  s += F("{\"sta\":\""); s += WiFi.localIP().toString();
  s += F("\",\"ap\":\"");  s += WiFi.softAPIP().toString(); s += F("\",");

  s += F("\"names\":[");
  for(int i=0;i<10;i++){ if(i) s+=','; s += '\"'; s += NAME[i]; s += '\"'; }
  s += F("],\"photos\":[");
  for(int i=0;i<10;i++){ if(i) s+=','; s += '\"'; s += urlFor(i); s += '\"'; }
  s += F("]}");
  server.send(200,"application/json",s);
}

void sendTally(){
  int maxv=1; for(int i=0;i<10;i++) if(tally[i]>maxv) maxv=tally[i];
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

// ====== Upload handler ======
File fsUploadFile;
void handleUpload(){
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/" + filename;
    if(!filename.startsWith("/img/")) filename = "/img" + filename; // บังคับเก็บใน /img
    fsUploadFile = FILESYS.open(filename, FILE_WRITE);
  }else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
  }else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile) fsUploadFile.close();
  }
}

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

// ====== Setup ======
void setup(){
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW);

  // Mount FS
  if(!FILESYS.begin(true)){ Serial.println("[FS] mount fail"); }  // true = format if needed
  else {
    Serial.println("[FS] mounted");
    FILESYS.mkdir("/img");
  }

  // UART
  VOTE.begin(UART_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);
  Serial.println("[UART] Listen UNO on GPIO16");

  // Wi-Fi (STA + AP)
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
  // upload page + handler (multiple)
  server.on("/up", HTTP_GET, [](){ sendUpPage(); });
  server.on("/up", HTTP_POST, [](){ server.sendHeader("Location","/up"); server.send(303); }, handleUpload);

  // serve files under /img/*
  server.serveStatic("/img/", FILESYS, "/img/");

  server.onNotFound(handle404);
  server.begin();
  Serial.println("[WEB] Ready -> http://<STA-IP>/  หรือ  http://192.168.4.1/");
}

// ====== Loop ======
void loop(){
  // UART parse non-blocking
  uint8_t guard=0;
  while(VOTE.available() && guard++<80){
    char c=(char)VOTE.read();
    if(c=='\n'){ lineBuf.trim(); if(lineBuf.length()) tryParseLine(lineBuf); lineBuf=""; }
    else if(c!='\r'){ if(c>=32 && c<=126) lineBuf+=c; if(lineBuf.length()>64) lineBuf=""; }
  }
  server.handleClient();

  // ทดสอบจาก USB Serial
  if(Serial.available()){
    String s=Serial.readStringUntil('\n'); s.trim();
    if(s.length()) tryParseLine(s);
  }
}