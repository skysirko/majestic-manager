#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="mavproxy-router.service"
SERVICE_PATH="/etc/systemd/system/${SERVICE_NAME}"

USER_NAME="pi"
WORKDIR="/home/pi"

PYTHON_BIN="/home/pi/venvs/mav/bin/python3"
MAVPROXY_BIN="/home/pi/venvs/mav/bin/mavproxy.py"

MASTER="udp:192.168.1.254:14550"
OUT_1="udp:192.168.1.149:14550"
OUT_2="udp:192.168.1.140:14550"
OUT_3="udp:192.168.1.100:14550"

echo "Checking binaries..."
if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "ERROR: Python binary not found or not executable: ${PYTHON_BIN}"
  exit 1
fi

if [[ ! -f "${MAVPROXY_BIN}" ]]; then
  echo "ERROR: MAVProxy script not found: ${MAVPROXY_BIN}"
  exit 1
fi

echo "Creating systemd service: ${SERVICE_PATH}"
sudo tee "${SERVICE_PATH}" > /dev/null <<EOF
[Unit]
Description=MAVProxy telemetry router
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${USER_NAME}
WorkingDirectory=${WORKDIR}
ExecStart=${PYTHON_BIN} ${MAVPROXY_BIN} --daemon --non-interactive --master=${MASTER} --out=${OUT_1} --out=${OUT_2} --out=${OUT_3}
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

echo "Reloading systemd..."
sudo systemctl daemon-reload

echo "Enabling service..."
sudo systemctl enable "${SERVICE_NAME}"

echo "Restarting service..."
sudo systemctl restart "${SERVICE_NAME}"

echo
echo "Service status:"
sudo systemctl status "${SERVICE_NAME}" --no-pager -l || true

echo
echo "Recent logs:"
sudo journalctl -u "${SERVICE_NAME}" -n 30 --no-pager || true

echo
echo "Done."