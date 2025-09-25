#!/bin/bash
# Smart Voting Deploy Script for Odroid

echo "🚀 Setting up Smart Voting Machine System on Odroid..."

# 1. Update system
echo "📦 Updating system packages..."
sudo apt update && sudo apt upgrade -y

# 2. Install required packages
echo "🔧 Installing required packages..."
sudo apt install -y python3 python3-pip python3-venv git nginx ufw openssl fail2ban iptables-persistent

# Configure fail2ban for DDoS protection
echo "🛡️ Setting up fail2ban..."
sudo tee /etc/fail2ban/jail.local > /dev/null <<EOF
[DEFAULT]
bantime = 3600
findtime = 600
maxretry = 5

[nginx-req-limit]
enabled = true
filter = nginx-req-limit
action = iptables-multiport[name=ReqLimit, port="http,https"]
logpath = /var/log/nginx/error.log
findtime = 600
maxretry = 10
bantime = 7200

[nginx-badbots]
enabled = true
filter = nginx-badbots
action = iptables-multiport[name=BadBots, port="http,https"]
logpath = /var/log/nginx/access.log
maxretry = 2

[nginx-botsearch]
enabled = true
filter = nginx-botsearch
action = iptables-multiport[name=BotSearch, port="http,https"]
logpath = /var/log/nginx/access.log
maxretry = 2
EOF

# Create custom fail2ban filter for rate limiting
sudo tee /etc/fail2ban/filter.d/nginx-req-limit.conf > /dev/null <<EOF
[Definition]
failregex = limiting requests, excess:.* by zone.*client: <HOST>
ignoreregex =
EOF

# Configure iptables for DDoS protection
echo "🛡️ Configuring iptables rules..."
# Limit connections per IP
sudo iptables -A INPUT -p tcp --dport 80 -m connlimit --connlimit-above 20 --connlimit-mask 32 -j DROP
sudo iptables -A INPUT -p tcp --dport 443 -m connlimit --connlimit-above 20 --connlimit-mask 32 -j DROP

# Limit new connections rate
sudo iptables -A INPUT -p tcp --dport 80 -m state --state NEW -m limit --limit 50/minute --limit-burst 100 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 443 -m state --state NEW -m limit --limit 50/minute --limit-burst 100 -j ACCEPT

# Save iptables rules
sudo netfilter-persistent save

# Generate self-signed certificate
echo "🔒 Generating self-signed SSL certificate..."
sudo mkdir -p /etc/nginx/ssl
sudo openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout /etc/nginx/ssl/nginx.key \
    -out /etc/nginx/ssl/nginx.crt \
    -subj "/C=TH/ST=Bangkok/L=Bangkok/O=Smart Voting Machine/OU=IT/CN=$(hostname -I | awk '{print $1}')"

# 3. Create project directory
echo "📁 Creating project directory..."
sudo mkdir -p /opt/smart-voting-machine
sudo chown $USER:$USER /opt/smart-voting-machine

# 4. Setup Python virtual environment
echo "🐍 Setting up Python environment..."
cd /opt/smart-voting-machine
python3 -m venv venv
source venv/bin/activate
pip install fastapi uvicorn python-multipart

# 5. Configure firewall
echo "🔥 Configuring firewall..."
sudo ufw allow 22    # SSH
sudo ufw allow 80    # HTTP
sudo ufw allow 443   # HTTPS
sudo ufw allow 8000  # FastAPI (temporary)
sudo ufw --force enable

# 6. Create systemd service
echo "⚙️ Creating systemd service..."
sudo tee /etc/systemd/system/smart-voting-machine.service > /dev/null <<EOF
[Unit]
Description=Smart Voting System
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=/opt/smart-voting-machine/dashboard\(totally\)
Environment=PATH=/opt/smart-voting-machine/venv/bin
ExecStart=/opt/smart-voting-machine/venv/bin/uvicorn app:app --host 0.0.0.0 --port 8000
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

# 7. Configure Nginx reverse proxy
echo "🌐 Configuring Nginx..."

# Create custom Nginx configuration for security
sudo tee /etc/nginx/conf.d/security.conf > /dev/null <<EOF
# Rate limiting zones
limit_req_zone \$binary_remote_addr zone=one:10m rate=30r/m;
limit_conn_zone \$binary_remote_addr zone=addr:10m;

# Security headers
add_header X-Frame-Options "SAMEORIGIN" always;
add_header X-XSS-Protection "1; mode=block" always;
add_header X-Content-Type-Options "nosniff" always;
add_header Referrer-Policy "strict-origin-when-cross-origin" always;
add_header Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval'; style-src 'self' 'unsafe-inline';" always;
add_header Strict-Transport-Security "max-age=31536000; includeSubDomains; preload" always;

# Block common attacks
server_tokens off;
EOF

sudo tee /etc/nginx/sites-available/smart-voting-machine > /dev/null <<EOF
# DDoS protection
limit_req zone=one burst=5 nodelay;
limit_conn addr 10;

