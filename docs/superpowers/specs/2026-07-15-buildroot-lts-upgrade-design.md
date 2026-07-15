# Buildroot 2025.02 LTS Upgrade Design

**Status:** Approved

**Date:** 2026-07-15

## Summary

Upgrade the Ardor pedal image from Buildroot `2024.02.11` to the current
Buildroot LTS patch release, `2025.02.15`. The upgrade restores native builds
on Apple Silicon, makes the Buildroot source and Ardor configuration
reproducible, and replaces the duplicated Docker command sequences with one
validated build entrypoint.

The first migration deliberately preserves the hardware stack already proven
on the Raspberry Pi 4: the custom Raspberry Pi Linux 6.18 commit, the
era-matched Raspberry Pi firmware release, display and controls overlays,
Codec Zero setup, read-only root filesystem, and data partition layout. Kernel
and firmware modernization remain separate follow-up work so a Buildroot
userspace upgrade cannot be confused with a hardware regression.

## Background

The repository currently pins Buildroot `2024.02.11`. That version cannot
build the Go-based `ardor-managerd` package when Buildroot runs natively in an
ARM64 Docker container because its host-Go architecture capability evaluates
false. Buildroot then silently removes `BR2_PACKAGE_ARDOR_MANAGERD` during
configuration. The temporary workaround forces an x86_64 Ubuntu container on
Apple Silicon.

This exposed several related build-system problems:

- `BUILD.md` and `README.md` duplicate a long, manually maintained Docker
  workflow.
- `setup-defconfig.sh` does not contain all settings from the checked-in Ardor
  defconfig. In particular, it omits `BR2_PACKAGE_ARDOR_MANAGERD=y`.
- The build can complete even when an expected package has disappeared from
  `.config` unless the caller remembers to add explicit checks.
- A named Docker volume can contain a different Buildroot release from the one
  documented by the repository.
- Required image composition steps do not consistently fail when an expected
  generated artifact is absent.
- The post-image comments and source assumptions are tied to Buildroot 2024.02.

Buildroot `2025.02.15` is the current LTS patch release. Its host-Go selection
supports AArch64 hosts through the prebuilt Go bootstrap path, so an Apple
Silicon Mac no longer needs x86 emulation to build the ARM64 target daemon.

## Goals

- Pin the image build to exact Buildroot release `2025.02.15` and verify its
  downloaded source archive.
- Run Docker builds natively on Apple Silicon without `--platform linux/amd64`.
- Continue supporting x86_64 Docker hosts with the same script.
- Guarantee that both `ardor-pedal` and `ardor-managerd` remain selected and
  installed in every complete image.
- Establish one tracked Ardor configuration fragment and one deterministic
  process for generating the complete Buildroot defconfig.
- Establish one canonical image-build command for developers and documentation.
- Fail early with actionable errors for source-version, configuration,
  architecture, package, overlay, and image-output problems.
- Preserve the currently working Pi kernel, firmware, boot files, display,
  touch, controls, audio, preset storage, SSH, and REST API behavior.
- Document rollback and device validation before treating the upgraded image
  as the new baseline.
- Keep all intentionally deferred modernization work visible in the repository.

## Non-Goals

- Moving to the non-LTS Buildroot `2026.05` release in this migration.
- Replacing the proven Raspberry Pi Linux 6.18 kernel commit.
- Changing the Raspberry Pi firmware release or display overlay behavior.
- Changing partition sizes, filesystem policy, preset schema, daemon API, or
  Tauri application behavior.
- Enabling production API authentication by default during development.
- Adding CI image builds, OTA updates, or a custom prebuilt Docker image.
- Validating Windows as a Buildroot host. Windows remains relevant to the Tauri
  client, not the macOS-first firmware build workflow.

## Version And Source Policy

The repository will add `buildroot/buildroot-version.env` as the machine-readable
source of truth for:

```text
BUILDROOT_VERSION=2025.02.15
BUILDROOT_SOURCE_URL=https://buildroot.org/downloads/buildroot-2025.02.15.tar.xz
BUILDROOT_SOURCE_SHA256=98d0db6ed648b542821629659ee9cd38918bbb9b90a987e1c55517214a0df99e
BUILDROOT_DOCKER_VOLUME=buildroot_2025_02_15
```

Build and deployment scripts must source this file instead of carrying their
own default version or volume name. Documentation may state the selected
version for readers, but all executable behavior comes from this file.

The image build downloads the exact release archive and validates SHA-256
before extraction. A Docker volume initialized for another release is rejected
rather than modified in place. The versioned default volume leaves the existing
2024.02 and temporary amd64 volumes untouched for rollback or comparison.

