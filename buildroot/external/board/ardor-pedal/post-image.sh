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

# Build the data.ext4 image seeded with default presets
mkfs.ext4 -L "ardor-data" -d "${DATA_SEED}" \
    "${BINARIES}/data.ext4" 268435456  # 256M

# Compile the controls overlay
dtc -@ -I dts -O dtb -o "${BINARIES}/ardor-controls.dtbo" \
    "${BOARD_DIR}/ardor-controls.dts" || true

# Run genimage
rm -rf "${GENIMAGE_TMP}"
mkdir -p "${GENIMAGE_TMP}"

genimage \
    --rootpath "${TARGET_DIR}" \
    --tmppath "${GENIMAGE_TMP}" \
    --inputpath "${BINARIES}" \
    --outputpath "${BINARIES}" \
    --config "${GENIMAGE_CFG}"
