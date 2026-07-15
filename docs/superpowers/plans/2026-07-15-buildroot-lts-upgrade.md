# Buildroot 2025.02 LTS Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Repository policy forbids subagents unless the user explicitly requests them. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a reproducible Buildroot `2025.02.15` Raspberry Pi 4 image natively on Apple Silicon, with both Ardor services present and the existing hardware stack preserved.

**Architecture:** A checked-in version manifest pins the verified Buildroot archive and default Docker volume. A normalized defconfig generator combines the exact upstream Raspberry Pi 4 base with one Ardor fragment, while a host wrapper validates Docker architecture and delegates source bootstrap, configuration, compilation, and artifact checks to a container-only helper. The existing post-image pipeline retains the proven Linux 6.18 and firmware versions but becomes checksum-verified and fail-fast.

**Tech Stack:** POSIX shell, Docker Desktop, Ubuntu 24.04, Buildroot 2025.02.15, Buildroot Kconfig, Raspberry Pi Linux 6.18, genimage, Go Buildroot package infrastructure, CMake Buildroot package infrastructure.

---

## Execution Constraints

- Work from `/Users/bbalazs/Documents/Ardor`.
- Do not stage or modify the user's existing changes in `ardor-pedal`,
  `presets/bank-000/preset-0.json`, `.codex/`, `.superpowers/brainstorm/`,
  `docs/audio-engine-remediation-plan.md`,
  `docs/pi-realtime-validation-runbook.md`, `sdcard.img`, or `test.html` unless a
  later user request explicitly changes scope.
- Do not delete or reuse `buildroot_vol`, `buildroot_vol_amd64`, or any existing
  rollback image.
- Do not add `--platform` to Docker commands. The wrapper must verify native
  container architecture instead.
- Do not update the custom Linux commit, Raspberry Pi firmware tag, partition
  sizes, API contract, preset schema, or auth-off development default.
- Never commit generated SD-card images, Docker volumes, or rollback images.

## File Map

**Create:**

- `buildroot/buildroot-version.env`: exact Buildroot release, archive checksum,
  source URL, and default volume name.
- `buildroot/external/configs/ardor-pedal.fragment`: all Ardor additions and
  intentional overrides to the upstream Pi 4 defconfig.
- `buildroot/external/board/ardor-pedal/patches/linux/linux.hash`: SHA-256 for
  the custom Linux archive required by the LTS base's forced-hash policy.
- `scripts/build-image.sh`: host-facing, native-architecture Docker wrapper and
  atomic image publisher.
- `scripts/build-image-in-container.sh`: Ubuntu dependency installation,
  verified source bootstrap, Buildroot configuration, compilation, and
  artifact validation.

**Modify:**

- `buildroot/external/board/ardor-pedal/setup-defconfig.sh`: add `--write` and
  `--check`, exact-version validation, Kconfig merge, and normalized output.
- `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`: regenerate
  from Buildroot 2025.02.15 plus the Ardor fragment; never edit manually.
- `buildroot/external/board/ardor-pedal/post-image.sh`: pin firmware checksums,
  fail on required overlay errors, and validate image inputs and outputs.
- `scripts/deploy-lan.sh`: source the central version manifest and use the same
  versioned volume for app-only rebuilds.
- `BUILD.md`: become the sole complete build, flash, rollback, troubleshooting,
  and validation guide.
- `README.md`: replace the duplicated Buildroot recipe with a concise overview
  and link to `BUILD.md`.
- `docs/superpowers/specs/2026-07-15-buildroot-lts-upgrade-design.md`: record
  approved status; this change is already included with this plan document.

## Task 1: Add The Exact Buildroot Version Manifest

**Files:**

- Create: `buildroot/buildroot-version.env`

- [ ] **Step 1: Run the manifest contract before creating the file**

Run:

```sh
test -f buildroot/buildroot-version.env &&
  sh -c '. buildroot/buildroot-version.env
    test "$BUILDROOT_VERSION" = 2025.02.15
    test "$BUILDROOT_SOURCE_SHA256" = 98d0db6ed648b542821629659ee9cd38918bbb9b90a987e1c55517214a0df99e
    test "$BUILDROOT_DOCKER_VOLUME" = buildroot_2025_02_15'
```

Expected: FAIL because `buildroot/buildroot-version.env` does not exist.

- [ ] **Step 2: Create the version manifest**

Create `buildroot/buildroot-version.env` with exactly:

```sh
BUILDROOT_VERSION=2025.02.15
BUILDROOT_SOURCE_URL=https://buildroot.org/downloads/buildroot-2025.02.15.tar.xz
BUILDROOT_SOURCE_SHA256=98d0db6ed648b542821629659ee9cd38918bbb9b90a987e1c55517214a0df99e
BUILDROOT_DOCKER_VOLUME=buildroot_2025_02_15
```

- [ ] **Step 3: Validate syntax and exact values**

Run:

```sh
sh -n buildroot/buildroot-version.env
sh -c '. buildroot/buildroot-version.env
  test "$BUILDROOT_VERSION" = 2025.02.15
  test "$BUILDROOT_SOURCE_URL" = https://buildroot.org/downloads/buildroot-2025.02.15.tar.xz
  test "$BUILDROOT_SOURCE_SHA256" = 98d0db6ed648b542821629659ee9cd38918bbb9b90a987e1c55517214a0df99e
  test "$BUILDROOT_DOCKER_VOLUME" = buildroot_2025_02_15'
```

Expected: both commands exit 0 with no output.

- [ ] **Step 4: Commit the version pin**

```sh
git add buildroot/buildroot-version.env
git commit -m "build: pin Buildroot 2025.02.15"
```

