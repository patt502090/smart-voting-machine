#!/usr/bin/env bash
set -euo pipefail

# --- config: ปรับตามต้องการ ---
SCRIPT="final(odroid).py"   # ชื่อไฟล์ Python ของคุณ
VENV_DIR=".venv"           # ถ้าใช้ virtualenv, ถ้าไม่ก็ leave empty
CAM_DEV_DEFAULT="/dev/video0"
CAM_W_DEFAULT=1280         # แนะนำลดจาก 2560 -> 1280 เพื่อให้ Odroid ไหว
CAM_H_DEFAULT=720
SHOW_DEFAULT=0             # 0 = headless (no GUI), 1 = show window (only if GUI supported)
# ----------------------------------

# 1) หา device ที่ใช้ได้ (auto)
echo "[RUN] Probing /dev/video*..."
found=""
for dev in /dev/video*; do
  [ -e "$dev" ] || continue
  # quick try open with ffmpeg/ffprobe? we'll try open via python quick test below
  found="$dev"
  break
done

if [ -z "$found" ]; then
  echo "[ERROR] No /dev/video* found. Is the webcam connected?"
  exit 1
fi

# 2) ถ้าต้องการ เปลี่ยนค่า resolution สูง ให้ระวัง CPU/USB bandwidth
export CAM_SRC="${CAM_SRC:-$found}"
export CAM_W="${CAM_W:-$CAM_W_DEFAULT}"
export CAM_H="${CAM_H:-$CAM_H_DEFAULT}"
export CAM_FPS="${CAM_FPS:-15}"
export SHOW="${SHOW:-$SHOW_DEFAULT}"

echo "[RUN] Using camera: $CAM_SRC  target WxH=${CAM_W}x${CAM_H}  SHOW=$SHOW"

# 3) ถ้ามี venv ให้ activate (ปลอดภัย)
if [ -n "$VENV_DIR" ] && [ -f "$VENV_DIR/bin/activate" ]; then
  echo "[RUN] Activating venv $VENV_DIR"
  # shellcheck disable=SC1090
  source "$VENV_DIR/bin/activate"
fi

# 4) แนะนำ: อย่าใช้ sudo — ถ้าต้องใช้ ให้เตือน
if [ "$(id -u)" -eq 0 ]; then
  echo "[WARN] Running as root. Prefer running as normal user with 'video' group membership."
fi

# 5) Run python script
python3 "$SCRIPT"