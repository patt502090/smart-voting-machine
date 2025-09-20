#!/bin/bash
# Smart Voting Deploy Script for Odroid

echo "🚀 Setting up Smart Voting System on Odroid..."

# 1. Update system
echo "📦 Updating system packages..."
sudo apt update && sudo apt upgrade -y

# 2. Install required packages
echo "🔧 Installing required packages..."
sudo apt install -y python3 python3-pip python3-venv git nginx ufw

# 3. Create project directory
echo "📁 Creating project directory..."
sudo mkdir -p /opt/smart-voting
sudo chown $USER:$USER /opt/smart-voting

# 4. Setup Python virtual environment
echo "🐍 Setting up Python environment..."
cd /opt/smart-voting
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
sudo tee /etc/systemd/system/smart-voting.service > /dev/null <<EOF
[Unit]
Description=Smart Voting System
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=/opt/smart-voting/dashboard\(totally\)
Environment=PATH=/opt/smart-voting/venv/bin
ExecStart=/opt/smart-voting/venv/bin/uvicorn app:app --host 0.0.0.0 --port 8000
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

# 7. Configure Nginx reverse proxy
echo "🌐 Configuring Nginx..."
sudo tee /etc/nginx/sites-available/smart-voting > /dev/null <<EOF
server {
    listen 80;
    server_name _;
    
    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
    }
    
    # Handle WebSocket connections (if needed)
    location /ws {
        proxy_pass http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
EOF

# 8. Enable Nginx site
sudo ln -sf /etc/nginx/sites-available/smart-voting /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t && sudo systemctl reload nginx

# 9. Enable and start services
echo "🚀 Starting services..."
sudo systemctl daemon-reload
sudo systemctl enable smart-voting
sudo systemctl start smart-voting
sudo systemctl enable nginx
sudo systemctl start nginx

echo "✅ Smart Voting System deployed successfully!"
echo "🌐 Access your system at: http://$(hostname -I | awk '{print $1}')"
echo "📊 Admin panel: http://$(hostname -I | awk '{print $1}')/admin"
echo ""
echo "🔧 Useful commands:"
echo "  sudo systemctl status smart-voting    # Check service status"
echo "  sudo systemctl restart smart-voting   # Restart service"
echo "  sudo journalctl -u smart-voting -f    # View logs"
echo "  sudo ufw status                       # Check firewall"