Expected: the commit contains only `buildroot/buildroot-version.env`.

## Task 2: Make Ardor Configuration Generation Deterministic

**Files:**

- Create: `buildroot/external/configs/ardor-pedal.fragment`
- Create: `buildroot/external/board/ardor-pedal/patches/linux/linux.hash`
- Modify: `buildroot/external/board/ardor-pedal/setup-defconfig.sh:1-46`
- Modify, generated: `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`

- [ ] **Step 1: Run the generator contract before implementation**

Run:

```sh
test -f buildroot/external/configs/ardor-pedal.fragment &&
  rg -q '^BR2_PACKAGE_ARDOR_MANAGERD=y$' buildroot/external/configs/ardor-pedal.fragment &&
  rg -q -- '--check' buildroot/external/board/ardor-pedal/setup-defconfig.sh &&
  rg -q -- '--write' buildroot/external/board/ardor-pedal/setup-defconfig.sh
```

Expected: FAIL because the fragment and generator modes do not exist.

- [ ] **Step 2: Create the Ardor-only configuration fragment**

Create `buildroot/external/configs/ardor-pedal.fragment` with exactly:

```text
# Raspberry Pi 4 target. Restated so the Ardor fragment remains explicit about
# the only supported board and target architecture.
BR2_aarch64=y
BR2_cortex_a72=y
BR2_ARM_FPU_VFPV4=y
BR2_GLOBAL_PATCH_DIR="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/patches board/raspberrypi/patches"

# Userspace and device management required for coldplugged display, touch,
# controls, and compressed kernel modules.
BR2_TOOLCHAIN_BUILDROOT_CXX=y
BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y
BR2_SYSTEM_DHCP="eth0"
BR2_PACKAGE_BUSYBOX_SHOW_OTHERS=y
BR2_PACKAGE_XZ=y
BR2_PACKAGE_KMOD=y
BR2_PACKAGE_KMOD_TOOLS=y
BR2_PACKAGE_HOST_KMOD_XZ=y

# Keep the Linux 6.18 hardware stack already validated on Touch Display 2.
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_TARBALL=y
BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="$(call github,raspberrypi,linux,256d6b4bc33527fae9967773b2a0d3b92e1bd000)/linux-256d6b4bc33527fae9967773b2a0d3b92e1bd000.tar.gz"
BR2_LINUX_KERNEL_DEFCONFIG="bcm2711"
BR2_LINUX_KERNEL_DTS_SUPPORT=y
BR2_LINUX_KERNEL_INTREE_DTS_NAME="broadcom/bcm2711-rpi-4-b"
BR2_LINUX_KERNEL_NEEDS_HOST_OPENSSL=y

# Raspberry Pi firmware and board-owned boot configuration.
BR2_PACKAGE_RPI_FIRMWARE=y
BR2_PACKAGE_RPI_FIRMWARE_VARIANT_PI4=y
BR2_PACKAGE_RPI_FIRMWARE_CONFIG_FILE="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/config.txt"

# Audio tools, device services, and developer access.
BR2_PACKAGE_ALSA_UTILS=y
BR2_PACKAGE_ALSA_UTILS_ALSACTL=y
BR2_PACKAGE_ALSA_UTILS_AMIXER=y
BR2_PACKAGE_ALSA_UTILS_APLAY=y
BR2_PACKAGE_OPENSSH=y
BR2_PACKAGE_ARDOR_PEDAL=y
BR2_PACKAGE_ARDOR_MANAGERD=y
BR2_TARGET_GENERIC_ROOT_PASSWD="ardor"

# Read-only root with the writable data partition mounted by the overlay.
# BR2_TARGET_GENERIC_REMOUNT_ROOTFS_RW is not set
BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/rootfs-overlay"
BR2_ROOTFS_POST_BUILD_SCRIPT="board/raspberrypi4-64/post-build.sh $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/post-build.sh"
BR2_ROOTFS_POST_IMAGE_SCRIPT="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/post-image.sh"
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="256M"
# BR2_TARGET_ROOTFS_TAR is not set
BR2_PACKAGE_HOST_DOSFSTOOLS=y
BR2_PACKAGE_HOST_GENIMAGE=y
BR2_PACKAGE_HOST_MTOOLS=y
```

Buildroot selects `BR2_KERNEL_HEADERS_AS_KERNEL=y` by default when Linux is
enabled. Set `BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_6_12=y` in the fragment;
this is the highest series Buildroot 2025.02 exposes and declares the
compatibility floor for headers built from the pinned Linux 6.18 source.

- [ ] **Step 3: Add the custom Linux archive hash**

Create
`buildroot/external/board/ardor-pedal/patches/linux/linux.hash` with exactly:

```text
# Locally calculated from the pinned GitHub archive
sha256  c295269861734859d3f2f756d8981b7104c6b0fe14614ee81981540608173142  linux-256d6b4bc33527fae9967773b2a0d3b92e1bd000.tar.gz
```

Add the same entry at
`buildroot/external/board/ardor-pedal/patches/linux-headers/linux-headers.hash`.
Buildroot downloads the configured kernel separately while preparing the libc
headers, and forced hash verification requires a package-local hash file for
that stage as well.

The external patch directory must precede Buildroot's Raspberry Pi patch
directory in `BR2_GLOBAL_PATCH_DIR`. Buildroot uses the first matching hash file,
so this order selects Ardor's hash for the overridden Linux archive while still
retaining upstream Raspberry Pi hashes and patches for other packages.

- [ ] **Step 4: Replace the defconfig generator**

Replace `buildroot/external/board/ardor-pedal/setup-defconfig.sh` with:

```sh
#!/bin/sh
set -eu

usage() {
  cat <<'EOF'
Usage:
  setup-defconfig.sh --write <BR2_EXTERNAL>
  setup-defconfig.sh --check <BR2_EXTERNAL>

Run from the root of the Buildroot checkout selected by
buildroot/buildroot-version.env.
EOF
}

die() {
  echo "setup-defconfig: $*" >&2
  exit 1
}

mode=${1:-}
external_arg=${2:-}

case "$mode" in
  --write|--check) ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

[ -n "$external_arg" ] || die "BR2_EXTERNAL path is required"
[ "$#" -eq 2 ] || die "expected a mode and one BR2_EXTERNAL path"

topdir=$(pwd -P)
external_dir=$(CDPATH= cd -- "$external_arg" && pwd -P)
version_file="$external_dir/../buildroot-version.env"
upstream="$topdir/configs/raspberrypi4_64_defconfig"
fragment="$external_dir/configs/ardor-pedal.fragment"
defconfig_out="$external_dir/configs/raspberrypi4_ardor_pedal_defconfig"

[ -f "$topdir/Makefile" ] || die "run this script from a Buildroot checkout"
[ -f "$version_file" ] || die "missing version manifest: $version_file"
[ -f "$upstream" ] || die "missing upstream base: $upstream"
[ -f "$fragment" ] || die "missing Ardor fragment: $fragment"

# shellcheck disable=SC1090
. "$version_file"
: "${BUILDROOT_VERSION:?BUILDROOT_VERSION is required}"

actual_version=$(sed -n 's/^export BR2_VERSION := //p' "$topdir/Makefile")
[ "$actual_version" = "$BUILDROOT_VERSION" ] ||
  die "Buildroot $actual_version does not match required $BUILDROOT_VERSION"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ardor-defconfig.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/output"

CONFIG_=BR2_ KCONFIG_CONFIG="$tmp/merged.config" \
  "$topdir/support/kconfig/merge_config.sh" -m \
  -e "$external_dir" "$upstream" "$fragment"

make -C "$topdir" O="$tmp/output" \
  BR2_EXTERNAL="$external_dir" \
  BR2_CONFIG="$tmp/merged.config" olddefconfig

make -C "$topdir" O="$tmp/output" \
  BR2_EXTERNAL="$external_dir" \
  BR2_CONFIG="$tmp/merged.config" \
  BR2_DEFCONFIG="$tmp/generated.defconfig" savedefconfig

case "$mode" in
  --write)
    cp "$tmp/generated.defconfig" "$defconfig_out"
    echo "Wrote $defconfig_out"
    ;;
  --check)
    if ! cmp -s "$tmp/generated.defconfig" "$defconfig_out"; then
      echo "setup-defconfig: generated defconfig is stale" >&2
      diff -u "$defconfig_out" "$tmp/generated.defconfig" || true
      echo "Run: $0 --write $external_dir" >&2
      exit 1
    fi
    echo "Defconfig is current: $defconfig_out"
    ;;
esac
```

- [ ] **Step 5: Validate the generator in an exact temporary LTS checkout**

Run this from the repository root. It uses a throwaway container filesystem and
writes only the generated defconfig back to the repository:

```sh
docker pull ubuntu:24.04
docker run --rm \
  -v "$PWD:/ardor" \
  -w /tmp \
  ubuntu:24.04 bash -lc '
    set -eu
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq build-essential ca-certificates curl xz-utils > /dev/null
    . /ardor/buildroot/buildroot-version.env
    curl -fsSLo buildroot.tar.xz "$BUILDROOT_SOURCE_URL"
    echo "$BUILDROOT_SOURCE_SHA256  buildroot.tar.xz" | sha256sum -c -
    mkdir buildroot
    tar -xJf buildroot.tar.xz --strip-components=1 -C buildroot
    cd buildroot
    /ardor/buildroot/external/board/ardor-pedal/setup-defconfig.sh \
      --write /ardor/buildroot/external
    /ardor/buildroot/external/board/ardor-pedal/setup-defconfig.sh \
      --check /ardor/buildroot/external
  '
```

Expected:

```text
buildroot.tar.xz: OK
Wrote /ardor/buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig
Defconfig is current: /ardor/buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig
```

The Kconfig output must not contain `warning: override: reassigning`.

- [ ] **Step 6: Verify the generated configuration contains the migration invariants**

Run:

```sh
rg -n 'BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_6_12=y|BR2_DOWNLOAD_FORCE_CHECK_HASHES=y|BR2_PACKAGE_ARDOR_PEDAL=y|BR2_PACKAGE_ARDOR_MANAGERD=y|256d6b4bc33527fae9967773b2a0d3b92e1bd000|BR2_TARGET_ROOTFS_EXT2_SIZE="256M"' \
  buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig
rg -n 'c295269861734859d3f2f756d8981b7104c6b0fe14614ee81981540608173142' \
  buildroot/external/board/ardor-pedal/patches/linux/linux.hash
```

Expected: one match for each required setting, including toolchain headers
from the preserved Linux 6.18 source commit.

- [ ] **Step 7: Commit the deterministic configuration**

```sh
git add \
  buildroot/external/configs/ardor-pedal.fragment \
  buildroot/external/board/ardor-pedal/patches/linux/linux.hash \
  buildroot/external/board/ardor-pedal/setup-defconfig.sh \
  buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig
git commit -m "build: generate Ardor defconfig from LTS base"
```

Expected: the commit contains only the fragment, generator, and generated
defconfig.

## Task 3: Harden Firmware And Image Assembly

**Files:**

