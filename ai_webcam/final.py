import os, sys, time, math, threading, requests, subprocess, random
import cv2
import numpy as np

# ========= Config (ENV overrides) =========
SRC = os.getenv("CAM_SRC", "1"); SRC = int(SRC) if SRC.isdigit() else SRC
CAM_W  = int(os.getenv("CAM_W",  640))
CAM_H  = int(os.getenv("CAM_H",  360))
CAM_FPS= int(os.getenv("CAM_FPS",15))

SHOW   = int(os.getenv("SHOW", 1))
DRAW   = int(os.getenv("DRAW", 1))
NEAR_M = float(os.getenv("NEAR_M", 0.20))
NEAR_KEEP = int(os.getenv("NEAR_KEEP", 3))
FAR_KEEP  = int(os.getenv("FAR_KEEP", 8))

# ROI (fraction of full frame)
ROI_XF = float(os.getenv("ROI_XF", 0.2))
ROI_YF = float(os.getenv("ROI_YF", 0.15))
ROI_WF = float(os.getenv("ROI_WF", 0.6))
ROI_HF = float(os.getenv("ROI_HF", 0.7))

# dwell & cooldown
MIN_DWELL_MS = int(os.getenv("MIN_DWELL_MS", 800))
COOLDOWN_S   = float(os.getenv("COOLDOWN_S", 5.0))

# Haar params (stricter to reduce false)
HAAR_SCALE   = float(os.getenv("HAAR_SCALE", 1.1))
HAAR_NEIGH   = int(os.getenv("HAAR_NEIGH", 6))
HAAR_MINSZ   = int(os.getenv("HAAR_MINSZ", 80))

# face size & focal
FACE_WIDTH_M       = float(os.getenv("FACE_WIDTH_M", 0.16))
CALIB_PIX_AT_0_5M  = float(os.getenv("CALIB_PIX_AT_0_5M", 240))
FOCAL_PX = CALIB_PIX_AT_0_5M * 0.5 / FACE_WIDTH_M
NEAR_PX  = (FACE_WIDTH_M * FOCAL_PX) / max(NEAR_M, 1e-6)

# GPIO wake command
GPIO_WAKE_CMD = os.getenv("GPIO_WAKE_CMD", "gpio -1 write 36 1; sleep 0.5; gpio -1 write 36 0")
SIMULATE_GPIO = int(os.getenv("SIMULATE_GPIO", "0"))

# Branding / theme
APP_TITLE   = os.getenv("APP_TITLE", "Presence Wake Demo")
LOGO_PATH   = os.getenv("LOGO_PATH", "")     # ใส่ path โลโก้ PNG (พื้นหลังโปร่งใสจะสวย)
BANNER_TXT  = os.getenv("BANNER_TXT", "Please stand in the box")
ACCENT_BRG  = int(os.getenv("ACCENT_BRG", "255")) # ความสว่างสี accent (0~255)

# Colors (BGR)
COLOR_ACCENT = (40, 160, ACCENT_BRG)   # ฟ้าอมเขียว
COLOR_OK     = (60, 220, 60)
COLOR_WARN   = (30, 180, 255)
COLOR_BAD    = (50, 60, 230)
COLOR_BG     = (24, 24, 28)
COLOR_TEXT   = (255, 255, 255)

# Mac backend
backend = 0
if sys.platform == "darwin" and isinstance(SRC, int):
    backend = cv2.CAP_AVFOUNDATION

# ========= Utilities =========
def _fire(url, payload):
    try:
        hdr = {"X-Token": os.getenv("AI_TOKEN","")}
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

# ========= Vision =========
face = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
def faces_detect(gray_roi):
    return face.detectMultiScale(
        gray_roi,
        scaleFactor=HAAR_SCALE,
        minNeighbors=HAAR_NEIGH,
        minSize=(HAAR_MINSZ, HAAR_MINSZ)
    )

# ========= Fancy drawing helpers =========
def put_text_shadow(img, text, org, scale=0.8, color=(255,255,255), thickness=2):
    x,y = org
    cv2.putText(img, text, (x+1,y+1), cv2.FONT_HERSHEY_SIMPLEX, scale, (0,0,0), thickness+2, cv2.LINE_AA)
    cv2.putText(img, text, (x, y),    cv2.FONT_HERSHEY_SIMPLEX, scale, color,  thickness,   cv2.LINE_AA)

def draw_rounded_rect(img, pt1, pt2, color, radius=12, thickness=2):
    # simple rounded rectangle
    x1,y1 = pt1; x2,y2 = pt2
    cv2.rectangle(img, (x1+radius, y1), (x2-radius, y2), color, thickness)
    cv2.rectangle(img, (x1, y1+radius), (x2, y2-radius), color, thickness)
    cv2.circle(img, (x1+radius, y1+radius), radius, color, thickness)
    cv2.circle(img, (x2-radius, y1+radius), radius, color, thickness)
    cv2.circle(img, (x1+radius, y2-radius), radius, color, thickness)
    cv2.circle(img, (x2-radius, y2-radius), radius, color, thickness)

