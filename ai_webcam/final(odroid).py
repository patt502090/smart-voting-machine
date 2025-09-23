#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
present.py — Face-near wake helper (English UI + dwell + progress + success toast)
Optimized for kiosk/booth use (e.g., Odroid + USB cam).

ENV (optional):
  CAM_SRC=0|1|/dev/videoX|rtsp://...      default 1
  CAM_W=640 CAM_H=360 CAM_FPS=15
  SHOW=1 DRAW=1
  ROI_XF=0.20 ROI_YF=0.15 ROI_WF=0.60 ROI_HF=0.70
  NEAR_M=1.5            # meters threshold to WAKE (default 1.5 m)
  MIN_DWELL_MS=1500     # must stay in-ROI continuously >= ms to WAKE
  NEAR_KEEP=1 FAR_KEEP=8
  COOLDOWN_S=8
  HAAR_SCALE=1.1 HAAR_NEIGH=6 HAAR_MINSZ=70
  FACE_WIDTH_M=0.16 CALIB_PIX_AT_0_5M=240
  GPIO_WAKE_CMD='gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0'
  SIMULATE_GPIO=0
  WAKE_URL=... SLEEP_URL=... AI_TOKEN=...
  APP_TITLE='Smart Voting Machine'
  BANNER_TXT='Please stand inside the frame to wake the screen'
  LOGO_PATH=./logo_university.png LOGO_MAXW=160 LOGO_ANCHOR=top-right|top-left
  ACCENT_BRG=220
  SUCCESS_TOAST_S=1.4    # seconds to show success banner

  # Lighting / camera tweaks (optional; best-effort per backend):
  CLAHE=1                # apply CLAHE to ROI grayscale (improves contrast)
  CLAHE_CLIP=2.0         # CLAHE clip limit
  CLAHE_GRID=8           # CLAHE grid size (NxN)
  GAMMA=1.0              # >1.0 brightens mid-tones, <1.0 darkens
  LOCK_AE=0              # 1 to try to disable auto-exposure after warmup
  EXPOSURE=-6            # manual exposure value (backend dependent)
  GAIN=0                 # manual gain (if supported)