- Modify: `buildroot/external/board/ardor-pedal/post-image.sh:27-99`

- [ ] **Step 1: Run the image-assembly contract before implementation**

Run:

```sh
rg -q '3484b5ddc0f655e0e562680b7e2462ec21177763cc2d884b37d31e53b800bb02' \
  buildroot/external/board/ardor-pedal/post-image.sh &&
rg -q 'c571fe712649b66409a4834b06af39dcef8dca6727e3abc10c6043f02028b5e5' \
  buildroot/external/board/ardor-pedal/post-image.sh &&
! rg -q '\|\| true' buildroot/external/board/ardor-pedal/post-image.sh
```

Expected: FAIL because checksums are absent and the controls overlay currently
ignores compilation failure.

- [ ] **Step 2: Add verified firmware download helpers**

In `post-image.sh`, replace the current firmware block at lines 44-55 with:

```sh
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
```

- [ ] **Step 3: Make all required overlay builds fatal**

Replace the controls and kernel-overlay block at current lines 57-81 with:

```sh
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
```

- [ ] **Step 4: Add required boot-input and final-image assertions**

Immediately before creating `boot.vfat`, add:

```sh
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
```

Immediately after the `genimage` command, add:

```sh
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
```

- [ ] **Step 5: Validate shell syntax and hardening markers**

Run:

```sh
sh -n buildroot/external/board/ardor-pedal/post-image.sh
rg -q '3484b5ddc0f655e0e562680b7e2462ec21177763cc2d884b37d31e53b800bb02' \
  buildroot/external/board/ardor-pedal/post-image.sh
rg -q 'c571fe712649b66409a4834b06af39dcef8dca6727e3abc10c6043f02028b5e5' \
  buildroot/external/board/ardor-pedal/post-image.sh
! rg -q '\|\| true' buildroot/external/board/ardor-pedal/post-image.sh
```

Expected: all commands exit 0. Firmware hashes match the pinned files:

```text
start4.elf  3484b5ddc0f655e0e562680b7e2462ec21177763cc2d884b37d31e53b800bb02
fixup4.dat  c571fe712649b66409a4834b06af39dcef8dca6727e3abc10c6043f02028b5e5
```

- [ ] **Step 6: Commit image hardening**

```sh
git add buildroot/external/board/ardor-pedal/post-image.sh
git commit -m "build: verify pedal image assembly"
```

## Task 4: Add The Native Docker Image Build Entrypoint

**Files:**

- Create: `scripts/build-image.sh`
- Create: `scripts/build-image-in-container.sh`

- [ ] **Step 1: Run the build-entrypoint contract before implementation**

Run:

```sh
test -x scripts/build-image.sh &&
  test -x scripts/build-image-in-container.sh &&
  sh -n scripts/build-image.sh &&
  sh -n scripts/build-image-in-container.sh &&
  ! rg -q -- '--platform' scripts/build-image.sh scripts/build-image-in-container.sh
```

Expected: FAIL because both scripts are absent.

- [ ] **Step 2: Create the host-facing wrapper**

Create `scripts/build-image.sh` with:

```sh
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

case "$jobs" in
  ''|0|*[!0-9]*) die "ARDOR_BUILD_JOBS must be a positive integer" ;;
esac

case "$output" in
  /*) ;;
  *) output="$repo_dir/$output" ;;
esac

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
output_dir=$(dirname -- "$output")
output_tmp="$output.tmp.$$"
rm -f "$staging" "$output_tmp"
trap 'rm -f "$staging" "$output_tmp"' EXIT HUP INT TERM

docker run --rm \
  -v "$volume:/buildroot" \
  -v "$repo_dir:/ardor" \
  -w /buildroot \
  -e FORCE_UNSAFE_CONFIGURE=1 \
  -e ARDOR_BUILD_JOBS="$jobs" \
  -e ARDOR_HOST_UID="$(id -u)" \
  -e ARDOR_HOST_GID="$(id -g)" \
  -e ARDOR_STAGING_IMAGE="/ardor/$(basename -- "$staging")" \
  "$image" /ardor/scripts/build-image-in-container.sh

[ -s "$staging" ] || die "container did not produce a non-empty image"
mkdir -p "$output_dir"
mv "$staging" "$output_tmp"
mv "$output_tmp" "$output"

echo "Built Ardor image with Buildroot $BUILDROOT_VERSION"
echo "Image: $output"
checksum_file "$output"
```

- [ ] **Step 3: Create the container-only build helper**

Create `scripts/build-image-in-container.sh` with:

```sh
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
```

- [ ] **Step 4: Make both scripts executable and run fast checks**

Run:

```sh
chmod 755 scripts/build-image.sh scripts/build-image-in-container.sh
sh -n scripts/build-image.sh
sh -n scripts/build-image-in-container.sh
! rg -q -- '--platform' scripts/build-image.sh scripts/build-image-in-container.sh
docker info --format '{{.Architecture}}'
docker image inspect ubuntu:24.04 --format '{{.Architecture}}'
```

Expected on the Apple Silicon development host:

```text
aarch64
arm64
```

The two architecture spellings normalize to the same value in the wrapper.

- [ ] **Step 5: Verify fail-fast argument handling without starting a build**

Run:

```sh
ARDOR_BUILD_JOBS=0 scripts/build-image.sh
```

Expected: exit 1 with:

```text
build-image: ARDOR_BUILD_JOBS must be a positive integer
```

- [ ] **Step 6: Commit the build entrypoint**

```sh
git add scripts/build-image.sh scripts/build-image-in-container.sh
git commit -m "build: add verified native image builder"
```

## Task 5: Align Fast LAN Deployment With The LTS Volume

