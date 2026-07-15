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

# Always take config.txt from the board dir, not the (stamped, possibly stale)
# rpi-firmware copy in BINARIES — config.txt edits must reach every image.
cp "${BOARD_DIR}/config.txt" "${BOOT}/config.txt"

# Keep the GPU firmware era-matched with the validated rpi-6.18.y kernel.
# The exact blobs are cached in BINARIES and verified on every image build.
FW_TAG="1.20260521"
START4_SHA256="3484b5ddc0f655e0e562680b7e2462ec21177763cc2d884b37d31e53b800bb02"
FIXUP4_SHA256="c571fe712649b66409a4834b06af39dcef8dca6727e3abc10c6043f02028b5e5"
FW_CACHE="${BINARIES}/rpi-firmware-${FW_TAG}"
mkdir -p "${FW_CACHE}"

verify_sha256() {
    expected="$1"
    path="$2"
    actual=$(sha256sum "${path}" | awk '{print $1}')
    [ "${actual}" = "${expected}" ] || {
        echo "ERROR: checksum mismatch for ${path}" >&2
        echo "Expected: ${expected}" >&2
        echo "Actual:   ${actual}" >&2
        return 1
    }
}

fetch_firmware_blob() {
    blob="$1"
    expected="$2"
    destination="${FW_CACHE}/${blob}"
    temporary="${destination}.tmp"

    if [ -f "${destination}" ]; then
        verify_sha256 "${expected}" "${destination}" || {
            echo "Remove the invalid cached file and rebuild." >&2
            exit 1
        }
    else
        rm -f "${temporary}"
        curl -fsSL -o "${temporary}" \
            "https://github.com/raspberrypi/firmware/raw/${FW_TAG}/boot/${blob}"
        verify_sha256 "${expected}" "${temporary}"
        mv "${temporary}" "${destination}"
    fi

    cp "${destination}" "${BOOT}/${blob}"
}

fetch_firmware_blob start4.elf "${START4_SHA256}"
fetch_firmware_blob fixup4.dat "${FIXUP4_SHA256}"

# Compile the Ardor controls overlay. Missing hardware controls are an image
# failure, not an optional degradation.
dtc -@ -I dts -O dtb -o "${BOOT}/overlays/ardor-controls.dtbo" \
    "${BOARD_DIR}/ardor-controls.dts"
[ -s "${BOOT}/overlays/ardor-controls.dtbo" ] || {
    echo "ERROR: ardor-controls.dtbo was not generated." >&2
    exit 1
}

# Recompile version-sensitive overlays from the validated kernel source.
KDIR="${BUILD_DIR}/linux-custom"
OVL_SRC="${KDIR}/arch/arm/boot/dts/overlays"
for ov in vc4-kms-v3d vc4-kms-v3d-pi4 rpi-codeczero vc4-kms-dsi-ili9881-5inch; do
    src="${OVL_SRC}/${ov}-overlay.dts"
    output="${BOOT}/overlays/${ov}.dtbo"
    [ -f "${src}" ] || {
        echo "ERROR: required overlay source missing: ${src}" >&2
        echo "Run 'make linux-dirclean' before rebuilding after a kernel-source change." >&2
        exit 1
    }
    cpp -nostdinc -I "${KDIR}/include" -I "${OVL_SRC}" \
        -I "${KDIR}/arch/arm/boot/dts" -undef -D__DTS__ \
        -x assembler-with-cpp "${src}" \
        | dtc -@ -I dts -O dtb -o "${output}" -
    [ -s "${output}" ] || {
        echo "ERROR: required overlay was not generated: ${output}" >&2
        exit 1
    }
done

for required in \
    "${BOOT}/start4.elf" \
    "${BOOT}/fixup4.dat" \
    "${BOOT}/config.txt" \
    "${BOOT}/Image" \
    "${BOOT}/bcm2711-rpi-4-b.dtb" \
    "${BOOT}/overlays/ardor-controls.dtbo" \
    "${BOOT}/overlays/vc4-kms-dsi-ili9881-5inch.dtbo" \
    "${BINARIES}/rootfs.ext4" \
    "${BINARIES}/data.ext4"; do
    [ -s "${required}" ] || {
        echo "ERROR: required image input is missing or empty: ${required}" >&2
        exit 1
    }
done

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

for generated in \
    "${BINARIES}/boot.vfat" \
    "${BINARIES}/rootfs.ext4" \
    "${BINARIES}/data.ext4" \
    "${BINARIES}/sdcard.img"; do
    [ -s "${generated}" ] || {
        echo "ERROR: generated image is missing or empty: ${generated}" >&2
        exit 1
    }
done
