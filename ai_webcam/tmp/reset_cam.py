#!/usr/bin/env bash
# Reset a UVC webcam (e.g., NUBWO NWC-560) to safe auto settings + 720p@30.

set -e

# เลือกกล้อง: ถ้ารู้พอร์ตใส่เอง (เช่น /dev/video2), ไม่งั้นเดาเป็นตัวแรก
DEV="${1:-/dev/video0}"

# เปิดโหมดออโต้ทั้งหมด (บางคอนโทรลอาจไม่มีในกล้อง บางบรรทัดเลยใส่ || true)
v4l2-ctl -d "$DEV" -c exposure_auto=3                       # 3 = Aperture Priority (Auto)
v4l2-ctl -d "$DEV" -c exposure_auto_priority=1       || true
v4l2-ctl -d "$DEV" -c white_balance_temperature_auto=1 || true
v4l2-ctl -d "$DEV" -c focus_auto=1                   || true
v4l2-ctl -d "$DEV" -c gain_auto=1                    || true
v4l2-ctl -d "$DEV" -c backlight_compensation=1       || true

# ไฟกระพริบ 50/60Hz: เลือกตามที่หน้างานใช้ไฟอะไร (1=50Hz, 2=60Hz)
v4l2-ctl -d "$DEV" -c power_line_frequency=1         || true  # เปลี่ยนเป็น 2 ถ้าไฟ 60Hz

# ตั้งรูปแบบวิดีโอเป็น 1280x720@30 (ถ้ากล้องรองรับ MJPG จะช่วยเรื่องแบนด์วิดท์)
v4l2-ctl -d "$DEV" --set-fmt-video=width=1280,height=720,pixelformat=MJPG || \
v4l2-ctl -d "$DEV" --set-fmt-video=width=1280,height=720
v4l2-ctl -d "$DEV" --set-parm=30                 || true

# แสดงผลสถานะปัจจุบัน
echo "=== CURRENT VIDEO STATE ($DEV) ==="
v4l2-ctl -d "$DEV" --all