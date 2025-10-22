#!/usr/bin/env bash
set -euo pipefail

IMAGE="linkchat:latest"
NET="linkchat-net"
PREFIX="lc"

usage() {
  cat <<EOF
Usage: $0 <command>

Commands:
  up [N]     Build image, create network, start N containers (default 2)
  attach <c> Attach to container (lc1|lc2)
  down       Stop and remove containers and network
  clean      Down + remove image
  rebuild    Down then Up (recreate containers with fresh image)

Examples:
  $0 up
  $0 up 3
  $0 attach lc1
  $0 down
  $0 clean
  $0 rebuild
EOF
}

build_image() {
  echo "[+] Building image ${IMAGE}"
  docker build -f dockerfile -t "${IMAGE}" .
}

ensure_network() {
  if ! docker network inspect "${NET}" >/dev/null 2>&1; then
    echo "[+] Creating network ${NET}"
    docker network create --driver bridge "${NET}"
  else
    echo "[=] Network ${NET} already exists"
  fi
}

ensure_dirs() {
  local count="$1"
  for i in $(seq 1 "${count}"); do
    mkdir -p "inbox${i}"
  done
}

run_container() {
  local name="$1" vol="$2"
  if docker ps -a --format '{{.Names}}' | grep -qx "${name}"; then
    echo "[=] Container ${name} already exists"
  else
    echo "[+] Starting container ${name}"
    docker run -d --name "${name}" \
      --cap-add NET_RAW \
      --network "${NET}" \
      -v "${vol}:/inbox" \
      -it "${IMAGE}"
  fi
}

cmd_up() {
  local count="${1:-2}"
  if ! [[ "${count}" =~ ^[0-9]+$ ]] || [ "${count}" -lt 1 ]; then
    echo "[ERR] N must be a positive integer" >&2
    exit 1
  fi
  build_image
  ensure_network
  ensure_dirs "${count}"
  for i in $(seq 1 "${count}"); do
    run_container "${PREFIX}${i}" "$(pwd)/inbox${i}"
  done
  echo "[i] Use: $0 attach ${PREFIX}N to interact (e.g., ${PREFIX}1)"
}

cmd_attach() {
  local name="${1:-}"
  if [[ -z "${name}" ]]; then echo "Specify container name"; exit 1; fi
  docker attach "${name}"
}

stop_and_rm() {
  local name="$1"
  if docker ps -a --format '{{.Names}}' | grep -qx "${name}"; then
    echo "[+] Removing ${name}"
    docker rm -f "${name}" >/dev/null 2>&1 || true
  fi
}

rm_network() {
  if docker network inspect "${NET}" >/dev/null 2>&1; then
    echo "[+] Removing network ${NET}"
    docker network rm "${NET}" >/dev/null 2>&1 || true
  fi
}

rm_image() {
  if docker images --format '{{.Repository}}:{{.Tag}}' | grep -qx "${IMAGE}"; then
    echo "[+] Removing image ${IMAGE}"
    docker rmi "${IMAGE}" >/dev/null 2>&1 || true
  fi
}

cmd_down() {
  # remove all containers matching prefix lc[0-9]+
  local names
  names=$(docker ps -a --format '{{.Names}}' | grep -E "^${PREFIX}[0-9]+$" || true)
  if [ -n "${names}" ]; then
    for n in ${names}; do
      stop_and_rm "${n}"
    done
  fi
  rm_network
}

cmd_clean() {
  cmd_down
  rm_image
}

cmd_rebuild() {
  cmd_down
  cmd_up
}

main() {
  local cmd="${1:-}"
  case "${cmd}" in
    up) shift; cmd_up "$@" ;;
    attach) shift; cmd_attach "$@" ;;
    down) shift; cmd_down "$@" ;;
    clean) shift; cmd_clean "$@" ;;
    rebuild) shift; cmd_rebuild "$@" ;;
    *) usage; exit 1 ;;
  esac
}

main "$@"
