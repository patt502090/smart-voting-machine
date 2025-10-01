#!/usr/bin/env python3
import os, time, sys, threading, requests, serial

# ==== ปรับค่าฝั่งคุณ ====
SERIAL_PORT   = os.environ.get('ESP_PORT', '/dev/ttyUSB0')   # เปลี่ยนตามจริง
BAUD          = int(os.environ.get('ESP_BAUD', '9600'))
WAKE_GPIOCHIP = os.environ.get('WAKE_CHIP', '/dev/gpiochip1') # ชิปที่มี line 72
WAKE_LINE     = int(os.environ.get('WAKE_LINE', '72'))        # ODROID PIN_33 = line 72
WAKE_PULSE_MS = int(os.environ.get('WAKE_PULSE_MS', '120'))

# ตัวเลือกเรียกเว็บเมื่อพบคนใกล้/ไกล (ถ้าอยากคุยกับ ai-preview ของคุณ)
WAKE_URL  = os.environ.get('WAKE_URL',  '')
SLEEP_URL = os.environ.get('SLEEP_URL', '')

# ถ้าต้องปรับเกณฑ์ ultrasonic ขณะรัน
NEARTHR = os.environ.get('NEARTHR')  # เช่น "18" (ซม.)
FARTHR  = os.environ.get('FARTHR')   # เช่น "28"

# ==== ปลุก ESP32 ด้วย gpiod (line 72) ====
def pulse_wake():
    try:
        import gpiod
        with gpiod.request_lines(
            WAKE_GPIOCHIP,
            consumer="esp-wake",
            config={WAKE_LINE: gpiod.LineSettings(direction=gpiod.LineDirection.OUTPUT, output_value=gpiod.LineValue.INACTIVE)}
        ) as req:
            req.set_value(WAKE_LINE, gpiod.LineValue.ACTIVE)   # HIGH
            time.sleep(WAKE_PULSE_MS/1000.0)
            req.set_value(WAKE_LINE, gpiod.LineValue.INACTIVE) # LOW
        print("[WAKE] pulse sent")
    except Exception as e:
        print(f"[WAKE] error: {e}", file=sys.stderr)

def http_get(url):
    if not url: return
    try:
        r = requests.get(url, timeout=2)
        print(f"[HTTP] {url} -> {r.status_code}")
    except Exception as e:
        print(f"[HTTP] error calling {url}: {e}", file=sys.stderr)

def main():
    # เปิดซีเรียลคุยกับ ESP32
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.2)
    time.sleep(0.5)

    # ปรับเกณฑ์ ultrasonic ถ้ากำหนดไว้
    if NEARTHR:
        ser.write(f"NEARTHR {NEARTHR}\n".encode())
    if FARTHR:
        ser.write(f"FARTHR {FARTHR}\n".encode())

    # ปลุก ESP32 ตอนเริ่ม (กันกรณีหลับอยู่)
    pulse_wake()
    time.sleep(0.2)
    ser.write(b"ULTRA?\n")  # ขออ่านสถานะครั้งแรก

    last = None
    print("[BRIDGE] listening...")
    buf = b""
    while True:
        try:
            data = ser.read(256)
            if data:
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    msg = line.decode(errors='ignore').strip()
                    if not msg:
                        continue
                    print(f"[ESP] {msg}")

                    # โค้ดฝั่ง ESP32 จะส่ง "NEAR" / "FAR" เมื่อสถานะเปลี่ยน
                    if msg in ("NEAR", "FAR"):
                        if msg != last:
                            last = msg
                            if msg == "NEAR":
                                # เรียกปลุก UI/กล้องฝั่ง AI + ไม่ให้ ESP หลับ
                                pulse_wake()
                                http_get(WAKE_URL)
                            else:
                                # ไกลแล้ว จะลองปิด/พักระบบกล้อง
                                http_get(SLEEP_URL)

                    # ตอบจาก ULTRA? เช่น: ULTRA cm=xx.x near=0/1 (ใช้แสดงบน log)
                    elif msg.startswith("ULTRA "):
                        pass

            else:
                # โพลสถานะบ้างเป็นระยะ (เผื่อขาด event)
                ser.write(b"ULTRA?\n")
                time.sleep(1.0)

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[ERR] {e}", file=sys.stderr)
            time.sleep(0.5)

if __name__ == "__main__":
    main()