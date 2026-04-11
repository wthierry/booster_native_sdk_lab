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
TARGET_HOST="openspeech.bytedance.com"
LISTEN_HOST="127.0.0.1"
LISTEN_PORT="443"
ROOT_DIR="/home/booster/Workspace/booster_native_sdk_lab"
LIB_SOURCE="${ROOT_DIR}/tools/lui_ssl_payload_logger.c"
LIB_OUTPUT="${ROOT_DIR}/build-robot/liblui_ssl_payload_logger.so"
SERVER_SCRIPT="${ROOT_DIR}/scripts/fake_bytedance_openai_asr.py"
CERT_PATH="${ROOT_DIR}/build-robot/openspeech_local.crt"
KEY_PATH="${ROOT_DIR}/build-robot/openspeech_local.key"
LOG_PATH="/var/log/fake_bytedance_openai_asr.log"
PID_PATH="/tmp/fake_bytedance_openai_asr.pid"
WORK_DIR="/tmp/fake_bytedance_openai_asr"
OPENAI_ENV_PATH="${ROOT_DIR}/.env.openai_proxy"
BOOSTER_PYTHONPATH="/home/booster/.local/lib/python3.10/site-packages"

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

stop_server() {
  systemctl stop "$FAKE_SERVICE_NAME" >/dev/null 2>&1 || true
  systemctl disable "$FAKE_SERVICE_NAME" >/dev/null 2>&1 || true
  pkill -f "$SERVER_SCRIPT" || true
  pkill -f "/home/booster/Workspace/booster_native_sdk_lab/scripts/openspeech_tcp_relay.py" || true
  rm -f /run/openspeech_tcp_relay.pid || true
  rm -f "$PID_PATH" || true
}

mkdir -p "${ROOT_DIR}/build-robot" "${ROOT_DIR}/scripts" "${ROOT_DIR}/tools"

gcc -shared -fPIC -O2 -Wall -Wextra -o "$LIB_OUTPUT" "$LIB_SOURCE" -ldl

if [ ! -f "$CERT_PATH" ] || [ ! -f "$KEY_PATH" ]; then
  openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$KEY_PATH" \
    -out "$CERT_PATH" \
    -days 3650 \
    -subj "/CN=${TARGET_HOST}" \
    -addext "subjectAltName=DNS:${TARGET_HOST}" >/dev/null 2>&1
fi
chown booster:booster "$KEY_PATH" "$CERT_PATH"
chmod 600 "$KEY_PATH"
chmod 644 "$CERT_PATH"

if [ -f "$DROPIN_PATH" ] && [ ! -f "$BACKUP_PATH" ]; then
  cp "$DROPIN_PATH" "$BACKUP_PATH"
fi

mkdir -p "$DROPIN_DIR"
cat > "$DROPIN_PATH" <<EOF
[Unit]
After=${FAKE_SERVICE_NAME}
Requires=${FAKE_SERVICE_NAME}

[Service]
Environment=LD_PRELOAD=${LIB_OUTPUT}
Environment=BOOSTER_LUI_SSL_LOG_INDEX=/tmp/booster_lui_ssl_payload_index.tsv
Environment=BOOSTER_LUI_SSL_LOG_PAYLOAD=/tmp/booster_lui_ssl_payload.bin
Environment=BOOSTER_LUI_SSL_SKIP_VERIFY=1
EOF

if [ ! -f "$HOSTS_BACKUP" ]; then
  cp "$HOSTS_FILE" "$HOSTS_BACKUP"
fi
remove_hosts_block
cat >> "$HOSTS_FILE" <<EOF
$BEGIN_MARKER
$LISTEN_HOST $TARGET_HOST
$END_MARKER
EOF

stop_server
: > "$LOG_PATH"
chown booster:booster "$LOG_PATH"
chmod 664 "$LOG_PATH"
mkdir -p "$WORK_DIR"
chown -R booster:booster "$WORK_DIR"

cat > "$FAKE_SERVICE_PATH" <<EOF
[Unit]
Description=Fake ByteDance ASR server backed by OpenAI
After=network.target
Before=${SERVICE_NAME}

[Service]
Type=simple
User=booster
Group=booster
WorkingDirectory=${ROOT_DIR}
Environment=HOME=/home/booster
Environment=PYTHONPATH=${BOOSTER_PYTHONPATH}
ExecStart=/usr/bin/python3 ${SERVER_SCRIPT} --host ${LISTEN_HOST} --port ${LISTEN_PORT} --cert ${CERT_PATH} --key ${KEY_PATH} --log-path ${LOG_PATH} --pid-path ${PID_PATH} --work-dir ${WORK_DIR}
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=true
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now "$FAKE_SERVICE_NAME"
systemctl is-active --quiet "$FAKE_SERVICE_NAME"
systemctl restart "$SERVICE_NAME"
systemctl is-active --quiet "$SERVICE_NAME"

echo "installed booster_lui OpenAI redirect"
echo "fake service: $FAKE_SERVICE_NAME"
echo "openai env path: $OPENAI_ENV_PATH"
