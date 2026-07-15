# Building the Ardor Pedal Image

This produces a bootable SD-card image for a Raspberry Pi 4B with the Raspberry
Pi Touch Display 2 over DSI. The image is built inside Docker; macOS and Linux
hosts need Docker only.

The build pins Buildroot 2025.02.15 and preserves the Raspberry Pi Linux 6.18,
firmware, display, touch, Codec Zero, and data-partition stack that is already
validated on the pedal.

## What Gets Built

`sdcard.img` is a full disk image with three partitions:

| Partition | FS | Contents |
|---|---|---|
| `p1` boot | FAT32 | firmware blobs, kernel `Image`, DTB, overlays, `config.txt` |
| `p2` root | ext4 (ro) | Buildroot rootfs, `ardor-pedal`, `ardor-managerd` |
| `p3` data | ext4 (rw) | presets, NAM models, IRs at `/opt/ardor-pedal` |

The key Buildroot files are under `buildroot/external/`:

- `configs/ardor-pedal.fragment`: Ardor-specific settings and intentional
  overrides of the upstream Raspberry Pi 4 base configuration.
- `configs/raspberrypi4_ardor_pedal_defconfig`: normalized generated defconfig.
- `board/ardor-pedal/setup-defconfig.sh`: regenerates or checks the defconfig.
- `board/ardor-pedal/post-image.sh`: assembles verified boot, root, data, and SD
  images.
- `package/ardor-pedal/`: realtime pedal app package and init script.
- `package/ardor-managerd/`: Go REST daemon package and init script.

## Prerequisites

- Docker Desktop on macOS, or Docker Engine on Linux.
- An SD card and Pi 4 for device validation.
- `jq` for the Mac-side REST checks below.

Build settings are defined once in `buildroot/buildroot-version.env`:

```text
Buildroot release: 2025.02.15
Buildroot volume:  buildroot_2025_02_15
Container image:   ubuntu:24.04
Output image:      ./sdcard.img
Target:            AArch64 Raspberry Pi 4
```

The default Buildroot volume is separate from earlier 2024.02 or amd64 volumes.
Do not delete those earlier volumes until the LTS image has passed hardware
validation and a rollback image has been preserved.

## Build

From the repository root, run:

```sh
./scripts/build-image.sh
```

The script:

1. Checks that the selected Docker image matches Docker's native host
   architecture. Apple Silicon builds therefore run in an arm64 container;
   x86_64 hosts use amd64.
2. Downloads and SHA-256 verifies the exact Buildroot archive into an empty,
   versioned Docker volume.
3. Checks that the normalized Ardor defconfig matches Buildroot's 2025.02.15
   Raspberry Pi 4 base plus `ardor-pedal.fragment`.
4. Verifies Linux 6.6 toolchain headers, AArch64 target selection, host-Go
   support, and both Ardor packages before compiling.
5. Cleans both local packages so Buildroot re-syncs the current repository.
6. Verifies both AArch64 executables, init scripts, environment files, boot
   files, filesystems, and `sdcard.img`.
7. Publishes `sdcard.img` atomically and prints its SHA-256.

Useful overrides:

```sh
ARDOR_BUILD_JOBS=8 ./scripts/build-image.sh
ARDOR_OUTPUT_IMAGE=artifacts/ardor-pedal.img ./scripts/build-image.sh
ARDOR_BUILDROOT_VOLUME=buildroot_2025_02_15_test ./scripts/build-image.sh
ARDOR_DOCKER_IMAGE=ubuntu:24.04 ./scripts/build-image.sh
```

The first build downloads and compiles the toolchain and kernel. Later builds
reuse the versioned volume but still rebuild both Ardor packages from the current
working tree.

### Defconfig Maintenance

The generated defconfig must never be edited by hand. Regenerate it from an
exact Buildroot 2025.02.15 checkout:

```sh
cd /path/to/buildroot-2025.02.15
/absolute/path/to/Ardor/buildroot/external/board/ardor-pedal/setup-defconfig.sh \
  --write /absolute/path/to/Ardor/buildroot/external
```

The complete image build runs the corresponding `--check` mode and fails if the
checked-in defconfig is stale.

### Inspect Or Reset The Build Cache

```sh
. buildroot/buildroot-version.env
docker volume inspect "$BUILDROOT_DOCKER_VOLUME"
docker volume rm "$BUILDROOT_DOCKER_VOLUME"
```

Removing this volume discards the incremental cache. Do not remove it while a
release candidate is being compared with a prior image. A non-empty volume from
another Buildroot release is rejected rather than modified in place.

## Preserve A Rollback Image

