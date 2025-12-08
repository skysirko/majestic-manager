cat >/root/zoom.sh <<"EOF"
#!/bin/sh

CONF="/etc/majestic.yaml"

# Zoom presets (for 3840x2160)
CROP1="0x0x3840x2160"         # 1x (full frame)
CROP2="640x360x2560x1440"     # ~1.5x
CROP3="960x540x1920x1080"     # 2x
CROP4="1280x720x1280x720"     # 3x

set_crop() {
    CROP="$1"

    # Replace existing crop line under video0
    # Assumes you have one 'crop:' line (we added it earlier)
    sed -i "s/^[[:space:]]*crop:.*/  crop: ${CROP}/" "$CONF"

    echo "Applied crop: $CROP"

    # Ask Majestic to reload config (SIGHUP)
    # This pattern is used in OpenIPC scripts as well. :contentReference[oaicite:2]{index=2}
    killall -1 majestic 2>/dev/null || killall majestic 2>/dev/null
}

while true; do
    echo
    echo "Select zoom level:"
    echo "  1 - 1x  (full frame)"
    echo "  2 - 1.5x (center crop)"
    echo "  3 - 2x   (center crop)"
    echo "  4 - 3x   (center crop)"
    echo "  q - quit"
    printf "> "
    read key || exit 0

    case "$key" in
        1) set_crop "$CROP1" ;;
        2) set_crop "$CROP2" ;;
        3) set_crop "$CROP3" ;;
        4) set_crop "$CROP4" ;;
        q|Q) echo "Bye"; exit 0 ;;
        *) echo "Unknown option: $key" ;;
    esac
done
EOF

chmod +x /root/zoom.sh
