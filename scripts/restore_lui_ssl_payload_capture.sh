#!/bin/bash
set -euo pipefail

SERVICE_NAME="booster-lui.service"
DROPIN_DIR="/etc/systemd/system/${SERVICE_NAME}.d"
DROPIN_PATH="${DROPIN_DIR}/override.conf"
BACKUP_PATH="/etc/systemd/system/${SERVICE_NAME}.d/override.conf.bak-preload"

if [ -f "$BACKUP_PATH" ]; then
  cp "$BACKUP_PATH" "$DROPIN_PATH"
elif [ -f "$DROPIN_PATH" ]; then
  rm -f "$DROPIN_PATH"
fi

if [ -d "$DROPIN_DIR" ] && [ -z "$(ls -A "$DROPIN_DIR" 2>/dev/null)" ]; then
  rmdir "$DROPIN_DIR"
fi

systemctl daemon-reload
systemctl restart "$SERVICE_NAME"
systemctl is-active --quiet "$SERVICE_NAME"

echo "restored booster_lui service without SSL payload capture"
