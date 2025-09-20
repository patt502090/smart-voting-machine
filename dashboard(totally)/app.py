    import os, re, sqlite3, threading, time, csv, io, collections
from typing import Dict, List
from fastapi import FastAPI, Request, HTTPException, UploadFile, File
from fastapi.responses import FileResponse, StreamingResponse, HTMLResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi.middleware.gzip import GZipMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

APP_NAME   = os.getenv("APP_NAME", "Smart Voting — Dashboard")
DB_PATH    = os.getenv("VOTES_DB", "votes.db")
INIT_OPTS  = [s.strip() for s in os.getenv("VOTE_OPTIONS", "0,1,2,3,4,5,6,7,8,9").split(",") if s.strip()]
API_TOKEN  = os.getenv("API_TOKEN", "mysecret")
CORS_ALLOW = os.getenv("CORS", "*").split(",")
POLL_MS    = int(os.getenv("POLL_MS", "1000"))
HIST_SIZE  = int(os.getenv("HIST_SIZE", "600"))

lock = threading.Lock()
app = FastAPI(title=APP_NAME)
app.add_middleware(GZipMiddleware, minimum_size=512)
app.add_middleware(CORSMiddleware, allow_origins=CORS_ALLOW, allow_methods=["*"], allow_headers=["*"])

def init_db():
    os.makedirs("static/img", exist_ok=True)
    with sqlite3.connect(DB_PATH) as con:
        con.execute("""CREATE TABLE IF NOT EXISTS votes (option TEXT PRIMARY KEY, count INTEGER NOT NULL)""")
        con.execute("""CREATE TABLE IF NOT EXISTS names (option TEXT PRIMARY KEY, label TEXT NOT NULL)""")
        for opt in INIT_OPTS:
            con.execute("INSERT OR IGNORE INTO votes(option,count) VALUES(?,0)", (opt,))
            # label = เลขตัวเอง (0..9) ก่อน เปลี่ยนภายหลังได้ที่หน้า admin
            con.execute("INSERT OR IGNORE INTO names(option,label) VALUES(?,?)", (opt,opt))
init_db()

def tally_dict()->Dict[str,int]:
    with sqlite3.connect(DB_PATH) as con:
        rows = con.execute("SELECT option,count FROM votes ORDER BY option").fetchall()
    return {k:v for k,v in rows}

def names_dict()->Dict[str,str]:
    with sqlite3.connect(DB_PATH) as con:
        rows = con.execute("SELECT option,label FROM names ORDER BY option").fetchall()
    return {k:v for k,v in rows}

# history (in-memory)
HistoryPoint = dict
history = collections.deque(maxlen=HIST_SIZE)
def push_history():
    snap = tally_dict()
    history.append({"ts": int(time.time()), "total": sum(snap.values()), "counts": snap})
if not history: push_history()

class Vote(BaseModel):   option: str
class OptName(BaseModel): option: str; label: str
class OptOnly(BaseModel): option: str

def guarded(req: Request):
    if req.headers.get("X-API-KEY") != API_TOKEN:
        raise HTTPException(401, "invalid token")

@app.get("/config")
def config(): return {"name": APP_NAME, "poll_ms": POLL_MS}

@app.get("/tally")
def tally():
    t = tally_dict(); total = sum(t.values()); m = max(t.values()) if t else 1
    return {"total": total, "max": m, "counts": t, "labels": names_dict()}

@app.get("/history")
def get_history(): return list(history)

@app.get("/photos")
def get_photos():
    # คืนลิงก์รูป 0..9 ถ้าไม่มีไฟล์จะไม่ส่งคีย์นั้น
    out={}
    for i in range(10):
        p = f"static/img/{i}.jpg"
        if os.path.exists(p): out[str(i)] = f"/static/img/{i}.jpg?ts={int(os.path.getmtime(p))}"
    return out

@app.get("/export.csv")
def export_csv():
    buf=io.StringIO(); w=csv.writer(buf)
    keys=list(tally_dict().keys()); w.writerow(["ts","total"]+keys)
    for p in history: w.writerow([p["ts"],p["total"]]+[p["counts"].get(k,0) for k in keys])
    buf.seek(0)
    return StreamingResponse(iter([buf.read()]), media_type="text/csv",
        headers={"Content-Disposition":"attachment; filename=votes.csv"})

@app.post("/vote")
def vote(req: Request, v: Vote):
    guarded(req)
    with lock, sqlite3.connect(DB_PATH) as con:
        cur=con.execute("UPDATE votes SET count=count+1 WHERE option=?",(v.option,))
        if cur.rowcount==0: raise HTTPException(400,"unknown option (must be 0-9)")
    push_history(); return {"ok": True}

# ---------- Admin ----------
@app.post("/admin/reset")
def reset(req: Request):
    guarded(req)
    with lock, sqlite3.connect(DB_PATH) as con:
        con.execute("UPDATE votes SET count=0")
    push_history(); return {"ok": True}

@app.get("/admin/options")
def list_options(req: Request):
    guarded(req)
    t=tally_dict(); n=names_dict()
    return {"options":[{"option":k,"label":n.get(k,k),"count":t[k]} for k in t.keys()]}

@app.post("/admin/options/rename")
def rename(req: Request, body: OptName):
    guarded(req)
    opt=body.option.strip(); lab=body.label.strip()
    if not (opt and lab): raise HTTPException(400,"option/label required")
    with lock, sqlite3.connect(DB_PATH) as con:
        con.execute("UPDATE names SET label=? WHERE option=?", (lab,opt))
    return {"ok": True}

# อัปโหลดรูปหลายไฟล์: ชื่อไฟล์ต้องเป็น 0.jpg..9.jpg (แนะนำ .jpg)
@app.post("/admin/upload")
async def upload(req: Request, files: List[UploadFile]=File(...)):
    guarded(req)
    os.makedirs("static/img", exist_ok=True)
    saved=[]
    for f in files:
        name=os.path.basename(f.filename)
        m=re.match(r'^([0-9])\.(jpg|jpeg|png)$', name, re.IGNORECASE)
        if not m: continue
        idx=m.group(1); ext=m.group(2).lower()
        if ext=="jpeg": ext="jpg"
        if ext=="png":  ext="jpg"  # บังคับบันทึกเป็น .jpg ชื่อเดียวกัน (แนะนำอัปโหลดเป็น jpg อยู่แล้ว)
        dest=f"static/img/{idx}.jpg"
        with open(dest,"wb") as o: o.write(await f.read())
        saved.append(dest)
    return {"ok": True, "saved": saved}

# Static & Pages
# Static & Pages
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/", response_class=HTMLResponse)
def page_index():
    return FileResponse("static/index.html")

@app.get("/admin", response_class=HTMLResponse)
def page_admin():
    return FileResponse("static/admin.html")



