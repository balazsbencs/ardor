# Pi Buildroot Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a flashable Raspberry Pi 4 SD-card image that boots the integrated pedal app (one process: audio + LVGL UI + controls), passes Codec Zero validation, survives power loss, and respawns on crash.

**Architecture:** Base the defconfig on upstream Buildroot's `raspberrypi4_64_defconfig` (which already solves rpi-firmware, kernel, DTBs, genimage, and the boot partition) and add deltas in the external tree. Ship one binary (`ardor-pedal`, the integrated `pedal-poc`), one init script with supervision, a read-only rootfs, and a separate writable data partition for presets/assets. No SDL, no Mesa, no second UI daemon — the UI renders through LVGL's in-tree fbdev driver with evdev touch.

**Tech Stack:** Buildroot external tree, CMake package infrastructure, SysV init, ALSA utilities + `alsactl` state restore, LVGL fbdev/evdev, genimage.

## Baseline Reality Check (read before editing)

The files this plan modifies are currently at POC state, not near-final:

- `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig` is 10 lines: no `rpi-firmware`, no boot partition/genimage, no `config.txt`, no Codec Zero overlay. **It cannot produce a bootable image today** — it produces only `rootfs.ext4`.
- `buildroot/external/package/ardor-pedal/ardor-pedal.mk` installs only `pedal-poc`, passes only `-DCMAKE_BUILD_TYPE=Release`, and would fail to configure on target because the CMake project requires SDL2 unless `ARDOR_ENABLE_LVGL_UI`/the UI backend option is set for a non-SDL build.
- `buildroot/external/package/ardor-pedal/S99ardor-pedal` hardcodes `--model /opt/ardor-pedal/model.nam --ir /opt/ardor-pedal/cab.wav` and has no supervision.

## Global Constraints

- One process on the device; there is no `ardor-ui` binary and no `S98ardor-ui` script. (Superseded: earlier drafts of this plan packaged `pedal-ui-sim` as a second daemon.)
- Boot to the fullscreen integrated app via LVGL fbdev — **not** SDL. `SDL_VIDEODRIVER=fbcon` does not exist in SDL2 and must not appear in any script.
- Keep known-good audio defaults: `48000 Hz`, `--block-size 64`, `--ir-samples 8192`.
- Root filesystem is read-only at runtime; all writable state lives on the data partition mounted at `/opt/ardor-pedal`.
- Do not vendor Buildroot into this repo.
- CMake `FetchContent` (miniaudio, NAMCore, LVGL) clones from git at configure time. The Buildroot build host therefore needs network access during `make ardor-pedal`. All three are pinned (tag `0.11.25`, commit `4c0ee78`, tag `v9.5.0`); do not loosen the pins. Offline builds are out of scope for v1.

---

## File Structure

- `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`: full bootable config based on upstream `raspberrypi4_64_defconfig`.
- `buildroot/external/board/ardor-pedal/config.txt`: firmware config with Codec Zero + controls overlays.
- `buildroot/external/board/ardor-pedal/ardor-controls.dts`: gpio-keys (KEY_F1..KEY_F4) + rotary-encoder overlay, from the sketch in `docs/hardware-assembly.md`.
- `buildroot/external/board/ardor-pedal/genimage-ardor.cfg`: boot + rootfs + data partition layout.
- `buildroot/external/board/ardor-pedal/post-image.sh`: genimage invocation (adapted from upstream raspberrypi board script).
- `buildroot/external/board/ardor-pedal/rootfs-overlay/etc/fstab`: mount data partition at `/opt/ardor-pedal`.
- `buildroot/external/board/ardor-pedal/codec-zero.state`: ALSA mixer state for the Codec Zero AUX in/out routing.
- `buildroot/external/package/ardor-pedal/ardor-pedal.mk`: install the integrated binary, init script, config, mixer state, default presets.
- `buildroot/external/package/ardor-pedal/S99ardor-pedal`: governor + mixer restore + supervised start from config.
- `buildroot/external/package/ardor-pedal/ardor-pedal.env`: default runtime configuration.
- `buildroot/external/package/ardor-pedal/preset-default.json`: safe pass-through preset installed into all four slots.
- `src/preset/PresetStore.cpp`: fsync + torn-write recovery (Task 4).
- `docs/hardware-validation.md`: first-boot, Codec Zero, power-loss, and thermal checklist.
- `README.md`: update Buildroot build/run notes.

---

## Dependency

Implement this after:

- `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md`
- `docs/superpowers/plans/2026-07-08-hardware-controls-integration.md`
- `docs/superpowers/plans/2026-07-09-ui-audio-integration.md`

