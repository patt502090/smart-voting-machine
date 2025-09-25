#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
present.py — Face-near wake helper (English UI + dwell + progress + success toast)
Optimized for kiosk/booth use (e.g., Odroid + USB cam).

SAFE CHANGES:
- Default SAFE_UVC=1 => only set width/height/fps. No exposure/gain/FOURCC/etc.
- Optional FORCE_AUTO=1 (off by default) to gently ask driver for auto modes via v4l2-ctl.
- Prints note when running under sudo (env may be dropped unless using sudo -E).

ENV (optional):
  CAM_SRC=auto|0|/dev/videoX
  CAM_W=640 CAM_H=360 CAM_FPS=15
  SHOW=1 DRAW=1
  ROI_XF=0.20 ROI_YF=0.15 ROI_WF=0.60 ROI_HF=0.70
  NEAR_M=1.5 MIN_DWELL_MS=1500 NEAR_KEEP=1 FAR_KEEP=8 COOLDOWN_S=8
  HAAR_SCALE=1.1 HAAR_NEIGH=6 HAAR_MINSZ=70
  FACE_WIDTH_M=0.16 CALIB_PIX_AT_0_5M=240
  GPIO_WAKE_CMD='gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0'
  SIMULATE_GPIO=0 WAKE_URL=... SLEEP_URL=... AI_TOKEN=...
  APP_TITLE='Smart Voting Machine'
  BANNER_TXT='Please stand inside the frame to wake the screen'
  LOGO_PATH=./logo_university.png LOGO_MAXW=160 LOGO_ANCHOR=top-right
  ACCENT_BRG=220 SUCCESS_TOAST_S=1.4

  # Image preprocess (does NOT touch camera hardware):
  CLAHE=1 CLAHE_CLIP=2.0 CLAHE_GRID=8
  GAMMA=1.0

  # Camera control toggles:
  SAFE_UVC=1         # 1 = safest (only W/H/FPS). 0 = allow advanced sets below
  LOCK_AE=0          # (only applied when SAFE_UVC=0)
  EXPOSURE=''        # (only applied when SAFE_UVC=0)
  GAIN=''            # (only applied when SAFE_UVC=0)
  FORCE_AUTO=0       # if 1, try 'v4l2-ctl' to set auto modes (best effort; still safe)
  POWERLINE=''       # '50' or '60' for power_line_frequency via v4l2-ctl when FORCE_AUTO=1
