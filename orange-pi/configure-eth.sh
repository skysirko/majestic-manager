#!/usr/bin/env bash
set -euo pipefail

echo "==============================================================="
echo " Configuring eth0 with static IP"
echo "==============================================================="

ETH_IF="eth0"
ETH_IP="192.168.1.150/24"

# -------------------------------------------------------------------
# Disable Netplan completely (prevents overriding systemd-networkd)
# -------------------------------------------------------------------
echo "==> Disabling netplan..."

if [[ -d /etc/netplan ]]; then
  mv /etc/netplan /etc/netplan.disabled.$(date +%s)
  echo "    Moved /etc/netplan → /etc/netplan.disabled.*"
fi

if ls /run/systemd/network/10-netplan* &>/dev/null; then
  rm -f /run/systemd/network/10-netplan*
  echo "    Removed active netplan-generated configs"
fi

# -------------------------------------------------------------------
# Enable systemd-networkd + resolved
# -------------------------------------------------------------------
echo "==> Enabling systemd-networkd..."
systemctl enable systemd-networkd.service
systemctl enable systemd-resolved.service

# -------------------------------------------------------------------
# Configure eth0
# -------------------------------------------------------------------
echo "==> Creating systemd-networkd config for ${ETH_IF}..."

tee /etc/systemd/network/10-${ETH_IF}.network >/dev/null <<EOF
[Match]
Name=${ETH_IF}

[Network]
Address=${ETH_IP}
EOF

echo "==> Reloading systemd-networkd..."
systemctl restart systemd-networkd

echo ""
echo "==============================================================="
echo " DONE!"
echo ""
echo "eth0:"
echo "  Orange Pi: ${ETH_IP}"
echo ""
echo "Reboot recommended: sudo reboot"
echo "==============================================================="
