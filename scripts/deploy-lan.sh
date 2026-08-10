#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
version_file="$repo_dir/buildroot/buildroot-version.env"

[ -f "$version_file" ] || {
  echo "deploy-lan: missing version manifest: $version_file" >&2
  exit 1
}
# shellcheck disable=SC1090
. "$version_file"
: "${BUILDROOT_VERSION:?BUILDROOT_VERSION is required}"
: "${BUILDROOT_DOCKER_VOLUME:?BUILDROOT_DOCKER_VOLUME is required}"

usage() {
  cat <<'EOF'
Usage:
  scripts/deploy-lan.sh <pi-host-or-ip>

Environment:
  ARDOR_PI_HOST          Host/IP when no positional host is passed.
  ARDOR_SSH_USER         SSH user on the pedal. Default: root
  ARDOR_SSH_OPTS         Extra options passed to ssh/scp.
  ARDOR_SKIP_BUILD=1     Upload existing ./ardor-pedal and ./ardor-managerd.
  ARDOR_SKIP_WEB_BUILD=1 Reuse the embedded manager bundle already in managerd.
  ARDOR_LOCAL_AUTH       on, off, or preserve. Default: on

Docker build defaults:
  ARDOR_BUILD_MODE       docker or native. Default: docker
  ARDOR_BUILDROOT_VOLUME Docker volume initialized by build-image.sh.
                         Default comes from buildroot/buildroot-version.env.
  ARDOR_DOCKER_IMAGE     Build container image. Default: ubuntu:24.04

Native build mode:
  ARDOR_BUILDROOT        Path to a local Buildroot checkout.
  BR2_EXTERNAL           Buildroot external tree. Default: repo/buildroot/external

The host Go toolchain cross-compiles the pure-Go manager daemon for Linux ARM64.
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

ssh_user="${ARDOR_SSH_USER:-root}"
ssh_opts="${ARDOR_SSH_OPTS:-}"
ssh_target="$ssh_user@$host"

pedal_bin="${ARDOR_LOCAL_BIN:-$repo_dir/ardor-pedal}"
managerd_bin="${ARDOR_MANAGERD_LOCAL_BIN:-$repo_dir/ardor-managerd}"
pedal_remote_tmp="${ARDOR_REMOTE_TMP:-/tmp/ardor-pedal.new}"
managerd_remote_tmp="${ARDOR_MANAGERD_REMOTE_TMP:-/tmp/ardor-managerd.new}"
pedal_target="${ARDOR_TARGET_BIN:-/usr/bin/ardor-pedal}"
managerd_target="${ARDOR_MANAGERD_TARGET_BIN:-/usr/bin/ardor-managerd}"
pedal_service="${ARDOR_SERVICE:-/etc/init.d/S99ardor-pedal}"
managerd_service="${ARDOR_MANAGERD_SERVICE:-/etc/init.d/S98ardor-managerd}"
managerd_env="${ARDOR_MANAGERD_ENV:-/etc/ardor-managerd.env}"
local_auth="${ARDOR_LOCAL_AUTH:-on}"

case "$local_auth" in
  on|off|preserve) ;;
  *) die "ARDOR_LOCAL_AUTH must be on, off, or preserve" ;;
esac

build_manager_web() {
  [ "${ARDOR_SKIP_WEB_BUILD:-0}" != "1" ] || {
    echo "Skipping device-hosted manager web build"
    return
  }

  nvm_script="${NVM_DIR:-${HOME:?HOME is required}/.nvm}/nvm.sh"
  if [ -r "$nvm_script" ]; then
    bash -c '. "$1"; cd "$2/apps/manager"; nvm use; npm run build:device' \
      deploy-lan "$nvm_script" "$repo_dir"
    return
  fi
  command -v npm >/dev/null 2>&1 || die "npm or nvm is required to build the device-hosted manager"
  (
    cd "$repo_dir/apps/manager"
    npm run build:device
  )
}

build_managerd() {
  command -v go >/dev/null 2>&1 || die "Go 1.22 or newer is required to build ardor-managerd"
  (
    cd "$repo_dir/services/managerd"
    CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
      go build -buildvcs=false -trimpath -ldflags='-s -w' -o "$managerd_bin" ./cmd/ardor-managerd
  )
}

