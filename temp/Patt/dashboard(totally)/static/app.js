const grid = document.getElementById('grid');
const ts   = document.getElementById('ts');
const title= document.getElementById('title');
const meta = document.getElementById('meta');

let pollMs = 1000;
async function boot(){
  try{
    const c = await fetch('/config',{cache:'no-store'}).then(r=>r.json());
    title.textContent = c.name || 'Vote Dashboard';
    pollMs = c.poll_ms || 1000;
    meta.innerHTML = `<span>Refresh: ${pollMs} ms</span> <span>Export: <a class="link" href="/export.csv">CSV</a></span>`;
  }catch(e){}
}
async function refresh(){
  try{
    const data = await fetch('/tally',{cache:'no-store'}).then(r=>r.json());
    grid.innerHTML = '';
    const keys = Object.keys(data).sort((a,b)=> a.localeCompare(b, undefined, {numeric:true}));
    for(const k of keys){
      grid.innerHTML += `
        <div class="card">
          <div class="label">Option</div>
          <div class="value">${k}</div>
          <div class="label" style="margin-top:8px">Votes</div>
          <div class="value">${data[k]}</div>
        </div>`;
    }
    ts.textContent = 'Updated: ' + new Date().toLocaleString();
  }catch(e){ ts.textContent='Error loading...'; }
}
boot().then(()=>{ refresh(); setInterval(refresh, pollMs); });

