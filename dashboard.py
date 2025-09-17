# streamlit dashboard + robust serial reader for "CF:X"
import time, threading, collections
import streamlit as st
import pandas as pd
import serial
from serial.tools import list_ports

st.set_page_config(page_title="Smart Voting Dashboard", layout="wide")

# ---------- Shared state object (persist across reruns) ----------
class Shared:
    def __init__(self):
        self.lock = threading.Lock()
        self.tally = [0]*10
        self.total = 0
        self.connected_port = None
        self.last_error = ""
        self.last_seen = 0.0
        self.log = collections.deque(maxlen=300)
        self.want_port = None
        self.want_baud = 115200
        self.running = True
        self.thread_started = False

def ports_list():
    items = []
    for p in list_ports.comports():
        desc = p.description or ""
        items.append((p.device, f"{p.device}  •  {desc}"))
    return items

@st.cache_resource
def get_shared():
    return Shared()

shared = get_shared()

# ---------- Serial worker (auto-reconnect, exclusive open, DTR/RTS low) ----------
def open_serial(port, baud):
    ser = serial.Serial(
        port=port, baudrate=baud, timeout=0.2,
        rtscts=False, dsrdtr=False, exclusive=True
    )
    # กด DTR/RTS ลง กันบอร์ดรีเซ็ต
    ser.dtr = False
    ser.rts = False
    time.sleep(0.2)
    ser.reset_input_buffer()
    return ser

def choose_port(hint):
    # ถ้ามี hint ใช้เลย
    if hint:
        return hint
    # auto เลือกตัวที่มี "usbmodem"/"usbserial" หรือคำว่า Arduino
    for p in list_ports.comports():
        name = (p.description or "").lower()
        if "arduino" in name or "usbmodem" in p.device or "usbserial" in p.device:
            return p.device
    # ไม่เจอ
    return None

def serial_worker(shared: Shared):
    ser = None
    buf = bytearray()
    while shared.running:
        # เปิดพอร์ตถ้ายังไม่มี
        if ser is None:
            port = choose_port(shared.want_port)
            if not port:
                with shared.lock:
                    shared.connected_port = None
                    shared.last_error = "No serial port found"
                time.sleep(1.0)
                continue
            try:
                ser = open_serial(port, shared.want_baud)
                with shared.lock:
                    shared.connected_port = port
                    shared.last_error = ""
            except Exception as e:
                with shared.lock:
                    shared.connected_port = None
                    shared.last_error = f"open fail: {e}"
                time.sleep(1.0)
                continue

        # ถ้าผู้ใช้เปลี่ยนพอร์ต/บอดเรตใน UI → ปิด แล้วเปิดใหม่
        with shared.lock:
            need_port = shared.want_port
            need_baud = shared.want_baud
            cur_port  = shared.connected_port
        if need_port and cur_port and (need_port != cur_port):
            try: ser.close()
            except: pass
            ser = None
            continue
        if ser and ser.baudrate != need_baud:
            try: ser.baudrate = need_baud
            except:
                try: ser.close()
                except: pass
                ser = None
            continue

        # อ่านข้อมูล
        try:
            data = ser.read(ser.in_waiting or 1)
            if data:
                buf.extend(data)
                while b"\n" in buf:
                    line, _, buf = buf.partition(b"\n")
                    s = line.strip().decode("utf-8", errors="ignore")
                    with shared.lock:
                        shared.log.append(s)
                    # parse CF:X
                    if len(s) >= 4 and s.startswith("CF:") and s[3].isdigit():
                        d = ord(s[3]) - 48
                        if 0 <= d <= 9:
                            with shared.lock:
                                shared.tally[d] += 1
                                shared.total += 1
                                shared.last_seen = time.time()
            else:
                time.sleep(0.02)
        except serial.SerialException as e:
            with shared.lock:
                shared.last_error = f"lost: {e}"
                shared.connected_port = None
            try: ser.close()
            except: pass
            ser = None
            time.sleep(0.8)
        except Exception as e:
            with shared.lock:
                shared.last_error = f"err: {e}"
            time.sleep(0.2)

# start thread once
if not shared.thread_started:
    t = threading.Thread(target=serial_worker, args=(shared,), daemon=True)
    t.start()
    shared.thread_started = True

# ---------- Sidebar controls ----------
st.sidebar.header("Serial Settings")
plist = ports_list()
port_labels = ["(auto)"] + [label for (_, label) in plist]
port_values = [None] + [dev for (dev, _) in plist]

cur_port = None
with shared.lock: cur_port = shared.connected_port

default_index = 0
if cur_port and cur_port in port_values:
    default_index = port_values.index(cur_port)

sel = st.sidebar.selectbox("Port", port_labels, index=default_index)
sel_port = port_values[port_labels.index(sel)]
baud = st.sidebar.selectbox("Baudrate", [115200, 57600, 38400, 9600], index=0)

colA, colB = st.sidebar.columns(2)
apply_clicked = colA.button("Apply")
reset_clicked = colB.button("Reset counts")

if apply_clicked:
    with shared.lock:
        shared.want_port = sel_port
        shared.want_baud = int(baud)
        shared.last_error = ""

if reset_clicked:
    with shared.lock:
        shared.tally = [0]*10
        shared.total = 0
        shared.last_seen = 0.0

# ---------- Main UI ----------
st.title("Smart Voting — Live Tally (CF:X over Serial)")

with shared.lock:
    tally = shared.tally[:]
    total = shared.total
    connected_port = shared.connected_port
    last_error = shared.last_error
    last_seen = shared.last_seen
    log_lines = list(shared.log)[-15:]

status = "🟢 Connected" if connected_port else "🔴 Disconnected"
st.markdown(f"**Status:** {status}  |  **Port:** `{connected_port or '—'}`  |  **Baud:** `{baud}`")
if last_error:
    st.warning(last_error)

if total == 0:
    st.info("ยังไม่มีโหวตเข้ามา ลองกด confirm บน UNO ดูนะ")

cols = st.columns(5)
for i in range(10):
    with cols[i//2]:
        st.metric(label=f"Choice {i}", value=tally[i])

st.metric(label="TOTAL", value=total)

# bar chart
df = pd.DataFrame({"choice": list(range(10)), "count": tally}).set_index("choice")
st.bar_chart(df, height=260)

# log tail
st.subheader("Recent lines")
st.code("\n".join(log_lines) or "(no data yet)")

# auto refresh# --- Auto-refresh (native) ---
auto = st.sidebar.checkbox("Auto-refresh", value=True)
interval_ms = st.sidebar.slider("Refresh every (ms)", 500, 5000, 1000, 100)

if auto:
    time.sleep(interval_ms / 1000.0)
    try:
        st.rerun()  # Streamlit >= 1.25
    except Exception:
        st.experimental_rerun()  # เผื่อบางเครื่องยังเป็นเวอร์ชันเก่า