Before the first LTS build overwrites `sdcard.img`, preserve the current known
good image:

```sh
cp -n sdcard.img sdcard.pre-buildroot-2025.02.15.img
shasum -a 256 sdcard.pre-buildroot-2025.02.15.img \
  > sdcard.pre-buildroot-2025.02.15.img.sha256
shasum -a 256 -c sdcard.pre-buildroot-2025.02.15.img.sha256
```

Keep the image and checksum outside Git. If the new image fails any boot,
display, touch, controls, audio, filesystem, or API check, reflash this image
before investigating further.

## Flash On macOS

First identify and inspect the removable SD card:

```sh
diskutil list
diskutil info /dev/disk4
```

Only continue after `diskutil info` identifies the intended removable SD card.
The disk number is an example; selecting an internal disk is destructive.

```sh
diskutil unmountDisk /dev/disk4
sudo dd if=sdcard.img of=/dev/rdisk4 bs=4m status=progress
sync
diskutil eject /dev/disk4
```

## First Boot And Device Checks

The pedal uses DHCP and SSH is enabled. The development image uses root password
`ardor`. The root filesystem is read-only; presets and uploaded assets are stored
on the writable `/opt/ardor-pedal` data partition.

Connect to the known development device:

```sh
ssh root@192.168.88.18
```

On the Pi, verify services, storage, and audio devices:

```sh
ls -l /usr/bin/ardor-pedal /usr/bin/ardor-managerd
ls -l /etc/init.d/S99ardor-pedal /etc/init.d/S98ardor-managerd
cat /etc/ardor-managerd.env
ps | grep -E 'ardor-pedal|ardor-managerd'
netstat -lnt | grep ':8080'
mount | grep -E ' on / |/opt/ardor-pedal'
ls -l /opt/ardor-pedal/presets/bank-000/
aplay -l
arecord -l
```

Expected development defaults:

- `S99ardor-pedal` and `S98ardor-managerd` are executable and both processes
  are running.
- Port 8080 is listening.
- `/` is mounted read-only and `/opt/ardor-pedal` is mounted read-write.
- Four seeded presets, `preset-0.json` through `preset-3.json`, exist.
- `ARDOR_API_AUTH=off` appears in `/etc/ardor-managerd.env`.
- Codec Zero capture and playback devices appear in `arecord -l` and `aplay -l`.

### REST API Smoke Checks

Run these from the Mac. They do not modify assets or presets:

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

Expected results: the device reports auth disabled, the preset list contains
400 slot summaries, bank 0 slot 0 is present, the invalid bank request returns
HTTP 400, and its JSON error code is `slot_out_of_range`.

## Fast App-Only Iteration

For changes limited to the realtime pedal application source, rebuild and deploy
only `/usr/bin/ardor-pedal`:

```sh
./scripts/deploy-lan.sh 192.168.88.18
```

This command requires a Buildroot volume initialized by `scripts/build-image.sh`.
It runs `ardor-pedal-dirclean`, uploads the rebuilt binary over legacy SCP,
temporarily remounts root read-write, and restarts `S99ardor-pedal`.

Changes to the manager daemon, Buildroot packages, init scripts, rootfs overlay,
kernel, firmware, overlays, or partitions require a complete image build and
flash.

## Troubleshooting

| Failure | Meaning | Action |
|---|---|---|
| Docker image architecture mismatch | A stale image tag points at an emulated architecture | Run `docker pull ubuntu:24.04`, then rerun the build |
| Volume marker mismatch | The named volume belongs to another Buildroot release | Use the default versioned volume or a new empty override volume |
| Defconfig is stale | Fragment/base and generated defconfig differ | Run the generator with `--write` in the exact LTS checkout and commit the result |
| Host-Go support missing | The selected host/config cannot build Go target packages | Confirm a native arm64 or amd64 image and Buildroot 2025.02.15 |
| Overlay source missing | Linux build state does not match the pinned source | Run `make linux-dirclean` in the versioned volume and rebuild |
| Ardor source edit absent | A local package retained stale synced sources | Use the complete builder or LAN deploy script; both run package `dirclean` |

## Hardware Notes

- The panel is native 720x1280 portrait; the UI runs rotated 270 degrees as
  1280x720 landscape because the installed panel is upside down.
- LVGL fbdev uses full render mode and double buffering so the entire frame is
  rotated together.
- Touch reports native portrait coordinates; LVGL applies the selected display
  rotation to align touch with the landscape UI.
- A release candidate is not accepted until display, touch, hardware controls,
  Codec Zero audio, preset switching, networking, SSH, manager daemon, and REST
  checks all pass on the Pi.

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
