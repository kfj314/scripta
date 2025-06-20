#!/bin/bash

set -e

echo "Updating system..."
sudo apt update && sudo apt upgrade -y

echo "Installing system packages..."
sudo apt install -y python3-pip python3-venv python3-pil python3-numpy python3-lib2to3 python3-dev build-essential libjpeg-dev zlib1g-dev spidev i2c-tools

echo "Creating Python virtual environment..."
cd ~/your-existing-repo/scripta
python3 -m venv venv
source venv/bin/activate

echo "Installing Python packages..."
pip install --extra-index-url https://www.piwheels.org/simple pillow numpy RPi.GPIO spidev

echo "Scripta setup complete!"
echo "Activate virtualenv: source ~/your-existing-repo/scripta/venv/bin/activate"
echo "Run test: python3 epd_test.py"