The image boots the integrated `--realtime --ui --data-root --bank --slot` path those plans produce. Phase 0 of the roadmap (Pi feasibility spike) should already have validated the CPU budget on real hardware.

---

### Task 1: Bootable Defconfig And Board Files

**Files:**
- Modify: `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`
- Create: `buildroot/external/board/ardor-pedal/config.txt`
- Create: `buildroot/external/board/ardor-pedal/ardor-controls.dts`
- Create: `buildroot/external/board/ardor-pedal/genimage-ardor.cfg`
- Create: `buildroot/external/board/ardor-pedal/post-image.sh`
- Create: `buildroot/external/board/ardor-pedal/rootfs-overlay/etc/fstab`

- [ ] **Step 1: Start from upstream**

Copy the current upstream `configs/raspberrypi4_64_defconfig` from the Buildroot checkout as the base — it already carries the Raspberry Pi kernel fork, `bcm2711` defconfig, DTB selection, `rpi-firmware`, and the genimage host tools. Do not hand-roll these lines from memory; copy them, then apply the deltas below.

- [ ] **Step 2: Apply the Ardor deltas**

```text
# Ardor additions on top of raspberrypi4_64_defconfig:
BR2_PACKAGE_ALSA_UTILS=y
BR2_PACKAGE_ALSA_UTILS_ALSACTL=y
BR2_PACKAGE_ALSA_UTILS_AMIXER=y
BR2_PACKAGE_ARDOR_PEDAL=y
BR2_PACKAGE_OPENSSH=y
# Read-only rootfs: do NOT remount rw at boot.
# BR2_TARGET_GENERIC_REMOUNT_ROOTFS_RW is not set
BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/rootfs-overlay"
BR2_PACKAGE_RPI_FIRMWARE_CONFIG_FILE="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/config.txt"
BR2_ROOTFS_POST_IMAGE_SCRIPT="$(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/post-image.sh"
```

Remove/replace the upstream post-image script line so only the Ardor one runs. Keep the upstream `BR2_ROOTFS_POST_BUILD_SCRIPT` if present.

- [ ] **Step 3: Firmware config with overlays**

Create `board/ardor-pedal/config.txt` based on the upstream Pi 4 64-bit one, with:

```text
kernel=Image
arm_64bit=1
enable_uart=1
# Codec Zero (I2S DAC/ADC, DA7212)
dtparam=audio=off
dtparam=i2c_arm=on
dtparam=i2s=on
dtoverlay=rpi-codeczero
# Footswitches + encoder (compiled from ardor-controls.dts)
dtoverlay=ardor-controls
```

`dtparam=audio=off` keeps the Codec Zero as ALSA card 0. The DSI display needs no config.txt entry; the firmware initializes it and provides `/dev/fb0`, which is exactly what LVGL's fbdev driver consumes.

- [ ] **Step 4: Controls overlay**

Create `board/ardor-pedal/ardor-controls.dts` from the device-tree sketch in `docs/hardware-assembly.md` § Linux Device Tree Sketch (which uses `KEY_F1`..`KEY_F4` — the same codes `LinuxInput.cpp` matches; if the two ever disagree, the assembly doc and this overlay are wrong, not the code). Compile it in `post-build.sh` (or a package hook) with the kernel's `dtc` and install the resulting `ardor-controls.dtbo` into the boot files directory so genimage picks it up into the `overlays/` directory of the boot partition.

- [ ] **Step 5: Partition layout with a data partition**

Create `board/ardor-pedal/genimage-ardor.cfg` (adapted from upstream `genimage-raspberrypi4-64.cfg`) with three partitions:

```text
image boot.vfat {
  vfat { files = { "bcm2711-rpi-4-b.dtb", "rpi-firmware/", "overlays/", "Image" } }
  size = 64M
}

image sdcard.img {
  hdimage {}
  partition boot   { partition-type = 0xC;  bootable = "true"; image = "boot.vfat" }
  partition rootfs { partition-type = 0x83; image = "rootfs.ext4" }
  partition data   { partition-type = 0x83; image = "data.ext4" }
}
```

`post-image.sh` builds an empty `data.ext4` (256M, `mkfs.ext4 -d` seeded with the default preset layout from Task 2) and runs genimage. Copy the upstream raspberrypi `post-image.sh` as the starting point.

- [ ] **Step 6: Mount the data partition**

`rootfs-overlay/etc/fstab` adds:

```text
/dev/mmcblk0p3  /opt/ardor-pedal  ext4  rw,noatime,data=journal  0 2
```