**Files:**

- Modify: `scripts/deploy-lan.sh:4-90`

- [ ] **Step 1: Run the deployment-default contract before implementation**

Run:

```sh
rg -q 'buildroot-version.env' scripts/deploy-lan.sh &&
  ! rg -q 'Default: buildroot_vol$' scripts/deploy-lan.sh &&
  rg -q 'BUILDROOT_DOCKER_VOLUME' scripts/deploy-lan.sh
```

Expected: FAIL because `deploy-lan.sh` still hard-codes `buildroot_vol`.

- [ ] **Step 2: Load the central version manifest before processing build defaults**

Move the existing `script_dir` and `repo_dir` assignments to immediately after
`set -eu`, then add:

```sh
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
```

Remove the old duplicate `script_dir` and `repo_dir` assignments at current
lines 45-46.

- [ ] **Step 3: Replace the documented and executable Docker volume default**

In `usage()`, replace:

```text
  ARDOR_BUILDROOT_VOLUME Docker volume with Buildroot. Default: buildroot_vol
```

with:

```text
  ARDOR_BUILDROOT_VOLUME Docker volume initialized by build-image.sh.
                         Default comes from buildroot/buildroot-version.env.
```

In `build_with_docker()`, replace:

```sh
volume="${ARDOR_BUILDROOT_VOLUME:-buildroot_vol}"
```

with:

```sh
volume="${ARDOR_BUILDROOT_VOLUME:-$BUILDROOT_DOCKER_VOLUME}"
```

- [ ] **Step 4: Reject an uninitialized or wrong-version volume in the container**

At the beginning of the `bash -lc` body, after `set -eu`, add:

```sh
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
```

- [ ] **Step 5: Validate the deploy script**

Run:

```sh
sh -n scripts/deploy-lan.sh
rg -n 'buildroot-version.env|BUILDROOT_DOCKER_VOLUME|run scripts/build-image.sh once' \
  scripts/deploy-lan.sh
! rg -q 'Default: buildroot_vol$' scripts/deploy-lan.sh
```

Expected: syntax passes; all three new integration markers are present; the old
default is absent.

- [ ] **Step 6: Commit LAN workflow alignment**

```sh
git add scripts/deploy-lan.sh
git commit -m "build: align LAN deploy with LTS volume"
```

## Task 6: Make BUILD.md Canonical And Preserve The Deferred Backlog

**Files:**

- Modify: `BUILD.md:1-190`
- Modify: `README.md:336-380`
- Verify: `docs/superpowers/specs/2026-07-15-buildroot-lts-upgrade-design.md:3`

- [ ] **Step 1: Run the active-documentation contract before edits**

Run:

```sh
! rg -n '2024\.02\.11|linux/amd64|buildroot_vol_amd64' BUILD.md README.md &&
  rg -q 'scripts/build-image.sh' BUILD.md README.md &&
  rg -q '^## Deferred Work$' BUILD.md
```

Expected: FAIL because active documentation still describes the temporary
2024.02 x86 workflow and has no deferred-work section.

- [ ] **Step 2: Replace BUILD.md with the canonical workflow**

Retain the existing title, image-layout table, first-boot details, and hardware
notes, but replace the prerequisite and build recipe with this exact command:

```sh
./scripts/build-image.sh
```

Document these defaults from `buildroot/buildroot-version.env`:

```text
Buildroot release: 2025.02.15
Buildroot volume:  buildroot_2025_02_15
Container image:   ubuntu:24.04
Output image:      ./sdcard.img
Target:            AArch64 Raspberry Pi 4
```

State explicitly that the script:

1. Selects a Docker image matching Docker's native host architecture.
2. Downloads and SHA-256 verifies the exact Buildroot archive into an empty
   versioned volume.
3. Checks the generated defconfig against the upstream LTS base plus Ardor
   fragment.
4. Verifies host-Go support and both Ardor package symbols before compiling.
5. Cleans both local packages before the full build.
6. Verifies both AArch64 executables, init scripts, boot files, filesystems, and
   final disk image.
7. Publishes `sdcard.img` atomically and prints its SHA-256.

Include these exact override examples:

```sh
ARDOR_BUILD_JOBS=8 ./scripts/build-image.sh
ARDOR_OUTPUT_IMAGE=artifacts/ardor-pedal.img ./scripts/build-image.sh
ARDOR_BUILDROOT_VOLUME=buildroot_2025_02_15_test ./scripts/build-image.sh
ARDOR_DOCKER_IMAGE=ubuntu:24.04 ./scripts/build-image.sh
```

Include volume inspection and reset commands that first source the manifest:

```sh
. buildroot/buildroot-version.env
docker volume inspect "$BUILDROOT_DOCKER_VOLUME"
docker volume rm "$BUILDROOT_DOCKER_VOLUME"
```

Warn that volume removal discards the incremental build cache and must not be
used while preserving a rollback build.

Add a rollback section with these exact commands before the first LTS build:

```sh
cp -n sdcard.img sdcard.pre-buildroot-2025.02.15.img
shasum -a 256 sdcard.pre-buildroot-2025.02.15.img \
  > sdcard.pre-buildroot-2025.02.15.img.sha256
```

Keep the existing macOS flash commands, but precede `dd` with a requirement to
inspect `diskutil info` and confirm the removable SD device. Do not hard-code a
disk number in the documentation.

Expand first-boot verification to include:

```sh
ssh root@192.168.88.18
ls -l /usr/bin/ardor-pedal /usr/bin/ardor-managerd
ls -l /etc/init.d/S99ardor-pedal /etc/init.d/S98ardor-managerd
cat /etc/ardor-managerd.env
ps | grep -E 'ardor-pedal|ardor-managerd'
netstat -lnt | grep ':8080'
mount | grep -E ' on / |/opt/ardor-pedal'
aplay -l
arecord -l
```