def draw_translucent_box(img, pt1, pt2, fill_color=(40,40,40), alpha=0.35):
    overlay = img.copy()
    cv2.rectangle(overlay, pt1, pt2, fill_color, -1)
    cv2.addWeighted(overlay, alpha, img, 1-alpha, 0, img)

def overlay_png(frame, png, pos=(0,0), max_w=None):
    if png is None: return
    x,y = pos
    h,w = png.shape[:2]
    if max_w and w > max_w:
        scale = max_w / w
        png = cv2.resize(png, (int(w*scale), int(h*scale)), interpolation=cv2.INTER_AREA)
        h,w = png.shape[:2]

    if y+h > frame.shape[0] or x+w > frame.shape[1]: return
    if png.shape[2] == 4:
        # RGBA
        b,g,r,a = cv2.split(png)
        fg = cv2.merge((b,g,r))
        mask = cv2.merge((a,a,a)) / 255.0
        roi = frame[y:y+h, x:x+w].astype(np.float32)
        frame[y:y+h, x:x+w] = (fg * mask + roi * (1-mask)).astype(np.uint8)
    else:
        frame[y:y+h, x:x+w] = png

def make_wake_flash(w,h):
    # radial flash mask
    cx, cy = w//2, h//2
    y, x = np.ogrid[:h, :w]
    d = np.sqrt((x-cx)**2 + (y-cy)**2)
    d = 1.0 - np.clip(d / (0.7*max(w,h)), 0, 1)
    mask = (d*255).astype(np.uint8)
    return mask

