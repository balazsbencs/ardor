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
  ARDOR_VERBOSE=1        Trace local and Docker build commands.
  ARDOR_SKIP_BUILD=1     Upload existing ./ardor-pedal and ./ardor-managerd.
  ARDOR_SKIP_WEB_BUILD=1 Reuse the embedded manager bundle already in managerd.
  ARDOR_PEDAL_DIRCLEAN=1 Force a clean Buildroot pedal-package rebuild.
  ARDOR_LOCAL_AUTH       on, off, or preserve. Default: on
  ARDOR_SERVICE_LOCAL    Local pedal supervisor script. Defaults to the
                         Buildroot package's S99ardor-pedal.
  ARDOR_CA_BUNDLE_LOCAL  PEM bundle uploaded for manager HTTPS trust. Default:
                         /etc/ssl/certs/ca-certificates.crt

Docker build defaults:
  ARDOR_BUILD_MODE       docker or native. Default: docker
  ARDOR_BUILDROOT_VOLUME Docker volume initialized by build-image.sh.
                         Default comes from buildroot/buildroot-version.env.
  ARDOR_DOCKER_IMAGE     Cached local builder image. Default:
                         ardor-builder:ubuntu-24.04. Built automatically if missing.
  ARDOR_APT_MIRROR       Ubuntu mirror used only when building the cached image.
                         Default:
                         http://archive.ubuntu.com/ubuntu

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
pedal_service_local="${ARDOR_SERVICE_LOCAL:-$repo_dir/buildroot/external/package/ardor-pedal/S99ardor-pedal}"
pedal_service_remote_tmp="${ARDOR_SERVICE_REMOTE_TMP:-/tmp/S99ardor-pedal.new}"
managerd_service="${ARDOR_MANAGERD_SERVICE:-/etc/init.d/S98ardor-managerd}"
managerd_service_local="${ARDOR_MANAGERD_SERVICE_LOCAL:-$repo_dir/buildroot/external/package/ardor-managerd/S98ardor-managerd}"
managerd_service_remote_tmp="${ARDOR_MANAGERD_SERVICE_REMOTE_TMP:-/tmp/S98ardor-managerd.new}"
managerd_env="${ARDOR_MANAGERD_ENV:-/etc/ardor-managerd.env}"
local_auth="${ARDOR_LOCAL_AUTH:-on}"
tone3000_client_id="${TONE3000_CLIENT_ID:-}"
tone3000_base_url="${TONE3000_BASE_URL:-https://www.tone3000.com}"
ca_bundle_local="${ARDOR_CA_BUNDLE_LOCAL:-/etc/ssl/certs/ca-certificates.crt}"
ca_bundle_remote_tmp="${ARDOR_CA_BUNDLE_REMOTE_TMP:-/tmp/ca-certificates.crt.new}"
ca_bundle_target="${ARDOR_CA_BUNDLE_TARGET:-/etc/ssl/certs/ca-certificates.crt}"
verbose="${ARDOR_VERBOSE:-0}"
pedal_dirclean="${ARDOR_PEDAL_DIRCLEAN:-0}"
# The wah reads a circuit table at run time. It ships on the read-only root and
# S99ardor-pedal copies it into the data partition, so a deploy has to refresh
# the root copy — the data partition itself is only seeded when an image is built.
wah_table_local="$repo_dir/assets/wah/gcb95.wahtable"
wah_table_remote_tmp="/tmp/gcb95.wahtable.new"
wah_table_target="/usr/share/ardor-pedal/assets/wah/gcb95.wahtable"

case "$local_auth" in
  on|off|preserve) ;;
  *) die "ARDOR_LOCAL_AUTH must be on, off, or preserve" ;;
esac
case "$verbose" in
  0|1) ;;
  *) die "ARDOR_VERBOSE must be 0 or 1" ;;
esac
case "$pedal_dirclean" in
  0|1) ;;
  *) die "ARDOR_PEDAL_DIRCLEAN must be 0 or 1" ;;