Include Mac-side REST checks:

```sh
curl -fsS http://192.168.88.18:8080/api/device | jq .
curl -fsS http://192.168.88.18:8080/api/assets/models | jq .
curl -fsS http://192.168.88.18:8080/api/assets/irs | jq .
curl -fsS http://192.168.88.18:8080/api/presets | jq '.presets | length'
curl -fsS http://192.168.88.18:8080/api/presets/banks/0/slots/0 | jq .
curl -sS -o /tmp/ardor-invalid-slot.json -w '%{http_code}\n' \
  http://192.168.88.18:8080/api/presets/banks/100/slots/0
jq -e '.error == "slot_out_of_range"' /tmp/ardor-invalid-slot.json
```

Document expected results: auth disabled for development, 400 preset summaries,
the seeded bank 0 slot 0, HTTP `400` for bank 100, and structured error code
`slot_out_of_range`.

Keep `scripts/deploy-lan.sh` as the fast app-only path, but state that manager
daemon, package, rootfs, kernel, firmware, overlay, and partition changes require
a full image build and flash.

Add this troubleshooting table:

| Failure | Meaning | Action |
|---|---|---|
| Docker image architecture mismatch | A stale image tag points at the emulated architecture | Run `docker pull ubuntu:24.04`, then rerun the build |
| Volume marker mismatch | The named volume belongs to another Buildroot release | Use the default versioned volume or a new empty override volume |
| Defconfig is stale | Fragment/base and generated defconfig differ | Run the generator `--write` command in the exact LTS checkout and commit the result |
| Host-Go support missing | The selected host/config cannot build Go target packages | Confirm native arm64/amd64 image and Buildroot 2025.02.15 |
| Overlay source missing | Linux build state does not match the pinned source | Run `make linux-dirclean` inside the versioned volume and rebuild |
| Ardor source edit absent | A local package build directory retained stale synced source | Use the complete builder or the LAN deploy script; both run package `dirclean` |

Add this exact deferred section:

```markdown
## Deferred Work

- Evaluate a newer Raspberry Pi kernel and matching firmware as a separate
  hardware-compatibility project covering display timing, touch, controls,
  Codec Zero, realtime audio, and rollback.
- Evaluate Buildroot 2026.05 only after the 2025.02 LTS image is stable.
- Add CI checks for defconfig drift, shell syntax, package metadata, and
  eventually complete image builds.
- Replace per-run Ubuntu dependency installation with a pinned build container
  when build frequency justifies maintaining one.
- Enable and provision manager-daemon authentication before distributing images
  outside trusted development networks.
- Remove compatibility overrides only after upstream defaults pass the full
  hardware validation matrix.
- Add a manager-daemon fast-deployment workflow if daemon iteration needs to
  avoid full reflashing.
```

- [ ] **Step 3: Replace the duplicated README Buildroot recipe**

Replace `README.md` lines 336-380 with:

````markdown
## Buildroot Firmware Image

The repository contains a Buildroot external tree for a Raspberry Pi 4 pedal
image. The build pins and verifies Buildroot 2025.02.15, runs in a native Docker
container on Apple Silicon or x86_64, and preserves the validated Raspberry Pi
Linux 6.18 hardware stack.

Build the complete image from the repository root:

```sh
./scripts/build-image.sh
```

The resulting `sdcard.img` contains:

- `/usr/bin/ardor-pedal` and `/etc/init.d/S99ardor-pedal`.
- `/usr/bin/ardor-managerd` and `/etc/init.d/S98ardor-managerd`.
- `/etc/ardor-pedal.env`, `/etc/ardor-managerd.env`, and the Codec Zero mixer
  state.
- A read-only root filesystem and writable `/opt/ardor-pedal` data partition
  seeded with four presets.

See [BUILD.md](BUILD.md) for prerequisites, versioned-volume behavior, flashing,
rollback, troubleshooting, hardware checks, REST verification, and deferred
upgrade work.
````

Preserve the following `## Hardware Validation` section and all later content.

- [ ] **Step 4: Confirm the design spec records approval**

Ensure line 3 of
`docs/superpowers/specs/2026-07-15-buildroot-lts-upgrade-design.md` reads:

```markdown
**Status:** Approved
```

- [ ] **Step 5: Validate active documentation**

Run:

```sh
! rg -n '2024\.02\.11|linux/amd64|buildroot_vol_amd64' BUILD.md README.md
rg -n 'scripts/build-image.sh|2025\.02\.15|buildroot_2025_02_15|## Deferred Work' \
  BUILD.md README.md
rg -n '/usr/bin/ardor-managerd|S98ardor-managerd' BUILD.md README.md
```

Expected: no stale active guidance; both docs name the canonical script and
both device services; `BUILD.md` contains the deferred backlog.

- [ ] **Step 6: Commit canonical documentation**

```sh
git add \
  BUILD.md \
  README.md
git commit -m "docs: document Buildroot LTS workflow"
```

## Task 7: Run Host Preflight And Build The Complete Image

**Files:**

- Verify only; do not commit: `sdcard.img`
- Preserve locally; do not commit:
  `sdcard.pre-buildroot-2025.02.15.img` and its checksum file

- [ ] **Step 1: Preserve the current known-good image before overwriting output**

Run:

