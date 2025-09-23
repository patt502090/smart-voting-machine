#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
present.py — Face-near wake helper with modern-gov UI

ENV knobs (all optional):
  CAM_SRC=0|1|/dev/videoX|rtsp://...      camera source (default 1)
  CAM_W=640 CAM_H=360 CAM_FPS=15          camera size & fps
  SHOW=1 DRAW=1                           show window / draw UI
  ROI_XF=0.20 ROI_YF=0.15 ROI_WF=0.60 ROI_HF=0.70   ROI as fractions
  NEAR_M=0.20                             "near" distance (m)
  NEAR_KEEP=3 FAR_KEEP=8                  debounced state flips
  MIN_DWELL_MS=900                        must stand in ROI >= ms
  COOLDOWN_S=8                            lockout after wake (s)
  HAAR_SCALE=1.12 HAAR_NEIGH=7 HAAR_MINSZ=100   stricter face det
  FACE_WIDTH_M=0.16 CALIB_PIX_AT_0_5M=240        FOCAL calibration
  GPIO_WAKE_CMD='gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0'
  SIMULATE_GPIO=0                         log instead of running cmd
  WAKE_URL=... SLEEP_URL=... AI_TOKEN=... webhook headers
  APP_TITLE='Ministry Smart Kiosk'
  BANNER_TXT='กรุณายืนภายในกรอบเพื่อตื่นหน้าจอ / Please stand inside the frame'
  LOGO_PATH=./logo_university.png LOGO_MAXW=160 LOGO_ANCHOR=top-right|top-left
  ACCENT_BRG=220                          accent brightness (190..255)
