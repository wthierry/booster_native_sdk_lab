#!/bin/bash
set -euo pipefail

PROXY_SCRIPT="/home/booster/Workspace/booster_native_sdk_lab/scripts/openspeech_tcp_relay.py"
PID_PATH="/run/openspeech_tcp_relay.pid"
HOSTS_FILE="/etc/hosts"
BACKUP_PATH="/etc/hosts.bak-lui-openspeech"
BEGIN_MARKER="# BEGIN booster_lui_openspeech_intercept"
END_MARKER="# END booster_lui_openspeech_intercept"

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

if [ -f "$PID_PATH" ]; then
  pid="$(cat "$PID_PATH" 2>/dev/null || true)"
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid" || true
    sleep 1
  fi
  rm -f "$PID_PATH"
fi
pkill -f "$PROXY_SCRIPT" || true

if [ -f "$BACKUP_PATH" ]; then
  cp "$BACKUP_PATH" "$HOSTS_FILE"
else
  remove_hosts_block
fi

pkill -f '/opt/booster/BoosterLui/bin/booster_lui' || true
echo "restored original booster_lui openspeech routing"