`data=journal` trades write throughput (irrelevant here — preset saves are tiny and rare) for the strongest torn-write protection on the only writable filesystem.

- [ ] **Step 7: Build and verify**

From a Buildroot checkout:

```bash
make BR2_EXTERNAL=/Users/bbalazs/Documents/Ardor/buildroot/external raspberrypi4_ardor_pedal_defconfig
make
ls output/images/sdcard.img
```

Expected: a flashable `sdcard.img` exists containing boot, rootfs, and data partitions. This step is the gate — a defconfig that only produces `rootfs.ext4` is a regression to the broken baseline.

- [ ] **Step 8: Commit**

```bash
git add buildroot/external/configs buildroot/external/board
git commit -m "feat: bootable pi4 image with codec zero and data partition"
```

---

### Task 2: Package The Integrated Binary, Asset Layout, And Default Presets

**Files:**
- Modify: `buildroot/external/package/ardor-pedal/ardor-pedal.mk`
- Create: `buildroot/external/package/ardor-pedal/ardor-pedal.env`
- Create: `buildroot/external/package/ardor-pedal/preset-default.json`
- Create: `buildroot/external/package/ardor-pedal/README-assets.txt`

- [ ] **Step 1: Default presets — the app must never crash-loop on an empty device**

Create `preset-default.json`: a valid preset with an empty block chain (clean pass-through):

```json
{
  "version": 1,
  "name": "Init",
  "routing": "serial",
  "global": { "inputGainDb": 0.0, "outputGainDb": 0.0, "safetyLimitDb": -1.0 },
  "blocks": []
}
```

It is installed into all four slots of `bank-000` on the data partition seed (Task 1 Step 5), so first boot always finds loadable presets and produces dry pass-through audio.

- [ ] **Step 2: Target-side config**

Create `ardor-pedal.env`:

```sh
DATA_ROOT=/opt/ardor-pedal
BANK=0
SLOT=0
SAMPLE_RATE=48000
BLOCK_SIZE=64
IR_SAMPLES=8192
CAPTURE_DEVICE=-1
PLAYBACK_DEVICE=-1
INPUT_CHANNEL=0
OUTPUT_CHANNEL=both
CONTROL_DEVICES="/dev/input/event0 /dev/input/event1"
UI=1
```

- [ ] **Step 3: Package makefile**

```make
ARDOR_PEDAL_VERSION = 1.0
ARDOR_PEDAL_SITE = $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../..
ARDOR_PEDAL_SITE_METHOD = local
ARDOR_PEDAL_DEPENDENCIES = alsa-lib
ARDOR_PEDAL_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DARDOR_UI_BACKEND=fbdev

define ARDOR_PEDAL_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/S99ardor-pedal \
		$(TARGET_DIR)/etc/init.d/S99ardor-pedal
endef

define ARDOR_PEDAL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pedal-poc $(TARGET_DIR)/usr/bin/ardor-pedal
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/ardor-pedal.env \
		$(TARGET_DIR)/etc/ardor-pedal.env
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/codec-zero.state \
		$(TARGET_DIR)/etc/ardor-codec-zero.state
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/ardor-pedal
endef

$(eval $(cmake-package))
```

Notes: `ARDOR_UI_BACKEND=fbdev` (added by the integration plan) builds LVGL with `LV_USE_LINUX_FBDEV`/`LV_USE_EVDEV` and must not `find_package(SDL2)`. `alsa-lib` is a runtime dependency (miniaudio dlopens `libasound`); `alsa-utils` comes from the defconfig. `/opt/ardor-pedal` is just the mount point — content lives on the data partition. No `pedal-ui-sim` is installed.

- [ ] **Step 4: Asset note**

Create `README-assets.txt` describing the data-partition layout (models/, irs/, presets/bank-000/preset-{0..3}.json; default presets are pass-through; user assets replace them; nothing user-provided ships in the image). Install it in the data partition seed.

- [ ] **Step 5: Build the package**

```bash
make BR2_EXTERNAL=/Users/bbalazs/Documents/Ardor/buildroot/external raspberrypi4_ardor_pedal_defconfig
make ardor-pedal-dirclean ardor-pedal
```

Expected: configure succeeds **without SDL2 present** (this is the regression test for the old `find_package(SDL2 REQUIRED)` failure), and `/usr/bin/ardor-pedal` lands in the target dir.

- [ ] **Step 6: Commit**

```bash
git add buildroot/external/package/ardor-pedal
git commit -m "feat: package integrated pedal binary and default presets"
```

