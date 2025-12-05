#!/bin/sh

echo "[WiFi] Creating /etc/wpa_supplicant.conf..."
cat >/etc/wpa_supplicant.conf <<EOF
ctrl_interface=/var/run/wpa_supplicant
ap_scan=1

network={
    ssid="Skyassist"
    psk="wifipass01"
    priority=15
}

network={
    ssid="bolshoj_opossum"
    psk="22020840"
    priority=10
}

network={
    ssid="malenkij_kotik"
    psk="22020840"
    priority=5
}
EOF

echo "[WiFi] Killing any old wpa_supplicant..."
killall wpa_supplicant 2>/dev/null

echo "[WiFi] Bringing wlan0 up..."
ifconfig wlan0 up

echo "[WiFi] Starting wpa_supplicant on wlan0..."
wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf

echo "[WiFi] Requesting DHCP IP..."
udhcpc -i wlan0 -q

echo "[WiFi] Checking IP..."
ip addr show wlan0

echo "[WiFi] Ensuring Wi-Fi autostart on boot..."

# --- Install into /etc/rc.local ---
RC="/etc/rc.local"
ENTRY="sh /root/setup-wifi.sh"

# If rc.local doesn't exist, create it
if [ ! -f "$RC" ]; then
    echo "[WiFi] /etc/rc.local not found. Creating a new one..."
    cat >"$RC" <<EOF
#!/bin/sh
$ENTRY
exit 0
EOF
    chmod +x "$RC"
    echo "[WiFi] Created new /etc/rc.local with Wi-Fi autostart."
else
    # Ensure executable
    chmod +x "$RC"

    # Add Wi-Fi start entry if missing
    if ! grep -Fxq "$ENTRY" "$RC"; then
        echo "[WiFi] Adding Wi-Fi autostart to /etc/rc.local..."
        # Insert before "exit 0"
        sed -i "s|exit 0|$ENTRY\nexit 0|" "$RC"
    else
        echo "[WiFi] Autostart entry already present."
    fi
fi

echo "[WiFi] Wi-Fi setup finished."
echo "[WiFi] It will now auto-connect on every boot."
