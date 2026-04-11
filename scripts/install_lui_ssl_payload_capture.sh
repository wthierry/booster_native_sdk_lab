#!/bin/bash
set -euo pipefail

SERVICE_NAME="booster-lui.service"
DROPIN_DIR="/etc/systemd/system/${SERVICE_NAME}.d"
DROPIN_PATH="${DROPIN_DIR}/override.conf"
BACKUP_PATH="/etc/systemd/system/${SERVICE_NAME}.d/override.conf.bak-preload"
LIB_SOURCE="/home/booster/Workspace/booster_native_sdk_lab/tools/lui_ssl_payload_logger.c"
LIB_OUTPUT="/home/booster/Workspace/booster_native_sdk_lab/build-robot/liblui_ssl_payload_logger.so"
PAYLOAD_PATH="/tmp/booster_lui_ssl_payload.bin"
INDEX_PATH="/tmp/booster_lui_ssl_payload_index.tsv"

mkdir -p "$(dirname "$LIB_OUTPUT")"
gcc -shared -fPIC -O2 -Wall -Wextra -o "$LIB_OUTPUT" "$LIB_SOURCE" -ldl

if [ -f "$DROPIN_PATH" ] && [ ! -f "$BACKUP_PATH" ]; then
  cp "$DROPIN_PATH" "$BACKUP_PATH"
fi

mkdir -p "$DROPIN_DIR"
cat > "$DROPIN_PATH" <<EOF
[Service]
Environment=LD_PRELOAD=${LIB_OUTPUT}
Environment=BOOSTER_LUI_SSL_LOG_INDEX=${INDEX_PATH}
Environment=BOOSTER_LUI_SSL_LOG_PAYLOAD=${PAYLOAD_PATH}
EOF

: > "$PAYLOAD_PATH"
: > "$INDEX_PATH"
chown booster:booster "$PAYLOAD_PATH" "$INDEX_PATH"
chmod 664 "$PAYLOAD_PATH" "$INDEX_PATH"

systemctl daemon-reload
systemctl restart "$SERVICE_NAME"
systemctl is-active --quiet "$SERVICE_NAME"

echo "installed booster_lui SSL payload capture"
echo "library: $LIB_OUTPUT"
echo "index: $INDEX_PATH"
echo "payload: $PAYLOAD_PATH"
