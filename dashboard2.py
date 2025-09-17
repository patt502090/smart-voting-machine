# dash.py — Smart Voting (one-page modern) + robust serial CF:X reader
# Run: streamlit run dash.py

import os, time, threading, collections
import pandas as pd
import streamlit as st
import serial
from serial.tools import list_ports

# ---- Optional: Pillow for nice placeholders ----
try:
    from PIL import Image, ImageOps, ImageDraw, ImageFont
    HAS_PIL = True
except Exception:
    HAS_PIL = False

# ---------------- base ----------------
st.set_page_config(page_title="Smart Voting", page_icon="🗳️", layout="wide")

# Support either `assets/` or `asset/` folders seamlessly
_here = os.path.dirname(__file__)
_assets_primary = os.path.join(_here, "assets")
_assets_fallback = os.path.join(_here, "asset")
ASSET_DIR = _assets_primary if os.path.isdir(_assets_primary) else _assets_fallback

CANDIDATE_NAMES = [
    "Abstain",
    "Candidate 1","Candidate 2","Candidate 3",
    "Candidate 4","Candidate 5","Candidate 6",
    "Candidate 7","Candidate 8","Candidate 9",
]
PALETTE = [
    "#64748b",  # 0  slate
    "#60a5fa",  # 1  blue
    "#34d399",  # 2  emerald
    "#f59e0b",  # 3  amber
    "#a78bfa",  # 4  violet
    "#f43f5e",  # 5  rose
    "#22d3ee",  # 6  cyan
    "#fb7185",  # 7  pink/red
    "#84cc16",  # 8  lime
    "#f97316",  # 9  orange
]

# ---------------- styles ----------------
st.markdown("""
<style>
#MainMenu{visibility:hidden} header, footer{visibility:hidden}
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap');
:root{
  --bg:#0b0f1a; --fg:#e7e9ee; --muted:#94a3b84d;
  --card:#0e1320; --card-2:#0c111c; --border:rgba(148,163,184,.18);
  --hi:#5b8cff; --hi2:#4dd5d3; --ok:#22c55e; --ng:#ef4444;
}
html, body, [data-testid="stAppViewContainer"]{
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, "Apple Color Emoji", "Segoe UI Emoji";
  -webkit-font-smoothing: antialiased; -moz-osx-font-smoothing: grayscale;
  background:
    radial-gradient(1200px 700px at 12% -10%, rgba(91,140,255,.08), transparent),
    radial-gradient(1000px 600px at 100% 0%, rgba(77,213,211,.08), transparent),
    linear-gradient(180deg, #0b0f1a 0%, #0a0e17 100%);
  color:var(--fg);
}
.hero{
  padding:16px 20px; border:1px solid var(--border); border-radius:16px;
  background:linear-gradient(180deg, rgba(255,255,255,.03), rgba(255,255,255,.01));
  backdrop-filter: blur(6px);
}
.status{display:flex;gap:12px;flex-wrap:wrap;color:#a3adc2;align-items:center}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:6px}
.ok{background:var(--ok);box-shadow:0 0 0 3px rgba(34,197,94,.20)}
.ng{background:var(--ng);box-shadow:0 0 0 3px rgba(239,68,68,.18)}
.metric{
  background:linear-gradient(180deg, rgba(255,255,255,.03), rgba(255,255,255,.01));
  border:1px solid var(--border); border-radius:16px; text-align:center;
  padding:18px 20px; color:#e5e9f2;
}
.metric .label{color:#9aa3b2;font-size:.85rem;letter-spacing:.02em}
.metric .value{font-size:2.6rem;font-weight:800;letter-spacing:-.02em}
.legend-card{
  display:flex; gap:12px; align-items:center;
  border:1px solid var(--border); border-radius:14px; padding:10px 12px;
  background:var(--card);
  transition: transform .15s ease, background .15s ease, border-color .15s ease;
}
.legend-card:hover{ transform: translateY(-2px); background:var(--card-2); border-color:rgba(148,163,184,.28); }
.legend-chip{width:10px;height:10px;border-radius:50%;}
.legend-name{font-weight:700}
.legend-count{margin-left:auto;font-weight:800;color:#c7d2fe}
.logbox{
  border:1px solid var(--border); background:linear-gradient(180deg, rgba(255,255,255,.03), rgba(255,255,255,.01));
  border-radius:14px; padding:12px 14px; font-family:ui-monospace, SFMono-Regular, Menlo, monospace;
  color:#cbd5e1; height:270px; overflow:auto; white-space:pre-wrap;
}
[data-testid="stSidebar"]{border-right:1px solid var(--border)}
[data-testid="stSidebar"] .stSelectbox, [data-testid="stSidebar"] .stSlider, [data-testid="stSidebar"] .stCheckbox{color:#e7e9ee}
[data-testid="stSidebar"] section{gap:8px}
[data-testid="stSidebar"] .stButton>button{width:100%; border-radius:10px; background:#141a29; border:1px solid var(--border)}
/* Altair chart tooltip tweaks */
.vega-embed .vega-actions{display:none}
</style>
""", unsafe_allow_html=True)