```sh
if [ -f sdcard.img ] && [ ! -f sdcard.pre-buildroot-2025.02.15.img ]; then
  cp sdcard.img sdcard.pre-buildroot-2025.02.15.img
  shasum -a 256 sdcard.pre-buildroot-2025.02.15.img \
    > sdcard.pre-buildroot-2025.02.15.img.sha256
fi
test -s sdcard.pre-buildroot-2025.02.15.img
shasum -a 256 -c sdcard.pre-buildroot-2025.02.15.img.sha256
```

Expected: checksum reports `OK`. If no prior `sdcard.img` exists, stop and
identify another known-good image before flashing; compilation may continue,
but device rollout may not.

- [ ] **Step 2: Run all fast preflight checks**

Run:

```sh
sh -n \
  buildroot/buildroot-version.env \
  buildroot/external/board/ardor-pedal/setup-defconfig.sh \
  buildroot/external/board/ardor-pedal/post-build.sh \
  buildroot/external/board/ardor-pedal/post-image.sh \
  scripts/build-image.sh \
  scripts/build-image-in-container.sh \
  scripts/deploy-lan.sh
! rg -n -- '--platform|2024\.02\.11|buildroot_vol_amd64' \
  BUILD.md README.md scripts buildroot
git diff --check
```

Expected: every command exits 0 and the stale-workaround search has no matches.

- [ ] **Step 3: Confirm the default Docker image is native**

Run:

```sh
docker pull ubuntu:24.04
docker info --format '{{.Architecture}}'
docker image inspect ubuntu:24.04 --format '{{.Architecture}}'
```

Expected on Apple Silicon: Docker reports `aarch64` and the image reports
`arm64`. No architecture warning should appear when running the complete build.

- [ ] **Step 4: Build the complete image**

Run:

```sh
./scripts/build-image.sh
```

Expected on first use:

- The exact archive checksum reports `OK`.
- Defconfig check reports current.
- Pre-build config checks include host-Go support and both Ardor packages.
- Both local packages are rebuilt from the current repository.
- Required overlay and image checks pass.
- Both target binaries are identified as AArch64 ELF executables.
- `sdcard.img` is published and its SHA-256 is printed.
- The command exits 0 without a Docker platform warning.

- [ ] **Step 5: Inspect the versioned volume and target outputs independently**

Run:

```sh
. buildroot/buildroot-version.env
docker run --rm \
  -v "$BUILDROOT_DOCKER_VOLUME:/buildroot" \
  -v "$PWD:/ardor" \
  -w /buildroot \
  ubuntu:24.04 sh -ec '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq file > /dev/null
    test "$(cat .ardor-buildroot-version)" = 2025.02.15
    grep -Fqx "BR2_PACKAGE_HOST_GO_TARGET_ARCH_SUPPORTS=y" .config
    grep -Fqx "BR2_PACKAGE_ARDOR_PEDAL=y" .config
    grep -Fqx "BR2_PACKAGE_ARDOR_MANAGERD=y" .config
    test -x output/target/usr/bin/ardor-pedal
    test -x output/target/usr/bin/ardor-managerd
    test -x output/target/etc/init.d/S98ardor-managerd
    test -x output/target/etc/init.d/S99ardor-pedal
    test -s output/target/etc/ardor-managerd.env
    test -s output/images/boot.vfat
    test -s output/images/rootfs.ext4
    test -s output/images/data.ext4
    test -s output/images/sdcard.img
    file output/target/usr/bin/ardor-pedal
    file output/target/usr/bin/ardor-managerd
  '
```

Expected: all tests pass and both `file` lines contain `ARM aarch64`.

- [ ] **Step 6: Prove defconfig generation is stable after the build**

Run:

```sh
. buildroot/buildroot-version.env
docker run --rm \
  -v "$BUILDROOT_DOCKER_VOLUME:/buildroot" \
  -v "$PWD:/ardor" \
  -w /buildroot \
  ubuntu:24.04 sh -ec '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq build-essential > /dev/null
    /ardor/buildroot/external/board/ardor-pedal/setup-defconfig.sh \
      --check /ardor/buildroot/external
  '
```

Expected:

```text
Defconfig is current: /ardor/buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig
```

- [ ] **Step 7: Record the new image checksum without staging image files**

Run:

```sh
shasum -a 256 sdcard.img
git status --short
```

Expected: `sdcard.img` and rollback artifacts remain untracked or ignored. Do
not pass them to `git add`.

## Task 8: Flash And Validate The Raspberry Pi Release Candidate

**Files:**

- Verification only; no repository file changes.

- [ ] **Step 1: Identify and explicitly confirm the SD-card device**

Run:

```sh
diskutil list
printf 'Verified removable SD disk (for example /dev/disk4): '
read DISK
case "$DISK" in
  /dev/disk[0-9]*) ;;
  *) echo "Refusing invalid disk path: $DISK" >&2; exit 1 ;;
esac
diskutil info "$DISK"
printf 'Type FLASH to overwrite %s: ' "$DISK"
read CONFIRM
[ "$CONFIRM" = FLASH ] || { echo 'Flash cancelled.'; exit 1; }
```

Expected: `diskutil info` identifies the intended removable SD card. Stop if it
shows an internal or unexpected disk.

- [ ] **Step 2: Flash and eject the confirmed card**

Run in the same shell where `DISK` is set:

```sh
diskutil unmountDisk "$DISK"
sudo dd if=sdcard.img of="/dev/r${DISK#/dev/}" bs=4m status=progress
sync
diskutil eject "$DISK"
```

Expected: `dd` completes without an I/O error and `diskutil` ejects the card.

- [ ] **Step 3: Verify boot, services, storage, network, and audio over SSH**

After inserting the card and allowing the Pi to boot, run:

