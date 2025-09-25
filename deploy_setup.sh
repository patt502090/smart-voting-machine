#!/bin/bash
# Smart Voting Deploy Script for Odroid

echo "🚀 Setting up Smart Voting Machine System on Odroid..."

# 1. Update system
echo "📦 Updating system packages..."
sudo apt update && sudo apt upgrade -y

# 2. Install required packages
echo "🔧 Installing required packages..."
sudo apt install -y python3 python3-pip python3-venv git nginx ufw openssl

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
sudo tee /etc/nginx/sites-available/smart-voting-machine > /dev/null <<EOF
server {
    listen 80;
    listen 443 ssl;
    server_name _;

    # SSL configuration
    ssl_certificate /etc/nginx/ssl/nginx.crt;
    ssl_certificate_key /etc/nginx/ssl/nginx.key;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;

    # Security headers
    add_header X-Frame-Options "SAMEORIGIN";
    add_header X-XSS-Protection "1; mode=block";
    add_header X-Content-Type-Options "nosniff";
    
    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
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
