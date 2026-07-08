# Pi Buildroot Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Raspberry Pi 4 Buildroot image that packages the current pedal app, boots it automatically, and provides the expected asset layout for Codec Zero testing.

**Architecture:** Keep the existing Buildroot external tree and SysV init. Package both `pedal-poc` and `pedal-ui-sim`, install them as target binaries, and use small init scripts/config files instead of adding a desktop stack.

**Tech Stack:** Buildroot external tree, CMake package infrastructure, SysV init scripts, ALSA utilities, SDL framebuffer for LVGL.

## Global Constraints

- Package current app.
- Boot to fullscreen LVGL app.
- Verify Codec Zero input/output.
- Add service startup and asset directory layout.
- Keep known-good audio defaults: `48000 Hz`, `--block-size 64`, `--ir-samples 8192`.
- Do not vendor Buildroot into this repo.

---

## File Structure

- `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`: enable enough packages for ALSA, SDL framebuffer UI, SSH, and writable asset storage.
- `buildroot/external/package/ardor-pedal/ardor-pedal.mk`: install `pedal-poc`, `pedal-ui-sim`, init scripts, config, and asset directories.
- `buildroot/external/package/ardor-pedal/S99ardor-pedal`: start realtime audio from a config file.
- `buildroot/external/package/ardor-pedal/S98ardor-ui`: start the fullscreen UI.
- `buildroot/external/package/ardor-pedal/ardor-pedal.env`: default runtime configuration.
- `buildroot/external/package/ardor-pedal/README-assets.txt`: target-side asset layout note.
- `docs/hardware-validation.md`: add Codec Zero and first boot checklist.
- `README.md`: update Buildroot build/run notes.

---

## Dependency

Implement this after:

- `docs/superpowers/plans/2026-07-08-ui-save-load-wiring.md`
- `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md`

The Buildroot image starts the same `--data-root --bank --slot` paths those plans add.

---

### Task 1: Package Both Runtime Binaries And Asset Layout

**Files:**
- Modify: `buildroot/external/package/ardor-pedal/ardor-pedal.mk`
- Create: `buildroot/external/package/ardor-pedal/ardor-pedal.env`
- Create: `buildroot/external/package/ardor-pedal/README-assets.txt`

**Interfaces:**
- Consumes:
  - Existing CMake targets: `pedal-poc`, `pedal-ui-sim`
- Produces:
  - `/usr/bin/ardor-pedal`
  - `/usr/bin/ardor-ui`
  - `/etc/ardor-pedal.env`
  - `/opt/ardor-pedal/models`
  - `/opt/ardor-pedal/irs`
  - `/opt/ardor-pedal/presets/bank-000`

- [ ] **Step 1: Add target-side config**

Create `buildroot/external/package/ardor-pedal/ardor-pedal.env`:

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
CONTROL_DEVICES=
```

- [ ] **Step 2: Add asset layout note**

Create `buildroot/external/package/ardor-pedal/README-assets.txt`:

```text
Ardor pedal assets live here.