"""

import os, sys, time, math, threading, subprocess
import cv2
import numpy as np
import requests

# -------------------- Config --------------------
def get_env_int(key, default): return int(os.getenv(key, str(default)))
def get_env_float(key, default): return float(os.getenv(key, str(default)))

SRC = os.getenv("CAM_SRC", "1"); SRC = int(SRC) if SRC.isdigit() else SRC
CAM_W  = get_env_int("CAM_W", 640)
CAM_H  = get_env_int("CAM_H", 360)
CAM_FPS= get_env_int("CAM_FPS", 15)

SHOW   = get_env_int("SHOW", 1)
DRAW   = get_env_int("DRAW", 1)

# ROI as fractions
ROI_XF = get_env_float("ROI_XF", 0.20)
ROI_YF = get_env_float("ROI_YF", 0.15)
ROI_WF = get_env_float("ROI_WF", 0.60)
ROI_HF = get_env_float("ROI_HF", 0.70)

# Face near logic
NEAR_M      = get_env_float("NEAR_M", 0.20)
NEAR_KEEP   = get_env_int("NEAR_KEEP", 3)
FAR_KEEP    = get_env_int("FAR_KEEP", 8)
MIN_DWELL_MS= get_env_int("MIN_DWELL_MS", 900)
COOLDOWN_S  = get_env_float("COOLDOWN_S", 8.0)

# Haar tuning
HAAR_SCALE  = get_env_float("HAAR_SCALE", 1.12)
HAAR_NEIGH  = get_env_int("HAAR_NEIGH", 7)
HAAR_MINSZ  = get_env_int("HAAR_MINSZ", 100)

# Simple focal calibration
FACE_WIDTH_M       = get_env_float("FACE_WIDTH_M", 0.16)   # ~16cm
CALIB_PIX_AT_0_5M  = get_env_float("CALIB_PIX_AT_0_5M", 240)
FOCAL_PX = CALIB_PIX_AT_0_5M * 0.5 / max(FACE_WIDTH_M, 1e-6)
NEAR_PX  = (FACE_WIDTH_M * FOCAL_PX) / max(NEAR_M, 1e-6)

# GPIO / webhooks
GPIO_WAKE_CMD = os.getenv("GPIO_WAKE_CMD", "gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0")
SIMULATE_GPIO = get_env_int("SIMULATE_GPIO", 0)
WAKE_URL  = os.getenv("WAKE_URL",  "")
SLEEP_URL = os.getenv("SLEEP_URL", "")
AI_TOKEN  = os.getenv("AI_TOKEN",  "")

# UI text
APP_TITLE  = os.getenv("APP_TITLE", "Ministry Smart Kiosk")
BANNER_TXT = os.getenv("BANNER_TXT","กรุณายืนภายในกรอบเพื่อตื่นหน้าจอ / Please stand inside the frame")

# Logo
LOGO_PATH   = os.getenv("LOGO_PATH", "")
LOGO_MAXW   = get_env_int("LOGO_MAXW", 160)
LOGO_ANCHOR = os.getenv("LOGO_ANCHOR", "top-right").lower()  # top-right | top-left

# Accent brightness (govern-ish)
ACCENT_BRG  = get_env_int("ACCENT_BRG", 220)  # 190..255 recommended

# Backend for mac
backend = 0
if sys.platform == "darwin" and isinstance(SRC, int):
    backend = cv2.CAP_AVFOUNDATION

# -------------------- Helpers --------------------
def _fire(url, payload):
    try:
        hdr = {"X-Token": AI_TOKEN} if AI_TOKEN else {}
        requests.post(url, json=payload, headers=hdr, timeout=1.5)
    except Exception:
        pass

def notify(url, payload):
    if not url: return
    threading.Thread(target=_fire, args=(url, payload), daemon=True).start()

def _run_shell(cmd):
    try:
        subprocess.run(["sh","-c",cmd], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        print("[GPIO] failed:", e, "stderr:", (e.stderr or b"").decode(errors="ignore"))
    except Exception as e:
        print("[GPIO] exec error:", e)

def poke_gpio_async():
    if SIMULATE_GPIO:
        print("[GPIO] SIMULATE ->", GPIO_WAKE_CMD)
        return
    threading.Thread(target=_run_shell, args=(GPIO_WAKE_CMD,), daemon=True).start()

# UI primitives
def put_text_shadow(img, text, org, font, scale, color, thickness=1):
    x,y = org
    cv2.putText(img, text, (x+1,y+1), font, scale, (0,0,0), thickness+2, cv2.LINE_AA)
    cv2.putText(img, text, (x, y),    font, scale, color, thickness, cv2.LINE_AA)

def draw_round_rect(img, pt1, pt2, color, radius=10, thickness=2):
    # simple rounded rect using filled + eroded corners
    x1,y1 = pt1; x2,y2 = pt2
    w,h = x2-x1, y2-y1
    overlay = img.copy()
    cv2.rectangle(overlay,(x1+radius,y1),(x2-radius,y2),color,thickness)
    cv2.rectangle(overlay,(x1,y1+radius),(x2,y2-radius),color,thickness)
    # corners as circles
    for cx,cy in [(x1+radius,y1+radius),(x2-radius,y1+radius),(x1+radius,y2-radius),(x2-radius,y2-radius)]:
        cv2.circle(overlay,(cx,cy),radius,color,thickness, cv2.LINE_AA)
    cv2.addWeighted(overlay,1.0,img,0.0,0,img)

def fill_round_rect(img, pt1, pt2, color, alpha=0.2, radius=12):
    x1,y1 = pt1; x2,y2 = pt2
    overlay = img.copy()
    # draw filled with rounding by drawing 3 rects + 4 circles
    cv2.rectangle(overlay,(x1+radius,y1),(x2-radius,y2),color,-1)
    cv2.rectangle(overlay,(x1,y1+radius),(x2,y2-radius),color,-1)
    for cx,cy in [(x1+radius,y1+radius),(x2-radius,y1+radius),(x1+radius,y2-radius),(x2-radius,y2-radius)]:
        cv2.circle(overlay,(cx,cy),radius,color,-1, cv2.LINE_AA)
    cv2.addWeighted(overlay,alpha,img,1-alpha,0,img)

def overlay_png(dst, png_rgba, pos):
    """Alpha blend RGBA PNG onto BGR image at pos (x,y)."""
    x,y = pos
    h,w = png_rgba.shape[:2]
    if x>=dst.shape[1] or y>=dst.shape[0]: return
    x2 = min(dst.shape[1], x+w); y2 = min(dst.shape[0], y+h)
    if x2<=x or y2<=y: return
    sub = dst[y:y2, x:x2]
    png = png_rgba[0:(y2-y), 0:(x2-x)]
    if png.shape[2]==3:
        # no alpha; just paste
        dst[y:y2, x:x2] = png
        return
    bgr = png[:,:,:3]
    alpha = png[:,:,3:4].astype(np.float32)/255.0
    sub[:] = (alpha*bgr + (1-alpha)*sub).astype(np.uint8)

def resize_logo(logo, maxw):
    h,w = logo.shape[:2]
    if w <= maxw: return logo
    sc = maxw/float(w)
    return cv2.resize(logo, (int(w*sc), int(h*sc)), interpolation=cv2.INTER_AREA)

def place_logo(vis, logo):
    if logo is None: return
    lg = resize_logo(logo, LOGO_MAXW)
    pad = 10
    if LOGO_ANCHOR == "top-left":
        overlay_png(vis, lg, (pad, pad))
    else:  # top-right
        lw = lg.shape[1]
        overlay_png(vis, lg, (vis.shape[1]-pad-lw, pad))

def gov_colors(brg=220):
    # navy header, teal accent, soft gray
    brg = np.clip(brg, 160, 255)
    header   = (60, 60, 80)          # BGR deep slate
    accent   = (min(255, int(brg*0.8)), int(brg), min(255, int(brg*0.9)))  # light teal-ish
    pillIdle = (90, 90, 110)
    pillNear = (70, 180, 180)
    pillCool = (120, 160, 200)
    return header, accent, pillIdle, pillNear, pillCool

# -------------------- Face detector --------------------
face = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")

def faces_detect(gray_roi):
    return face.detectMultiScale(
        gray_roi,
        scaleFactor=HAAR_SCALE,
        minNeighbors=HAAR_NEIGH,
        minSize=(HAAR_MINSZ, HAAR_MINSZ)
    )

# -------------------- Main --------------------
def main():
    header, accent, pillIdle, pillNear, pillCool = gov_colors(ACCENT_BRG)

    print(f"[INFO] focal_px≈{FOCAL_PX:.1f}, near<{NEAR_M}m ⇒ need face_w≥{NEAR_PX:.0f}px")
    print(f"[INFO] ROI frac: x={ROI_XF}, y={ROI_YF}, w={ROI_WF}, h={ROI_HF}")
    print(f"[INFO] COOLDOWN={COOLDOWN_S}s, DWELL≥{MIN_DWELL_MS}ms, keepNear={NEAR_KEEP}, keepFar={FAR_KEEP}")
    print(f"[INFO] GPIO cmd: {GPIO_WAKE_CMD}  SIMULATE={SIMULATE_GPIO}")

    cap = cv2.VideoCapture(SRC, backend) if backend else cv2.VideoCapture(SRC)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  CAM_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_H)
    cap.set(cv2.CAP_PROP_FPS,          CAM_FPS)

    ok, frame = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError(f"Cannot open camera: {SRC}")

    rx = int(CAM_W*ROI_XF); ry = int(CAM_H*ROI_YF)
    rw = int(CAM_W*ROI_WF); rh = int(CAM_H*ROI_HF)

    near_cnt = 0
    far_cnt  = 0
    state_near = False
    first_seen_ms = 0
    last_wake_ts = 0.0
    last_flash_ts = 0.0

    # Load logo (optional)
    logo = None
    if LOGO_PATH and os.path.exists(LOGO_PATH):
        logo = cv2.imread(LOGO_PATH, cv2.IMREAD_UNCHANGED)

    if SHOW: cv2.namedWindow("SmartKiosk", cv2.WINDOW_NORMAL)

    while True:
        ok, frame = cap.read()
        if not ok: break

        vis = frame.copy()

        # --- header bar ---
        # top header strip with title
        fill_round_rect(vis, (0,0), (CAM_W, 56), header, alpha=0.85, radius=0)
        put_text_shadow(vis, APP_TITLE, (12, 36), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (240,240,240), 2)

        # banner (below header)
        put_text_shadow(vis, BANNER_TXT, (12, 68), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (235,235,235), 1)

        # --- ROI crop & detect ---
        roi = frame[ry:ry+rh, rx:rx+rw]
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = cv2.equalizeHist(gray)

        dets = faces_detect(gray)
        best = None; best_w = 0
        for (x,y,w,h) in dets:
            if w > best_w:
                best = (x,y,w,h); best_w = w

        nearest_m = None
        if best is not None and best_w >= NEAR_PX:
            nearest_m = (FACE_WIDTH_M * FOCAL_PX) / float(best_w)

        now_s  = time.time()
        now_ms = int(now_s*1000)
        in_cooldown = (now_s - last_wake_ts) < COOLDOWN_S

        if (nearest_m is not None) and not in_cooldown:
            if first_seen_ms == 0:
                first_seen_ms = now_ms
            dwell_ok = (now_ms - first_seen_ms) >= MIN_DWELL_MS
            if dwell_ok:
                near_cnt += 1; far_cnt = 0
            else:
                near_cnt = 0; far_cnt += 1
        else:
            first_seen_ms = 0
            far_cnt  += 1; near_cnt = 0

        # --- flip state ---
        just_woke = False
        if (not state_near) and (near_cnt >= NEAR_KEEP) and (not in_cooldown):
            state_near = True
            last_wake_ts = now_s
            last_flash_ts = now_s
            just_woke = True
            print(f"[NEAR] w≈{best_w}px (>= {NEAR_PX:.0f}px) -> WAKE")
            poke_gpio_async()
            notify(WAKE_URL, {"event":"wake","w_px":int(best_w)})

        if state_near and (far_cnt >= FAR_KEEP):
            state_near = False
            print("[FAR] -> SLEEP (idle)")
            notify(SLEEP_URL, {"event":"sleep"})

        # --- draw ROI block ---
        if DRAW:
            # semi glass ROI
            fill_round_rect(vis, (rx,ry), (rx+rw,ry+rh), accent, alpha=0.12, radius=18)
            draw_round_rect(vis, (rx,ry), (rx+rw,ry+rh), tuple(int(c*0.75) for c in accent), radius=18, thickness=2)

            # best face box (shift coord)
            if best is not None:
                x,y,w,h = best
                cv2.rectangle(vis, (rx+x,ry+y), (rx+x+w,ry+y+h), (80,220,220), 2)
                dtxt = f"{(FACE_WIDTH_M*FOCAL_PX/max(1.0,float(w))):.2f} m"
                put_text_shadow(vis, dtxt, (rx+x, max(ry+20, ry+y-8)), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (240,240,240), 2)

        # --- status pill (right-lower corner) ---
        pill_text = "NEAR" if state_near else ("COOLDOWN" if in_cooldown else "IDLE")
        pill_col  = pillNear if state_near else (pillCool if in_cooldown else pillIdle)
        pill_w, pill_h = 150, 36
        px2, py2 = CAM_W-10, CAM_H-10
        px1, py1 = px2-pill_w, py2-pill_h
        fill_round_rect(vis, (px1,py1), (px2,py2), pill_col, alpha=0.85, radius=18)
        put_text_shadow(vis, pill_text, (px1+16, py1+24), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255,255,255), 2)

        # tiny info line (bottom-left)
        info = f"need_w>={NEAR_PX:.0f}px  dwell>={MIN_DWELL_MS}ms  cooldown={max(0,int(max(0.0,COOLDOWN_S-(now_s-last_wake_ts))))}s"
        put_text_shadow(vis, info, (12, CAM_H-12), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (225,225,225), 1)

        # logo
        place_logo(vis, logo)

        # subtle flash on wake
        if just_woke or (now_s - last_flash_ts) < 0.20:
            overlay = vis.copy()
            white = np.full_like(overlay, 255)
            cv2.addWeighted(white, 0.20, overlay, 0.80, 0, vis)

        if SHOW:
            cv2.imshow("SmartKiosk", vis)
            if (cv2.waitKey(1) & 0xFF) in (27, ord('q')): break

    cap.release()
    if SHOW: cv2.destroyAllWindows()

# -------------------- Entry --------------------
if __name__ == "__main__":
    main()