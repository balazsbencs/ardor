#!/bin/sh
set -eu

die() {
  echo "build-image-in-container: $*" >&2
  exit 1
}

require_config() {
  grep -Fqx "$1" .config || die "required Buildroot config missing: $1"
}

require_file() {
  [ -s "$1" ] || die "required build artifact missing or empty: $1"
}

require_executable() {
  [ -x "$1" ] || die "required executable missing: $1"
}

version_file=/ardor/buildroot/buildroot-version.env
[ -f "$version_file" ] || die "missing version manifest: $version_file"
# shellcheck disable=SC1090
. "$version_file"
: "${BUILDROOT_VERSION:?BUILDROOT_VERSION is required}"
: "${BUILDROOT_SOURCE_URL:?BUILDROOT_SOURCE_URL is required}"
: "${BUILDROOT_SOURCE_SHA256:?BUILDROOT_SOURCE_SHA256 is required}"
: "${ARDOR_BUILD_JOBS:?ARDOR_BUILD_JOBS is required}"
: "${ARDOR_HOST_UID:?ARDOR_HOST_UID is required}"
: "${ARDOR_HOST_GID:?ARDOR_HOST_GID is required}"
: "${ARDOR_STAGING_IMAGE:?ARDOR_STAGING_IMAGE is required}"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
  build-essential bc bzip2 ca-certificates cpio curl device-tree-compiler \
  dosfstools e2fsprogs file git genimage gzip libelf-dev libssl-dev mtools \
  openssh-client patch perl pkg-config python3 python3-dev rsync sed unzip \
  wget xz-utils > /dev/null

marker=/buildroot/.ardor-buildroot-version
if [ ! -f /buildroot/Makefile ]; then
  unexpected=$(find /buildroot -mindepth 1 -maxdepth 1 \
    ! -name lost+found -print -quit)
  [ -z "$unexpected" ] ||
    die "Buildroot volume is non-empty but has no Buildroot Makefile: $unexpected"

  archive=/tmp/buildroot.tar.xz
  rm -f "$archive"
  curl -fsSLo "$archive" "$BUILDROOT_SOURCE_URL"
  echo "$BUILDROOT_SOURCE_SHA256  $archive" | sha256sum -c -
  tar -xJf "$archive" --strip-components=1 -C /buildroot
  printf '%s\n' "$BUILDROOT_VERSION" > "$marker"
fi

[ -f "$marker" ] ||
  die "existing volume was not initialized by scripts/build-image.sh"
[ "$(cat "$marker")" = "$BUILDROOT_VERSION" ] ||
  die "volume marker $(cat "$marker") does not match $BUILDROOT_VERSION"

actual_version=$(sed -n 's/^export BR2_VERSION := //p' /buildroot/Makefile)
[ "$actual_version" = "$BUILDROOT_VERSION" ] ||
  die "Buildroot source is $actual_version, expected $BUILDROOT_VERSION"

external=/ardor/buildroot/external
generator="$external/board/ardor-pedal/setup-defconfig.sh"
"$generator" --check "$external"

make raspberrypi4_ardor_pedal_defconfig BR2_EXTERNAL="$external"

require_config 'BR2_aarch64=y'
require_config 'BR2_cortex_a72=y'
require_config 'BR2_KERNEL_HEADERS_AS_KERNEL=y'
require_config 'BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_6_12=y'
require_config 'BR2_DOWNLOAD_FORCE_CHECK_HASHES=y'
require_config 'BR2_PACKAGE_HOST_GO_TARGET_ARCH_SUPPORTS=y'
require_config 'BR2_PACKAGE_ARDOR_PEDAL=y'
require_config 'BR2_PACKAGE_ARDOR_MANAGERD=y'
require_config 'BR2_PACKAGE_OPENSSH=y'
require_config 'BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y'
require_config '# BR2_TARGET_GENERIC_REMOUNT_ROOTFS_RW is not set'
require_config 'BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="$(call github,raspberrypi,linux,256d6b4bc33527fae9967773b2a0d3b92e1bd000)/linux-256d6b4bc33527fae9967773b2a0d3b92e1bd000.tar.gz"'
require_config 'BR2_ROOTFS_POST_IMAGE_SCRIPT="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/post-image.sh"'

make ardor-managerd-dirclean BR2_EXTERNAL="$external"
make ardor-pedal-dirclean BR2_EXTERNAL="$external"
make -j"$ARDOR_BUILD_JOBS" BR2_EXTERNAL="$external"

require_executable output/target/usr/bin/ardor-pedal
require_executable output/target/usr/bin/ardor-managerd
require_executable output/target/etc/init.d/S98ardor-managerd
require_executable output/target/etc/init.d/S99ardor-pedal
require_file output/target/etc/ardor-managerd.env
require_file output/images/Image
require_file output/images/bcm2711-rpi-4-b.dtb
require_file output/images/boot.vfat
require_file output/images/rootfs.ext4
require_file output/images/data.ext4
require_file output/images/sdcard.img

file output/target/usr/bin/ardor-pedal | grep -q 'ELF 64-bit.*ARM aarch64' ||
  die "ardor-pedal is not an AArch64 ELF executable"
file output/target/usr/bin/ardor-managerd | grep -q 'ELF 64-bit.*ARM aarch64' ||
  die "ardor-managerd is not an AArch64 ELF executable"

cp output/images/sdcard.img "$ARDOR_STAGING_IMAGE"
chown "$ARDOR_HOST_UID:$ARDOR_HOST_GID" "$ARDOR_STAGING_IMAGE" 2>/dev/null || true
sha256sum "$ARDOR_STAGING_IMAGE"
