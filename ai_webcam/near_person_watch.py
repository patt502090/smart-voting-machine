#!/usr/bin/env python3
import os, sys, time, math, cv2, threading, requests
from ultralytics import YOLO

# ====== ENV (ปรับได้) ======
SRC        = os.getenv("CAM_SRC", "0")            # "0" หรืออุปกรณ์เช่น "/dev/video0"
WIDTH      = int(os.getenv("CAM_W", "640"))
HEIGHT     = int(os.getenv("CAM_H", "360"))
FPS        = int(os.getenv("CAM_FPS", "15"))

CONF_TH    = float(os.getenv("CONF_TH", "0.5"))   # YOLO confidence
NEAR_M     = float(os.getenv("NEAR_M", "0.5"))    # เกณฑ์ใกล้ (เมตร)  <-- เป้าหมาย 0.5 m
NEAR_KEEP  = int(os.getenv("NEAR_KEEP", "2"))     # ต้องใกล้ติดกันกี่เฟรมถึงจะเปลี่ยนสถานะ
FAR_KEEP   = int(os.getenv("FAR_KEEP", "6"))      # ต้องไกลติดกันกี่เฟรมถึงจะเปลี่ยนสถานะ

# โหมด A: ค่าคาลิเบรต (พิกเซล) ของ "ความสูงกรอบคน" ที่ 0.5 m
CALIB_PIX_AT_0_5M = float(os.getenv("CALIB_PIX_AT_0_5M", "0"))  # 0 = ไม่ใช้โหมด A

# โหมด B: สูตรมุมมองกล้อง (ถ้าไม่ใช้คาลิเบรต)
FOVY_DEG   = float(os.getenv("FOVY_DEG", "49"))   # มุมมองแนวตั้ง (deg) ของกล้องคุณ (ประมาณได้จากสเปค)
ASSUME_HEIGHT_M = float(os.getenv("ASSUME_HEIGHT_M", "1.7"))  # ส่วนสูงคนสมมติ (เมตร)

WAKE_URL   = os.getenv("WAKE_URL", "")            # webhook เมื่อเข้าใกล้
SLEEP_URL  = os.getenv("SLEEP_URL", "")           # webhook เมื่อห่าง
TOKEN      = os.getenv("AI_TOKEN", "")
MODEL_PATH = os.getenv("MODEL", "yolov8n.pt")

SHOW       = os.getenv("SHOW", "1") == "1"        # เปิดหน้าต่างพรีวิวไหม
DRAW       = os.getenv("DRAW", "1") == "1"        # วาดกรอบ

def open_camera():
    if sys.platform == "darwin":
        cap = cv2.VideoCapture(0 if SRC=="0" else int(SRC) if SRC.isdigit() else SRC, cv2.CAP_AVFOUNDATION)
        try: cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        except Exception: pass
    else:
        cap = cv2.VideoCapture(0 if SRC=="0" else int(SRC) if SRC.isdigit() else SRC)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, HEIGHT)
    cap.set(cv2.CAP_PROP_FPS,          FPS)
    ok, _ = cap.read()
    return cap if ok else None

def post(url, payload):
    if not url: return
    headers = {"Authorization": f"Bearer {TOKEN}"} if TOKEN else {}
    try:
        requests.post(url, json=payload, headers=headers, timeout=2)
    except Exception:
        pass

# ---- ระยะจากกรอบ (สองโหมด) ----
def estimate_distance_m_from_bbox_h(bbox_h_px: int, img_h_px: int) -> float:
    if bbox_h_px <= 0: 
        return 1e9

    # โหมด A: ใช้คาลิเบรตตรงๆ (สัดส่วนกลับกัน)
    if CALIB_PIX_AT_0_5M > 0:
        # ที่ 0.5 m ได้ความสูงกรอบ ~ CALIB_PIX_AT_0_5M px
        # ระยะประมาณ: d ≈ 0.5 * (CALIB_PIX_AT_0_5M / bbox_h_px)
        return 0.5 * (CALIB_PIX_AT_0_5M / float(bbox_h_px))

    # โหมด B: pinhole + FOVy + ส่วนสูงคนสมมติ
    # f_pixels = (H/2) / tan(FOVy/2),  distance ≈ (H_real * f_pixels) / h_pixels
    f_pixels = (img_h_px / 2.0) / math.tan(math.radians(FOVY_DEG) / 2.0)
    d = (ASSUME_HEIGHT_M * f_pixels) / float(bbox_h_px)
    return d

def main():
    print("[INFO] Loading model:", MODEL_PATH)
    model = YOLO(MODEL_PATH)

    cap = open_camera()
    if not cap:
        raise RuntimeError(f"Cannot open camera: {SRC}")

    near_cnt = 0
    far_cnt  = 0
    state_near = False

    print(f"[INFO] Running... SRC={SRC} {WIDTH}x{HEIGHT}@{FPS}  NEAR_M={NEAR_M}m  "
          f"{'CALIB' if CALIB_PIX_AT_0_5M>0 else 'FOVY'} mode")

    last_print = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            time.sleep(0.02)
            continue

        results = model.predict(frame, conf=CONF_TH, verbose=False, classes=[0])  # 0=person
        dists = []
        best = None

        if results and len(results):
            r = results[0]
            for b in r.boxes:
                if int(b.cls) != 0: 
                    continue
                x1, y1, x2, y2 = map(int, b.xyxy[0].tolist())
                conf = float(b.conf)
                hpx = max(1, y2 - y1)
                dist_m = estimate_distance_m_from_bbox_h(hpx, frame.shape[0])
                dists.append((dist_m, (x1,y1,x2,y2,conf,hpx)))
                if best is None or dist_m < best[0]:
                    best = (dist_m, (x1,y1,x2,y2,conf,hpx))

        # ใช้คนที่ใกล้ที่สุดเป็นตัวตัดสิน
        is_near = False
        if best:
            is_near = best[0] <= NEAR_M

        if is_near:
            near_cnt += 1; far_cnt = 0
            if (not state_near) and near_cnt >= NEAR_KEEP:
                state_near = True
                print(f"[EVENT] NEAR ({best[0]:.2f} m) -> WAKE")
                threading.Thread(target=post, args=(WAKE_URL, {"event":"near","dist":best[0]}), daemon=True).start()
        else:
            far_cnt += 1; near_cnt = 0
            if state_near and far_cnt >= FAR_KEEP:
                print("[EVENT] FAR -> SLEEP")
                state_near = False
                threading.Thread(target=post, args=(SLEEP_URL, {"event":"far"}), daemon=True).start()

        if SHOW:
            view = frame.copy()
            if DRAW and best:
                x1,y1,x2,y2,conf,hpx = best[1]
                color = (0,0,255) if state_near else (0,255,0)
                cv2.rectangle(view,(x1,y1),(x2,y2),color,2)
                cv2.putText(view, f"{best[0]:.2f} m  conf={conf:.2f}",
                            (x1, max(20,y1-8)), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255,255,255), 2, cv2.LINE_AA)
            cv2.putText(view, "NEAR" if state_near else "FAR", (8,24),
                        cv2.FONT_HERSHEY_DUPLEX, 0.8, (0,0,255) if state_near else (220,220,220), 2, cv2.LINE_AA)
            cv2.imshow("near-person (distance approx.)", view)
            if cv2.waitKey(1) & 0xFF == 27:
                break

        now = time.time()
        if now - last_print > 3:
            last_print = now
            if best:
                print(f"[STAT] nearest={best[0]:.2f}m conf={best[1][4]:.2f} hpx={best[1][5]}")
            else:
                print("[STAT] no person")

    cap.release()
    if SHOW: cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
