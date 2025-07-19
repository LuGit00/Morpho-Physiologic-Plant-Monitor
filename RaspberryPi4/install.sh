#!/bin/bash

# Enable verbose debugging for the entire script
set -x

clear
if [ $# -eq 0 ]
then
    echo "ERROR: Run .../install.sh <Replace with your Raspberry OS Username>@<Replace with your Raspberry IP>"
    exit
fi

echo "Installing Dependencies"
# IMPORTANT: Uncomment and run this block. It's crucial for lighttpd and build tools.
# I've included the full list you had, as your C programs might depend on them.
ssh "$1" "sudo apt update && sudo apt full-upgrade -y && \
sudo apt install -y \
    lighttpd build-essential pkg-config libjpeg-dev libtiff5-dev libpng-dev \
    libavcodec-dev libavformat-dev libswscale-dev libv4l-dev libxvidcore-dev \
    libx264-dev libfontconfig1-dev libcairo2-dev libgdk-pixbuf2.0-dev libpango1.0-dev \
    libatk1.0-dev libglib2.0-dev libgtk2.0-dev libhdf5-dev libhdf5-103 libtbb-dev \
    libatlas-base-dev gfortran libfaac-dev libmp3lame-dev libvorbis-dev libopenexr-dev \
    libwebp-dev libopencv-dev network-manager dnsmasq hostapd wget"

echo "Sending Repository's Files"
scp -r ../RaspberryPi4 "$1":~/

echo "INSTALL START"
# Using a 'here-document' for the SSH command for better multi-line handling and quoting.
# The 'EOF' marker must be on a line by itself, and the closing 'EOF' must be at the very beginning of its line.
ssh "$1" << 'EOF_REMOTE_COMMANDS'
set -x

echo "--- Processing lighttpd.conf ---"
sudo mv ~/RaspberryPi4/lighttpd.conf /etc/lighttpd/lighttptd.conf

echo "--- Compiling and setting up index.cgi (Web UI) ---"
sudo mkdir -p /usr/lib/cgi-bin/
sudo gcc -o /usr/lib/cgi-bin/index.cgi ~/RaspberryPi4/index.c
sudo chown www-data:www-data /usr/lib/cgi-bin/index.cgi
sudo chmod 755 /usr/lib/cgi-bin/index.cgi

echo "--- Compiling and setting up ping.cgi (Device Pings) ---"
sudo gcc -o /usr/lib/cgi-bin/ping.cgi ~/RaspberryPi4/ping.c
sudo chown www-data:www-data /usr/lib/cgi-bin/ping.cgi
sudo chmod 755 /usr/lib/cgi-bin/ping.cgi

echo "--- Compiling and setting up application binary ---"
sudo gcc -o /usr/local/bin/application ~/RaspberryPi4/application.c
sudo chmod 755 /usr/local/bin/application

echo "--- Compiling and setting up OpenCV image generator (generate_plant_images.cpp) ---"
if pkg-config opencv4 --cflags --libs >/dev/null 2>&1; then
    sudo g++ -o /usr/local/bin/generate_plant_images ~/RaspberryPi4/generate_plant_images.cpp $(pkg-config opencv4 --cflags --libs) -lstdc++fs
elif pkg-config opencv --cflags --libs >/dev/null 2>&1; then
    sudo g++ -o /usr/local/bin/generate_plant_images ~/RaspberryPi4/generate_plant_images.cpp $(pkg-config opencv --cflags --libs) -lstdc++fs
else
    echo "Error: OpenCV pkg-config not found. Please ensure OpenCV development libraries are installed."
    exit 1
fi
sudo chmod 755 /usr/local/bin/generate_plant_images

echo "--- Managing application.service ---"
sudo mv ~/RaspberryPi4/application.service /etc/systemd/system/application.service
sudo systemctl daemon-reload

sudo systemctl stop application.service || true
sudo systemctl disable application.service || true
sudo systemctl reset-failed application.service || true

sleep 1

echo "--- Clearing and setting permissions for data files (before app starts) ---"
sudo mkdir -p /var/www/html/data
sudo chown www-data:www-data /var/www/html/data
sudo chmod 775 /var/www/html/data
sudo mkdir -p /var/www/html/data/images
sudo chown www-data:www-data /var/www/html/data/images
sudo chmod 775 /var/www/html/data/images

sudo rm -rf /var/www/html/data/*

sudo touch /var/www/html/data/ping.txt
sudo touch /var/www/html/data/devices.txt
sudo touch /var/www/html/data/plants.txt
sudo touch /var/www/html/data/processes.txt # Ensure processes.txt is created
sudo chown www-data:www-data /var/www/html/data/ping.txt
sudo chown www-data:www-data /var/www/html/data/devices.txt
sudo chown www-data:www-data /var/www/html/data/plants.txt
sudo chown www-data:www-data /var/www/html/data/processes.txt # Set ownership for processes.txt
sudo chmod 664 /var/www/html/data/ping.txt
sudo chmod 664 /var/www/html/data/devices.txt
sudo chmod 664 /var/www/html/data/plants.txt
sudo chmod 664 /var/www/html/data/processes.txt # Set permissions for processes.txt

# Initialize processes.txt with default values if it's empty
if [ ! -s /var/www/html/data/processes.txt ]; then
    echo "0,3600" | sudo tee /var/www/html/data/processes.txt > /dev/null
    sudo chown www-data:www-data /var/www/html/data/processes.txt # Re-chown after tee
    sudo chmod 664 /var/www/html/data/processes.txt # Re-chmod after tee
fi


sudo systemctl enable application.service
sudo systemctl start application.service

echo "--- Checking application.service status after start ---"
systemctl status application.service --no-pager
echo "--- Last 20 lines of application.service journal ---"
journalctl -u application.service --no-pager -n 20

echo "--- Restarting and enabling lighttpd ---"
sudo systemctl restart lighttpd
sudo systemctl enable lighttpd

echo "--- Checking lighttpd.service status after restart ---"
systemctl status lighttpd.service --no-pager

echo "--- Cleaning up local repository files on Raspberry Pi ---"
sudo rm -rf ~/RaspberryPi4

echo "INSTALL END"
EOF_REMOTE_COMMANDS