The release is an exact reproducibility pin, not a floating request for
"latest LTS." Moving to a later `2025.02.x` patch requires a reviewed change to
the version file, regenerated configuration, and the same validation process.

## Configuration Model

Configuration will have three layers:

1. Buildroot's `configs/raspberrypi4_64_defconfig` from exactly `2025.02.15`.
2. A tracked Ardor-only fragment at
   `buildroot/external/configs/ardor-pedal.fragment`.
3. The generated, normalized Buildroot defconfig at
   `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`.

The fragment is the source of truth for every Ardor addition and every
intentional override of the upstream Raspberry Pi 4 configuration. It must
include, at minimum:

- AArch64 Cortex-A72 target settings.
- The custom Raspberry Pi Linux 6.18 source commit
  `256d6b4bc33527fae9967773b2a0d3b92e1bd000`.
- The `bcm2711` kernel defconfig and Pi 4 device tree.
- eudev, kmod, XZ module support, and required host image tools.
- The 256 MiB ext4 root filesystem and read-only root policy.
- Ardor rootfs overlay, post-build, post-image, and firmware config paths.
- ALSA utilities and OpenSSH.
- `BR2_PACKAGE_ARDOR_PEDAL=y`.
- `BR2_PACKAGE_ARDOR_MANAGERD=y`.
- The current development root password and the rootfs overlay that supplies
  the manager daemon's auth-off development environment.

Settings inherited unchanged from upstream do not belong in the fragment.
Intentional overrides do. The generated defconfig remains checked in because
Buildroot discovers external defconfigs there and because reviewers need to see
the resolved configuration.

`setup-defconfig.sh` will support two explicit modes:

- `--write` merges the exact upstream base with the Ardor fragment, resolves it
  through Buildroot Kconfig, and writes a normalized defconfig.
- `--check` performs the same generation in a temporary output directory and
  fails if the checked-in defconfig differs.

Normalization must avoid duplicate-symbol override warnings. The complete
image build runs `--check`, so edits to either the fragment or generated
defconfig cannot drift silently.

The migration adopts the upstream LTS toolchain header baseline of Linux 6.6.
The runtime kernel remains 6.18. Building userspace against older 6.6 headers
and running it on a newer 6.18 kernel is the supported compatibility direction.

## Build Entrypoint

The canonical command will be:

```sh
scripts/build-image.sh
```

`scripts/build-image.sh` owns the complete Docker workflow:

1. Read and validate `buildroot/buildroot-version.env`.
2. Require Docker and verify that the repository can be mounted.
3. Create or reuse the versioned Buildroot Docker volume.
4. Install the documented Ubuntu build dependencies in the container.
5. Initialize an empty volume from the verified Buildroot archive.
6. Reject a non-empty volume whose Buildroot version does not match the pin.
7. Run the defconfig drift check.
8. Load `raspberrypi4_ardor_pedal_defconfig` with `BR2_EXTERNAL` set.
9. Assert critical `.config` symbols before compiling.
10. Remove stale build state for both local packages, then run the full build.
11. Assert target binaries, init scripts, environment files, boot files,
    filesystems, and `sdcard.img`.
12. Verify that both target executables are AArch64 ELF files.
13. Copy `sdcard.img` to the repository atomically and print its SHA-256.

The script will not specify a Docker platform. Docker therefore uses the host's
native architecture: arm64 on Apple Silicon and amd64 on Intel/x86_64 hosts.
Buildroot remains responsible for producing the AArch64 Raspberry Pi target.

Supported environment overrides will be limited and documented:

- `ARDOR_BUILDROOT_VOLUME` for an intentionally separate cache.
- `ARDOR_DOCKER_IMAGE`, defaulting to `ubuntu:24.04`.
- `ARDOR_BUILD_JOBS` for Buildroot parallelism.
- `ARDOR_OUTPUT_IMAGE` for the copied image path.

Overrides may change storage, container base, concurrency, or output location;
they may not bypass source checksum, version, configuration, or artifact checks.

The build script uses noninteractive package installation and `set -eu`. Errors
must identify the failed invariant and the corrective action, such as selecting
the versioned volume, regenerating the defconfig, or cleaning a stale Linux
build after a source-pin change.

## Mandatory Pre-Build Checks

After loading the Ardor defconfig, the build stops unless `.config` contains:

```text
BR2_aarch64=y
BR2_cortex_a72=y
BR2_PACKAGE_HOST_GO_TARGET_ARCH_SUPPORTS=y
BR2_PACKAGE_ARDOR_PEDAL=y
BR2_PACKAGE_ARDOR_MANAGERD=y
BR2_PACKAGE_OPENSSH=y
BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y
# BR2_TARGET_GENERIC_REMOUNT_ROOTFS_RW is not set
```

