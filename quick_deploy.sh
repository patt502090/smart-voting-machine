#!/bin/bash
# Quick Deploy Script for Smart Voting System

echo "🚀 Quick Deploy Smart Voting System"
echo "=================================="

# Get current IP
IP=$(hostname -I | awk '{print $1}')
echo "📍 Current IP: $IP"

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "❌ Please don't run as root. Use: bash quick_deploy.sh"
    exit 1
fi

# Install dependencies
echo "📦 Installing dependencies..."
sudo apt update
sudo apt install -y python3 python3-pip python3-venv nginx

# Create project directory
echo "📁 Setting up project..."
mkdir -p ~/smart-voting
cd ~/smart-voting

# Copy project files (assuming they're in current directory)
if [ -d "../dashboard(totally)" ]; then
    cp -r "../dashboard(totally)" ./
    echo "✅ Project files copied"
else
    echo "❌ Project files not found. Please run this script from the project directory."
    exit 1
fi

# Setup Python environment
echo "🐍 Setting up Python environment..."
cd "dashboard(totally)"
python3 -m venv venv
source venv/bin/activate
pip install fastapi uvicorn python-multipart

# Create simple startup script
echo "⚙️ Creating startup script..."
cat > start_server.sh << 'EOF'
#!/bin/bash
cd ~/smart-voting/dashboard\(totally\)
source venv/bin/activate
uvicorn app:app --host 0.0.0.0 --port 8000
EOF
chmod +x start_server.sh

# Configure Nginx
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
    }
}
EOF

# Enable site
sudo ln -sf /etc/nginx/sites-available/smart-voting /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t && sudo systemctl reload nginx

echo ""
echo "✅ Deploy completed!"
echo ""
echo "🌐 Access your system:"
echo "   Dashboard: http://$IP"
echo "   Admin:     http://$IP/admin"
echo ""
echo "🚀 To start the server:"
echo "   cd ~/smart-voting/dashboard\(totally\)"
echo "   ./start_server.sh"
echo ""
echo "🔧 Or run manually:"
echo "   cd ~/smart-voting/dashboard\(totally\)"
echo "   source venv/bin/activate"
echo "   uvicorn app:app --host 0.0.0.0 --port 8000"
echo ""
echo "📱 Mobile access: Use the same URLs on your phone/tablet"
echo "🔒 Admin API Key: mysecret"
