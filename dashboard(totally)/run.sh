source /opt/ai-cam/.venv/bin/activate
AI_TOKEN="mysecret" \
WAKE_URL="http://192.168.1.50/wake" \
SLEEP_URL="http://192.168.1.50/sleep" \
NO_PEOPLE_TIMEOUT=45 CONF_TH=0.5 FRAME_SKIP=3 MIN_DET=2 MIN_MISS=8 \
MODEL_PATH="$HOME/.cache/ultralytics/yolov8n.pt" \
/opt/ai-cam/people_watch.py --src 0 --width 640 --height 360