# 1) put your PNG logo in the same folder, e.g. logo_university.png
# 2) run with government look + stricter detection for expo booths:


# ฟอนต์ไทย (ถ้ามี) และโลโก้
export THAI_FONT_PATH=./NotoSansThai-Regular.ttf
export LOGO_PATH=./CoE/CoE-th.png
# เกณฑ์การตื่น
export NEAR_M=1.5
export MIN_DWELL_MS=1500
# แสดงผล + โทนสี
export SHOW=1 DRAW=1 ACCENT_BRG=220
# กล้อง
export CAM_SRC=1 CAM_W=1280 CAM_H=720 CAM_FPS=30
python3 final.py