server {
    listen 80;
    server_name _;
    # Redirect all HTTP to HTTPS
    return 301 https://\$host\$request_uri;
}

server {
    listen 443 ssl http2;
    server_name _;

    # SSL configuration
    ssl_certificate /etc/nginx/ssl/nginx.crt;
    ssl_certificate_key /etc/nginx/ssl/nginx.key;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_prefer_server_ciphers on;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384;
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:50m;
    ssl_session_tickets off;

    # Security headers
    add_header X-Frame-Options "SAMEORIGIN";
    add_header X-XSS-Protection "1; mode=block";
    add_header X-Content-Type-Options "nosniff";
    
    # Security measures
    if (\$request_method !~ ^(GET|HEAD|POST)$) {
        return 444;
    }

    # Block common vulnerability scanners and bots
    if (\$http_user_agent ~* (nikto|sqlmap|arachni|nessus|acunetix|qualys|nmap)) {
        return 403;
    }

    # Anti SQL-injection
    set \$block_sql_injections 0;
    if (\$query_string ~ "union.*select.*\(") {
        set \$block_sql_injections 1;
    }
    if (\$query_string ~ "union.*all.*select.*") {
        set \$block_sql_injections 1;
    }
    if (\$block_sql_injections = 1) {
        return 403;
    }

    location / {
        # Rate limiting
        limit_req zone=one burst=5 nodelay;
        limit_conn addr 10;

        # Basic proxy settings
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;

        # Security headers
        proxy_hide_header X-Powered-By;
        proxy_hide_header Server;
        
        # Timeout settings
        proxy_connect_timeout 60s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
    }

    location /static {
        alias /opt/smart-voting-machine/dashboard\(totally\)/static;
        expires 1y;
        add_header Cache-Control "public, no-transform";
    }
    
    # Handle WebSocket connections
    location /ws {
        proxy_pass http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
    }

    client_max_body_size 10M;
}
EOF

# 8. Enable Nginx site
sudo ln -sf /etc/nginx/sites-available/smart-voting-machine /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t && sudo systemctl reload nginx

# 9. Enable and start services
echo "🚀 Starting services..."
sudo systemctl daemon-reload
sudo systemctl enable smart-voting-machine
sudo systemctl start smart-voting-machine
sudo systemctl enable nginx
sudo systemctl start nginx
sudo systemctl enable fail2ban
sudo systemctl start fail2ban

# Additional security measures
echo "🔒 Setting additional security measures..."

# Secure shared memory
echo "tmpfs     /run/shm     tmpfs     defaults,noexec,nosuid     0     0" | sudo tee -a /etc/fstab

# Secure sysctl settings
sudo tee -a /etc/sysctl.conf > /dev/null <<EOF
# IP Spoofing protection
net.ipv4.conf.all.rp_filter = 1
net.ipv4.conf.default.rp_filter = 1

# Ignore ICMP broadcast requests
net.ipv4.icmp_echo_ignore_broadcasts = 1

# Disable source packet routing
net.ipv4.conf.all.accept_source_route = 0
net.ipv4.conf.default.accept_source_route = 0

# Ignore send redirects
net.ipv4.conf.all.send_redirects = 0
net.ipv4.conf.default.send_redirects = 0

# Block SYN attacks
net.ipv4.tcp_syncookies = 1
net.ipv4.tcp_max_syn_backlog = 2048
net.ipv4.tcp_synack_retries = 2
net.ipv4.tcp_syn_retries = 5

# Log Martians
net.ipv4.conf.all.log_martians = 1
net.ipv4.conf.default.log_martians = 1

# Increase system file descriptor limit
fs.file-max = 65535
EOF

# Apply sysctl settings
sudo sysctl -p

echo "✅ Smart Voting Machine System deployed successfully!"
SERVER_IP=$(hostname -I | awk '{print $1}')
echo "🌐 Access your system at:"
echo "  HTTP:  http://${SERVER_IP}"
echo "  HTTPS: https://${SERVER_IP} (Will show security warning - this is normal for self-signed certificates)"
echo ""
echo "📊 Admin panels:"
echo "  HTTP:  http://${SERVER_IP}/admin"
echo "  HTTPS: https://${SERVER_IP}/admin"
echo ""
echo "🔧 Useful commands:"
echo "  sudo systemctl status smart-voting-machine    # Check service status"
echo "  sudo systemctl restart smart-voting-machine   # Restart service"
echo "  sudo journalctl -u smart-voting-machine -f    # View logs"
echo "  sudo ufw status                              # Check firewall"
echo ""
echo "📝 Notes:"
echo "  1. When accessing via HTTPS, you will see a security warning - this is normal"
echo "  2. Click 'Advanced' and 'Proceed anyway' in your browser"
echo "  3. The self-signed certificate is valid for 365 days"
