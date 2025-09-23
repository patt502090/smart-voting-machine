#!/usr/bin/env bash
set -euo pipefail

# --- ปรับให้ process ใช้ทรัพยากรเบา ๆ ---
#  - nice: ลด priority CPU
#  - ionice: ลด I/O priority
#  - taskset: ปักให้รันบน 1 คอร์ (เช่น core0) เพื่อลด wakeups ทั้งระบบ
NICE="nice -n 10"
IONICE="ionice -c2 -n7"
TASKSET="taskset -c 0"

# --- ตำแหน่งไฟล์ ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${PYTHON_BIN:-python3}"
APP="${SCRIPT_DIR}/final(odroid)v2-light.py"

# --- กล้อง 2K ที่มี: เราจะ “ขอ” 640x360@8fps เพื่อลดโหลด ---
#     (โค้ดจะทำ preprocess ช่วยให้ยังตรวจได้ดี)
# --- SAFE_UVC=1: ไม่แตะ exposure/gain/FOURCC ของกล้อง (ปลอดภัยสุด) ---
# --- SHOW=0: ปิด GUI debug ลดโหลด ---
# --- SKIP_N=2: ตรวจทุก ๆ 3 เฟรม (ต้องมีโค้ดรองรับด้านล่างในหัวข้อโค้ด) ---
# --- CLAHE/GAMMA: ปรับสว่าง/คอนทราสต์แบบซอฟต์แวร์ ไม่ยุ่งฮาร์ดแวร์ ---
export SUDO_KEEPENV=1

ENVVARS=(
  CAM_SRC=auto
  CAM_W=640
  CAM_H=360
  CAM_FPS=8
  SAFE_UVC=1
  SHOW=0
  DRAW=1
  ROI_XF=0.20
  ROI_YF=0.15
  ROI_WF=0.60
  ROI_HF=0.70
  MIN_DWELL_MS=1200
  COOLDOWN_S=12
  CLAHE=1
  CLAHE_CLIP=2.0
  CLAHE_GRID=8
  GAMMA=1.08
  SKIP_N=2
)

# รันด้วย sudo -E เพื่อคง ENV ทั้งหมดไว้
# (ถ้าไม่ต้อง sudo ก็ลบ 'sudo -E' ออก)
echo "[RUN] low-power kiosk (safe UVC, low FPS/size, headless)..."
sudo -E ${TASKSET} ${IONICE} ${NICE} env "${ENVVARS[@]}" \
  "${PY}" "${APP}"