#!/bin/bash

# Exit if any command fails
set -e

echo "=========================="
echo "Raspberry Pi Setup Script"
echo "=========================="
echo "Updating system..."
sudo apt update && sudo apt upgrade -y

echo "Installing packages..."
sudo apt install -y git build-essential python3-pip python3-dev python3-setuptools python3-pil python3-numpy python3-smbus fbgrab micro ranger

echo "Installing Syncthing..."
curl -s https://syncthing.net/release-key.txt | sudo apt-key add -
echo "deb https://apt.syncthing.net/ syncthing stable" | sudo tee /etc/apt/sources.list.d/syncthing.list
sudo apt update
sudo apt install -y syncthing

echo "Enabling Syncthing service..."
sudo systemctl enable syncthing@pi.service
sudo systemctl start syncthing@pi.service

echo "Installing Waveshare e-Paper drivers..."
cd ~
git clone https://github.com/waveshare/e-Paper.git
cd e-Paper/RaspberryPi_JetsonNano/python
sudo python3 setup.py install

echo "Enabling SPI interface..."
sudo raspi-config nonint do_spi 0

echo "Setting up /boot/config.txt..."
sudo tee -a /boot/config.txt > /dev/null <<EOF

# Waveshare e-Paper SPI
dtparam=spi=on
EOF

echo "Copying console mirroring script..."
cp "$(dirname "$0")/eink-console.py" ~/
chmod +x ~/eink-console.py

echo "Creating systemd service file..."
sudo tee /etc/systemd/system/einkconsole.service > /dev/null <<EOF
[Unit]
Description=E-Ink Console Mirror

[Service]
ExecStart=/usr/bin/python3 /home/pi/eink-console.py
Restart=always
User=pi

[Install]
WantedBy=multi-user.target
EOF

echo "Enabling word count plugin for Micro..."
micro -plugin install wc

echo "All done! Reboot and enable the console mirroring service if ready:"
echo "sudo systemctl enable einkconsole.service"
echo "sudo systemctl start einkconsole.service"