It must also verify the exact custom Linux source pin and the Ardor post-image
script. This converts Buildroot's normal symbol pruning into an explicit,
diagnosable build failure before a long compile begins.

## Image Composition Compatibility

The initial LTS image preserves these known-good runtime inputs:

- Raspberry Pi Linux commit
  `256d6b4bc33527fae9967773b2a0d3b92e1bd000` from the 6.18 line.
- Raspberry Pi firmware release `1.20260521` for `start4.elf` and `fixup4.dat`.
- Kernel-source versions of the display, KMS, and Codec Zero overlays.
- The custom `ardor-controls.dtbo` overlay.
- Existing `config.txt`, Codec Zero state, and post-build composition.
- A read-only 256 MiB root filesystem and writable 256 MiB `ardor-data`
  partition seeded with four default presets.

The post-image script will update stale Buildroot-2024.02 commentary and verify
checksums for externally downloaded firmware blobs. Required firmware, kernel,
device-tree, overlay, boot filesystem, root filesystem, and data filesystem
outputs must be present and non-empty. Failure to compile a required custom
overlay must fail the image build rather than being ignored.

The LTS upgrade may adapt paths or package symbols that changed in Buildroot,
but it must not opportunistically change runtime hardware versions. Such a
change would invalidate the controlled migration and belongs in the deferred
hardware-stack work.

## Package And Rootfs Validation

Before copying the final image, the build must verify at least:

```text
output/target/usr/bin/ardor-pedal
output/target/usr/bin/ardor-managerd
output/target/etc/init.d/S98ardor-managerd
output/target/etc/init.d/S99ardor-pedal
output/target/etc/ardor-managerd.env
output/images/Image
output/images/bcm2711-rpi-4-b.dtb
output/images/boot.vfat
output/images/rootfs.ext4
output/images/data.ext4
output/images/sdcard.img
```

Executables and init scripts must have executable permissions. `file` output
for `ardor-pedal` and `ardor-managerd` must identify 64-bit AArch64 ELF
executables. Image files must be non-empty. The checks run after the complete
build, not only after individual package targets.

## Fast Deployment Workflow

`scripts/deploy-lan.sh` remains the app-only iteration path. It will source the
central Buildroot version file and use the same versioned Docker volume by
default. It continues to rebuild and deploy only `ardor-pedal`; it does not
replace full-image validation or deploy `ardor-managerd`.

The documentation must distinguish clearly between:

- `scripts/build-image.sh` for a bootable image containing all services.
- `scripts/deploy-lan.sh <host>` for fast replacement of the existing pedal
  runtime on an already compatible image.

Changes to Buildroot packages, init scripts, rootfs overlays, the manager
daemon, kernel, firmware, or partitions require a complete image build and
flash unless a separate, explicit deployment procedure is added later.

## Documentation Changes

`BUILD.md` becomes the canonical firmware build and validation guide. It will
cover:

- Native Apple Silicon prerequisites and Docker behavior.
- The one-command complete image build.
- Versioned volume creation, inspection, cleanup, and recovery.
- Output checks and checksum recording.
- macOS flashing steps.
- First boot, SSH, service, hardware, and REST API verification.
- Rollback to the previously known-good image.
- Fast app-only deployment and its limitations.
- Troubleshooting for config drift, stale package state, stale Linux state,
  source-version mismatch, and missing host-Go support.

`README.md` will provide a concise Buildroot summary and link to `BUILD.md`
instead of maintaining a second full build recipe. It must list both pedal
executables and both init services as image contents.

No active build documentation may instruct Apple Silicon users to force
`linux/amd64`, use `buildroot_vol_amd64`, or clone `2024.02.11` after the
migration. Historical design documents need not be rewritten.

## Verification Strategy

### Host Verification

- Validate shell syntax for every changed shell script.
- Run defconfig `--write`, then `--check`, and confirm a second generation is
  byte-for-byte stable.
- Confirm the critical `.config` symbols before the build starts.
- Complete a native arm64 Docker build on macOS.
- Confirm all package, architecture, boot, filesystem, and image assertions.
- Record the resulting image SHA-256 and Buildroot version.

An x86_64 host build is expected to use the same workflow, but it is not a
release gate for this macOS-first migration.

### Device Boot And Hardware Verification

Flash the new image while retaining the prior known-good image. On the Pi 4,
verify:

- Boot completes without an emergency shell or repeated service restart.
- Display output is stable and the full LVGL interface renders.
- Touch input maps correctly.
- Hardware controls are detected and affect the expected parameters.
- Codec Zero input and output devices are present.
- Audio passes through the realtime application without new underruns.
- The four seeded presets exist on the data partition and preset switching
  works.
