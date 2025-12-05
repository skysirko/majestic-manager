#!/usr/bin/env bash
set -euo pipefail

echo "===================="
echo " FPV Pipeline Setup "
echo "===================="

### 1) Must run as root
if [[ $EUID -ne 0 ]]; then
  echo "Run as root: sudo $0"
  exit 1
fi

echo "==> Updating APT..."
apt update

echo "==> Installing required packages..."
apt install -y \
  linux-image-current-sunxi64 \
  armbian-firmware-full \
  gstreamer1.0-tools \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-base-apps \
  tcpdump \
  python3 \
  python3-gi \
  python3-gst-1.0 \
  gir1.2-gstreamer-1.0 \

echo ""
echo "=============================================================="
echo "KERNEL + DRIVER PACKAGES INSTALLED."
echo "A REBOOT IS REQUIRED NOW to load the correct kernel (6.12.47)"
echo "and to enable the Realtek r8152 USB-Ethernet driver."
echo ""
echo "After reboot, run the SECOND SCRIPT:"
echo "  ./configure_network.sh"
echo "=============================================================="
echo ""

exit 0
