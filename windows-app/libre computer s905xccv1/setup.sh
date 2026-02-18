#!/bin/bash
# Setup script for Tiny Home Personal Assistant on Ubuntu 22.04

echo "Setting up Tiny Home Personal Assistant..."

# Update system
sudo apt update && sudo apt upgrade -y

# Install system dependencies
sudo apt install -y \
    python3-pip \
    python3-venv \
    python3-dev \
    build-essential \
    portaudio19-dev \
    espeak \
    espeak-data \
    libespeak1 \
    libespeak-dev \
    festival \
    festvox-kallpc16k \
    alsa-utils \
    pulseaudio \
    cec-utils \
    git \
    curl \
    wget

# Install Python dependencies
pip3 install --upgrade pip
pip3 install -r requirements.txt

# Create virtual environment
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# Setup audio
echo "Configuring audio..."
sudo usermod -a -G audio $USER

# Setup CEC for TV control
echo "Setting up CEC for TV control..."
sudo apt install -y cec-utils

# Create systemd service for autostart
sudo tee /etc/systemd/system/tiny-home-assistant.service > /dev/null <<EOF
[Unit]
Description=Tiny Home Personal Assistant
After=network.target sound.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$(pwd)
Environment=PATH=$(pwd)/venv/bin:/usr/bin:/bin
ExecStart=$(pwd)/venv/bin/python main.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# Enable service (but don't start yet)
sudo systemctl daemon-reload
sudo systemctl enable tiny-home-assistant.service

# Set up 4K display optimization
echo "Setting up 4K display optimization..."
cat >> ~/.bashrc << 'EOF'
# 4K TV optimization
export GDK_SCALE=1.25
export QT_SCALE_FACTOR=1.25
EOF

# Create desktop entry
mkdir -p ~/.local/share/applications
cat > ~/.local/share/applications/tiny-home-assistant.desktop << EOF
[Desktop Entry]
Name=Tiny Home Assistant
Comment=Personal Assistant for Tiny Home
Exec=$(pwd)/venv/bin/python $(pwd)/main.py
Icon=applications-multimedia
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
EOF

echo ""
echo "Setup complete!"
echo ""
echo "Next steps:"
echo "1. Configure your Roku TV IP in config.json"
echo "2. Test CEC: echo 'on 0' | cec-client -s -d 1"
echo "3. Run the assistant: python3 main.py"
echo "4. To auto-start: sudo systemctl start tiny-home-assistant"
echo ""
echo "Voice commands:"
echo "- 'hey assistant turn on tv'"
echo "- 'hey assistant netflix'"
echo "- 'hey assistant lights on'"
echo "- 'hey assistant what time is it'"