# 🌐 การตั้งค่าเครือข่ายสำหรับ Smart Voting System

## 📡 การเข้าถึงจากภายนอก

### 1. ภายในเครือข่ายเดียวกัน (Local Network)
```
http://192.168.1.100    # แทนที่ด้วย IP ของ Odroid
http://192.168.1.100/admin
```

### 2. จากภายนอก (Internet Access)

#### วิธีที่ 1: Port Forwarding
1. เข้า Router Admin Panel (มักเป็น 192.168.1.1)
2. หา "Port Forwarding" หรือ "Virtual Server"
3. ตั้งค่า:
   - **External Port**: 80
   - **Internal IP**: IP ของ Odroid (เช่น 192.168.1.100)
   - **Internal Port**: 80
   - **Protocol**: TCP

4. เข้าผ่าน: `http://your-public-ip`

#### วิธีที่ 2: Dynamic DNS (DDNS)
1. ลงทะเบียน DDNS service (เช่น No-IP, DuckDNS)
2. ตั้งค่าใน Router หรือ Odroid
3. เข้าผ่าน: `http://your-domain.ddns.net`

#### วิธีที่ 3: Cloudflare Tunnel (แนะนำ)
```bash
# ติดตั้ง cloudflared
wget https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64
sudo mv cloudflared-linux-arm64 /usr/local/bin/cloudflared
sudo chmod +x /usr/local/bin/cloudflared

# สร้าง tunnel
cloudflared tunnel create smart-voting
cloudflared tunnel route dns smart-voting your-domain.com
cloudflared tunnel run smart-voting
```

## 🔒 ความปลอดภัยเครือข่าย

### 1. Firewall Configuration
```bash
# เปิดเฉพาะ port ที่จำเป็น
sudo ufw allow 22    # SSH
sudo ufw allow 80    # HTTP
sudo ufw allow 443   # HTTPS
sudo ufw deny 8000   # ปิด direct access ถึง FastAPI
sudo ufw enable
```

### 2. IP Whitelist สำหรับ Admin
```bash
# แก้ไข nginx config
sudo nano /etc/nginx/sites-available/smart-voting
```

เพิ่มใน location /admin:
```nginx
location /admin {
    allow 192.168.1.0/24;  # เครือข่ายภายใน
    allow YOUR_OFFICE_IP;   # IP ของสำนักงาน
    deny all;
    
    proxy_pass http://127.0.0.1:8000;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
}
```

### 3. Rate Limiting
```nginx
# ใน nginx config
http {
    limit_req_zone $binary_remote_addr zone=api:10m rate=10r/m;
    
    server {
        location /admin {
            limit_req zone=api burst=5 nodelay;
            # ... proxy settings
        }
    }
}
```

## 📱 การใช้งานบน Mobile

### 1. Responsive Design
- ระบบรองรับการใช้งานบนมือถือ
- ปุ่มและ UI ปรับขนาดตามหน้าจอ
- รองรับการสัมผัส (touch)

### 2. PWA (Progressive Web App)
```javascript
// เพิ่มใน index.html
<link rel="manifest" href="/manifest.json">
<meta name="theme-color" content="#1e40af">
<meta name="apple-mobile-web-app-capable" content="yes">
```

### 3. Offline Support
```javascript
// Service Worker สำหรับ offline access
if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js');
}
```

## 🔧 การ Monitor เครือข่าย

### 1. ตรวจสอบการเชื่อมต่อ
```bash
# ดู active connections
netstat -tulpn | grep :80
ss -tulpn | grep :80

# ดู traffic
iftop
nethogs
```

### 2. Log Analysis
```bash
# ดู nginx access logs
sudo tail -f /var/log/nginx/access.log

# ดู error logs
sudo tail -f /var/log/nginx/error.log

# วิเคราะห์ traffic
sudo awk '{print $1}' /var/log/nginx/access.log | sort | uniq -c | sort -nr
```

### 3. Performance Monitoring
```bash
# ดู system resources
htop
iotop
free -h
df -h
```

## 🚨 Troubleshooting เครือข่าย

### 1. ไม่สามารถเข้าถึงจากภายนอก
```bash
# ตรวจสอบ firewall
sudo ufw status

# ตรวจสอบ port forwarding
sudo netstat -tulpn | grep :80

# ทดสอบ local access
curl http://localhost:8000
```

### 2. ช้า/Timeout
```bash
# ตรวจสอบ bandwidth
speedtest-cli

# ตรวจสอบ latency
ping google.com

# ตรวจสอบ DNS
nslookup your-domain.com
```

### 3. SSL/HTTPS Issues
```bash
# ตรวจสอบ certificate
openssl s_client -connect your-domain.com:443

# Renew certificate
sudo certbot renew --dry-run
```

## 📊 การใช้งานจริง

### 1. การตั้งค่าสำหรับการเลือกตั้ง
- ใช้เครือข่ายภายใน (LAN) เพื่อความปลอดภัย
- ตั้งค่า WiFi แยกสำหรับระบบลงคะแนน
- ใช้ VPN สำหรับการเข้าถึงจากภายนอก

### 2. การ Monitor แบบ Real-time
```bash
# Script สำหรับ monitor
#!/bin/bash
while true; do
    echo "$(date): $(curl -s http://localhost:8000/tally | jq '.total') votes"
    sleep 5
done
```

### 3. Backup และ Recovery
```bash
# Backup ข้อมูล
rsync -av /opt/smart-voting/ backup-server:/backup/smart-voting/

# Auto backup script
echo "0 2 * * * rsync -av /opt/smart-voting/ backup-server:/backup/" | crontab -
```

---

**หมายเหตุ**: สำหรับการใช้งานจริงในระดับประเทศ ควรพิจารณาใช้ Cloud Infrastructure และ Security ที่เข้มงวดกว่า รวมถึงการทำ Load Balancing และ High Availability
