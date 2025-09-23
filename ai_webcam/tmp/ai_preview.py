# ai_preview.py
import os, sys, cv2, time
from fastapi import FastAPI
from fastapi.responses import HTMLResponse, StreamingResponse
from ultralytics import YOLO

def _env_int(name, default):
    try: return int(os.environ.get(name, str(default)))
    except: return default

SRC_RAW = os.environ.get("CAM_SRC", "0")
SRC = int(SRC_RAW) if SRC_RAW.isdigit() else SRC_RAW
W   = _env_int("CAM_W",   640)
H   = _env_int("CAM_H",   360)
FPS = _env_int("CAM_FPS", 15)

cap = None
model = None

def open_camera(src, w, h, fps):
    if sys.platform == "darwin":
        cap = cv2.VideoCapture(src, cv2.CAP_AVFOUNDATION)
    else:
        cap = cv2.VideoCapture(src)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS,          fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE,   1)
    ok, _ = cap.read()
    return cap if ok else None

app = FastAPI()

@app.on_event("startup")
def on_start():
    global cap, model
    cap = open_camera(SRC, W, H, FPS)
    if cap is None:
        raise RuntimeError(f"Cannot open camera: {SRC}")
    # โหลดโมเดลเบา ๆ (ถ้า pin เวอร์ชั่นแล้วจะเสถียรกว่า)
    model = YOLO("yolov8n.pt")

@app.on_event("shutdown")
def on_stop():
    global cap
    if cap: cap.release()

@app.get("/", response_class=HTMLResponse)
def index():
    return """<!doctype html><meta charset="utf-8">
    <h3>AI Preview</h3>
    <p><a href="/video">/video</a> = MJPEG stream</p>
    <img src="/video" style="max-width:100%;"/>"""

def mjpeg_gen():
    last_t = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            time.sleep(0.05); continue
        # (เลือกจะวาด bbox ก็ได้ แต่เพื่อความลื่น แสดงดิบๆก่อน)
        # results = model(frame, verbose=False)
        # for r in results:
        #     for b in r.boxes.xyxy.cpu().numpy().astype(int):
        #         x1,y1,x2,y2 = b[:4]
        #         cv2.rectangle(frame, (x1,y1), (x2,y2), (0,255,0), 2)
        ret, jpg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
        if not ret: continue
        yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + jpg.tobytes() + b"\r\n")

@app.get("/video")
def video():
    return StreamingResponse(mjpeg_gen(), media_type="multipart/x-mixed-replace; boundary=frame")