esac
[ "$verbose" = "1" ] && set -x

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
  image="${ARDOR_DOCKER_IMAGE:-ardor-builder:ubuntu-24.04}"
  apt_mirror="${ARDOR_APT_MIRROR:-http://archive.ubuntu.com/ubuntu}"

  case "$apt_mirror" in
    http://*|https://*) ;;
    *) die "ARDOR_APT_MIRROR must be an http:// or https:// URL" ;;
  esac
  apt_mirror=${apt_mirror%/}

  if ! docker image inspect "$image" >/dev/null 2>&1; then
    echo "deploy-lan: building cached Docker builder image $image"
    docker build --build-arg "APT_MIRROR=$apt_mirror" -t "$image" \
      -f "$script_dir/Dockerfile.ardor-builder" "$script_dir"
  fi

  docker run --rm \
    -v "$volume:/buildroot" \
    -v "$repo_dir:/ardor" \
    -w /buildroot \
    -e FORCE_UNSAFE_CONFIGURE=1 \
    -e ARDOR_VERBOSE="$verbose" \
    -e ARDOR_PEDAL_DIRCLEAN="$pedal_dirclean" \
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
      [ "$ARDOR_VERBOSE" = "1" ] && set -x
      # The versioned source volume can survive an interrupted image build with
      # no active Buildroot configuration. Restore the checked-in Ardor config
      # before asking make for a package-specific target.
      make raspberrypi4_ardor_pedal_defconfig BR2_EXTERNAL=/ardor/buildroot/external
      if [ "${ARDOR_PEDAL_DIRCLEAN:-0}" = "1" ]; then
        make ardor-pedal-dirclean BR2_EXTERNAL=/ardor/buildroot/external
      fi
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
  if [ "$pedal_dirclean" = "1" ]; then
    make -C "$buildroot" ardor-pedal-dirclean BR2_EXTERNAL="$br2_external"
  fi
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
[ -f "$pedal_service_local" ] || die "pedal supervisor is missing: $pedal_service_local"
[ -f "$managerd_service_local" ] || die "manager daemon supervisor is missing: $managerd_service_local"
[ -f "$wah_table_local" ] || die "wah circuit table is missing: $wah_table_local"
[ -f "$ca_bundle_local" ] || die "CA certificate bundle is missing: $ca_bundle_local"

# Authenticate once, then share that authenticated connection between every
# legacy-SCP upload and the installation command. This preserves OpenSSH's
# normal password prompt without putting it in a command or environment.
ssh_control_dir=$(mktemp -d "${TMPDIR:-/tmp}/ardor-deploy-ssh.XXXXXX") ||
  die "could not create temporary SSH control directory"
ssh_control_path="$ssh_control_dir/control"
cleanup_ssh_control() {
  ssh $ssh_opts -o "ControlPath=$ssh_control_path" -O exit "$ssh_target" \
    >/dev/null 2>&1 || true
  rmdir "$ssh_control_dir" 2>/dev/null || true
}
trap cleanup_ssh_control EXIT
trap 'exit 130' HUP INT TERM

echo "Authenticating to $ssh_target (password prompt appears once, if needed)"
# ARDOR_SSH_OPTS is intentionally split into separate ssh/scp arguments.
# shellcheck disable=SC2086
ssh $ssh_opts -MNf \
  -o ControlMaster=yes \
  -o ControlPersist=5m \
  -o "ControlPath=$ssh_control_path" \
  "$ssh_target"

echo "Uploading pedal, supervisors, and manager daemon to $ssh_target"
# OpenSSH 9+ clients use SFTP for scp by default. The pedal image does not
# expose the SFTP subsystem, so force the compatible legacy SCP protocol.
# ARDOR_SSH_OPTS is intentionally split into separate ssh/scp arguments.
# shellcheck disable=SC2086
scp -O $ssh_opts -o "ControlPath=$ssh_control_path" "$pedal_bin" "$ssh_target:$pedal_remote_tmp"
# shellcheck disable=SC2086
scp -O $ssh_opts -o "ControlPath=$ssh_control_path" "$managerd_bin" "$ssh_target:$managerd_remote_tmp"
# shellcheck disable=SC2086
scp -O $ssh_opts -o "ControlPath=$ssh_control_path" "$pedal_service_local" "$ssh_target:$pedal_service_remote_tmp"
# shellcheck disable=SC2086
scp -O $ssh_opts -o "ControlPath=$ssh_control_path" "$managerd_service_local" "$ssh_target:$managerd_service_remote_tmp"
# shellcheck disable=SC2086
scp -O $ssh_opts -o "ControlPath=$ssh_control_path" "$wah_table_local" "$ssh_target:$wah_table_remote_tmp"
# shellcheck disable=SC2086
scp -O $ssh_opts -o "ControlPath=$ssh_control_path" "$ca_bundle_local" "$ssh_target:$ca_bundle_remote_tmp"

