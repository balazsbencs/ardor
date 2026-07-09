#!/bin/sh
set -eu

BOARD_DIR="$(dirname "$0")"
GENIMAGE_CFG="${BOARD_DIR}/genimage-ardor.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"
BINARIES="${BINARIES_DIR}"

# Seed the data partition with default presets
DATA_SEED="${GENIMAGE_TMP}/data-seed"
rm -rf "${DATA_SEED}"
mkdir -p "${DATA_SEED}/presets/bank-000"
mkdir -p "${DATA_SEED}/models"
mkdir -p "${DATA_SEED}/irs"

for slot in 0 1 2 3; do
    cp "${BOARD_DIR}/../../package/ardor-pedal/preset-default.json" \
       "${DATA_SEED}/presets/bank-000/preset-${slot}.json"
done
cp "${BOARD_DIR}/../../package/ardor-pedal/README-assets.txt" \
   "${DATA_SEED}/README-assets.txt"

# Build the data.ext4 image seeded with default presets (256 MiB)
truncate -s 256M "${BINARIES}/data.ext4"
mkfs.ext4 -F -L "ardor-data" -d "${DATA_SEED}" "${BINARIES}/data.ext4"

# Build a boot staging directory with the exact layout the Pi bootloader expects:
# - firmware files (start4.elf, fixup4.dat, config.txt, ...) at the root
# - overlays/ containing both the rpi-firmware overlays and our custom dtbo
# - kernel Image and DTB at the root
BOOT="${BINARIES}/boot-staging"
rm -rf "${BOOT}"
mkdir -p "${BOOT}/overlays"

find "${BINARIES}/rpi-firmware/" -maxdepth 1 -not -type d -exec cp {} "${BOOT}/" \;
cp -a "${BINARIES}/rpi-firmware/overlays/." "${BOOT}/overlays/"
cp "${BINARIES}/Image" "${BOOT}/"
cp "${BINARIES}/bcm2711-rpi-4-b.dtb" "${BOOT}/"

# Compile the controls overlay
dtc -@ -I dts -O dtb -o "${BOOT}/overlays/ardor-controls.dtbo" \
    "${BOARD_DIR}/ardor-controls.dts" || true

# Build boot.vfat from the staging directory using mtools.
# genimage's vfat 'directory' option requires genimage >=17; using mtools
# keeps us compatible with whatever version Buildroot provides.
truncate -s 64M "${BINARIES}/boot.vfat"
mkdosfs -F 32 -n "boot" "${BINARIES}/boot.vfat"
MTOOLS_SKIP_CHECK=1 mcopy -i "${BINARIES}/boot.vfat" -s "${BOOT}"/* "::"

# Run genimage
rm -rf "${GENIMAGE_TMP}"
mkdir -p "${GENIMAGE_TMP}"

genimage \
    --rootpath "${TARGET_DIR}" \
    --tmppath "${GENIMAGE_TMP}" \
    --inputpath "${BINARIES}" \
    --outputpath "${BINARIES}" \
    --config "${GENIMAGE_CFG}"
