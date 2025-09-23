import os, sys, time, math, threading, requests, subprocess
import cv2

# ---- config from env ----
SRC = os.getenv("CAM_SRC", "1")
SRC = int(SRC) if SRC.isdigit() else SRC
CAM_W  = int(os.getenv("CAM_W",  640))
CAM_H  = int(os.getenv("CAM_H",  360))
CAM_FPS= int(os.getenv("CAM_FPS",15))

SHOW   = int(os.getenv("SHOW", 1))       # เปิดหน้าต่างแสดงผล
DRAW   = int(os.getenv("DRAW", 1))       # วาดกรอบ/ตัวหนังสือ
CONF_TH= float(os.getenv("CONF_TH", 0.6))# ความมั่นใจต่ำสุดของ face det
NEAR_M = float(os.getenv("NEAR_M", 0.20))# ระยะ "ใกล้" (เมตร)
NEAR_KEEP = int(os.getenv("NEAR_KEEP", 2))
FAR_KEEP  = int(os.getenv("FAR_KEEP", 6))

# ขนาดหน้าเฉลี่ยตาม literature ~ 16 ซม. (แก้ให้ตรงกับหน้างานได้)
FACE_WIDTH_M = float(os.getenv("FACE_WIDTH_M", 0.16))

# pixel width ของใบหน้าที่ระยะ 0.5 m เพื่อคาลิเบรต focal_px
CALIB_PIX_AT_0_5M = float(os.getenv("CALIB_PIX_AT_0_5M", 240))

WAKE_URL  = os.getenv("WAKE_URL",  "")
SLEEP_URL = os.getenv("SLEEP_URL", "")
API_TOKEN = os.getenv("AI_TOKEN",  "")

# ---- GPIO poke config ----
# ค่า default เป็นคำสั่งที่คุณให้มา
GPIO_WAKE_CMD = os.getenv("GPIO_WAKE_CMD",
    "gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0")
# ถ้าต้องการเพียง log (เช่น บนเครื่อง dev) ให้ตั้ง ENV SIMULATE_GPIO=1
SIMULATE_GPIO = int(os.getenv("SIMULATE_GPIO", "0"))

# ---- focal from single-point calibration ----
FOCAL_PX = CALIB_PIX_AT_0_5M * 0.5 / FACE_WIDTH_M  # f = p * d / W

# ---- backend pick for mac ----
backend = 0
if sys.platform == "darwin" and isinstance(SRC, int):
    backend = cv2.CAP_AVFOUNDATION

# ---- light HTTP notify (async) ----
def _fire(url, payload):
    try:
        hdr = {"X-Token": API_TOKEN} if API_TOKEN else {}
        requests.post(url, json=payload, headers=hdr, timeout=1.5)
    except Exception:
        pass

def notify(url, payload):
    if not url: return
    threading.Thread(target=_fire, args=(url, payload), daemon=True).start()

# ---- run gpio command (async) ----
def _run_shell(cmd):
    try:
        # run via shell (so you can use ; and sleep)
        subprocess.run(["sh", "-c", cmd], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        # print error but don't crash
        print("[GPIO] command failed:", e, "stdout:", e.stdout, "stderr:", e.stderr)
    except Exception as e:
        print("[GPIO] exec error:", e)

def poke_gpio_async():
    """Spawn a thread to run the configured GPIO_WAKE_CMD (or just log if simulate)."""
    if SIMULATE_GPIO:
        print("[GPIO] SIMULATE -> would run:", GPIO_WAKE_CMD)
        return
    # spawn thread to avoid blocking main loop
    t = threading.Thread(target=_run_shell, args=(GPIO_WAKE_CMD,), daemon=True)
    t.start()

# ---- Face Detection (Haar) ----
face = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")

def faces_detect(gray):
    # ปรับ params ให้ “เอาใกล้/ชัด” มากขึ้น
    return face.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=5, minSize=(50,50))

# ---- main ----
def main():
    print(f"[INFO] focal_px ~ {FOCAL_PX:.1f}, near<{NEAR_M} m")
    print(f"[INFO] GPIO cmd: {GPIO_WAKE_CMD}  SIMULATE={SIMULATE_GPIO}")
    cap = cv2.VideoCapture(SRC, backend) if backend else cv2.VideoCapture(SRC)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  CAM_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_H)
    cap.set(cv2.CAP_PROP_FPS,          CAM_FPS)

    ok, _ = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError(f"Cannot open camera: {SRC}")

    near_cnt = 0
    far_cnt  = 0
    state_near = False
    last_dist = None

    win = "FaceDistance"
    if SHOW: cv2.namedWindow(win, cv2.WINDOW_NORMAL)

    while True:
        ok, frame = cap.read()
        if not ok: break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        # ปรับ contrast เล็กน้อยช่วย haar
        gray = cv2.equalizeHist(gray)

        dets = faces_detect(gray)
        nearest_m = None
        best = None
        for (x,y,w,h) in dets:
            # ระยะโดยใช้ความกว้างใบหน้า (พิกเซล)
            dist_m = (FACE_WIDTH_M * FOCAL_PX) / max(1.0, float(w))
            if nearest_m is None or dist_m < nearest_m:
                nearest_m = dist_m; best = (x,y,w,h)

        last_dist = nearest_m

        # hysteresis / debounce
        if nearest_m is not None and nearest_m <= NEAR_M:
            near_cnt += 1; far_cnt = 0
        else:
            far_cnt  += 1; near_cnt = 0

        # สลับสถานะ
        if not state_near and near_cnt >= NEAR_KEEP:
            state_near = True
            print(f"[NEAR] {nearest_m:.2f} m -> WAKE")
            # notify remote URL (if configured)
            notify(WAKE_URL,  {"event":"wake","distance_m":nearest_m})
            # poke GPIO to wake external device
            poke_gpio_async()

        if state_near and far_cnt >= FAR_KEEP:
            state_near = False
            print(f"[FAR] -> SLEEP")
            notify(SLEEP_URL, {"event":"sleep","distance_m":nearest_m})

        if SHOW:
            if DRAW:
                if best:
                    x,y,w,h = best
                    cv2.rectangle(frame,(x,y),(x+w,y+h),(0,255,0),2)
                    dtxt = f"{nearest_m:.2f} m" if nearest_m is not None else "N/A"
                    cv2.putText(frame, dtxt, (x, max(20,y-6)), cv2.FONT_HERSHEY_SIMPLEX, 0.7,(0,255,0),2, cv2.LINE_AA)
                status = "NEAR" if state_near else "IDLE"
                cv2.putText(frame, f"{status}  thr<{NEAR_M:.2f}m", (8, CAM_H-10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255,255,255),2, cv2.LINE_AA)
            cv2.imshow(win, frame)
            key = cv2.waitKey(1) & 0xFF
            if key in (27, ord('q')): break

    cap.release()
    if SHOW: cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