"""

import os, sys, time, threading, subprocess, glob, shutil
import cv2
import numpy as np
import requests

# -------------------- Config --------------------
def get_env_int(key, default): return int(os.getenv(key, str(default)))
def get_env_float(key, default): return float(os.getenv(key, str(default)))

SRC = os.getenv("CAM_SRC", "auto")
CAM_W  = get_env_int("CAM_W", 1980)
CAM_H  = get_env_int("CAM_H", 1080)
CAM_FPS= get_env_int("CAM_FPS", 15)

SHOW   = get_env_int("SHOW", 1)
DRAW   = get_env_int("DRAW", 1)

ROI_XF = get_env_float("ROI_XF", 0.20)
ROI_YF = get_env_float("ROI_YF", 0.15)
ROI_WF = get_env_float("ROI_WF", 0.60)
ROI_HF = get_env_float("ROI_HF", 0.70)

NEAR_M       = get_env_float("NEAR_M", 1.5)
MIN_DWELL_MS = get_env_int("MIN_DWELL_MS", 1500)
NEAR_KEEP    = get_env_int("NEAR_KEEP", 1)
FAR_KEEP     = get_env_int("FAR_KEEP", 8)
COOLDOWN_S   = get_env_float("COOLDOWN_S", 8.0)

HAAR_SCALE  = get_env_float("HAAR_SCALE", 1.1)
HAAR_NEIGH  = get_env_int("HAAR_NEIGH", 6)
HAAR_MINSZ  = get_env_int("HAAR_MINSZ", 70)

FACE_WIDTH_M       = get_env_float("FACE_WIDTH_M", 0.16)
CALIB_PIX_AT_0_5M  = get_env_float("CALIB_PIX_AT_0_5M", 240)
FOCAL_PX = CALIB_PIX_AT_0_5M * 0.5 / max(FACE_WIDTH_M, 1e-6)
NEAR_PX  = (FACE_WIDTH_M * FOCAL_PX) / max(NEAR_M, 1e-6)
HAAR_MINSZ_EFF = max(40, min(HAAR_MINSZ, int(NEAR_PX * 0.9)))

GPIO_WAKE_CMD = os.getenv("GPIO_WAKE_CMD", "gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0")
SIMULATE_GPIO = get_env_int("SIMULATE_GPIO", 0)
WAKE_URL  = os.getenv("WAKE_URL",  "")
SLEEP_URL = os.getenv("SLEEP_URL", "")
AI_TOKEN  = os.getenv("AI_TOKEN",  "")

APP_TITLE  = os.getenv("APP_TITLE", "Smart Voting Machine")
BANNER_TXT = os.getenv("BANNER_TXT","Please stand inside the frame to wake the screen")

LOGO_PATH   = os.getenv("LOGO_PATH", "")
LOGO_MAXW   = get_env_int("LOGO_MAXW", 160)
LOGO_ANCHOR = os.getenv("LOGO_ANCHOR", "top-right").lower()

ACCENT_BRG  = get_env_int("ACCENT_BRG", 220)
SUCCESS_TOAST_S = get_env_float("SUCCESS_TOAST_S", 1.4)

# Image preprocess (software only)
CLAHE       = get_env_int("CLAHE", 1)
CLAHE_CLIP  = get_env_float("CLAHE_CLIP", 2.0)
CLAHE_GRID  = get_env_int("CLAHE_GRID", 8)
GAMMA       = get_env_float("GAMMA", 1.0)

# Camera control toggles
SAFE_UVC    = get_env_int("SAFE_UVC", 1)  # default safest
LOCK_AE     = get_env_int("LOCK_AE", 0)
EXPOSURE    = os.getenv("EXPOSURE", "")
GAIN        = os.getenv("GAIN", "")
FORCE_AUTO  = get_env_int("FORCE_AUTO", 0)
POWERLINE   = os.getenv("POWERLINE", "")  # '50' or '60'

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
    header   = (60, 60, 80)
    accent   = (min(255, int(brg*0.8)), brg, min(255, int(brg*0.9)))
    pillIdle = (90, 90, 110)
    pillNear = (70, 180, 180)
    pillCool = (120, 160, 200)
    prog_bg  = (40, 40, 55)
    prog_fg  = (80, 220, 120)
    prog_bd  = (30, 180, 90)
    success_bg = (60, 180, 90)
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

# --------- Lighting preprocess (software-only) ----------
def preprocess_gray(gray):
    if CLAHE:
        clahe = cv2.createCLAHE(clipLimit=max(0.5, CLAHE_CLIP),
                                tileGridSize=(max(2, CLAHE_GRID), max(2, CLAHE_GRID)))
        gray = clahe.apply(gray)
    if abs(GAMMA - 1.0) > 1e-3:
        inv = 1.0 / max(1e-6, GAMMA)
        lut = np.array([((i/255.0) ** inv) * 255 for i in range(256)], dtype="uint8")
        gray = cv2.LUT(gray, lut)
    return gray

# --------- Camera controls (SAFE by default) ----------
def try_camera_options(cap):
    # Always safe: only size & fps
    try:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH,  CAM_W)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_H)
        cap.set(cv2.CAP_PROP_FPS,          CAM_FPS)
    except Exception:
        pass

    if SAFE_UVC:
        # In safe mode we DO NOT touch FOURCC/exposure/gain/AE, etc.
        return

    # Advanced tweaks (only when SAFE_UVC=0)
    try:
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        cap.set(cv2.CAP_PROP_CONVERT_RGB, 1)
    except Exception:
        pass
    if LOCK_AE:
        try: cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 0.75)  # manual (OpenCV V4L2 quirk)
        except Exception: pass
        if EXPOSURE != "":
            try: cap.set(cv2.CAP_PROP_EXPOSURE, float(EXPOSURE))
            except Exception: pass
        if GAIN != "":
            try: cap.set(cv2.CAP_PROP_GAIN, float(GAIN))
            except Exception: pass

def maybe_force_auto(device_hint):
    """Best-effort: use v4l2-ctl to set fully-auto modes. Runs ONLY if FORCE_AUTO=1."""
    if not FORCE_AUTO: return
    if shutil.which("v4l2-ctl") is None: 
        print("[WARN] FORCE_AUTO=1 but v4l2-ctl not found.")
        return
    dev = None
    if isinstance(device_hint, str) and device_hint.startswith("/dev/video"):
        dev = device_hint
    elif isinstance(device_hint, int):
        dev = f"/dev/video{device_hint}"
    if not dev: return
    cmds = [
        f'v4l2-ctl -d "{dev}" -c exposure_auto=3',
        f'v4l2-ctl -d "{dev}" -c white_balance_temperature_auto=1',
        f'v4l2-ctl -d "{dev}" -c focus_auto=1',
        f'v4l2-ctl -d "{dev}" -c backlight_compensation=1',
    ]
    if POWERLINE in ("50","60"):
        val = "1" if POWERLINE=="50" else "2"
        cmds.append(f'v4l2-ctl -d "{dev}" -c power_line_frequency={val}')
    for c in cmds:
        try: subprocess.run(["sh","-c",c], check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except Exception: pass

# --------- Auto camera detection ----------
def select_backend(candidate):
    if isinstance(candidate, int) and sys.platform.startswith("linux"):
        return cv2.CAP_V4L2
    if isinstance(candidate, int) and sys.platform == "darwin":
        return cv2.CAP_AVFOUNDATION
    return 0

def test_camera(candidate):
    backend = select_backend(candidate)
    cap = cv2.VideoCapture(candidate, backend) if backend else cv2.VideoCapture(candidate)
    if not cap or not cap.isOpened():
        if cap: cap.release()
        return None
    try_camera_options(cap)
    ok, _ = cap.read()
    if not ok:
        cap.release()
        return None
    return cap

def autodetect_camera():
    if SRC != "auto":
        cand = int(SRC) if (isinstance(SRC, str) and SRC.isdigit()) else SRC
        cap = test_camera(cand)
        if cap:
            print(f"[INFO] Using camera: {cand}")
            return cand, cap
        raise RuntimeError(f"Cannot open camera: {cand}")

    print("[INFO] Auto-detecting camera ports (SAFE_UVC=%d)..." % SAFE_UVC)
    candidates = []
    if sys.platform.startswith("linux"):
        candidates.extend(sorted(glob.glob("/dev/video*")))
        candidates.extend(list(range(0,10)))
    else:
        candidates.extend(list(range(0,6)))

    tried = []
    for cand in candidates:
        tried.append(str(cand))
        cap = test_camera(cand)
        if cap:
            print(f"[INFO] Found working camera: {cand}")
            return cand, cap

    print("[ERROR] Tried candidates:", ", ".join(tried))
    raise RuntimeError("No working camera found")

# -------------------- Main --------------------
def main():
    if os.geteuid() == 0 and not os.getenv("SUDO_KEEPENV"):
        print("[NOTE] Running under sudo. If your ENV vars don't apply, use: sudo -E python3 present.py")

    (header, accent, pillIdle, pillNear, pillCool,
     prog_bg, prog_fg, prog_bd, success_bg) = gov_colors(ACCENT_BRG)

    print(f"[INFO] focal_px≈{FOCAL_PX:.1f}, near<{NEAR_M}m ⇒ need face_w≥{NEAR_PX:.0f}px")
    print(f"[INFO] ROI frac: x={ROI_XF}, y={ROI_YF}, w={ROI_WF}, h={ROI_HF}")
    print(f"[INFO] DWELL≥{MIN_DWELL_MS}ms, keepNear={NEAR_KEEP}, keepFar={FAR_KEEP}, cooldown={COOLDOWN_S}s")
    print(f"[INFO] HAAR_MINSZ={HAAR_MINSZ} → EFF={HAAR_MINSZ_EFF}   SCALE={HAAR_SCALE} NEIGH={HAAR_NEIGH}")
    print(f"[INFO] SAFE_UVC={SAFE_UVC}  (LOCK_AE={LOCK_AE} only if SAFE_UVC=0)")
    print(f"[INFO] Software lighting: CLAHE={CLAHE} (clip={CLAHE_CLIP}, grid={CLAHE_GRID})  GAMMA={GAMMA}")
    if FORCE_AUTO:
        print("[INFO] FORCE_AUTO=1 -> will ask v4l2-ctl to enable auto modes (best-effort).")

    cam_id, cap = autodetect_camera()
    maybe_force_auto(cam_id)

    warmup_frames = 8
    rx = int(CAM_W*ROI_XF); ry = int(CAM_H*ROI_YF)
    rw = int(CAM_W*ROI_WF); rh = int(CAM_H*ROI_HF)

    state_near = False
    near_cnt = 0
    far_cnt  = 0
    first_seen_ms = 0
    last_wake_ts  = 0.0
    last_flash_ts = 0.0
    toast_until   = 0.0

    logo = None
    if LOGO_PATH and os.path.exists(LOGO_PATH):
        logo = cv2.imread(LOGO_PATH, cv2.IMREAD_UNCHANGED)

    global SHOW
    if SHOW:
        try:
            cv2.namedWindow("SmartKiosk", cv2.WINDOW_NORMAL)
        except cv2.error:
            print("[WARN] OpenCV GUI backend not available. Forcing SHOW=0.")
            SHOW = 0

    while True:
        ok, frame = cap.read()
        if not ok: break

        if warmup_frames > 0:
            warmup_frames -= 1

        vis = frame.copy()

        # header + title
        fill_round_rect(vis, (0,0), (CAM_W, 56), header, alpha=0.85, radius=0)
        put_text_shadow(vis, APP_TITLE, (12, 36), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (240,240,240), 2)
        put_text_shadow(vis, BANNER_TXT, (12, 68), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (235,235,235), 1)

        # ROI + detect
        roi  = frame[ry:ry+rh, rx:rx+rw]
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = preprocess_gray(gray)
        dets = faces_detect(gray)

        best = None; best_w = 0
        for (x,y,w,h) in dets:
            if w > best_w:
                best = (x,y,w,h); best_w = w

        near_valid = (best is not None) and (best_w >= NEAR_PX)

        now_s  = time.time()
        now_ms = int(now_s*1000)
        in_cooldown = (now_s - last_wake_ts) < COOLDOWN_S

        if near_valid and not in_cooldown:
            if first_seen_ms == 0: first_seen_ms = now_ms
            dwell_elapsed = now_ms - first_seen_ms
            if dwell_elapsed >= MIN_DWELL_MS:
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
            # ROI glass
            fill_round_rect(vis, (rx,ry), (rx+rw,ry+rh),
                            (int(ACCENT_BRG*0.8), ACCENT_BRG, int(ACCENT_BRG*0.9)),
                            alpha=0.12, radius=18)
            draw_round_rect(vis, (rx,ry), (rx+rw,ry+rh),
                            (int(ACCENT_BRG*0.6), int(ACCENT_BRG*0.6), int(ACCENT_BRG*0.6)),
                            radius=18, thickness=2)

            # Largest face box
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
        if not in_cooldown and (first_seen_ms > 0):
            progress = min(1.0, max(0.0, dwell_elapsed / float(MIN_DWELL_MS)))

        # bg + border + fg
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
            # check icon
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

        # ---- safe imshow ----
        if SHOW:
            try:
                cv2.imshow("SmartKiosk", vis)
                if (cv2.waitKey(1) & 0xFF) in (27, ord('q')): break
            except cv2.error:
                print("[WARN] imshow() failed. Running headless (SHOW=0).")
                SHOW = 0

    cap.release()
    if SHOW:
        try: cv2.destroyAllWindows()
        except cv2.error: pass

# -------------------- Entry --------------------
if __name__ == "__main__":
    main()