- Root remains read-only during normal operation and data remains writable.
- Ethernet obtains an address and SSH login works.

Any display, touch, controls, audio, boot, or filesystem regression blocks
adoption of the new image even if compilation succeeded.

### Manager Daemon And REST Verification

With development auth disabled in `/etc/ardor-managerd.env`, verify:

- `/usr/bin/ardor-managerd` and `S98ardor-managerd` exist and are executable.
- The init service starts and remains running.
- Port 8080 is listening on the configured interface.
- `GET /api/device` returns device metadata and reports auth disabled.
- Asset-list endpoints return valid JSON.
- Preset-list and preset-fetch endpoints return the seeded bank and slots.
- A deliberately invalid preset request returns the documented JSON error and
  HTTP status rather than changing stored data.

The upgrade does not require destructive upload/delete tests on the first boot.
Those may be performed against disposable files after basic service validation.

## Rollback

Before flashing the LTS image, preserve the last known-good `sdcard.img` and its
checksum under an unambiguous name. Do not reuse or delete the old Buildroot
Docker volumes during migration.

If any release-gate check fails:

1. Capture serial console, service logs, kernel messages, and the failing API
   response where applicable.
2. Reflash the known-good image to restore the device.
3. Reproduce the failure from the versioned 2025.02.15 volume.
4. Fix the narrow migration issue without changing the preserved hardware
   stack unless evidence identifies that stack as the cause.

## Risks And Mitigations

- **Buildroot symbol or package changes:** regenerate through Kconfig and assert
  critical symbols before compilation.
- **Host-Go still unavailable:** assert
  `BR2_PACKAGE_HOST_GO_TARGET_ARCH_SUPPORTS=y` on the native host and fail
  before building local Go packages.
- **Stale named-volume state:** use a release-specific volume and validate the
  source version inside it.
- **Defconfig drift:** keep Ardor settings in one fragment and require generator
  `--check` in every complete build.
- **Kernel/userspace regression ambiguity:** preserve the proven kernel,
  firmware, and overlays during the LTS migration.
- **Silent image omissions:** assert target, boot, filesystem, service, and
  architecture outputs after the build.
- **Unrepeatable firmware download:** retain the exact firmware release and
  verify downloaded blob checksums.
- **Documentation drift:** make the build script executable truth and keep the
  full procedure only in `BUILD.md`.

## Deferred Work

The following tasks remain intentionally open after this migration and must be
kept in the implementation plan and final build documentation:

- Evaluate a newer Raspberry Pi kernel and matching firmware as a dedicated
  hardware compatibility project, including display timing, touch, controls,
  Codec Zero, realtime audio, and rollback testing.
- Evaluate Buildroot `2026.05` after the 2025.02 LTS image is stable, including
  release-note review and a fresh configuration diff.
- Add CI checks for defconfig drift, shell scripts, external package metadata,
  and eventually complete image builds on supported runners.
- Replace per-run Ubuntu dependency installation with a pinned, maintained
  build container if build frequency justifies it.
- Decide and document production manager-daemon authentication defaults,
  provisioning, and token rotation before distributing images outside trusted
  development networks.
- Remove temporary compatibility overrides only after upstream defaults are
  proven equivalent on the target hardware.
- Add a deliberate manager-daemon fast-deployment workflow if daemon iteration
  needs to avoid full reflashing.

## Acceptance Criteria

The migration is complete when all of the following are true:

- The source-of-truth version file pins and verifies Buildroot `2025.02.15`.
- `scripts/build-image.sh` builds successfully in a native arm64 Docker
  container on macOS without an explicit Docker platform.
- Defconfig generation is normalized, repeatable, and passes `--check`.
- The final `.config` contains both Ardor packages and host-Go AArch64 support.
- The final image contains both AArch64 binaries, both init scripts, manager
  environment configuration, required boot artifacts, and both ext4
  filesystems.
- `BUILD.md`, `README.md`, and `scripts/deploy-lan.sh` use the new versioned
  workflow and do not retain the x86 workaround as active guidance.
- The image passes the Pi boot, display, touch, controls, audio, preset, network,
  SSH, daemon, and REST checks.
- The previous image and Buildroot volume remain available for rollback.
- Every item not included in the migration is listed under Deferred Work rather
  than being left implicit.

## References

- Buildroot releases: <https://buildroot.org/download.html>
- Buildroot 2025.02.15 archive:
  <https://buildroot.org/downloads/buildroot-2025.02.15.tar.xz>
- Upstream AArch64 host-Go support change:
  <https://gitlab.com/buildroot.org/buildroot/-/commit/432cf9be9f43b26dbe7856feb3295b8e25c85048>
