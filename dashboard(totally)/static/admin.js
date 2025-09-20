const token = document.getElementById('token');
const msg   = document.getElementById('msg');
const tbl   = document.querySelector('#tbl tbody');
const upForm= document.getElementById('upForm');

function setMsg(s){ msg.textContent = s; }

async function call(path, method='GET', body){
  const r = await fetch(path, {
    method,
    headers: {'Content-Type':'application/json','X-API-KEY': token.value||''},
    body: body? JSON.stringify(body) : undefined
  });
  if(!r.ok) throw new Error((await r.text()) || r.statusText);
  return await r.json().catch(()=> ({}));
}

async function load(){
  // ตารางตัวเลือก + รูป
  const rows = await call('/admin/options','GET');
  const photos = await fetch('/photos').then(r=>r.json()).catch(()=> ({}));
  tbl.innerHTML = '';
  for(const it of rows.options){
    const tr = document.createElement('tr');
    const img = photos[it.option] ? `<img src="${photos[it.option]}" style="height:50px;border-radius:6px">` : '-';
    tr.innerHTML = `<td>${it.option}</td><td>${it.label}</td><td>${it.count}</td><td>${img}</td>`;
    tbl.appendChild(tr);
  }
  setMsg('Loaded: '+new Date().toLocaleString());
}

async function resetAll(){
  try{ await call('/admin/reset','POST'); setMsg('Reset done'); load(); }
  catch(e){ setMsg('Reset error: '+e.message); }
}

async function renameOpt(){
  const option = document.getElementById('optKey').value.trim();
  const label  = document.getElementById('optLabel').value.trim();
  if(!/^[0-9]$/.test(option)){ setMsg('กรุณาใส่หมายเลข 0-9'); return; }
  if(!label){ setMsg('กรุณาใส่ชื่อใหม่'); return; }
  try{ await call('/admin/options/rename','POST',{option,label}); setMsg('Renamed'); load(); }
  catch(e){ setMsg('Rename error: '+e.message); }
}

upForm.addEventListener('submit', async (ev)=>{
  ev.preventDefault();
  const files = document.getElementById('files').files;
  if(!files || !files.length){ setMsg('ยังไม่ได้เลือกไฟล์'); return; }
  const fd = new FormData();
  for(const f of files) fd.append('files', f);
  try{
    const r = await fetch('/admin/upload', {method:'POST', headers:{'X-API-KEY': token.value||''}, body: fd});
    if(!r.ok) throw new Error(await r.text());
    setMsg('อัปโหลดสำเร็จ'); load();
  }catch(e){ setMsg('Upload error: '+e.message); }
});

load(); setInterval(load, 2000);


