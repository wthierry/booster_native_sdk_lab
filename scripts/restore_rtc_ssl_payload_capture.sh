#!/bin/bash
set -euo pipefail

SERVICE_NAME="booster-rtc-speech.service"
DROPIN_DIR="/etc/systemd/system/${SERVICE_NAME}.d"
DROPIN_PATH="${DROPIN_DIR}/50-ssl-payload-capture.conf"

rm -f "$DROPIN_PATH"

if [ -d "$DROPIN_DIR" ] && [ -z "$(ls -A "$DROPIN_DIR" 2>/dev/null)" ]; then
  rmdir "$DROPIN_DIR"
fi

systemctl daemon-reload
systemctl restart "$SERVICE_NAME"
systemctl is-active --quiet "$SERVICE_NAME"

echo "restored booster_rtc_cli service without SSL payload capture"