build_with_docker() {
  command -v docker >/dev/null 2>&1 || die "docker is required for ARDOR_BUILD_MODE=docker"

  volume="${ARDOR_BUILDROOT_VOLUME:-$BUILDROOT_DOCKER_VOLUME}"
  image="${ARDOR_DOCKER_IMAGE:-ubuntu:24.04}"

  docker run --rm \
    -v "$volume:/buildroot" \
    -v "$repo_dir:/ardor" \
    -w /buildroot \
    -e FORCE_UNSAFE_CONFIGURE=1 \
    "$image" bash -lc '
      set -eu
      . /ardor/buildroot/buildroot-version.env
      marker=/buildroot/.ardor-buildroot-version
      [ -f "$marker" ] || {
        echo "deploy-lan: run scripts/build-image.sh once to initialize the Buildroot volume" >&2
        exit 1
      }
      [ "$(cat "$marker")" = "$BUILDROOT_VERSION" ] || {
        echo "deploy-lan: Buildroot volume version does not match $BUILDROOT_VERSION" >&2
        exit 1
      }
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq
      apt-get install -y -qq build-essential git curl wget rsync cpio unzip bc \
        python3 python3-dev file pkg-config libssl-dev libelf-dev \
        dosfstools genimage e2fsprogs mtools device-tree-compiler openssh-client > /dev/null
      # The versioned source volume can survive an interrupted image build with
      # no active Buildroot configuration. Restore the checked-in Ardor config
      # before asking make for a package-specific target.
      make raspberrypi4_ardor_pedal_defconfig BR2_EXTERNAL=/ardor/buildroot/external
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
  make -C "$buildroot" raspberrypi4_ardor_pedal_defconfig BR2_EXTERNAL="$br2_external"
  make -C "$buildroot" ardor-pedal-dirclean BR2_EXTERNAL="$br2_external"
  make -C "$buildroot" ardor-pedal BR2_EXTERNAL="$br2_external"
  cp "$buildroot/output/build/ardor-pedal-1.0/pedal-poc" "$pedal_bin"
}

if [ "${ARDOR_SKIP_BUILD:-0}" != "1" ]; then
  build_manager_web
  build_managerd
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
  echo "Skipping build; uploading $pedal_bin and $managerd_bin"
fi

[ -x "$pedal_bin" ] || die "built binary is missing or not executable: $pedal_bin"
[ -x "$managerd_bin" ] || die "built binary is missing or not executable: $managerd_bin"

echo "Uploading pedal and manager daemon to $ssh_target"
# OpenSSH 9+ clients use SFTP for scp by default. The pedal image does not
# expose the SFTP subsystem, so force the compatible legacy SCP protocol.
# ARDOR_SSH_OPTS is intentionally split into separate ssh/scp arguments.
scp -O $ssh_opts "$pedal_bin" "$ssh_target:$pedal_remote_tmp"
scp -O $ssh_opts "$managerd_bin" "$ssh_target:$managerd_remote_tmp"

echo "Installing and restarting Ardor services on $ssh_target"
# ARDOR_SSH_OPTS is intentionally split into separate ssh/scp arguments.
ssh $ssh_opts "$ssh_target" 'sh -s' \
  "$pedal_remote_tmp" "$pedal_target" "$pedal_service" \
  "$managerd_remote_tmp" "$managerd_target" "$managerd_service" \
  "$managerd_env" "$local_auth" <<'REMOTE'
set -eu

pedal_remote_tmp=$1
pedal_target=$2
pedal_service=$3
managerd_remote_tmp=$4
managerd_target=$5
managerd_service=$6
managerd_env=$7
local_auth=$8
remounted=0

cleanup() {
  if [ "$remounted" = "1" ]; then
    mount -o remount,ro / 2>/dev/null || true
  fi
}
trap cleanup EXIT

"$managerd_service" stop || true
"$pedal_service" stop || true
sleep 1

if mount -o remount,rw / 2>/dev/null; then
  remounted=1
fi

cp "$pedal_remote_tmp" "$pedal_target.new"
cp "$managerd_remote_tmp" "$managerd_target.new"
chmod 755 "$pedal_target.new" "$managerd_target.new"
mv "$pedal_target.new" "$pedal_target"
mv "$managerd_target.new" "$managerd_target"

if [ "$local_auth" != "preserve" ]; then
  env_tmp="$managerd_env.new"
  if [ -f "$managerd_env" ]; then
    sed '/^ARDOR_API_AUTH=/d; /^ARDOR_API_TOKEN=/d' "$managerd_env" > "$env_tmp"
  else
    : > "$env_tmp"
  fi
  echo "ARDOR_API_AUTH=$local_auth" >> "$env_tmp"
  chmod 644 "$env_tmp"
  mv "$env_tmp" "$managerd_env"
fi
sync

if [ "$remounted" = "1" ]; then
  mount -o remount,ro / 2>/dev/null || true
  remounted=0
fi

"$managerd_service" restart
"$pedal_service" restart
rm -f "$pedal_remote_tmp" "$managerd_remote_tmp"
REMOTE

echo "Done. Pedal and manager daemon restarted on $ssh_target."
