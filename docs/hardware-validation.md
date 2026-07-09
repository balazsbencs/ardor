# Hardware Validation

Assembly, pin assignments, and first-power checks live in `docs/hardware-assembly.md`.

## macOS UMC22 Realtime Test

Command:

```sh
./build/pedal-poc \
  --realtime \
  --model models/test.nam \
  --ir irs/test.wav \
  --sample-rate 48000 \
  --block-size 64 \
  --capture-device 1 \
  --playback-device 1 \
  --input-channel right \
  --output-channel both \
  --output-gain-db -12
```

Note: `--output-gain-db -12` is required because `PedalEngine` does not yet
apply the model's loudness metadata for normalization. Without it the Friedman
model clips the −1 dB safety hard-clipper and distorts.

Pass:

- Runs without xruns.
- No audible dropouts.
- Status prints increasing callback count.

Current result:

```text
date: 2026-07-07
device: Behringer U-Phoria UMC22, shown as USB Audio CODEC
sample rate: 48000
block size: 64
IR samples: 8192
input channel: right
output channel: both
xruns: 0
output gain: -12 dB (without it the Friedman model clips the safety limiter)
notes: sounds good in realtime; 12000 eventually produced xruns, 14000 produced a few xruns, 16000 was unusable
```

Partitioned convolution result:

```text
date: 2026-07-07
device: Behringer U-Phoria UMC22, shown as USB Audio CODEC
sample rate: 48000
block size: 64
IR samples: 24000 full irs/test.wav
input channel: right
output channel: both
over-budget callbacks: 0
max callback: about 0.86 ms
average callback: about 0.27 ms
budget: 1.33 ms
notes: short smoke run; full IR became practical after partitioned convolution
```

## DSP Microbenchmark Baseline

Per-component realtime cost at `48000 Hz / block 64 / IR 8192`. The realtime
telemetry lumps everything into one number; `pedal-dsp-bench` separates the two
expensive components (NAM inference, IR convolution) so we can see where the
1333 µs/block budget goes.

### Why this matters (context for whoever runs it)

The whole roadmap rests on one unvalidated assumption: that NAM + convolution
fit the Pi 4B's per-block budget at block size 64. All numbers so far are from
a Mac, which is roughly 5–10× faster per core than the Pi's Cortex-A72. The Pi
rows below are the roadmap **Phase 0 gate** — they decide model-tier targets
and whether the convolver optimization tasks in
`docs/superpowers/plans/2026-07-09-ir-convolver-performance.md` run at all.

### Recorded results

| Host | Component | avg µs/block | % of 1333 µs budget |
| --- | --- | ---: | ---: |
| macOS dev machine (2026-07-09) | IrConvolver | 22.8 (24.6 before precomputed twiddles; max 125→78) | 1.7% |
| macOS dev machine (2026-07-09) | NamProcessor (test.nam, SlimmableContainer/WaveNet) | 50.4 | 3.8% |
| Pi 4B (`performance` governor, 2026-07-09, commit 5634c92, test.nam) | IrConvolver | 91.25 (max 140) | 6.8% |
| Pi 4B (`performance` governor, 2026-07-09, commit 5634c92, test.nam) | NAM[tier-1] — standard/full model | 370.66 (max 431) | 27.8% |
| Pi 4B (`performance` governor, 2026-07-09, commit 5634c92, test.nam) | NAM[tier-0] — nano/feather model | 102.09 (max 152) | 7.7% |

### Pi measurement procedure (delegable, ~1–2 h including build time)

