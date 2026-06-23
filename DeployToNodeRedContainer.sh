#!/usr/bin/env bash
set -euo pipefail

# Run from Windows Git Bash/WSL/macOS/Linux.
# This script executes the CentOS deploy script remotely over SSH.

REMOTE_HOST="VMCentos00.ourhome.local"
REMOTE_USER="clyde@ourhome.local"
REMOTE_REPO="/home/clyde@ourhome.local/WaterLevelMetre"
REMOTE_BRANCH="main"
REMOTE_TARGET="nodered"

ssh -tt -l "${REMOTE_USER}" "${REMOTE_HOST}" \
    "sudo -v && cd '${REMOTE_REPO}' && bash ./scripts/deploy-centos.sh '${REMOTE_REPO}' '${REMOTE_BRANCH}' '${REMOTE_TARGET}'"