```sh
ssh root@192.168.88.18 'sh -s' <<'REMOTE'
set -eu
test -x /usr/bin/ardor-pedal
test -x /usr/bin/ardor-managerd
test -x /etc/init.d/S99ardor-pedal
test -x /etc/init.d/S98ardor-managerd
test -s /etc/ardor-managerd.env
grep -qx 'ARDOR_API_AUTH=off' /etc/ardor-managerd.env
ps | grep '[a]rdor-pedal'
ps | grep '[a]rdor-managerd'
netstat -lnt | grep ':8080'
mount | grep ' on / ' | grep '(ro,'
mount | grep '/opt/ardor-pedal' | grep '(rw,'
test -f /opt/ardor-pedal/presets/bank-000/preset-0.json
test -f /opt/ardor-pedal/presets/bank-000/preset-1.json
test -f /opt/ardor-pedal/presets/bank-000/preset-2.json
test -f /opt/ardor-pedal/presets/bank-000/preset-3.json
aplay -l
arecord -l
REMOTE
```

Expected: password `ardor` authenticates; both services run; port 8080 listens;
root is read-only; data is writable; four seeded presets exist; Codec Zero
capture and playback devices appear.

- [ ] **Step 4: Verify non-destructive REST behavior from the Mac**

Run:

```sh
curl -fsS http://192.168.88.18:8080/api/device | jq -e '.authEnabled == false'
curl -fsS http://192.168.88.18:8080/api/assets/models | jq .
curl -fsS http://192.168.88.18:8080/api/assets/irs | jq .
curl -fsS http://192.168.88.18:8080/api/presets | jq -e '.presets | length == 400'
curl -fsS http://192.168.88.18:8080/api/presets/banks/0/slots/0 \
  | jq -e '.bank == 0 and .slot == 0'
status=$(curl -sS -o /tmp/ardor-invalid-slot.json -w '%{http_code}' \
  http://192.168.88.18:8080/api/presets/banks/100/slots/0)
test "$status" = 400
jq -e '.error == "slot_out_of_range"' /tmp/ardor-invalid-slot.json
```

Expected: all commands exit 0. These checks do not upload, delete, save, or
apply data.

- [ ] **Step 5: Complete the physical hardware release gate**

On the device, verify each item and record failures before changing software:

- The Pi reaches the LVGL UI without an emergency shell or restart loop.
- The full 1280x720 landscape interface renders without DSI smearing.
- Touch coordinates align with displayed controls.
- Encoders, footswitches, and other controls affect their expected parameters.
- Codec Zero input reaches the processing chain and output is audible.
- Preset switching works across all four seeded slots.
- Realtime audio shows no new audible underruns or instability.

Expected: every item passes. Any failure blocks adoption and triggers the
rollback procedure from `BUILD.md` using
`sdcard.pre-buildroot-2025.02.15.img`.

## Task 9: Final Regression And Scope Audit

**Files:**

- Verify all files changed by Tasks 1-6.

- [ ] **Step 1: Run repository-level software tests**

Run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
(cd services/managerd && go test ./...)
```

Expected: CMake configuration and compilation complete, existing C++ tests
pass, and all manager-daemon Go tests pass. Do not change Buildroot migration
files to compensate for an unrelated test failure.

- [ ] **Step 2: Run final build-system checks**

Run:

```sh
sh -n \
  buildroot/buildroot-version.env \
  buildroot/external/board/ardor-pedal/setup-defconfig.sh \
  buildroot/external/board/ardor-pedal/post-build.sh \
  buildroot/external/board/ardor-pedal/post-image.sh \
  scripts/build-image.sh \
  scripts/build-image-in-container.sh \
  scripts/deploy-lan.sh
git diff --check
! rg -n -- '--platform|2024\.02\.11|buildroot_vol_amd64' \
  BUILD.md README.md scripts buildroot
rg -n '2026\.05|newer Raspberry Pi kernel|production manager-daemon authentication|complete image builds' \
  BUILD.md docs/superpowers/plans/2026-07-15-buildroot-lts-upgrade.md
```

Expected: syntax and whitespace checks pass; active x86/2024.02 guidance is
absent; deferred Buildroot, kernel, authentication, and CI work remains
documented.

- [ ] **Step 3: Review only the migration changes**

Run:

```sh
git log --oneline 4b8097e..HEAD
git diff --stat 4b8097e..HEAD
git diff --name-only 4b8097e..HEAD
git status --short
```

Expected committed migration files:

```text
BUILD.md
README.md
buildroot/buildroot-version.env
buildroot/external/board/ardor-pedal/post-image.sh
buildroot/external/board/ardor-pedal/patches/linux/linux.hash
buildroot/external/board/ardor-pedal/setup-defconfig.sh
buildroot/external/configs/ardor-pedal.fragment
buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig
docs/superpowers/specs/2026-07-15-buildroot-lts-upgrade-design.md
docs/superpowers/plans/2026-07-15-buildroot-lts-upgrade.md
scripts/build-image-in-container.sh
scripts/build-image.sh
scripts/deploy-lan.sh
```

The pre-existing dirty and untracked files listed under Execution Constraints
must remain untouched and uncommitted.

- [ ] **Step 4: Report the release evidence**

The implementation handoff must state:

- Buildroot version and source SHA-256 verification result.
- Docker host and image architectures.
- Final `sdcard.img` SHA-256.
- Defconfig drift-check result.
- Presence and target architecture of both Ardor executables.
- Host test results.
- Pi boot, display, touch, controls, audio, presets, SSH, daemon, and REST
  results.
- Rollback image path and checksum.
- Any failed or unexecuted release gate.
- The deferred-work section location in `BUILD.md`.
