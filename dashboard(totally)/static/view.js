const title = document.getElementById('title');
const meta  = document.getElementById('meta');
const cards = document.getElementById('cards');
const totalEl = document.getElementById('total');
const bars = document.getElementById('bars');
const ts   = document.getElementById('ts');
const line = document.getElementById('line');

let pollMs = 1000;
let labels = {};
let photos = {};
let lastHistory = [];

function fmt(n){ return n.toLocaleString(); }

function cardHtml(k, v){
  const label = labels[k] ?? k;
  const img = photos[k] ?? ''; // ถ้ายังไม่มีไฟล์ จะเป็นช่องว่าง
  return `
    <div class="card">
      <div class="label">หมายเลข</div>
      <div class="value">${label} (${k})</div>
      <div class="label" style="margin-top:6px">คะแนน</div>
      <div class="value">${fmt(v)}</div>
      ${img ? `<img class="photo" src="${img}" alt="${label}">` : ''}
    </div>`;
}

function drawBars(counts){
  const entries = Object.entries(counts);
  const total = entries.reduce((s,[,v])=> s+v, 0) || 1;
  bars.innerHTML = '';
  for(const [k,v] of entries){
    const pct = Math.round((v*100)/total);
    const row = document.createElement('div');
    row.style.margin = '6px 0';
    row.innerHTML = `
      <div class="row" style="justify-content:space-between">
        <div class="label">${labels[k]||k}</div>
        <div class="label">${fmt(v)} (${pct}%)</div>
      </div>
      <div class="bar"><div class="fill" style="width:${pct}%;"></div></div>`;
    bars.appendChild(row);
  }
}

function drawLine(history){
  const ctx = line.getContext('2d');
  const W = line.clientWidth, H = line.clientHeight;
  line.width = W * devicePixelRatio;
  line.height= H * devicePixelRatio;
  ctx.scale(devicePixelRatio, devicePixelRatio);
  ctx.clearRect(0,0,W,H);

  if(history.length < 2) return;
  const ys = history.map(p=>p.total);
  const min = Math.min(...ys), max = Math.max(...ys);
  const pad = 10;
  const xstep = (W - pad*2) / (history.length - 1);
  const yscale = (H - pad*2) / ((max - min) || 1);

  ctx.strokeStyle = '#6ea8fe';
  ctx.lineWidth = 2;
  ctx.beginPath();
  for(let i=0;i<history.length;i++){
    const x = pad + i*xstep;
    const y = H - pad - ( (history[i].total - min) * yscale );
    if(i==0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
  }
  ctx.stroke();

  const grad = ctx.createLinearGradient(0,0,0,H);
  grad.addColorStop(0,'rgba(110,168,254,0.35)');
  grad.addColorStop(1,'rgba(110,168,254,0.02)');
  ctx.fillStyle = grad;
  ctx.lineTo(W-pad,H-pad); ctx.lineTo(pad,H-pad); ctx.closePath(); ctx.fill();
}

async function boot(){
  try{
    const c = await fetch('/config').then(r=>r.json());
    title.textContent = c.name || 'Smart Voting — Dashboard';
    pollMs = c.poll_ms || 1000;
    meta.innerHTML = `<span>Refresh: ${pollMs} ms</span> <span><a class="btn" href="/export.csv">Export CSV</a></span>`;
  }catch(e){}
  await refresh(true);
  setInterval(refresh, pollMs);
}

async function refresh(first=false){
  // ชื่อ/คะแนน
  const t = await fetch('/tally',{cache:'no-store'}).then(r=>r.json());
  labels = t.labels || {};
  totalEl.textContent = fmt(t.total);
  ts.textContent = 'Updated: ' + new Date().toLocaleString();

  // รูป (0..9) – มี cache bust
  photos = await fetch('/photos',{cache:'no-store'}).then(r=>r.json()).catch(()=> ({}));

  const entries = Object.entries(t.counts).sort((a,b)=> parseInt(a[0])-parseInt(b[0]));
  cards.innerHTML = entries.map(([k,v])=> cardHtml(k,v)).join('');
  drawBars(t.counts);

  // history
  const hist = await fetch('/history',{cache:'no-store'}).then(r=>r.json()).catch(()=> []);
  lastHistory = hist; drawLine(lastHistory);
}
boot();