echo "Installing and restarting Ardor services on $ssh_target"
# ARDOR_SSH_OPTS is intentionally split into separate ssh/scp arguments.
# shellcheck disable=SC2086
ssh $ssh_opts -o "ControlPath=$ssh_control_path" "$ssh_target" 'sh -s' \
  "$pedal_remote_tmp" "$pedal_target" "$pedal_service" \
  "$managerd_remote_tmp" "$managerd_target" "$managerd_service" \
  "$managerd_env" "$local_auth" "$pedal_service_remote_tmp" \
  "$wah_table_remote_tmp" "$wah_table_target" "$managerd_service_remote_tmp" \
  "$tone3000_client_id" "$tone3000_base_url" "$ca_bundle_remote_tmp" "$ca_bundle_target" <<'REMOTE'
set -eu

pedal_remote_tmp=$1
pedal_target=$2
pedal_service=$3
managerd_remote_tmp=$4
managerd_target=$5
managerd_service=$6
managerd_env=$7
local_auth=$8
pedal_service_remote_tmp=$9
shift 9
wah_table_remote_tmp=$1
wah_table_target=$2
managerd_service_remote_tmp=$3
tone3000_client_id=$4
tone3000_base_url=$5
ca_bundle_remote_tmp=$6
ca_bundle_target=$7
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
cp "$pedal_service_remote_tmp" "$pedal_service.new"
cp "$managerd_service_remote_tmp" "$managerd_service.new"
chmod 755 "$pedal_target.new" "$managerd_target.new" "$pedal_service.new" "$managerd_service.new"
mv "$pedal_target.new" "$pedal_target"
mv "$managerd_target.new" "$managerd_target"
mv "$pedal_service.new" "$pedal_service"
mv "$managerd_service.new" "$managerd_service"

mkdir -p "$(dirname "$wah_table_target")"
cp "$wah_table_remote_tmp" "$wah_table_target.new"
chmod 644 "$wah_table_target.new"
mv "$wah_table_target.new" "$wah_table_target"

mkdir -p "$(dirname "$ca_bundle_target")"
cp "$ca_bundle_remote_tmp" "$ca_bundle_target.new"
chmod 644 "$ca_bundle_target.new"
mv "$ca_bundle_target.new" "$ca_bundle_target"

if [ "$local_auth" != "preserve" ]; then
  env_tmp="$managerd_env.new"
  if [ -f "$managerd_env" ]; then
    sed '/^ARDOR_API_AUTH=/d; /^ARDOR_API_TOKEN=/d; /^TONE3000_CLIENT_ID=/d; /^TONE3000_BASE_URL=/d' "$managerd_env" > "$env_tmp"
  else
    : > "$env_tmp"
  fi
  echo "ARDOR_API_AUTH=$local_auth" >> "$env_tmp"
  if [ -n "$tone3000_client_id" ]; then
    echo "TONE3000_CLIENT_ID=$tone3000_client_id" >> "$env_tmp"
    echo "TONE3000_BASE_URL=$tone3000_base_url" >> "$env_tmp"
  fi
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
rm -f "$pedal_remote_tmp" "$managerd_remote_tmp" "$pedal_service_remote_tmp" \
  "$managerd_service_remote_tmp" "$wah_table_remote_tmp"
rm -f "$ca_bundle_remote_tmp"
REMOTE

echo "Done. Pedal and manager daemon restarted on $ssh_target."
