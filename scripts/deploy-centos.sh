#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="${1:-$HOME/WaterLevelMetre}"
BRANCH="${2:-main}"
SERVICE_NAME="${3:-nodered}"

echo "[deploy] repo: ${REPO_DIR}"
echo "[deploy] branch: ${BRANCH}"
echo "[deploy] service: ${SERVICE_NAME}"

cd "${REPO_DIR}"

git fetch origin "${BRANCH}"
git checkout "${BRANCH}"
git pull --ff-only origin "${BRANCH}"

sudo systemctl restart "${SERVICE_NAME}"
sudo systemctl --no-pager --full status "${SERVICE_NAME}" | head -n 20

echo "[deploy] done"