Expected layout:

  /opt/ardor-pedal/models/*.nam
  /opt/ardor-pedal/irs/*.wav
  /opt/ardor-pedal/presets/bank-000/preset-0.json
  /opt/ardor-pedal/presets/bank-000/preset-1.json
  /opt/ardor-pedal/presets/bank-000/preset-2.json
  /opt/ardor-pedal/presets/bank-000/preset-3.json

NAM and IR files are user-provided and are not shipped in the firmware image.
```

- [ ] **Step 3: Install binaries and directories**

Update `buildroot/external/package/ardor-pedal/ardor-pedal.mk`:

```make
ARDOR_PEDAL_VERSION = 1.0
ARDOR_PEDAL_SITE = $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../..
ARDOR_PEDAL_SITE_METHOD = local
ARDOR_PEDAL_DEPENDENCIES = sdl2
ARDOR_PEDAL_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DARDOR_ENABLE_LVGL_UI=ON

define ARDOR_PEDAL_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/S99ardor-pedal \
		$(TARGET_DIR)/etc/init.d/S99ardor-pedal
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/S98ardor-ui \
		$(TARGET_DIR)/etc/init.d/S98ardor-ui
endef

define ARDOR_PEDAL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pedal-poc $(TARGET_DIR)/usr/bin/ardor-pedal
	$(INSTALL) -D -m 0755 $(@D)/pedal-ui-sim $(TARGET_DIR)/usr/bin/ardor-ui
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/ardor-pedal.env \
		$(TARGET_DIR)/etc/ardor-pedal.env
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/README-assets.txt \
		$(TARGET_DIR)/opt/ardor-pedal/README-assets.txt
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/ardor-pedal/models
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/ardor-pedal/irs
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/ardor-pedal/presets/bank-000
endef

$(eval $(cmake-package))
```

- [ ] **Step 4: Build package metadata only**

Run from a Buildroot checkout:

```bash
make BR2_EXTERNAL=/Users/bbalazs/Documents/Ardor/buildroot/external raspberrypi4_ardor_pedal_defconfig
make ardor-pedal-dirclean
make ardor-pedal
```

Expected: package builds and target directory contains `/usr/bin/ardor-pedal`, `/usr/bin/ardor-ui`, `/etc/ardor-pedal.env`, and `/opt/ardor-pedal`.

- [ ] **Step 5: Commit**

```bash
git add buildroot/external/package/ardor-pedal/ardor-pedal.mk buildroot/external/package/ardor-pedal/ardor-pedal.env buildroot/external/package/ardor-pedal/README-assets.txt
git commit -m "feat: package pedal app and asset layout"
```

---

### Task 2: Boot Audio Service From Config

**Files:**
- Modify: `buildroot/external/package/ardor-pedal/S99ardor-pedal`

**Interfaces:**
- Consumes:
  - `/etc/ardor-pedal.env`
  - `/usr/bin/ardor-pedal`
- Produces:
  - Config-driven realtime startup using preset slot mode.

- [ ] **Step 1: Replace the init script**

Replace `buildroot/external/package/ardor-pedal/S99ardor-pedal`:

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

control_args=""
for dev in $CONTROL_DEVICES; do
  control_args="$control_args --control-device $dev"
done

case "$1" in
  start)
    echo "Starting ardor-pedal"
    start-stop-daemon -S -b -m -p "$PID" --exec /usr/bin/ardor-pedal -- \
      --realtime \
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
      $control_args
    ;;
  stop)
    echo "Stopping ardor-pedal"
    start-stop-daemon -K -p "$PID"
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

- [ ] **Step 2: Shell-check by execution syntax**

Run:

```bash
sh -n buildroot/external/package/ardor-pedal/S99ardor-pedal
```

Expected: no output and exit code `0`.

- [ ] **Step 3: Commit**

```bash
git add buildroot/external/package/ardor-pedal/S99ardor-pedal
git commit -m "feat: start pedal audio from config"
```

---

### Task 3: Boot Fullscreen LVGL UI

**Files:**
- Create: `buildroot/external/package/ardor-pedal/S98ardor-ui`
- Modify: `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`

**Interfaces:**
- Consumes:
  - `/usr/bin/ardor-ui`
  - `/etc/ardor-pedal.env`
- Produces:
  - Boot-time fullscreen UI service.

- [ ] **Step 1: Add UI init script**

Create `buildroot/external/package/ardor-pedal/S98ardor-ui`:

```sh
#!/bin/sh

PID=/var/run/ardor-ui.pid
ENV=/etc/ardor-pedal.env

[ -r "$ENV" ] && . "$ENV"
: "${DATA_ROOT:=/opt/ardor-pedal}"
: "${BANK:=0}"

case "$1" in
  start)
    echo "Starting ardor-ui"
    export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-fbcon}
    export SDL_NOMOUSE=${SDL_NOMOUSE:-0}
    start-stop-daemon -S -b -m -p "$PID" --exec /usr/bin/ardor-ui -- \
      --data-root "$DATA_ROOT" \
      --bank "$BANK"
    ;;
  stop)
    echo "Stopping ardor-ui"
    start-stop-daemon -K -p "$PID"
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

- [ ] **Step 2: Enable display/runtime packages**

Update `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig` with:

```text
BR2_PACKAGE_SDL2=y
BR2_PACKAGE_SDL2_VIDEO=y
BR2_PACKAGE_OPENSSH=y
```

- [ ] **Step 3: Verify script syntax**

Run:

```bash
sh -n buildroot/external/package/ardor-pedal/S98ardor-ui
```

Expected: no output and exit code `0`.

- [ ] **Step 4: Build image**

Run from a Buildroot checkout:

```bash
make BR2_EXTERNAL=/Users/bbalazs/Documents/Ardor/buildroot/external raspberrypi4_ardor_pedal_defconfig
make
```

Expected: image builds and includes both init scripts.

- [ ] **Step 5: Commit**

```bash
git add buildroot/external/package/ardor-pedal/S98ardor-ui buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig
git commit -m "feat: boot fullscreen pedal ui"
```

---

### Task 4: Codec Zero Verification Checklist

**Files:**
- Modify: `docs/hardware-validation.md`
- Modify: `README.md`

**Interfaces:**
- Consumes:
  - `/etc/init.d/S99ardor-pedal`
  - `/etc/init.d/S98ardor-ui`
  - `/opt/ardor-pedal` asset layout
- Produces:
  - First-boot and Codec Zero validation instructions.

- [ ] **Step 1: Add hardware validation section**

Add to `docs/hardware-validation.md`:

````markdown
## Raspberry Pi Buildroot First Boot

Before boot:

- Copy one `.nam` file to `/opt/ardor-pedal/models/test.nam`.
- Copy one 48 kHz mono cab IR to `/opt/ardor-pedal/irs/test.wav`.
- Create `/opt/ardor-pedal/presets/bank-000/preset-0.json` with relative assets.

First boot checks:

```sh
cat /etc/ardor-pedal.env
ls -R /opt/ardor-pedal
/etc/init.d/S98ardor-ui restart
/etc/init.d/S99ardor-pedal restart
tail -f /var/log/messages
```

Codec Zero checks:

```sh
aplay -l
arecord -l
alsamixer
```

Pass:

- Codec Zero appears as ALSA playback and capture hardware.
- LVGL UI is visible fullscreen on the 5 inch display.
- Guitar input produces stereo output.
- Runtime telemetry stays near the `64 / 8192` baseline without repeated overruns.
````

- [ ] **Step 2: Update README Buildroot section**

Replace the package install note in `README.md` Buildroot section with:

````markdown
The package installs:

- `/usr/bin/ardor-pedal`
- `/usr/bin/ardor-ui`
- `/etc/init.d/S98ardor-ui`
- `/etc/init.d/S99ardor-pedal`
- `/etc/ardor-pedal.env`
- `/opt/ardor-pedal/models/`
- `/opt/ardor-pedal/irs/`
- `/opt/ardor-pedal/presets/bank-000/`

Put local `.nam`, `.wav`, and preset JSON files under `/opt/ardor-pedal`.
````

- [ ] **Step 3: Verify docs and scripts**

Run:

```bash
sh -n buildroot/external/package/ardor-pedal/S98ardor-ui
sh -n buildroot/external/package/ardor-pedal/S99ardor-pedal
git diff --check
```

Expected: scripts parse and diff check is clean.

- [ ] **Step 4: Commit**

```bash
git add docs/hardware-validation.md README.md
git commit -m "docs: document pi buildroot validation"
```

---

## Skipped For This Phase

- A custom splash screen or display manager; init scripts are enough for the first Pi image.
- Shipping user NAM/IR assets in the image.
- Automatic Codec Zero mixer setup; add once the actual card names and required mixer controls are confirmed on hardware.