You need: a Raspberry Pi 4B (the 1GB target unit or any Pi 4B — the CPU is the
same), an SD card with 64-bit Raspberry Pi OS Lite, network access, and two
`.nam` files: one standard WaveNet and one feather/lite variant (ask in the
project channel if you don't have them; they are not in git).

1. **Prepare the Pi.** Full Buildroot image not required — SSH into Raspberry
   Pi OS Lite is fine for this measurement.

   ```sh
   sudo apt update && sudo apt install -y git cmake g++ ninja-build
   ```

2. **Add swap before building.** The 1GB board cannot compile the
   Eigen-heavy NAM sources in RAM alone:

   ```sh
   sudo dphys-swapfile swapoff
   sudo sed -i 's/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=2048/' /etc/dphys-swapfile
   sudo dphys-swapfile setup && sudo dphys-swapfile swapon
   ```

3. **Clone and build** (needs network — CMake fetches miniaudio, NAM core,
   and LVGL at configure time; `-j1` on the 1GB board, `-j4` on a 4/8GB board):

   ```sh
   git clone <repo-url> ardor && cd ardor
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARDOR_UI_BACKEND=none
   cmake --build build --target pedal-dsp-bench -j1
   ```

4. **Set the governor and check for throttling.** Skipping this invalidates
   the measurement — `ondemand` frequency ramping inflates and destabilizes
   the numbers:

   ```sh
   echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   vcgencmd get_throttled    # must print throttled=0x0 before AND after the runs
   ```

5. **Run three times**, take the middle run:

   ```sh
   ./build/pedal-dsp-bench /path/to/model.nam
   ```

   NAM A2 files (SlimmableContainer) embed both tiers in one file. The bench
   detects this automatically and prints one `NAM[tier-N]` line per tier:
   tier-0 is the nano/feather model, tier-1 is the standard/full model.
   Non-slimmable files print a single `NamProcessor` line.

   Each run prints one line per component with min/avg/max µs and % of budget.
   If `vcgencmd get_throttled` is non-zero afterwards, cool the board and
   re-run — throttled numbers go in the bin, not the table.

6. **Record**: fill the TBD rows above (avg µs and %; note the max if it is
   more than ~3× the avg), plus the exact model filenames, the commit hash
   (`git rev-parse --short HEAD`), and `vcgencmd measure_temp` after the runs.
   Commit the edit to this file.

### How to read the result (decision table)

| Pi result | Action |
| --- | --- |
| IrConvolver < 200 µs/block | Convolver optimization Tasks 3–4 in the convolver plan **do not run**. Note it in that plan and move on. |
| IrConvolver ≥ 200 µs/block | Execute the convolver plan Tasks 2–3 (conjugate symmetry), re-measure; Task 4 (pffft) only if still ≥ 200 µs. |
| NAM (any tier) + convolver < ~65% of budget combined | Phase 0 gate passes with that tier — record which tier and continue the roadmap. |
| No model tier fits at block 64 | Stop; escalate to the roadmap owner — block-size 128 fallback or model constraints must be decided before any further phases (see roadmap Phase 0 "Stop If"). |

## Raspberry Pi Buildroot First Boot

Flash `output/images/sdcard.img`. No preparation is required — the image ships pass-through presets. To hear an amp, copy assets onto the data partition:

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

- LVGL UI is fullscreen on the DSI display.
- Guitar input produces stereo output (mixer state restored by S99).
- Touching a preset slot on screen is audible.
- `kill $(pidof ardor-pedal)` → respawn within ~2 s.
- Telemetry stays near the `64 / 8192` baseline without repeated overruns.

Thermal soak (enclosure closed, 10 minutes of playing):

```sh
vcgencmd measure_temp
vcgencmd get_throttled   # must stay 0x0
```

Power-loss check: yank power mid preset-save five times; every boot must load presets (worst case: the affected slot falls back to pass-through with a log line, never a crash loop).

Factory reset / update story (v1): reflash the SD card. There is no OTA.

## Raspberry Pi Codec Zero Realtime Test

Command on Pi:

```sh
/etc/init.d/S99ardor-pedal restart
tail -f /var/log/messages
```

Pass:

- App starts on boot.
- Guitar input produces stereo output.
- No ALSA xrun messages for 10 minutes.

## Latency Measurement Later

Method:

- Send a click from output to input with a loopback cable.
- Record the processed click.
- Measure input-to-output sample offset.
- Convert samples to milliseconds: `samples / 48000 * 1000`.

Pass:

- Round-trip latency is under 10 ms.

Result:

```text
date:
device:
sample rate:
block size:
measured samples:
measured ms:
notes:
```

## Footswitch And Encoder Input

V1 expects Linux input devices, not app-level GPIO polling.

Recommended Pi path:

- Expose four footswitches with the kernel `gpio-keys` overlay.
- Map them to `KEY_F1`, `KEY_F2`, `KEY_F3`, and `KEY_F4`.
- Expose the rotary encoder with the kernel `rotary-encoder` overlay.
- Confirm events with:

```sh
evtest /dev/input/eventX
```

Runtime command:

```sh
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --control-device /dev/input/event-footswitches \
  --control-device /dev/input/event-encoder \
  --block-size 64 --ir-samples 8192
```

Footswitches select slots `0` through `3`. Encoder relative motion changes master output volume from `0%` to `100%`.
