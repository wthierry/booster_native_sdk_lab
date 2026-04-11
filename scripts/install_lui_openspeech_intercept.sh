#!/bin/bash
set -euo pipefail

TARGET_HOST="openspeech.bytedance.com"
LISTEN_HOST="127.0.0.1"
LISTEN_PORT="443"
UPSTREAM_IPS=(
  "163.181.66.188"
  "163.181.66.189"
  "163.181.66.190"
  "163.181.66.191"
  "163.181.66.211"
  "163.181.66.212"
  "163.181.66.213"
  "163.181.66.214"
)
PROXY_SCRIPT="/home/booster/Workspace/booster_native_sdk_lab/scripts/openspeech_tcp_relay.py"
LOG_PATH="/var/log/openspeech_tcp_relay.log"
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

stop_proxy() {
  if [ -f "$PID_PATH" ]; then
    pid="$(cat "$PID_PATH" 2>/dev/null || true)"
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" || true
      sleep 1
    fi
    rm -f "$PID_PATH"
  fi
  pkill -f "$PROXY_SCRIPT" || true
}

restart_lui() {
  pkill -f '/opt/booster/BoosterLui/bin/booster_lui' || true
}

if [ ! -f "$BACKUP_PATH" ]; then
  cp "$HOSTS_FILE" "$BACKUP_PATH"
fi

remove_hosts_block
cat >> "$HOSTS_FILE" <<EOF
$BEGIN_MARKER
$LISTEN_HOST $TARGET_HOST
$END_MARKER
EOF

stop_proxy
: > "$LOG_PATH"
chmod 644 "$LOG_PATH"

args=(python3 "$PROXY_SCRIPT" --listen-host "$LISTEN_HOST" --listen-port "$LISTEN_PORT" --upstream-host "$TARGET_HOST" --upstream-port 443 --log-path "$LOG_PATH" --pid-path "$PID_PATH")
for ip in "${UPSTREAM_IPS[@]}"; do
  args+=(--upstream-ip "$ip")
done

nohup "${args[@]}" >/dev/null 2>&1 &
sleep 1

if [ ! -f "$PID_PATH" ]; then
  echo "proxy failed to start"
  exit 1
fi

restart_lui
echo "installed booster_lui openspeech intercept"
echo "proxy pid: $(cat "$PID_PATH")"
