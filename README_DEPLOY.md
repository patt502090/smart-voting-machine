# 🚀 คู่มือการ Deploy Smart Voting System บน Odroid

## 📋 ข้อกำหนดระบบ
- **Odroid** (XU4, N2+, หรือรุ่นใหม่กว่า)
- **OS**: Ubuntu 20.04+ หรือ Debian 11+
- **RAM**: อย่างน้อย 2GB
- **Storage**: อย่างน้อย 8GB
- **Network**: เชื่อมต่ออินเทอร์เน็ต

## 🔧 การติดตั้งแบบอัตโนมัติ

### 1. อัปโหลดไฟล์ไปยัง Odroid
```bash
# ใช้ SCP หรือ SFTP
scp -r dashboard\(totally\)/ user@odroid-ip:/opt/smart-voting/
scp deploy_setup.sh user@odroid-ip:/tmp/
scp requirements.txt user@odroid-ip:/opt/smart-voting/
```

### 2. รันสคริปต์ติดตั้ง
```bash
# SSH เข้า Odroid
ssh user@odroid-ip

# รันสคริปต์ติดตั้ง
sudo bash /tmp/deploy_setup.sh
```

## 🔧 การติดตั้งแบบ Manual

### 1. เตรียมระบบ
```bash
# อัปเดตระบบ
sudo apt update && sudo apt upgrade -y

# ติดตั้ง packages ที่จำเป็น
sudo apt install -y python3 python3-pip python3-venv git nginx ufw
```

### 2. ติดตั้ง Python Dependencies
```bash
cd /opt/smart-voting/dashboard\(totally\)
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### 3. ตั้งค่า Firewall
```bash
sudo ufw allow 22    # SSH
sudo ufw allow 80    # HTTP
sudo ufw allow 443   # HTTPS
sudo ufw --force enable
```

### 4. สร้าง Systemd Service
```bash
sudo nano /etc/systemd/system/smart-voting.service
```

เนื้อหาไฟล์:
```ini
[Unit]
Description=Smart Voting System
After=network.target

[Service]
Type=simple
User=odroid
WorkingDirectory=/opt/smart-voting/dashboard\(totally\)
Environment=PATH=/opt/smart-voting/venv/bin
ExecStart=/opt/smart-voting/venv/bin/uvicorn app:app --host 0.0.0.0 --port 8000
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

### 5. ตั้งค่า Nginx Reverse Proxy
```bash
sudo nano /etc/nginx/sites-available/smart-voting
```

เนื้อหาไฟล์:
```nginx
server {
    listen 80;
    server_name _;
    
    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

### 6. เปิดใช้งาน Services
```bash
# Enable Nginx site
sudo ln -sf /etc/nginx/sites-available/smart-voting /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t && sudo systemctl reload nginx

# Enable Smart Voting service
sudo systemctl daemon-reload
sudo systemctl enable smart-voting
sudo systemctl start smart-voting
```

## 🌐 การเข้าถึงระบบ

### ภายในเครือข่ายเดียวกัน
- **Dashboard**: `http://odroid-ip`
- **Admin Panel**: `http://odroid-ip/admin`

### จากภายนอก (ต้องตั้งค่า Port Forwarding)
1. เข้า Router Admin Panel
2. ตั้งค่า Port Forwarding: Port 80 → Odroid IP
3. เข้าผ่าน: `http://your-public-ip`

## 🔧 การจัดการระบบ

### ตรวจสอบสถานะ
```bash
sudo systemctl status smart-voting    # ตรวจสอบ service
sudo systemctl status nginx          # ตรวจสอบ nginx
sudo ufw status                      # ตรวจสอบ firewall
```

### ดู Logs
```bash
sudo journalctl -u smart-voting -f   # ดู logs แบบ real-time
sudo tail -f /var/log/nginx/access.log  # ดู nginx logs
```

### รีสตาร์ท Services
```bash
sudo systemctl restart smart-voting  # รีสตาร์ท voting system
sudo systemctl restart nginx        # รีสตาร์ท nginx
```

### อัปเดตระบบ
```bash
cd /opt/smart-voting/dashboard\(totally\)
git pull
source venv/bin/activate
pip install -r requirements.txt
sudo systemctl restart smart-voting
```

## 🔒 ความปลอดภัย

### 1. เปลี่ยน API Key
```bash
# แก้ไขใน app.py
sudo nano /opt/smart-voting/dashboard\(totally\)/app.py
# เปลี่ยน ADMIN_KEY = "mysecret" เป็นรหัสใหม่
sudo systemctl restart smart-voting
```

### 2. ตั้งค่า HTTPS (แนะนำ)
```bash
# ติดตั้ง Certbot
sudo apt install certbot python3-certbot-nginx

# ขอ SSL Certificate
sudo certbot --nginx -d your-domain.com

# Auto-renewal
sudo crontab -e
# เพิ่มบรรทัด: 0 12 * * * /usr/bin/certbot renew --quiet
```

### 3. จำกัดการเข้าถึง Admin
```bash
# จำกัด IP ที่เข้าถึง admin ได้
sudo nano /etc/nginx/sites-available/smart-voting
```

เพิ่มใน location /admin:
```nginx
location /admin {
    allow 192.168.1.0/24;  # อนุญาตเฉพาะเครือข่ายภายใน
    deny all;
    proxy_pass http://127.0.0.1:8000;
    # ... proxy settings
}
```

## 📊 การ Monitor

### ตรวจสอบ Performance
```bash
# ดู CPU และ Memory
htop

# ดู Disk Usage
df -h

# ดู Network
netstat -tulpn | grep :80
```

### Backup ข้อมูล
```bash
# Backup database
cp /opt/smart-voting/dashboard\(totally\)/votes.db /backup/votes-$(date +%Y%m%d).db

# Backup photos
tar -czf /backup/photos-$(date +%Y%m%d).tar.gz /opt/smart-voting/dashboard\(totally\)/photos/
```

## 🚨 Troubleshooting

### Service ไม่เริ่ม
```bash
sudo journalctl -u smart-voting --no-pager
sudo systemctl status smart-voting
```

### Nginx Error
```bash
sudo nginx -t
sudo tail -f /var/log/nginx/error.log
```

### Port ถูกใช้งาน
```bash
sudo netstat -tulpn | grep :8000
sudo lsof -i :8000
```

## 📱 การใช้งาน Mobile

ระบบรองรับการใช้งานบนมือถือ:
- **Responsive Design**: ปรับตัวตามขนาดหน้าจอ
- **Touch Friendly**: ปุ่มและ UI เหมาะสำหรับการสัมผัส
- **Fast Loading**: โหลดเร็วแม้บนเครือข่ายช้า

## 🎯 การใช้งานจริง

1. **ตั้งค่า ESP32**: เชื่อมต่อกับเครือข่ายเดียวกัน
2. **เข้าถึง Dashboard**: เปิดเบราว์เซอร์ไปที่ IP ของ Odroid
3. **จัดการระบบ**: ใช้ Admin Panel สำหรับการตั้งค่า
4. **Monitor ผลลัพธ์**: ดูผลการลงคะแนนแบบเรียลไทม์

---

**หมายเหตุ**: ระบบนี้เหมาะสำหรับการใช้งานในเครือข่ายภายในหรือเครือข่ายที่เชื่อถือได้ สำหรับการใช้งานจริงในระดับประเทศ ควรพิจารณาใช้ Cloud Infrastructure และ Security ที่เข้มงวดกว่า