# ---------------- shared state ----------------
class Shared:
    def __init__(self):
        self.lock = threading.Lock()
        self.tally = [0]*10
        self.total = 0
        self.connected_port = None
        self.last_error = ""
        self.last_seen = 0.0
        self.log = collections.deque(maxlen=400)
        self.want_port = None
        self.want_baud = 115200
        self.running = True
        self.thread_started = False

def list_serials():
    items=[]
    for p in list_ports.comports():
        desc = p.description or ""
        items.append((p.device, f"{p.device}  •  {desc}"))
    return items

@st.cache_resource
def get_shared():
    return Shared()

shared = get_shared()

# ---------------- serial worker ----------------
def open_serial(port, baud):
    ser = serial.Serial(port=port, baudrate=baud, timeout=0.2,
                        rtscts=False, dsrdtr=False, exclusive=True)
    ser.dtr = False; ser.rts = False
    time.sleep(0.2); ser.reset_input_buffer()
    return ser

def choose_port(hint):
    if hint: return hint
    for p in list_ports.comports():
        name=(p.description or "").lower()
        if "arduino" in name or "usbmodem" in p.device or "usbserial" in p.device:
            return p.device
    return None

def serial_worker(shared:Shared):
    ser=None; buf=bytearray()
    while shared.running:
        if ser is None:
            port=choose_port(shared.want_port)
            if not port:
                with shared.lock:
                    shared.connected_port=None
                    shared.last_error="No serial port found"
                time.sleep(1.0); continue
            try:
                ser=open_serial(port, shared.want_baud)
                with shared.lock:
                    shared.connected_port=port
                    shared.last_error=""
            except Exception as e:
                with shared.lock:
                    shared.connected_port=None
                    shared.last_error=f"open fail: {e}"
                time.sleep(1.0); continue

        with shared.lock:
            need_port=shared.want_port; need_baud=shared.want_baud; cur=shared.connected_port
        if need_port and cur and (need_port!=cur):
            try: ser.close()
            except: pass
            ser=None; continue
        if ser and ser.baudrate!=need_baud:
            try: ser.baudrate=need_baud
            except:
                try: ser.close()
                except: pass
                ser=None
            continue

        try:
            data=ser.read(ser.in_waiting or 1)
            if data:
                buf.extend(data)
                while b"\n" in buf:
                    line,_,buf=buf.partition(b"\n")
                    s=line.strip().decode("utf-8", errors="ignore")
                    with shared.lock: shared.log.append(s)
                    if len(s)>=4 and s.startswith("CF:") and s[3].isdigit():
                        d=ord(s[3])-48
                        if 0<=d<=9:
                            with shared.lock:
                                shared.tally[d]+=1
                                shared.total+=1
                                shared.last_seen=time.time()
            else:
                time.sleep(0.02)
        except serial.SerialException as e:
            with shared.lock:
                shared.last_error=f"lost: {e}"
                shared.connected_port=None
            try: ser.close()
            except: pass
            ser=None; time.sleep(0.8)
        except Exception as e:
            with shared.lock: shared.last_error=f"err: {e}"
            time.sleep(0.2)

if not shared.thread_started:
    threading.Thread(target=serial_worker, args=(shared,), daemon=True).start()
    shared.thread_started=True

# ---------------- image helpers ----------------
def _measure(draw, text, font):
    if hasattr(draw, "textbbox"):
        l,t,r,b=draw.textbbox((0,0), text, font=font); return r-l, b-t
    try:
        return draw.textsize(text, font=font)
    except Exception:
        return (len(text)*18, 36)

@st.cache_data(show_spinner=False)
def load_candidate_image(i:int):
    path=os.path.join(ASSET_DIR, f"{i}.jpg")
    if os.path.exists(path):
        if HAS_PIL:
            try:
                img=Image.open(path).convert("RGB")
                return ImageOps.fit(img, (640,480))
            except Exception:
                return path
        return path
    # placeholder
    if not HAS_PIL: return None
    img=Image.new("RGB",(640,480),(16,21,33))
    d=ImageDraw.Draw(img)
    # soft radial
    for r in range(320, 40, -10):
        c=(20+int(240*(320-r)/320), 60+int(120*(r/320)), 160-int(120*(r/320)))
        d.ellipse([(320-r,240-r),(320+r,240+r)], fill=c)
    txt=str(i)
    try: font=ImageFont.truetype("Arial.ttf", 180)
    except: font=ImageFont.load_default()
    w,h=_measure(d,txt,font)
    d.text(((640-w)/2,(480-h)/2-10), txt, font=font, fill=(12,14,20))
    return img

