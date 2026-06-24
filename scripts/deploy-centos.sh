#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="${1:-$HOME/WaterLevelMetre}"
BRANCH="${2:-main}"
TARGET_NAME="${3:-nodered}"
CONTAINER_PROJECT_DIR="${4:-/data/projects/$(basename "${REPO_DIR}")}"
DEPLOY_FLOW_FILE=""

cleanup_tmp_flow() {
	if [[ -n "${DEPLOY_FLOW_FILE}" && "${DEPLOY_FLOW_FILE}" != "${REPO_DIR}/flows.json" && -f "${DEPLOY_FLOW_FILE}" ]]; then
		rm -f "${DEPLOY_FLOW_FILE}"
	fi
}

trap cleanup_tmp_flow EXIT

update_host_repo() {
	local branch="$1"
	echo "[deploy] updating host repository checkout"
	git fetch origin "${branch}"
	git checkout "${branch}"
	git pull --ff-only origin "${branch}"
}

bump_dashboard_build_version() {
	local flow_file="${REPO_DIR}/flows.json"
	DEPLOY_FLOW_FILE="${flow_file}"
	if [[ ! -f "${flow_file}" ]]; then
		echo "[deploy] warning: ${flow_file} not found; skipping dashboard build bump"
		return
	fi

	local current token suffix next_suffix today next
	current=$(grep -o 'Dashboard build: [0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\.[0-9]\+' "${flow_file}" | head -n1 || true)
	if [[ -z "${current}" ]]; then
		echo "[deploy] warning: dashboard build marker not found; skipping bump"
		return
	fi

	token=${current#Dashboard build: }
	suffix=${token##*.}
	today=$(date +%F)

	if [[ ${token%.*} == "${today}" ]]; then
		next_suffix=$((suffix + 1))
	else
		next_suffix=1
	fi

	next="Dashboard build: ${today}.${next_suffix}"
	DEPLOY_FLOW_FILE=$(mktemp)
	cp "${flow_file}" "${DEPLOY_FLOW_FILE}"
	sed -i "0,/Dashboard build: [0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\.[0-9]\+/s//${next}/" "${DEPLOY_FLOW_FILE}"
	echo "[deploy] ${current} -> ${next}"
}

sync_container_flows() {
	local runtime="$1"
	local target_name="$2"
	local project_dir="$3"
	local flow_src="${DEPLOY_FLOW_FILE:-${REPO_DIR}/flows.json}"

	echo "[deploy] syncing flows.json to ${runtime} project: ${project_dir}"
	if ! sudo "${runtime}" cp "${flow_src}" "${target_name}:${project_dir}/flows.json"; then
		return 1
	fi
	if ! sudo "${runtime}" exec "${target_name}" sh -lc "chown node-red:node-red '${project_dir}/flows.json' && chmod 644 '${project_dir}/flows.json'"; then
		echo "[deploy] warning: could not fix permissions on ${project_dir}/flows.json" >&2
	fi

	# Some Node-RED setups still run directly from /data/flows.json.
	echo "[deploy] syncing flows.json to ${runtime} runtime: /data/flows.json"
	if ! sudo "${runtime}" cp "${flow_src}" "${target_name}:/data/flows.json"; then
		echo "[deploy] warning: could not sync /data/flows.json" >&2
	else
		if ! sudo "${runtime}" exec "${target_name}" sh -lc "chown node-red:node-red /data/flows.json && chmod 644 /data/flows.json"; then
			echo "[deploy] warning: could not fix permissions on /data/flows.json" >&2
		fi
	fi

	return 0
}

restart_target() {
	local target_name="$1"

	if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files --type=service --no-legend 2>/dev/null | awk '{print $1}' | grep -Fxq "${target_name}.service"; then
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

update_active_project() {
	local branch="$1"
	local target_name="$2"
	local project_dir="$3"

	update_host_repo "${branch}"
	bump_dashboard_build_version

	if command -v docker >/dev/null 2>&1 && sudo docker ps -a --format '{{.Names}}' | grep -Fxq "${target_name}"; then
		if sync_container_flows docker "${target_name}" "${project_dir}"; then
			return 0
		fi
	fi

	if command -v podman >/dev/null 2>&1 && sudo podman ps -a --format '{{.Names}}' | grep -Fxq "${target_name}"; then
		if sync_container_flows podman "${target_name}" "${project_dir}"; then
			return 0
		fi
	fi
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
echo "[deploy] container project: ${CONTAINER_PROJECT_DIR}"

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

update_active_project "${BRANCH}" "${TARGET_NAME}" "${CONTAINER_PROJECT_DIR}"

restart_target "${TARGET_NAME}"

echo "[deploy] done"
