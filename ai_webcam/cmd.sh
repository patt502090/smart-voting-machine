# 1) put your PNG logo in the same folder, e.g. logo_university.png
# 2) run with government look + stricter detection for expo booths:
APP_TITLE="Smart Voting Machine" \
BANNER_TXT="กรุณายืนภายในกรอบเพื่อตื่นหน้าจอ / Please stand inside the frame" \
LOGO_PATH=./CoE/CoE-th.png LOGO_MAXW=160 LOGO_ANCHOR=top-right \
HAAR_NEIGH=7 HAAR_MINSZ=120 HAAR_SCALE=1.12 \
ROI_WF=0.55 ROI_HF=0.60 MIN_DWELL_MS=1000 NEAR_KEEP=3 COOLDOWN_S=10 \
python3 final.py