---

### Task 3: Boot Service With Supervision, Governor, And Mixer Restore

**Files:**
- Modify: `buildroot/external/package/ardor-pedal/S99ardor-pedal`
- Create: `buildroot/external/board/ardor-pedal/codec-zero.state`

- [ ] **Step 1: Get the mixer state**

The Codec Zero (DA7212) passes no audio until its AUX in/out routing is set. Take the known-good state from Raspberry Pi's own repo (`github.com/raspberrypi/Pi-Codec`, `Codec_Zero_AUXIN_record_and_HP_playback.state` or the AUX-out variant matching the wiring in `docs/hardware-assembly.md`), verify it on hardware once with `alsactl restore`, and vendor the verified file as `board/ardor-pedal/codec-zero.state`. This was previously "skipped for this phase" — it cannot be skipped, because the phase gate "guitar input produces stereo output" depends on it.

- [ ] **Step 2: Replace the init script**

```sh
#!/bin/sh

PID=/var/run/ardor-pedal.pid
ENV=/etc/ardor-pedal.env

[ -r "$ENV" ] && . "$ENV"

: "${DATA_ROOT:=/opt/ardor-pedal}"
: "${BANK:=0}"
: "${SLOT:=0}"
: "${SAMPLE_RATE:=48000}"
: "${BLOCK_SIZE:=64}"
: "${IR_SAMPLES:=8192}"
: "${CAPTURE_DEVICE:=-1}"
: "${PLAYBACK_DEVICE:=-1}"
: "${INPUT_CHANNEL:=0}"
: "${OUTPUT_CHANNEL:=both}"
: "${CONTROL_DEVICES:=}"
: "${UI:=1}"

control_args=""
for dev in $CONTROL_DEVICES; do
  control_args="$control_args --control-device $dev"
done
ui_arg=""
[ "$UI" = "1" ] && ui_arg="--ui"

run_supervised() {
  # Mute -> route -> start -> unmute: no boot pop, and a crash-looping
  # app never plays half-initialized audio at full volume.
  amixer -q set 'Headphone' mute 2>/dev/null
  alsactl restore -f /etc/ardor-codec-zero.state 2>/dev/null
  echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
  while :; do
    /usr/bin/ardor-pedal \
      --realtime $ui_arg \
      --data-root "$DATA_ROOT" \
      --bank "$BANK" \
      --slot "$SLOT" \
      --sample-rate "$SAMPLE_RATE" \
      --block-size "$BLOCK_SIZE" \
      --ir-samples "$IR_SAMPLES" \
      --capture-device "$CAPTURE_DEVICE" \
      --playback-device "$PLAYBACK_DEVICE" \
      --input-channel "$INPUT_CHANNEL" \
      --output-channel "$OUTPUT_CHANNEL" \
      $control_args &
    APP=$!
    sleep 1
    amixer -q set 'Headphone' unmute 2>/dev/null
    wait $APP
    echo "ardor-pedal exited ($?), respawning" >&2
    amixer -q set 'Headphone' mute 2>/dev/null
    sleep 1
  done
}

case "$1" in
  start)
    echo "Starting ardor-pedal"
    start-stop-daemon -S -b -m -p "$PID" --exec /bin/sh -- "$0" run
    ;;
  run)
    run_supervised
    ;;
  stop)
    echo "Stopping ardor-pedal"
    start-stop-daemon -K -p "$PID"
    killall ardor-pedal 2>/dev/null
    ;;
  restart)
    "$0" stop
    "$0" start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
esac
```

The scaling-governor write is one line and removes the single most common source of xruns at a 1.33 ms budget (`ondemand` frequency ramping). Confirm the exact `amixer` control name (`Headphone`) against the real card during Step 1 and adjust — the mixer control names come from the codec driver, not from us.

- [ ] **Step 3: Syntax check**

```bash
sh -n buildroot/external/package/ardor-pedal/S99ardor-pedal
```

- [ ] **Step 4: On-target checks**

- `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor` → `performance`.
- `kill $(pidof ardor-pedal)` → app is back within ~2 s, audio resumes.
- Reboot with a scope/monitor on the output → no pop at boot.

- [ ] **Step 5: Commit**

```bash
git add buildroot/external/package/ardor-pedal/S99ardor-pedal buildroot/external/board/ardor-pedal/codec-zero.state
git commit -m "feat: supervised boot with governor and mixer restore"
```

---

### Task 4: Power-Loss-Safe Preset Storage

