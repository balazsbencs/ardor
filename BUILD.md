# Building the Ardor Pedal Image

This builds a bootable SD-card image for a **Raspberry Pi 4B** driving the
**Raspberry Pi Touch Display 2 (5-inch)** over DSI. The image is produced with
Buildroot inside a throwaway Ubuntu container; only Docker is required on the
host (macOS or Linux).

## What gets built

`output/images/sdcard.img` — a full disk image with three partitions:

| Partition | FS | Contents |
|-----------|----|----------|
| `p1` boot | FAT32 | firmware blobs, kernel `Image`, DTB, overlays, `config.txt` |
| `p2` root | ext4 (ro) | Buildroot rootfs + `ardor-pedal` app |
| `p3` data | ext4 (rw) | presets, NAM models, IRs (`/opt/ardor-pedal`) |

Key config lives under `buildroot/external/`:

- `configs/raspberrypi4_ardor_pedal_defconfig` — the Buildroot config
- `board/ardor-pedal/config.txt` — Pi firmware config (DSI overlay, codec, etc.)
- `board/ardor-pedal/post-build.sh` — SSH host keys, removes the tty1 getty
- `board/ardor-pedal/post-image.sh` — assembles the boot partition + `sdcard.img`
- `package/ardor-pedal/` — the app package (`ardor-pedal.mk`, init script)

The app itself is built from the repo root: `apps/`, `src/`, and `lv_conf.h`.

## Prerequisites

- Docker
- A persistent named volume for Buildroot's output (so rebuilds are incremental):

```bash
docker volume create buildroot_vol
```

- Buildroot checked out **inside** that volume once (first time only):

```bash
docker run --rm -v buildroot_vol:/buildroot -w /buildroot ubuntu:24.04 bash -c "
  apt-get update -qq && apt-get install -y -qq git > /dev/null && \
  git clone --depth 1 -b 2024.02.11 https://gitlab.com/buildroot.org/buildroot.git .
"
```

## Build

```bash
docker run --rm \
  -v buildroot_vol:/buildroot \
  -v /Users/bbalazs/Documents/Ardor:/ardor \
  -w /buildroot \
  -e FORCE_UNSAFE_CONFIGURE=1 \
  ubuntu:24.04 bash -c "
    apt-get update -qq && \
    apt-get install -y -qq build-essential git curl wget rsync cpio unzip bc \
      python3 python3-dev file pkg-config libssl-dev libelf-dev \
      dosfstools genimage e2fsprogs mtools device-tree-compiler openssh-client > /dev/null && \
    make raspberrypi4_ardor_pedal_defconfig BR2_EXTERNAL=/ardor/buildroot/external && \
    make ardor-pedal-dirclean BR2_EXTERNAL=/ardor/buildroot/external && \
    make BR2_EXTERNAL=/ardor/buildroot/external && \
    cp output/images/sdcard.img /ardor/sdcard.img
  "
```

The image lands at `./sdcard.img` in the repo.

> First build takes ~30–60 min (toolchain + kernel). Later builds are minutes.

### ⚠️ The `ardor-pedal-dirclean` line is not optional

The app package uses `SITE_METHOD = local`, which **rsyncs the working tree into
the build dir only at the _extract_ step**. Because `buildroot_vol` persists
across container runs:

- A plain `make` sees the package's build stamp and **skips it** → your source
  edits (`apps/`, `src/`, `lv_conf.h`) never make it into the image.
- Even `make ardor-pedal-rebuild` rebuilds from the **stale already-synced
  copy** — it does not re-rsync.

`make ardor-pedal-dirclean` wipes the package build dir, forcing a fresh rsync +
reconfigure + rebuild. Always run it before `make` when you've changed app code
or `lv_conf.h`. (Changes to `board/` scripts and `config.txt` are picked up by a
plain `make` since they are read directly from `BR2_EXTERNAL`, not rsynced.)

## Flash (macOS)

```bash
diskutil list                             # find the SD card, e.g. /dev/disk4
diskutil unmountDisk /dev/disk4
sudo dd if=sdcard.img of=/dev/rdisk4 bs=4m status=progress   # note the 'r' in rdisk
diskutil eject /dev/disk4
```

Double-check the disk number — `dd` to the wrong device is destructive.

## First boot & access

- The pedal boots straight into the LVGL UI on the DSI panel.
- Ethernet uses DHCP. SSH is enabled:

```bash
ssh root@<pi-ip>       # password: ardor
```

- Serial console: `ttyAMA0` @ 115200.
- The rootfs is **read-only**; the data partition (`/opt/ardor-pedal`) is
  read-write for presets/models/IRs.

## Fast iteration (app code only, no reflash)

Changing only app code (`apps/`, `src/`, `lv_conf.h`)? Rebuild just the binary
and swap it over SSH instead of reflashing:

```bash
./scripts/deploy-lan.sh <pi-ip>
```

The script uses the Docker Buildroot flow above by default, runs
`ardor-pedal-dirclean` so the local source tree is re-synced, uploads only the
new `ardor-pedal` binary, remounts `/` writable for the swap, then restarts
`/etc/init.d/S99ardor-pedal`.

Useful knobs:

```bash
ARDOR_PI_HOST=<pi-ip> ./scripts/deploy-lan.sh
ARDOR_SKIP_BUILD=1 ./scripts/deploy-lan.sh <pi-ip>        # upload ./ardor-pedal
ARDOR_BUILD_MODE=native ARDOR_BUILDROOT=/path/to/buildroot ./scripts/deploy-lan.sh <pi-ip>
```

Note: this only works for changes baked into the binary. Changes to `config.txt`,
overlays, the kernel, or `post-build.sh` still require a full build + reflash.

## Hardware notes

- Panel is native **720×1280 portrait**; the UI runs rotated 270° to **1280×720
  landscape** (software rotation in the fbdev flush), because the installed
  panel is upside down.
- LVGL fbdev is configured **FULL render mode + double buffer** (`lv_conf.h`) so
  the whole frame is rotated at once — PARTIAL mode leaves horizontal seams and
  lets the console text/cursor show through.
- Touch (Goodix, `/dev/input/event*`, matched by name) reports native portrait
  coordinates. LVGL applies the selected display rotation to those coordinates,
  keeping touch aligned with the upside-down landscape display. If touch is
  mirrored after a build, adjust the calibration in `apps/pedal-poc/main.cpp`
  (the fbdev block documents which knob to flip).
