# Morpho-Physiologic-Plant-Monitor

An implementation of a plant monitoring system utilizing sets of ESP32Cams and a Raspberry Pi 4.

## Table of Contents

* [Installation](#installation)

  * [Raspberry Pi 4](#raspberry-pi-4)

## Installation

Detailed steps on how to get your project up and running.

### Raspberry Pi 4

This section outlines the steps to install and set up the project on a Raspberry Pi 4.

1. **Prepare and Flash Raspberry Pi OS:**
    Use the official Raspberry Pi Imager (`rpi-imager`) to flash "Raspberry Pi OS (64-bit) Lite" onto your SD card. **Before writing the image**, ensure you enable SSH and configure your Wi-Fi settings within the Imager. Once prepared, insert the SD card into your Raspberry Pi 4.

2. **Establish SSH Connection:**
    After your Raspberry Pi boots and connects to your Wi-Fi network, determine its IP address (e.g., by checking your router's connected devices or using a network scanning tool). From your computer's terminal, run the following making the required changes in command:

   ```make USER=<Replace with your Rasbeberry OS Username> IP=<Replace with your Raspberry OS IP>```



