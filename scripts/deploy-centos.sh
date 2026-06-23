#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="${1:-$HOME/WaterLevelMetre}"
BRANCH="${2:-main}"
SERVICE_NAME="${3:-nodered}"

# Some shells/users accidentally pass a domain-qualified path like:
#   /home/user@domain.local/WaterLevelMetre
# Convert that to the actual local home path:
#   /home/user/WaterLevelMetre
if [[ "${REPO_DIR}" == /home/*@*/* ]]; then
	_user_segment="${REPO_DIR#/home/}"
	_user_segment="${_user_segment%%/*}"
	_rest="${REPO_DIR#"/home/${_user_segment}/"}"
	_short_user="${_user_segment%%@*}"
	_normalized="/home/${_short_user}/${_rest}"
	if [[ -d "${_normalized}" ]]; then
		REPO_DIR="${_normalized}"
	fi
fi

# If path does not exist, try a fallback under the current user's HOME.
if [[ ! -d "${REPO_DIR}" ]]; then
	_fallback="${HOME}/$(basename "${REPO_DIR}")"
	if [[ -d "${_fallback}" ]]; then
		REPO_DIR="${_fallback}"
	fi
fi

echo "[deploy] repo: ${REPO_DIR}"
echo "[deploy] branch: ${BRANCH}"
echo "[deploy] service: ${SERVICE_NAME}"

if [[ ! -d "${REPO_DIR}" ]]; then
	echo "[deploy] ERROR: repository directory does not exist: ${REPO_DIR}" >&2
	echo "[deploy] Tip: on CentOS this is usually /home/<user>/WaterLevelMetre" >&2
	echo "[deploy] Example: ./scripts/deploy-centos.sh /home/${USER}/WaterLevelMetre main nodered" >&2
	exit 1
fi

cd "${REPO_DIR}"

if [[ ! -d .git ]]; then
	echo "[deploy] ERROR: ${REPO_DIR} is not a git repository (.git missing)" >&2
	exit 1
fi

git fetch origin "${BRANCH}"
git checkout "${BRANCH}"
git pull --ff-only origin "${BRANCH}"

sudo systemctl restart "${SERVICE_NAME}"
sudo systemctl --no-pager --full status "${SERVICE_NAME}" | head -n 20

echo "[deploy] done"