# ---------------- sidebar ----------------
st.sidebar.header("Serial")
plist=list_serials()
labels=["(auto)"]+[lab for _,lab in plist]
values=[None]+[dev for dev,_ in plist]
with shared.lock: cur=shared.connected_port
idx=values.index(cur) if cur in values else 0
sel=st.sidebar.selectbox("Port", labels, index=idx)
sel_port=values[labels.index(sel)]
baud=st.sidebar.selectbox("Baudrate",[115200,57600,38400,9600], index=0)

a,b=st.sidebar.columns(2)
if a.button("Apply"):
    with shared.lock:
        shared.want_port=sel_port
        shared.want_baud=int(baud)
        shared.last_error=""
if b.button("Reset counts"):
    with shared.lock:
        shared.tally=[0]*10; shared.total=0; shared.last_seen=0.0

sort_desc = st.sidebar.checkbox("Sort bars by count (desc)", value=True)
auto = st.sidebar.checkbox("Auto-refresh", value=True)
interval_ms = st.sidebar.slider("Refresh (ms)", 600, 5000, 1000, 100)

# ---------------- header ----------------
with st.container():
    st.markdown('<div class="hero">', unsafe_allow_html=True)
    st.markdown("### 🗳️ Smart Voting — Live Overview", unsafe_allow_html=True)
    with shared.lock:
        port=shared.connected_port; err=shared.last_error; total=shared.total; last_seen=shared.last_seen
    dot = f'<span class="dot ok"></span>Connected' if port else f'<span class="dot ng"></span>Disconnected'
    seen = (" • last vote "
            + (f"{int(time.time()-last_seen)}s ago" if last_seen>0 else "—"))
    st.markdown(f'<div class="status">{dot} • Port: <code>{port or "—"}</code> • Baud: <code>{baud}</code>{seen}</div>', unsafe_allow_html=True)
    if err: st.warning(err)
    st.markdown('</div>', unsafe_allow_html=True)

# ---------------- main: one big section ----------------
with shared.lock:
    tally = shared.tally[:]
    total = shared.total
    logs  = list(shared.log)[-16:]

# 1) Big metric
m1, m2, m3 = st.columns([1.2,1,1.2])
with m2:
    st.markdown(f'<div class="metric"><div class="label">TOTAL VOTES</div><div class="value">{total}</div></div>', unsafe_allow_html=True)

st.write("")

# 2) Big colored bar chart (Altair) + recent log
import altair as alt
df = pd.DataFrame({
    "idx": list(range(10)),
    "candidate": [CANDIDATE_NAMES[i] for i in range(10)],
    "count": tally,
    "color": PALETTE
})

if sort_desc:
    df = df.sort_values("count", ascending=False).reset_index(drop=True)

chart = (
    alt.Chart(df, background='transparent')
      .mark_bar(cornerRadiusTopLeft=8, cornerRadiusTopRight=8)
      .encode(
          x=alt.X('candidate:N', sort=None,
                   axis=alt.Axis(
                       labelAngle=0,
                       labelColor="#cbd5e1",
                       labelFont="Inter",
                       title=None,
                   )),
          y=alt.Y('count:Q',
                   axis=alt.Axis(
                       grid=True,
                       gridOpacity=0.08,
                       tickColor="#2b3447",
                       labelColor="#cbd5e1",
                       labelFont="Inter",
                       title=None,
                   )),
          color=alt.Color('candidate:N',
                          scale=alt.Scale(domain=df["candidate"].tolist(), range=df["color"].tolist()),
                          legend=None),
          tooltip=[alt.Tooltip('candidate:N', title='Candidate'), alt.Tooltip('count:Q', title='Votes')]
      )
      .properties(height=380)
      .configure_view(stroke=None)
)

labels = chart.mark_text(dy=-8, color='#e2e8f0', fontSize=14, fontWeight='bold').encode(
    text=alt.condition(alt.datum.count > 0, 'count:Q', alt.value(''))
)

left, right = st.columns([1.8,1])
with left:
    st.subheader("Results")
    st.altair_chart(chart + labels, use_container_width=True)
with right:
    st.subheader("Live log")
    st.markdown('<div class="logbox">'+("\n".join(logs) or "(no data yet)")+'</div>', unsafe_allow_html=True)

st.write("")

# 3) Legend grid with avatar + color chip + live count
st.subheader("Candidates")
leg_cols = st.columns(5, gap="large")
order = df["idx"].tolist() if not sort_desc else [int(x) for x in df.sort_values("count", ascending=False)["idx"].tolist()]
for n,i in enumerate(order):
    with leg_cols[n % 5]:
        img = load_candidate_image(i)
        if img is not None:
            st.image(img, use_column_width=True)
        st.markdown(
            f'<div class="legend-card">'
            f'<div class="legend-chip" style="background:{PALETTE[i]};"></div>'
            f'<div class="legend-name">{CANDIDATE_NAMES[i]}</div>'
            f'<div class="legend-count">{tally[i]}</div>'
            f'</div>', unsafe_allow_html=True
        )

# 4) auto refresh
if auto:
    time.sleep(interval_ms/1000.0)
    try: st.rerun()
    except Exception: st.experimental_rerun()