"""

import os, sys, time, threading, subprocess
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

# Face near + dwell
NEAR_M       = get_env_float("NEAR_M", 1.5)
MIN_DWELL_MS = get_env_int("MIN_DWELL_MS", 1500)
NEAR_KEEP    = get_env_int("NEAR_KEEP", 1)
FAR_KEEP     = get_env_int("FAR_KEEP", 8)
COOLDOWN_S   = get_env_float("COOLDOWN_S", 8.0)

# Haar tuning
HAAR_SCALE  = get_env_float("HAAR_SCALE", 1.1)
HAAR_NEIGH  = get_env_int("HAAR_NEIGH", 6)
HAAR_MINSZ  = get_env_int("HAAR_MINSZ", 70)

# Focal calibration
FACE_WIDTH_M       = get_env_float("FACE_WIDTH_M", 0.16)   # ~16 cm
CALIB_PIX_AT_0_5M  = get_env_float("CALIB_PIX_AT_0_5M", 240)
FOCAL_PX = CALIB_PIX_AT_0_5M * 0.5 / max(FACE_WIDTH_M, 1e-6)
NEAR_PX  = (FACE_WIDTH_M * FOCAL_PX) / max(NEAR_M, 1e-6)

# Effective min face size
HAAR_MINSZ_EFF = max(40, min(HAAR_MINSZ, int(NEAR_PX * 0.9)))

# GPIO / webhooks
GPIO_WAKE_CMD = os.getenv("GPIO_WAKE_CMD", "gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0")
SIMULATE_GPIO = get_env_int("SIMULATE_GPIO", 0)
WAKE_URL  = os.getenv("WAKE_URL",  "")
SLEEP_URL = os.getenv("SLEEP_URL", "")
AI_TOKEN  = os.getenv("AI_TOKEN",  "")

# UI text
APP_TITLE  = os.getenv("APP_TITLE", "Smart Voting Machine")
BANNER_TXT = os.getenv("BANNER_TXT","Please stand inside the frame to wake the screen")

# Logo
LOGO_PATH   = os.getenv("LOGO_PATH", "")
LOGO_MAXW   = get_env_int("LOGO_MAXW", 160)
LOGO_ANCHOR = os.getenv("LOGO_ANCHOR", "top-right").lower()

# Colors
ACCENT_BRG  = get_env_int("ACCENT_BRG", 220)

# Success toast duration
SUCCESS_TOAST_S = get_env_float("SUCCESS_TOAST_S", 1.4)

# Lighting/Camera tweaks
CLAHE       = get_env_int("CLAHE", 1)
CLAHE_CLIP  = get_env_float("CLAHE_CLIP", 2.0)
CLAHE_GRID  = get_env_int("CLAHE_GRID", 8)
GAMMA       = get_env_float("GAMMA", 1.0)
LOCK_AE     = get_env_int("LOCK_AE", 0)
EXPOSURE    = os.getenv("EXPOSURE", "")
GAIN        = os.getenv("GAIN", "")

# Backend select (macOS/ Linux)
backend = 0
if isinstance(SRC, int):
    if sys.platform == "darwin":
        backend = cv2.CAP_AVFOUNDATION
    elif sys.platform.startswith("linux"):
        backend = cv2.CAP_V4L2

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
    except Exception as e:
        print("[GPIO] error:", e)

def poke_gpio_async():
    if SIMULATE_GPIO:
        print("[GPIO] SIMULATE ->", GPIO_WAKE_CMD)
        return
    threading.Thread(target=_run_shell, args=(GPIO_WAKE_CMD,), daemon=True).start()

# --- UI primitives ---
def put_text_shadow(img, text, org, font, scale, color, thickness=1):
    x,y = org
    cv2.putText(img, text, (x+1,y+1), font, scale, (0,0,0), thickness+2, cv2.LINE_AA)
    cv2.putText(img, text, (x, y),    font, scale, color, thickness, cv2.LINE_AA)

def draw_round_rect(img, pt1, pt2, color, radius=10, thickness=2):
    x1,y1 = pt1; x2,y2 = pt2
    overlay = img.copy()
    cv2.rectangle(overlay,(x1+radius,y1),(x2-radius,y2),color,thickness)
    cv2.rectangle(overlay,(x1,y1+radius),(x2,y2-radius),color,thickness)
    for cx,cy in [(x1+radius,y1+radius),(x2-radius,y1+radius),(x1+radius,y2-radius),(x2-radius,y2-radius)]:
        cv2.circle(overlay,(cx,cy),radius,color,thickness, cv2.LINE_AA)
    cv2.addWeighted(overlay,1.0,img,0.0,0,img)

def fill_round_rect(img, pt1, pt2, color, alpha=0.2, radius=12):
    x1,y1 = pt1; x2,y2 = pt2
    overlay = img.copy()
    cv2.rectangle(overlay,(x1+radius,y1),(x2-radius,y2),color,-1)
    cv2.rectangle(overlay,(x1,y1+radius),(x2,y2-radius),color,-1)
    for cx,cy in [(x1+radius,y1+radius),(x2-radius,y1+radius),(x1+radius,y2-radius),(x2-radius,y2-radius)]:
        cv2.circle(overlay,(cx,cy),radius,color,-1, cv2.LINE_AA)
    cv2.addWeighted(overlay,alpha,img,1-alpha,0,img)

def overlay_png(dst, png_rgba, pos):
    x,y = pos
    h,w = png_rgba.shape[:2]
    if x>=dst.shape[1] or y>=dst.shape[0]: return
    x2 = min(dst.shape[1], x+w); y2 = min(dst.shape[0], y+h)
    if x2<=x or y2<=y: return
    sub = dst[y:y2, x:x2]
    png = png_rgba[0:(y2-y), 0:(x2-x)]
    if png.shape[2]==3:
        sub[:] = png
        return
    bgr   = png[:,:,:3]
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
    else:
        lw = lg.shape[1]
        overlay_png(vis, lg, (vis.shape[1]-pad-lw, pad))

def gov_colors(brg=220):
    brg = int(np.clip(brg, 160, 255))
    header   = (60, 60, 80)          # deep slate
    accent   = (min(255, int(brg*0.8)), brg, min(255, int(brg*0.9)))  # teal-ish
    pillIdle = (90, 90, 110)
    pillNear = (70, 180, 180)
    pillCool = (120, 160, 200)
    # Progress palette (high contrast)
    prog_bg  = (40, 40, 55)          # dark
    prog_fg  = (80, 220, 120)        # vivid green
    prog_bd  = (30, 180, 90)         # border
    success_bg = (60, 180, 90)       # toast green
    return header, accent, pillIdle, pillNear, pillCool, prog_bg, prog_fg, prog_bd, success_bg

# -------------------- Face detector --------------------
face = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
def faces_detect(gray_roi):
    return face.detectMultiScale(
        gray_roi,
        scaleFactor=HAAR_SCALE,
        minNeighbors=HAAR_NEIGH,
        minSize=(HAAR_MINSZ_EFF, HAAR_MINSZ_EFF)
    )

# --------- Lighting preprocess (CLAHE + gamma) ----------
def preprocess_gray(gray):
    # CLAHE improves local contrast (faces under harsh light)
    if CLAHE:
        clahe = cv2.createCLAHE(clipLimit=max(0.5, CLAHE_CLIP),
                                tileGridSize=(max(2, CLAHE_GRID), max(2, CLAHE_GRID)))
        gray = clahe.apply(gray)
    # Gamma (post-CLAHE) for mid-tones
    if abs(GAMMA - 1.0) > 1e-3:
        # LUT is faster
        inv = 1.0 / max(1e-6, GAMMA)
        lut = np.array([((i/255.0) ** inv) * 255 for i in range(256)]).astype("uint8")
        gray = cv2.LUT(gray, lut)
    return gray

# --------- Camera controls (best-effort; backend-dependent) ----------
def try_camera_options(cap):
    # Some V4L backends accept these; others ignore silently.
    try:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH,  CAM_W)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_H)
        cap.set(cv2.CAP_PROP_FPS,          CAM_FPS)
        
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))  # ใช้ MJPEG ถ้ากล้องรองรับ
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)                            # ลดบัฟเฟอร์เฟรม
        cap.set(cv2.CAP_PROP_CONVERT_RGB, 1)                           # ให้ไดรเวอร์แปลงเป็น BGR
    except Exception:
        pass

    # Give auto exposure a few frames to settle, then optionally lock.
    # Many V4L drivers: CAP_PROP_AUTO_EXPOSURE: 0.25=auto, 0.75=manual (OpenCV weird scale)
    if LOCK_AE:
        try:
            cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 0.75)  # manual
        except Exception:
            pass
        if EXPOSURE != "":
            try: cap.set(cv2.CAP_PROP_EXPOSURE, float(EXPOSURE))
            except Exception: pass
        if GAIN != "":
            try: cap.set(cv2.CAP_PROP_GAIN, float(GAIN))
            except Exception: pass

# -------------------- Main --------------------
def main():
    (header, accent, pillIdle, pillNear, pillCool,
     prog_bg, prog_fg, prog_bd, success_bg) = gov_colors(ACCENT_BRG)

    print(f"[INFO] focal_px≈{FOCAL_PX:.1f}, near<{NEAR_M}m ⇒ need face_w≥{NEAR_PX:.0f}px")
    print(f"[INFO] ROI frac: x={ROI_XF}, y={ROI_YF}, w={ROI_WF}, h={ROI_HF}")
    print(f"[INFO] DWELL≥{MIN_DWELL_MS}ms, keepNear={NEAR_KEEP}, keepFar={FAR_KEEP}, cooldown={COOLDOWN_S}s")
    print(f"[INFO] HAAR_MINSZ={HAAR_MINSZ} → EFF={HAAR_MINSZ_EFF}   SCALE={HAAR_SCALE} NEIGH={HAAR_NEIGH}")
    print(f"[INFO] GPIO cmd: {GPIO_WAKE_CMD}  SIMULATE={SIMULATE_GPIO}")
    print(f"[INFO] Lighting: CLAHE={CLAHE} (clip={CLAHE_CLIP}, grid={CLAHE_GRID})  GAMMA={GAMMA}  LOCK_AE={LOCK_AE}")

    cap = cv2.VideoCapture(SRC, backend) if backend else cv2.VideoCapture(SRC)
    try_camera_options(cap)

    ok, _ = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError(f"Cannot open camera: {SRC}")

    rx = int(CAM_W*ROI_XF); ry = int(CAM_H*ROI_YF)
    rw = int(CAM_W*ROI_WF); rh = int(CAM_H*ROI_HF)

    state_near = False
    near_cnt = 0
    far_cnt  = 0

    first_seen_ms = 0
    last_wake_ts  = 0.0
    last_flash_ts = 0.0
    toast_until   = 0.0

    # Load logo (optional)
    logo = None
    if LOGO_PATH and os.path.exists(LOGO_PATH):
        logo = cv2.imread(LOGO_PATH, cv2.IMREAD_UNCHANGED)

    if SHOW: cv2.namedWindow("SmartKiosk", cv2.WINDOW_NORMAL)

    warmup_frames = 8  # let AE settle before we consider LOCK_AE

    while True:
        ok, frame = cap.read()
        if not ok: break
        if warmup_frames > 0:
            warmup_frames -= 1
            if warmup_frames == 0 and LOCK_AE:
                try_camera_options(cap)  # reapply after AE settle

        vis = frame.copy()

        # --- header bar + title ---
        fill_round_rect(vis, (0,0), (CAM_W, 56), header, alpha=0.85, radius=0)
        put_text_shadow(vis, APP_TITLE, (12, 36), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (240,240,240), 2)

        # banner
        put_text_shadow(vis, BANNER_TXT, (12, 68), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (235,235,235), 1)

        # --- ROI + detect ---
        roi  = frame[ry:ry+rh, rx:rx+rw]
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = preprocess_gray(gray)

        dets = faces_detect(gray)

        # pick largest (closest) face in ROI
        best = None; best_w = 0
        for (x,y,w,h) in dets:
            if w > best_w:
                best = (x,y,w,h); best_w = w

        # valid only if width big enough (i.e., near enough)
        near_valid = (best is not None) and (best_w >= NEAR_PX)

        now_s  = time.time()
        now_ms = int(now_s*1000)
        in_cooldown = (now_s - last_wake_ts) < COOLDOWN_S

        # dwell logic
        if near_valid and not in_cooldown:
            if first_seen_ms == 0:
                first_seen_ms = now_ms
            dwell_elapsed = now_ms - first_seen_ms
            dwell_ok = dwell_elapsed >= MIN_DWELL_MS
            if dwell_ok:
                near_cnt += 1; far_cnt = 0
            else:
                near_cnt = 0; far_cnt += 1
        else:
            first_seen_ms = 0
            dwell_elapsed = 0
            near_cnt = 0; far_cnt += 1

        # --- state flip ---
        just_woke = False
        if (not state_near) and (near_cnt >= NEAR_KEEP) and (not in_cooldown):
            state_near = True
            last_wake_ts = now_s
            last_flash_ts = now_s
            toast_until = now_s + SUCCESS_TOAST_S
            just_woke = True
            print(f"[WAKE] dwell>={MIN_DWELL_MS}ms, w≈{best_w}px (need≥{NEAR_PX:.0f}px)")
            poke_gpio_async()
            notify(WAKE_URL, {"event":"wake","w_px":int(best_w), "dwell_ms":MIN_DWELL_MS})

        if state_near and (far_cnt >= FAR_KEEP):
            state_near = False
            print("[IDLE] -> sleep state")
            notify(SLEEP_URL, {"event":"sleep"})

        # --- draw ROI & face box ---
        if DRAW:
            fill_round_rect(vis, (rx,ry), (rx+rw,ry+rh), (int(ACCENT_BRG*0.8), ACCENT_BRG, int(ACCENT_BRG*0.9)),
                            alpha=0.12, radius=18)
            draw_round_rect(vis, (rx,ry), (rx+rw,ry+rh),
                            (int(ACCENT_BRG*0.6), int(ACCENT_BRG*0.6), int(ACCENT_BRG*0.6)),
                            radius=18, thickness=2)

            if best is not None:
                x,y,w,h = best
                cv2.rectangle(vis, (rx+x,ry+y), (rx+x+w,ry+y+h), (80,220,220), 2)
                dist_m = (FACE_WIDTH_M * FOCAL_PX) / max(1.0, float(w))
                put_text_shadow(vis, f"{dist_m:.2f} m", (rx+x, max(ry+20, ry+y-8)),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (240,240,240), 2)

        # --- dwell progress bar ---
        bar_x, bar_y = 12, 96
        bar_w, bar_h = int(CAM_W*0.52), 16
        progress = 0.0
        if not in_cooldown and near_valid and first_seen_ms:
            progress = min(1.0, max(0.0, dwell_elapsed / float(MIN_DWELL_MS)))
        fill_round_rect(vis, (bar_x,bar_y), (bar_x+bar_w, bar_y+bar_h), (40,40,55), alpha=0.85, radius=9)
        draw_round_rect(vis, (bar_x,bar_y), (bar_x+bar_w, bar_y+bar_h), (30,180,90), radius=9, thickness=2)
        if progress > 0:
            fill_round_rect(vis, (bar_x,bar_y), (bar_x+int(bar_w*progress), bar_y+bar_h),
                            (80,220,120), alpha=0.90, radius=9)
        put_text_shadow(vis, f"Verifying... {int(progress*100):d}%", (bar_x+bar_w+12, bar_y+bar_h-3),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.55, (230,230,230), 1)

        # --- success toast ---
        if now_s < toast_until:
            tw, th = 360, 44
            cx = CAM_W//2; cy = 120
            x1, y1 = cx - tw//2, cy - th//2
            x2, y2 = cx + tw//2, cy + th//2
            fill_round_rect(vis, (x1,y1), (x2,y2), (60,180,90), alpha=0.95, radius=14)
            icon_r = 14
            icx = x1 + 22; icy = (y1+y2)//2
            cv2.circle(vis, (icx,icy), icon_r, (255,255,255), 2, cv2.LINE_AA)
            cv2.line(vis, (icx-6, icy+1), (icx-1, icy+6), (255,255,255), 2, cv2.LINE_AA)
            cv2.line(vis, (icx-1, icy+6), (icx+7, icy-5), (255,255,255), 2, cv2.LINE_AA)
            put_text_shadow(vis, "Verified - WAKE GRANTED",
                            (icx+18, icy+7), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255,255,255), 2)

        # --- status pill ---
        pill_text = "NEAR" if state_near else ("COOLDOWN" if in_cooldown else "IDLE")
        pill_col  = (70,180,180) if state_near else ((120,160,200) if in_cooldown else (90,90,110))
        pill_w, pill_h = 150, 36
        px2, py2 = CAM_W-10, CAM_H-10
        px1, py1 = px2-pill_w, py2-pill_h
        fill_round_rect(vis, (px1,py1), (px2,py2), pill_col, alpha=0.85, radius=18)
        put_text_shadow(vis, pill_text, (px1+16, py1+24), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255,255,255), 2)

        # --- debug info line ---
        remain = max(0, int(max(0.0, COOLDOWN_S-(now_s-last_wake_ts))))
        info = f"face_w={best_w}px thr≥{NEAR_PX:.0f}px  dwell={dwell_elapsed}/{MIN_DWELL_MS}ms  cooldown={remain}s"
        put_text_shadow(vis, info, (12, CAM_H-12), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (225,225,225), 1)

        # logo
        if LOGO_PATH:
            if logo is None and os.path.exists(LOGO_PATH):
                logo = cv2.imread(LOGO_PATH, cv2.IMREAD_UNCHANGED)
            place_logo(vis, logo)

        # subtle flash on wake
        if just_woke or (now_s - last_flash_ts) < 0.20:
            overlay = vis.copy()
            white = np.full_like(overlay, 255)
            cv2.addWeighted(white, 0.18, overlay, 0.82, 0, vis)

        if SHOW:
            cv2.imshow("SmartKiosk", vis)
            if (cv2.waitKey(1) & 0xFF) in (27, ord('q')): break

    cap.release()
    if SHOW: cv2.destroyAllWindows()

# -------------------- Entry --------------------
if __name__ == "__main__":
    main()