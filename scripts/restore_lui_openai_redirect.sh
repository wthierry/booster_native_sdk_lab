#!/bin/bash
set -euo pipefail

SERVICE_NAME="booster-lui.service"
FAKE_SERVICE_NAME="fake-bytedance-openai-asr.service"
FAKE_SERVICE_PATH="/etc/systemd/system/${FAKE_SERVICE_NAME}"
DROPIN_DIR="/etc/systemd/system/${SERVICE_NAME}.d"
DROPIN_PATH="${DROPIN_DIR}/override.conf"
BACKUP_PATH="${DROPIN_DIR}/override.conf.bak-openai-redirect"
HOSTS_FILE="/etc/hosts"
HOSTS_BACKUP="/etc/hosts.bak-lui-openai-redirect"
BEGIN_MARKER="# BEGIN booster_lui_openai_redirect"
END_MARKER="# END booster_lui_openai_redirect"
SERVER_SCRIPT="/home/booster/Workspace/booster_native_sdk_lab/scripts/fake_bytedance_openai_asr.py"
PID_PATH="/tmp/fake_bytedance_openai_asr.pid"

remove_hosts_block() {
  python3 - "$HOSTS_FILE" "$BEGIN_MARKER" "$END_MARKER" <<'PY'
import sys
from pathlib import Path
path = Path(sys.argv[1])
begin = sys.argv[2]
end = sys.argv[3]
lines = path.read_text().splitlines()
out = []
inside = False
for line in lines:
    if line.strip() == begin:
        inside = True
        continue
    if line.strip() == end:
        inside = False
        continue
    if not inside:
        out.append(line)
path.write_text("\n".join(out) + "\n")
PY
}

systemctl stop "$FAKE_SERVICE_NAME" >/dev/null 2>&1 || true
systemctl disable "$FAKE_SERVICE_NAME" >/dev/null 2>&1 || true
pkill -f "$SERVER_SCRIPT" || true
rm -f "$PID_PATH"
rm -f "$FAKE_SERVICE_PATH"

if [ -f "$BACKUP_PATH" ]; then
  cp "$BACKUP_PATH" "$DROPIN_PATH"
elif [ -f "$DROPIN_PATH" ]; then
  rm -f "$DROPIN_PATH"
fi

if [ -f "$HOSTS_BACKUP" ]; then
  cp "$HOSTS_BACKUP" "$HOSTS_FILE"
else
  remove_hosts_block
fi

systemctl daemon-reload
systemctl restart "$SERVICE_NAME"
systemctl is-active --quiet "$SERVICE_NAME"

echo "restored booster_lui original routing"
