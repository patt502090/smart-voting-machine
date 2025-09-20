const token = document.getElementById('token');
const msg   = document.getElementById('msg');
const tbl   = document.querySelector('#tbl tbody');
const upForm= document.getElementById('upForm');

function setMsg(text, type = 'success') {
  msg.textContent = text;
  msg.className = `status-msg status-${type}`;
  msg.style.display = 'block';
  
  // Auto hide after 5 seconds
  setTimeout(() => {
    msg.style.display = 'none';
  }, 5000);
}

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
  try {
    // ตารางตัวเลือก + รูป
    const rows = await call('/admin/options','GET');
    const photos = await fetch('/photos').then(r=>r.json()).catch(()=> ({}));
    tbl.innerHTML = '';
    
    for(const it of rows.options){
      const tr = document.createElement('tr');
      const img = photos[it.option] ? 
        `<img src="${photos[it.option]}" style="height:50px;border-radius:6px;object-fit:cover;border:2px solid var(--border)">` : 
        '<span style="color:var(--text-muted); font-style:italic;">ไม่มีรูป</span>';
      
      const status = it.count > 0 ? 
        `<span style="color:var(--success); font-weight:600;">มีคะแนน</span>` : 
        `<span style="color:var(--text-muted);">ยังไม่มีคะแนน</span>`;
      
      tr.innerHTML = `
        <td style="font-weight:600; color:var(--primary);">${it.option}</td>
        <td>${it.label}</td>
        <td style="font-weight:600; color:var(--primary);">${it.count.toLocaleString()}</td>
        <td>${img}</td>
        <td>${status}</td>
      `;
      tbl.appendChild(tr);
    }
    setMsg(`อัปเดตข้อมูลล่าสุด: ${new Date().toLocaleString('th-TH')}`, 'success');
  } catch(e) {
    setMsg(`เกิดข้อผิดพลาดในการโหลดข้อมูล: ${e.message}`, 'error');
  }
}

async function resetAll(){
  if(!confirm('คุณแน่ใจหรือไม่ที่จะรีเซ็ตคะแนนทั้งหมด? การดำเนินการนี้ไม่สามารถย้อนกลับได้')) {
    return;
  }
  try{ 
    await call('/admin/reset','POST'); 
    setMsg('✅ รีเซ็ตคะแนนทั้งหมดเรียบร้อยแล้ว', 'success'); 
    load(); 
  }
  catch(e){ setMsg(`❌ เกิดข้อผิดพลาดในการรีเซ็ต: ${e.message}`, 'error'); }
}

async function renameOpt(){
  const option = document.getElementById('optKey').value.trim();
  const label  = document.getElementById('optLabel').value.trim();
  if(!/^[0-9]$/.test(option)){ 
    setMsg('⚠️ กรุณาใส่หมายเลขผู้สมัคร 0-9', 'warning'); 
    return; 
  }
  if(!label){ 
    setMsg('⚠️ กรุณาใส่ชื่อผู้สมัครใหม่', 'warning'); 
    return; 
  }
  try{ 
    await call('/admin/options/rename','POST',{option,label}); 
    setMsg(`✅ เปลี่ยนชื่อผู้สมัครหมายเลข ${option} เป็น "${label}" เรียบร้อยแล้ว`, 'success'); 
    document.getElementById('optKey').value = '';
    document.getElementById('optLabel').value = '';
    load(); 
  }
  catch(e){ setMsg(`❌ เกิดข้อผิดพลาดในการเปลี่ยนชื่อ: ${e.message}`, 'error'); }
}

upForm.addEventListener('submit', async (ev)=>{
  ev.preventDefault();
  const files = document.getElementById('files').files;
  if(!files || !files.length){ 
    setMsg('⚠️ กรุณาเลือกไฟล์รูปภาพก่อนอัปโหลด', 'warning'); 
    return; 
  }
  
  const fd = new FormData();
  for(const f of files) fd.append('files', f);
  
  try{
    setMsg('📤 กำลังอัปโหลดไฟล์...', 'warning');
    const r = await fetch('/admin/upload', {method:'POST', headers:{'X-API-KEY': token.value||''}, body: fd});
    if(!r.ok) throw new Error(await r.text());
    setMsg(`✅ อัปโหลดไฟล์สำเร็จ (${files.length} ไฟล์)`, 'success'); 
    document.getElementById('files').value = '';
    load();
  }catch(e){ 
    setMsg(`❌ เกิดข้อผิดพลาดในการอัปโหลด: ${e.message}`, 'error'); 
  }
});

load(); setInterval(load, 2000);