# ========= Main =========
def main():
    print(f"[INFO] focal_px≈{FOCAL_PX:.1f}, near<{NEAR_M}m ⇒ need face_w≥{NEAR_PX:.0f}px")
    print(f"[INFO] ROI: x={ROI_XF}, y={ROI_YF}, w={ROI_WF}, h={ROI_HF}")
    print(f"[INFO] COOLDOWN={COOLDOWN_S}s, DWELL≥{MIN_DWELL_MS}ms, keepNear={NEAR_KEEP}, keepFar={FAR_KEEP}")
    print(f"[INFO] GPIO cmd: {GPIO_WAKE_CMD}  SIMULATE={SIMULATE_GPIO}")

    cap = cv2.VideoCapture(SRC, backend) if backend else cv2.VideoCapture(SRC)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  CAM_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_H)
    cap.set(cv2.CAP_PROP_FPS,          CAM_FPS)

    ok, _ = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError(f"Cannot open camera: {SRC}")

    # ROI in pixel
    rx = int(CAM_W*ROI_XF); ry = int(CAM_H*ROI_YF)
    rw = int(CAM_W*ROI_WF); rh = int(CAM_H*ROI_HF)

    near_cnt = 0
    far_cnt  = 0
    state_near = False
    first_seen_ms = 0
    last_wake_ts = 0.0

    # FPS
    fps = 0.0; last_fps_t = time.time(); frames = 0

    # Branding assets
    logo = None
    if LOGO_PATH and os.path.exists(LOGO_PATH):
        logo = cv2.imread(LOGO_PATH, cv2.IMREAD_UNCHANGED)

    # Wake flash
    flash_mask = make_wake_flash(CAM_W, CAM_H)
    flash_decay = 0.0   # 0..1

    win = "PresenceWake"
    if SHOW:
        cv2.namedWindow(win, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(win, CAM_W, CAM_H)

    while True:
        ok, frame = cap.read()
        if not ok: break

        # ====== VISION ======
        roi = frame[ry:ry+rh, rx:rx+rw]
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = cv2.equalizeHist(gray)
        dets = faces_detect(gray)

        best = None; best_w = 0
        for (x,y,w,h) in dets:
            if w > best_w:
                best = (x,y,w,h); best_w = w

        nearest_m = None
        if best is not None:
            _,_,w,h = best
            if w >= NEAR_PX:
                nearest_m = (FACE_WIDTH_M * FOCAL_PX) / float(w)

        now_ms = int(time.time()*1000)
        in_cooldown = (time.time() - last_wake_ts) < COOLDOWN_S

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

        # state flip
        if (not state_near) and (near_cnt >= NEAR_KEEP) and (not in_cooldown):
            state_near = True
            last_wake_ts = time.time()
            print(f"[NEAR] w≈{best_w}px (>= {NEAR_PX:.0f}px) -> WAKE")
            poke_gpio_async()
            notify(os.getenv("WAKE_URL",""), {"event":"wake","w_px":int(best_w)})
            flash_decay = 1.0  # trigger flash
        if state_near and (far_cnt >= FAR_KEEP):
            state_near = False
            print("[FAR] -> SLEEP (idle)")
            notify(os.getenv("SLEEP_URL",""), {"event":"sleep"})

        # ====== UI LAYER ======
        vis = frame

        # subtle vignette/brand bar on top
        draw_translucent_box(vis, (0,0), (CAM_W, 44), (32,32,40), 0.65)
        put_text_shadow(vis, APP_TITLE, (12,28), 0.9, COLOR_TEXT, 2)

        # logo (right)
        if logo is not None:
            overlay_png(vis, logo, (CAM_W-10-140, 4), max_w=140)

        # ROI fancy border
        draw_translucent_box(vis, (rx,ry), (rx+rw,ry+rh), (40,40,40), 0.20)
        draw_rounded_rect(vis, (rx,ry), (rx+rw,ry+rh), COLOR_ACCENT, radius=16, thickness=2)

        # Best face in ROI
        if best is not None:
            x,y,w,h = best
            # shift to full frame coords
            x0,y0,x1,y1 = rx+x, ry+y, rx+x+w, ry+y+h
            draw_rounded_rect(vis, (x0,y0), (x1,y1), COLOR_OK if nearest_m is not None else COLOR_WARN, radius=10, thickness=2)
            if nearest_m is not None:
                put_text_shadow(vis, f"{nearest_m:.2f} m", (x0, max(22, y0-8)), 0.75, COLOR_OK, 2)
            else:
                put_text_shadow(vis, "Move closer", (x0, max(22, y0-8)), 0.75, COLOR_WARN, 2)

        # Status pill (bottom-left)
        status = "NEAR" if state_near else ("COOLDOWN" if in_cooldown else "IDLE")
        color  = COLOR_OK if state_near else (COLOR_BAD if in_cooldown else COLOR_ACCENT)
        pill_w = 220
        draw_translucent_box(vis, (10, CAM_H-48), (10+pill_w, CAM_H-10), (32,32,36), 0.55)
        draw_rounded_rect(vis, (10, CAM_H-48), (10+pill_w, CAM_H-10), color, radius=12, thickness=2)
        put_text_shadow(vis, f"{status}", (20, CAM_H-18), 0.8, color, 2)

        # Info line (bottom-right)
        dwell = 0 if first_seen_ms==0 else (now_ms - first_seen_ms)
        right_info = []
        right_info.append(f"need_w≥{NEAR_PX:.0f}px")
        right_info.append(f"dwell {dwell}/{MIN_DWELL_MS} ms")
        if in_cooldown:
            left = max(0.0, COOLDOWN_S - (time.time()-last_wake_ts))
            right_info.append(f"cooldown {left:.1f}s")
        text = " | ".join(right_info)
        tw = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 2)[0][0]
        draw_translucent_box(vis, (CAM_W-20-tw-16, CAM_H-44), (CAM_W-10, CAM_H-12), (32,32,36), 0.45)
        put_text_shadow(vis, text, (CAM_W-18-tw, CAM_H-20), 0.55, COLOR_TEXT, 2)

        # Banner hint (top-center)
        if not state_near and not in_cooldown:
            tsize = cv2.getTextSize(BANNER_TXT, cv2.FONT_HERSHEY_SIMPLEX, 0.7, 2)[0]
            bx = (CAM_W - tsize[0])//2 - 10
            draw_translucent_box(vis, (max(6,bx-10), 50), (min(CAM_W-6,bx+tsize[0]+20), 85), (40,40,40), 0.40)
            put_text_shadow(vis, BANNER_TXT, (bx, 75), 0.7, COLOR_TEXT, 2)

        # Wake flash effect
        if flash_decay > 0:
            overlay = vis.copy()
            # white flash weighted by radial mask
            alpha = (flash_decay**2) * 0.8
            flash = np.zeros_like(vis)
            for c in range(3):
                flash[:,:,c] = flash_mask
            flash = cv2.cvtColor(flash, cv2.COLOR_BGR2RGB)  # mask is gray; keep BGR ok anyway
            cv2.addWeighted(flash, alpha/255.0, vis, 1.0, 0, vis)
            flash_decay = max(0.0, flash_decay - 0.08)

        # FPS (tiny)
        frames += 1
        if time.time() - last_fps_t >= 0.5:
            fps = frames / (time.time() - last_fps_t)
            last_fps_t = time.time()
            frames = 0
        put_text_shadow(vis, f"{fps:4.1f} FPS", (CAM_W-90, 26), 0.6, (200,200,200), 2)

        # ====== SHOW ======
        if SHOW:
            cv2.imshow(win, vis)
            k = cv2.waitKey(1) & 0xFF
            if k in (27, ord('q')): break

    cap.release()
    if SHOW: cv2.destroyAllWindows()

if __name__ == "__main__":
    main()