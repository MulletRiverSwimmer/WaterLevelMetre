#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="${1:-$HOME/WaterLevelMetre}"
BRANCH="${2:-main}"
TARGET_NAME="${3:-nodered}"

restart_target() {
	local target_name="$1"

	if sudo systemctl list-unit-files --type=service --no-legend 2>/dev/null | awk '{print $1}' | grep -Fxq "${target_name}.service"; then
		echo "[deploy] restarting systemd service: ${target_name}"
		sudo systemctl restart "${target_name}"
		sudo systemctl --no-pager --full status "${target_name}" | head -n 20
		return 0
	fi

	if command -v docker >/dev/null 2>&1 && sudo docker ps -a --format '{{.Names}}' | grep -Fxq "${target_name}"; then
		echo "[deploy] restarting docker container: ${target_name}"
		sudo docker restart "${target_name}"
		sudo docker ps --filter "name=^${target_name}$"
		return 0
	fi

	if command -v podman >/dev/null 2>&1 && sudo podman ps -a --format '{{.Names}}' | grep -Fxq "${target_name}"; then
		echo "[deploy] restarting podman container: ${target_name}"
		sudo podman restart "${target_name}"
		sudo podman ps --filter "name=^${target_name}$"
		return 0
	fi

	echo "[deploy] ERROR: could not find systemd service, docker container, or podman container named: ${target_name}" >&2
	echo "[deploy] Tip: pass your Node-RED container name as the third argument if it is not '${target_name}'" >&2
	return 1
}

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
echo "[deploy] target: ${TARGET_NAME}"

if [[ ! -d "${REPO_DIR}" ]]; then
	echo "[deploy] ERROR: repository directory does not exist: ${REPO_DIR}" >&2
	echo "[deploy] Tip: on CentOS this is usually /home/<user>/WaterLevelMetre" >&2
	echo "[deploy] Example: ./scripts/deploy-centos.sh /home/${USER}/WaterLevelMetre main nodered" >&2
	echo "[deploy] Example: ./scripts/deploy-centos.sh /home/${USER}@ourhome.local/WaterLevelMetre main node-red" >&2
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

restart_target "${TARGET_NAME}"

echo "[deploy] done"