A pedal is a device that gets unplugged mid-write. `PresetStore::save` already does tmp-file + rename, but never fsyncs the file or its directory — on ext4 a power cut can still leave a zero-length or torn preset, which then throws on every boot.

**Files:**
- Modify: `src/preset/PresetStore.cpp`
- Modify: `tests/preset_smoke.cpp`

- [ ] **Step 1: Write the failing test**

In `tests/preset_smoke.cpp`: write garbage bytes into a slot's `preset-N.json`, assert `PresetStore::load` throws; then assert a leftover `preset-N.json.tmp` from an interrupted save is ignored by `load` and removed by the next successful `save`.

- [ ] **Step 2: Make save durable**

In `PresetStore::save`, after writing the tmp file: `fsync` the file descriptor before `close` (use `::open`/`::write` or `fileno` on the stream's descriptor — `std::ofstream::flush` does not reach the disk), then `rename`, then `fsync` the parent directory descriptor so the rename itself is durable. Remove any stale `.tmp` before starting a new save.

- [ ] **Step 3: Recovery at load**

Callers that boot the device (slot loading in `pedal-poc`) must treat a preset that fails to parse as "empty pass-through preset + logged warning", never as a fatal error — combined with the Task 3 supervisor, a corrupt preset must not become a crash loop. Add this fallback where `applyPresetSlot` reports failure on the boot path.

- [ ] **Step 4: Run tests, commit**

```bash
cmake --build build --target pedal-preset-smoke && ctest --test-dir build -R pedal-preset-smoke --output-on-failure
git add src/preset/PresetStore.cpp tests/preset_smoke.cpp apps/pedal-poc/main.cpp
git commit -m "feat: power-loss safe preset storage"
```

---

### Task 5: First Boot And Codec Zero Verification Checklist

**Files:**
- Modify: `docs/hardware-validation.md`
- Modify: `README.md`

- [ ] **Step 1: Add hardware validation section**

Add to `docs/hardware-validation.md`:

````markdown
## Raspberry Pi Buildroot First Boot

Flash `output/images/sdcard.img`. No preparation is required — the image ships
pass-through presets. To hear an amp, copy assets onto the data partition:

- `/opt/ardor-pedal/models/*.nam`
- `/opt/ardor-pedal/irs/*.wav` (48 kHz mono)
- edit `/opt/ardor-pedal/presets/bank-000/preset-0.json` with relative assets

First boot checks:

```sh
cat /etc/ardor-pedal.env
mount | grep ardor            # data partition rw, rootfs ro
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor   # performance
aplay -l                      # Codec Zero is card 0
arecord -l
cat /proc/bus/input/devices   # footswitches + encoder present
```

Runtime checks:

- LVGL UI is fullscreen on the DSI display; touch works.
- Guitar input produces stereo output (mixer state restored by S99).
- Touching a preset slot on screen is audible.
- `kill $(pidof ardor-pedal)` -> respawn within ~2 s.
- Telemetry stays near the `64 / 8192` baseline without repeated overruns.

Thermal soak (enclosure closed, 10 minutes of playing):

```sh
vcgencmd measure_temp
vcgencmd get_throttled   # must stay 0x0
```

Power-loss check: yank power mid preset-save five times; every boot must load
presets (worst case: the affected slot falls back to pass-through with a log
line, never a crash loop).

Factory reset / update story (v1): reflash the SD card. There is no OTA.
````

- [ ] **Step 2: Update README Buildroot section**

Replace the install list: `/usr/bin/ardor-pedal`, `/etc/init.d/S99ardor-pedal`, `/etc/ardor-pedal.env`, `/etc/ardor-codec-zero.state`, data partition mounted at `/opt/ardor-pedal` with default pass-through presets. Note the network-at-build-time requirement for FetchContent.

- [ ] **Step 3: Verify and commit**

```bash
sh -n buildroot/external/package/ardor-pedal/S99ardor-pedal
git diff --check
git add docs/hardware-validation.md README.md
git commit -m "docs: document pi buildroot validation"
```

---

## Skipped For This Phase

- A custom splash screen or display manager; init scripts are enough for the first Pi image.
- Shipping user NAM/IR assets in the image.
- PREEMPT_RT kernel; the stock `bcm2711` kernel config plus the `performance` governor is the v1 baseline. Add a `CONFIG_PREEMPT=y` kernel fragment only if the Phase 0 spike or the first-boot soak shows scheduling-induced overruns.
- Hardware watchdog; deferred per the roadmap until field data shows hangs.
- Offline/vendored Buildroot builds; FetchContent pins are the v1 supply-chain story.
