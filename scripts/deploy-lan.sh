#!/bin/sh
set -eu

usage() {
  cat <<'EOF'
Usage:
  scripts/deploy-lan.sh <pi-host-or-ip>

Environment:
  ARDOR_PI_HOST          Host/IP when no positional host is passed.
  ARDOR_SSH_USER         SSH user on the pedal. Default: root
  ARDOR_SSH_OPTS         Extra options passed to ssh/scp.
  ARDOR_SKIP_BUILD=1     Upload the existing ./ardor-pedal binary.

Docker build defaults:
  ARDOR_BUILD_MODE       docker or native. Default: docker
  ARDOR_BUILDROOT_VOLUME Docker volume with Buildroot. Default: buildroot_vol
  ARDOR_DOCKER_IMAGE     Build container image. Default: ubuntu:24.04

Native build mode:
  ARDOR_BUILDROOT        Path to a local Buildroot checkout.
  BR2_EXTERNAL           Buildroot external tree. Default: repo/buildroot/external
EOF
}

die() {
  echo "deploy-lan: $*" >&2
  exit 1
}

case "${1:-}" in
  -h|--help)
    usage
    exit 0
    ;;
esac

host="${1:-${ARDOR_PI_HOST:-}}"
[ -n "$host" ] || {
  usage >&2
  exit 2
}
[ "$#" -le 1 ] || die "expected one host/IP argument"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

ssh_user="${ARDOR_SSH_USER:-root}"
ssh_opts="${ARDOR_SSH_OPTS:-}"
ssh_target="$ssh_user@$host"

local_bin="${ARDOR_LOCAL_BIN:-$repo_dir/ardor-pedal}"
remote_tmp="${ARDOR_REMOTE_TMP:-/tmp/ardor-pedal.new}"
target_bin="${ARDOR_TARGET_BIN:-/usr/bin/ardor-pedal}"
service="${ARDOR_SERVICE:-/etc/init.d/S99ardor-pedal}"

build_with_docker() {
  command -v docker >/dev/null 2>&1 || die "docker is required for ARDOR_BUILD_MODE=docker"

  volume="${ARDOR_BUILDROOT_VOLUME:-buildroot_vol}"
  image="${ARDOR_DOCKER_IMAGE:-ubuntu:24.04}"

  docker run --rm \
    -v "$volume:/buildroot" \
    -v "$repo_dir:/ardor" \
    -w /buildroot \
    -e FORCE_UNSAFE_CONFIGURE=1 \
    "$image" bash -lc '
      set -eu
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq
      apt-get install -y -qq build-essential git curl wget rsync cpio unzip bc \
        python3 python3-dev file pkg-config libssl-dev libelf-dev \
        dosfstools genimage e2fsprogs mtools device-tree-compiler openssh-client > /dev/null
      make ardor-pedal-dirclean BR2_EXTERNAL=/ardor/buildroot/external
      make ardor-pedal BR2_EXTERNAL=/ardor/buildroot/external
      cp output/build/ardor-pedal-1.0/pedal-poc /ardor/ardor-pedal
    '
}

build_native() {
  buildroot="${ARDOR_BUILDROOT:-}"
  [ -n "$buildroot" ] || die "set ARDOR_BUILDROOT for ARDOR_BUILD_MODE=native"
  [ -d "$buildroot" ] || die "ARDOR_BUILDROOT does not exist: $buildroot"

  br2_external="${BR2_EXTERNAL:-$repo_dir/buildroot/external}"
  make -C "$buildroot" ardor-pedal-dirclean BR2_EXTERNAL="$br2_external"
  make -C "$buildroot" ardor-pedal BR2_EXTERNAL="$br2_external"
  cp "$buildroot/output/build/ardor-pedal-1.0/pedal-poc" "$local_bin"
}

if [ "${ARDOR_SKIP_BUILD:-0}" != "1" ]; then
  case "${ARDOR_BUILD_MODE:-docker}" in
    docker)
      build_with_docker
      ;;
    native)
      build_native
      ;;
    *)
      die "ARDOR_BUILD_MODE must be docker or native"
      ;;
  esac
else
  echo "Skipping build; uploading $local_bin"
fi

[ -x "$local_bin" ] || die "built binary is missing or not executable: $local_bin"

echo "Uploading $local_bin to $ssh_target:$remote_tmp"
# ARDOR_SSH_OPTS is intentionally split into separate ssh/scp arguments.
scp $ssh_opts "$local_bin" "$ssh_target:$remote_tmp"

echo "Installing and restarting ardor-pedal on $ssh_target"
# ARDOR_SSH_OPTS is intentionally split into separate ssh/scp arguments.
ssh $ssh_opts "$ssh_target" 'sh -s' "$remote_tmp" "$target_bin" "$service" <<'REMOTE'
set -eu

remote_tmp=$1
target_bin=$2
service=$3
remounted=0

cleanup() {
  if [ "$remounted" = "1" ]; then
    mount -o remount,ro / 2>/dev/null || true
  fi
}
trap cleanup EXIT

"$service" stop || true
sleep 1

if mount -o remount,rw / 2>/dev/null; then
  remounted=1
fi

cp "$remote_tmp" "$target_bin"
chmod 755 "$target_bin"
sync

if [ "$remounted" = "1" ]; then
  mount -o remount,ro / 2>/dev/null || true
  remounted=0
fi

"$service" restart
rm -f "$remote_tmp"
REMOTE

echo "Done. App restarted on $ssh_target."
