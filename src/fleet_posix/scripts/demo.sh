#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
SEARCH_DIR="${SCRIPT_DIR}"
OVERLAY_SETUP=""
while [[ "${SEARCH_DIR}" != / ]]; do
  if [[ -f "${SEARCH_DIR}/install/setup.bash" ]]; then
    OVERLAY_SETUP="${SEARCH_DIR}/install/setup.bash"
    break
  fi
  if [[ -f "${SEARCH_DIR}/.colcon_install_layout" && -f "${SEARCH_DIR}/setup.bash" ]]; then
    OVERLAY_SETUP="${SEARCH_DIR}/setup.bash"
    break
  fi
  SEARCH_DIR="$(dirname "${SEARCH_DIR}")"
done

if [[ -z "${OVERLAY_SETUP}" ]]; then
  echo "Workspace nao compilado. Execute colcon build na raiz antes da demonstracao." >&2
  exit 1
fi

# Mesmo contrato do Makefile: ROS_SETUP/ROS_DISTRO configuraveis. Se o ROS ja foi
# carregado no terminal (AMENT_PREFIX_PATH definido), nao exige /opt/ros.
ROS_DISTRO="${ROS_DISTRO:-jazzy}"
ROS_SETUP="${ROS_SETUP:-/opt/ros/${ROS_DISTRO}/setup.bash}"
if [[ -f "${ROS_SETUP}" ]]; then
  source "${ROS_SETUP}"
elif [[ -z "${AMENT_PREFIX_PATH:-}" ]]; then
  echo "ROS nao encontrado em ${ROS_SETUP}. Defina ROS_SETUP ou carregue o ROS antes." >&2
  exit 1
fi
source "${OVERLAY_SETUP}"
set -u
exec ros2 run fleet_posix fleet_supervisor --demo "$@"
