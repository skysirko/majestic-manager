#!/bin/bash
set -e

WLAN_IF="wlan0"

echo "=== Creating systemd-networkd config ==="
cat >/etc/systemd/network/${WLAN_IF}.network <<EOF
[Match]
Name=${WLAN_IF}

[Network]
DHCP=yes
EOF

echo "=== Creating wpa_supplicant config with two possible networks ==="
cat >/etc/wpa_supplicant/wpa_supplicant-${WLAN_IF}.conf <<'EOF'
ctrl_interface=/run/wpa_supplicant
update_config=1
country=US

network={
    ssid="Skyassist"
    psk="wifipass01"
    priority=10
}

network={
    ssid="bolshoj_opossum"
    psk="22020840"
    priority=5
}
EOF

echo "=== Enabling services ==="
systemctl enable systemd-networkd.service
systemctl enable systemd-networkd-wait-online.service
systemctl enable wpa_supplicant@${WLAN_IF}.service

echo "=== Restarting services ==="
systemctl restart wpa_supplicant@${WLAN_IF}.service
systemctl restart systemd-networkd.service

echo "=== Checking wireless status ==="
ip a show ${WLAN_IF}
sleep 3
echo "=== Testing connectivity ==="
ping -c 3 8.8.8.8 || echo "Warning: ping failed (maybe no internet yet)."

echo "=== DONE ==="
echo "Reboot recommended:  reboot now"
