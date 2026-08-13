#!/bin/sh
set -eu

die() {
  echo "build-image: $*" >&2
  exit 1
}

normalize_arch() {
  case "$1" in
    arm64|aarch64) echo arm64 ;;
    amd64|x86_64) echo amd64 ;;
    *) echo "$1" ;;
  esac
}

checksum_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1"
  else
    sha256sum "$1"
  fi
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
version_file="$repo_dir/buildroot/buildroot-version.env"

[ -f "$version_file" ] || die "missing version manifest: $version_file"
# shellcheck disable=SC1090
. "$version_file"
: "${BUILDROOT_VERSION:?BUILDROOT_VERSION is required}"
: "${BUILDROOT_SOURCE_URL:?BUILDROOT_SOURCE_URL is required}"
: "${BUILDROOT_SOURCE_SHA256:?BUILDROOT_SOURCE_SHA256 is required}"
: "${BUILDROOT_DOCKER_VOLUME:?BUILDROOT_DOCKER_VOLUME is required}"

command -v docker >/dev/null 2>&1 || die "Docker is required"

volume=${ARDOR_BUILDROOT_VOLUME:-$BUILDROOT_DOCKER_VOLUME}
image=${ARDOR_DOCKER_IMAGE:-ubuntu:24.04}
output=${ARDOR_OUTPUT_IMAGE:-$repo_dir/sdcard.img}
payload_output=${ARDOR_OUTPUT_PAYLOAD_DIR:-}
download_dir=${ARDOR_BUILDROOT_DL_DIR:-}
release_version=${ARDOR_RELEASE_VERSION:-0.0.0}
release_commit=${ARDOR_RELEASE_COMMIT:-$(git -C "$repo_dir" rev-parse HEAD 2>/dev/null || echo unknown)}
update_public_key=${ARDOR_UPDATE_PUBLIC_KEY_BASE64:-}

case "$release_version" in
  *[!0-9.]*|.*|*.|*..*|*.*.*.*) die "ARDOR_RELEASE_VERSION must be a numeric x.y.z version" ;;
  *.*.*) ;;
  *) die "ARDOR_RELEASE_VERSION must be a numeric x.y.z version" ;;
esac
case "$release_commit" in
  unknown|[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) ;;
  *) die "ARDOR_RELEASE_COMMIT must be a hexadecimal Git commit or unknown" ;;
esac

if [ -z "${ARDOR_BUILD_JOBS:-}" ]; then
  if command -v sysctl >/dev/null 2>&1 &&
      jobs=$(sysctl -n hw.ncpu 2>/dev/null); then
    :
  else
    jobs=$(getconf _NPROCESSORS_ONLN)
  fi
else
  jobs=$ARDOR_BUILD_JOBS
fi

