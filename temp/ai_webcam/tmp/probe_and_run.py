# probe_and_run.py
import os, sys, cv2, subprocess, shlex

# รายการโหมดยอดนิยม (กว้าง x สูง @ fps) ไล่จากใหญ่->เล็ก
CANDIDATES = [
    (3840,2160,30), (2560,1440,30),
    (1920,1080,60), (1920,1080,30),
    (1600, 900,30), (1280, 720,60), (1280, 720,30),
    (1024, 576,30), (800, 600,30), (640, 480,30),
    (640, 360,30)
]

SRC = int(os.getenv("CAM_SRC", "0")) if os.getenv("CAM_SRC","").isdigit() else os.getenv("CAM_SRC","0")
backend = cv2.CAP_AVFOUNDATION if sys.platform == "darwin" and isinstance(SRC,int) else 0

def try_mode(w,h,fps):
    cap = cv2.VideoCapture(SRC, backend) if backend else cv2.VideoCapture(SRC)
    if not cap.isOpened():
        return False
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS,          fps)
    ok, frm = cap.read()
    # อ่านค่า back จากกล้องจริง
    rw = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH) or 0)
    rh = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT) or 0)
    rfps = cap.get(cv2.CAP_PROP_FPS) or 0
    cap.release()
    return ok and rw>=w and rh>=h  # ยอมให้ fps เพี้ยนเล็กน้อย

best = None
for (w,h,fps) in CANDIDATES:
    if try_mode(w,h,fps):
        best = (w,h,fps)
        break

if not best:
    print("[Probe] ไม่พบโหมดในลิสต์ จะย้อนใช้ 640x360@15")
    best = (640,360,15)

w,h,fps = best
print(f"[Probe] ใช้โหมด: {w}x{h}@{fps}")

# ส่งต่อ env + เรียก near_person_distance.py
env = os.environ.copy()
env["CAM_W"]   = str(w)
env["CAM_H"]   = str(h)
env["CAM_FPS"] = str(fps)
# ตั้งค่าอื่น ๆ จาก env เดิมตามที่คุณใช้
cmd = f"python near_person_distance.py"
subprocess.run(shlex.split(cmd), env=env)
