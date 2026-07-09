#!/bin/sh
# Run this from inside a Buildroot checkout to create the complete defconfig.
#
# Usage:
#   cd /path/to/buildroot
#   sh /path/to/ardor/buildroot/external/board/ardor-pedal/setup-defconfig.sh \
#     /path/to/ardor/buildroot/external

set -eu

EXTERNAL_DIR="$1"
DEFCONFIG_OUT="${EXTERNAL_DIR}/configs/raspberrypi4_ardor_pedal_defconfig"
UPSTREAM="configs/raspberrypi4_64_defconfig"

if [ ! -f "$UPSTREAM" ]; then
  echo "Error: $UPSTREAM not found. Run this from inside a Buildroot checkout."
  exit 1
fi

echo "Building complete defconfig from $UPSTREAM + Ardor additions..."

# Start with the upstream RPI4 64-bit base, then append Ardor overrides.
# In Buildroot defconfig format, last assignment wins, so appending is safe.
{
  cat "$UPSTREAM"
  cat << EOF

# --- Ardor additions (override upstream values where duplicated) ---
BR2_PACKAGE_ALSA_UTILS=y
BR2_PACKAGE_ALSA_UTILS_ALSACTL=y
BR2_PACKAGE_ALSA_UTILS_AMIXER=y
BR2_PACKAGE_ARDOR_PEDAL=y
BR2_PACKAGE_OPENSSH=y
# BR2_TARGET_GENERIC_REMOUNT_ROOTFS_RW is not set
BR2_ROOTFS_OVERLAY="\$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/rootfs-overlay"
BR2_PACKAGE_RPI_FIRMWARE_CONFIG_FILE="\$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/config.txt"
BR2_ROOTFS_POST_IMAGE_SCRIPT="\$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/post-image.sh"
EOF
} > "$DEFCONFIG_OUT"

echo "Written: $DEFCONFIG_OUT"
echo ""
echo "Next steps:"
echo "  make BR2_EXTERNAL=$EXTERNAL_DIR raspberrypi4_ardor_pedal_defconfig"
echo "  make"
