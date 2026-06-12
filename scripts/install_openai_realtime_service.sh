#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVICE_NAME="booster-openai-realtime.service"
SERVICE_SRC="${REPO_ROOT}/${SERVICE_NAME}"
SERVICE_DST="/etc/systemd/system/${SERVICE_NAME}"
SYSTEMCTL_BIN="$(command -v systemctl)"
SUDOERS_DST="/etc/sudoers.d/booster-openai-realtime"

if [[ ! -f "${SERVICE_SRC}" ]]; then
  echo "missing service file: ${SERVICE_SRC}" >&2
  exit 1
fi

sudo cp "${SERVICE_SRC}" "${SERVICE_DST}"
sudo systemctl daemon-reload
sudo systemctl enable "${SERVICE_NAME}"

cat <<EOF | sudo tee "${SUDOERS_DST}" >/dev/null
booster ALL=(root) NOPASSWD: ${SYSTEMCTL_BIN} start ${SERVICE_NAME}, ${SYSTEMCTL_BIN} stop ${SERVICE_NAME}, ${SYSTEMCTL_BIN} restart ${SERVICE_NAME}
EOF
sudo chmod 440 "${SUDOERS_DST}"
sudo visudo -cf "${SUDOERS_DST}"

echo "installed ${SERVICE_NAME}"
echo "start manually with: sudo systemctl start ${SERVICE_NAME}"
