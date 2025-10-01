# print_one_uart.py
import serial
import time

PORT = "/dev/ttyAML0"   # <-- เปลี่ยนเป็นพอร์ตของคุณ: /dev/ttyAML0, /dev/ttyS0, /dev/ttyUSB0, ฯลฯ
BAUD = 19200            # บอดเรตของเครื่องพิมพ์ (หลายรุ่นใช้ 9600/19200)

ser = serial.Serial(
    PORT,
    BAUD,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=1,
    xonxoff=False,      # ปิดซอฟต์แวร์โฟลว์คอนโทรล
    rtscts=False,       # ปิดฮาร์ดแวร์โฟลว์คอนโทรล
    dsrdtr=False
)

# รอให้พอร์ตพร้อมสักครู่
time.sleep(0.2)

# เคลียร์บัฟเฟอร์ขาเข้า (กันขยะค้าง)
ser.reset_input_buffer()

# พิมพ์เลข 1 และขึ้นบรรทัดใหม่
ser.write(b"1\r\n")

# (ออปชัน) คำสั่ง ESC/POS ตัดกระดาษ ถ้ารุ่นรองรับ
# ser.write(b"\x1D\x56\x41\x10")

ser.flush()
ser.close()
print("Sent: 1")



# for run
# sudo apt install python3-pip -y && pip3 install pyserial
# python3 print_one_uart.py