# An optional host-side WPA configuration is mounted into the build container
# only for this invocation. It is installed in the writable data partition by
# post-image.sh and must not be committed to the repository.
wifi_config=${ARDOR_WIFI_CONFIG:-}
if [ -n "$wifi_config" ]; then
  case "$wifi_config" in
    /*) ;;
    *) wifi_config="$repo_dir/$wifi_config" ;;
  esac
  [ -f "$wifi_config" ] || die "ARDOR_WIFI_CONFIG is not a regular file: $wifi_config"
  [ -r "$wifi_config" ] || die "ARDOR_WIFI_CONFIG is not readable: $wifi_config"
fi

case "$jobs" in
  ''|0|*[!0-9]*) die "ARDOR_BUILD_JOBS must be a positive integer" ;;
esac

case "$output" in
  /*) ;;
  *) output="$repo_dir/$output" ;;
esac

if [ -n "$download_dir" ]; then
  case "$download_dir" in
    /*) ;;
    *) download_dir="$repo_dir/$download_dir" ;;
  esac
  mkdir -p "$download_dir"
fi

docker_host_raw=$(docker info --format '{{.Architecture}}') ||
  die "Docker daemon is unavailable"
docker_host_arch=$(normalize_arch "$docker_host_raw")

if ! docker image inspect "$image" >/dev/null 2>&1; then
  docker pull "$image" || die "could not pull Docker image: $image"
fi

image_arch=$(normalize_arch "$(docker image inspect "$image" --format '{{.Architecture}}')")
if [ "$image_arch" != "$docker_host_arch" ]; then
  echo "Refreshing $image for native $docker_host_arch execution..."
  docker pull "$image" || die "could not refresh Docker image: $image"
  image_arch=$(normalize_arch "$(docker image inspect "$image" --format '{{.Architecture}}')")
fi

[ "$image_arch" = "$docker_host_arch" ] ||
  die "Docker image architecture $image_arch does not match host $docker_host_arch"

docker volume create "$volume" >/dev/null

staging="$repo_dir/.ardor-sdcard.img.$$.tmp"
payload_staging="$repo_dir/.ardor-ota-payload.$$.tmp"
output_dir=$(dirname -- "$output")
output_tmp="$output.tmp.$$"
rm -f "$staging" "$output_tmp"
rm -rf "$payload_staging"
trap 'rm -f "$staging" "$output_tmp"; rm -rf "$payload_staging"' EXIT HUP INT TERM

set -- docker run --rm \
  -v "$volume:/buildroot" \
  -v "$repo_dir:/ardor" \
  -w /buildroot \
  -e FORCE_UNSAFE_CONFIGURE=1 \
  -e ARDOR_BUILD_JOBS="$jobs" \
  -e ARDOR_HOST_UID="$(id -u)" \
  -e ARDOR_HOST_GID="$(id -g)" \
  -e ARDOR_RELEASE_VERSION="$release_version" \
  -e ARDOR_RELEASE_COMMIT="$release_commit" \
  -e ARDOR_STAGING_IMAGE="/ardor/$(basename -- "$staging")"

if [ -n "$payload_output" ]; then
  case "$payload_output" in
    /*) ;;
    *) payload_output="$repo_dir/$payload_output" ;;
  esac
  [ "$payload_output" != "/" ] || die "ARDOR_OUTPUT_PAYLOAD_DIR cannot be /"
  [ "$payload_output" != "$repo_dir" ] || die "ARDOR_OUTPUT_PAYLOAD_DIR cannot be the repository root"
  [ ! -e "$payload_output" ] || die "ARDOR_OUTPUT_PAYLOAD_DIR already exists: $payload_output"
  set -- "$@" -e ARDOR_STAGING_PAYLOAD_DIR="/ardor/$(basename -- "$payload_staging")"
fi

if [ -n "$update_public_key" ]; then
  set -- "$@" -e ARDOR_UPDATE_PUBLIC_KEY_BASE64="$update_public_key"
fi

if [ -n "$download_dir" ]; then
  set -- "$@" \
    -v "$download_dir:/buildroot-dl" \
    -e ARDOR_BUILDROOT_DL_DIR=/buildroot-dl
fi

if [ -n "$wifi_config" ]; then
  set -- "$@" \
    -v "$wifi_config:/run/secrets/ardor-wifi.conf:ro" \
    -e ARDOR_WIFI_CONFIG=/run/secrets/ardor-wifi.conf
fi

"$@" "$image" /ardor/scripts/build-image-in-container.sh

[ -s "$staging" ] || die "container did not produce a non-empty image"
mkdir -p "$output_dir"
mv "$staging" "$output_tmp"
mv "$output_tmp" "$output"

if [ -n "$payload_output" ]; then
  [ -x "$payload_staging/ardor-pedal" ] || die "container did not export ardor-pedal"
  [ -x "$payload_staging/ardor-managerd" ] || die "container did not export ardor-managerd"
  [ -x "$payload_staging/ardor-updater" ] || die "container did not export ardor-updater"
  mkdir -p "$(dirname -- "$payload_output")"
  mv "$payload_staging" "$payload_output"
fi

echo "Built Ardor image with Buildroot $BUILDROOT_VERSION"
echo "Image: $output"
checksum_file "$output"
