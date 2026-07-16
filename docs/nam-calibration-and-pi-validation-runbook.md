# NAM Calibration and Raspberry Pi Validation Runbook

Use this runbook on the shipping Raspberry Pi, Codec Zero, power supply, enclosure, and audio
I/O path. Its two outputs are:

1. a measured Codec Zero input reference for NAM models that declare `input_level_dbu`; and
2. a reproducible Pi realtime-validation evidence directory.

Do not guess an analog full-scale value from a datasheet, a mixer slider, or a different sound
card. The calibration value applies only to the exact codec gain/mixer configuration that was
measured. Keep this document and the resulting evidence with the release record.

The current engine uses NAM output-loudness metadata, but does **not** yet apply NAM input-level
metadata. This procedure establishes the value and the listening evidence needed before that input
correction is enabled. Models without input-level metadata must retain a documented 0 dB input
correction rather than receiving a guessed adjustment.

## 1. Equipment and safety

You need:

- the production Pi, Codec Zero, enclosure, power supply, SD image, and final audio cabling;
- a stable 1 kHz sine source whose output voltage can be measured accurately (audio analyser,
  calibrated interface, or function generator);
- a true-RMS multimeter or oscilloscope to measure the signal at the Codec Zero input connector;
- an appropriate DI, reamp, pad, or isolator for the codec input range; and
- headphones/monitoring at a safe level.

Never connect a source whose level or DC offset exceeds the Codec Zero input specification. Do not
use phantom power while a line-level generator, reamp box, or measurement device is connected.
Disable AGC, limiters, noise suppression, and other automatic gain controls. Fix every Codec Zero
capture gain and mixer setting before measuring; changing any one invalidates the result.

## 2. Create an evidence directory

Run all commands as the account that normally launches Ardor. Replace `/opt/ardor` and the ALSA
device names if your image uses different paths.

```sh
RUN_DIR="$HOME/ardor-validation/$(date +%Y%m%d-%H%M%S)-nam-calibration"
mkdir -p "$RUN_DIR"

uname -a | tee "$RUN_DIR/uname.txt"
git -C /opt/ardor rev-parse HEAD | tee "$RUN_DIR/git-revision.txt"
vcgencmd measure_temp | tee "$RUN_DIR/temp-before.txt"
vcgencmd get_throttled | tee "$RUN_DIR/throttled-before.txt"
aplay -l | tee "$RUN_DIR/aplay-devices.txt"
arecord -l | tee "$RUN_DIR/arecord-devices.txt"
amixer -c <codec-card> contents | tee "$RUN_DIR/codec-mixer-before.txt"
```

`vcgencmd get_throttled` must be `throttled=0x0` before a formal validation run. Correct power or
cooling first if it is not. Record the Codec Zero card/device name, selected input, capture gain,
source/reamp settings, and measured cable point in `$RUN_DIR/notes.txt`.

## 3. Measure the Codec Zero input reference

The required calibration constant is:

```text
codec_input_0dBFS_dBu = measured_input_dBu - recorded_sine_level_dBFS
```

It means: “what analog input level in dBu would produce a full-scale digital sine at this exact
input gain?” It is often called the input dBuFS reference.

### 3.1 Capture a controlled sine

1. Set the generator to a clean 1 kHz sine, initially well below clipping (around −20 dBFS in the
   captured file is a good target).
2. Measure the RMS voltage **at the Codec Zero input connector**. Convert it to dBu:

   ```text
   input_dBu = 20 * log10(volts_RMS / 0.775)
   ```

3. Record 10 seconds from the same capture device, at the production 48 kHz rate. Use `S32_LE`
   unless `arecord -L`/your device documentation requires another format.

   ```sh
   arecord -D <capture-device> -c 1 -r 48000 -f S32_LE -d 10 \
     "$RUN_DIR/codec-1khz-level-a.wav"
   ```

4. Calculate the captured RMS level. This Python standard-library snippet reads the S32_LE WAV
   produced above and ignores the first/last second to avoid start/stop transients:

   ```sh
   python3 - "$RUN_DIR/codec-1khz-level-a.wav" <<'PY' | tee "$RUN_DIR/codec-1khz-level-a.txt"
   import math, struct, sys, wave

   with wave.open(sys.argv[1], "rb") as wav:
       assert wav.getnchannels() == 1, "expected mono capture"
       assert wav.getframerate() == 48000, "expected 48 kHz capture"
       assert wav.getsampwidth() == 4, "expected S32_LE capture"
       samples = struct.unpack("<%di" % wav.getnframes(), wav.readframes(wav.getnframes()))
       start, end = wav.getframerate(), len(samples) - wav.getframerate()
       samples = samples[start:end]
       rms = math.sqrt(sum((sample / 2147483648.0) ** 2 for sample in samples) / len(samples))
       peak = max(abs(sample / 2147483648.0) for sample in samples)
       print(f"rms_dBFS={20 * math.log10(rms):.3f}")
       print(f"peak_dBFS={20 * math.log10(peak):.3f}")
   PY
   ```

5. In `notes.txt`, record the measured voltage, calculated `input_dBu`, `rms_dBFS`, and:

   ```text
   codec_input_0dBFS_dBu = input_dBu - rms_dBFS
   ```

For example, a −20.0 dBu signal recorded at −18.0 dBFS yields a codec reference of −2.0 dBu at
0 dBFS. This is only an example, not a target value.

### 3.2 Check linearity and headroom

Repeat the capture at a second input level at least 10 dB away, without changing mixer/PGA
settings. Recalculate the dBuFS reference. The two results should agree within 1 dB. Also confirm:

- neither take clips (`peak_dBFS` remains safely below 0 dBFS);
- the 1 kHz tone has no obvious harmonic distortion or gain pumping; and
- the measured reference changes as expected if—and only if—you deliberately change capture gain.

If results differ by more than 1 dB, stop. Check source impedance, pads/reamp hardware, input
selection, automatic processing, meter accuracy, and whether the waveform is actually a sine.
Do not average inconsistent readings into a “close enough” calibration.

## 4. Establish the NAM input-correction contract

For a NAM model that supplies `input_level_dbu`, the planned correction is:

```text
input_gain_dB = codec_input_0dBFS_dBu - model_input_level_dBu
input_gain     = 10^(input_gain_dB / 20)
```

Before shipping that behavior, validate it with at least two models whose metadata is known and
whose capture/reference material is available. Reamp the same clean DI through the calibrated
input path, then compare drive, breakup onset, low-end response, and output level against the
model’s reference. The correction must be applied before the NAM processor; output loudness
normalization is a separate post-model concern and must not be used to hide an input-drive error.

For each model, record:

```text
model file and checksum:
HasInputLevel / GetInputLevel value:
codec_input_0dBFS_dBu used:
calculated input_gain_dB:
capture-gain/mixer configuration:
DI/reamp source and its measured level:
audible comparison and result:
```

If the formula produces obvious overdrive, clipping, or a drive mismatch, preserve the recording
and investigate the metadata convention and signal path before changing the formula. Do not add a
per-model magic offset. Models with no input-level metadata remain at 0 dB correction and must be
labelled as such in the report.

## 5. Pi realtime validation

Perform this after calibration, using the same final hardware configuration. The full supporting
procedure is in [pi-realtime-validation-runbook.md](pi-realtime-validation-runbook.md); the gates
below are the minimum release pass.

Use native 48 kHz and 64 frames. The callback budget is 1.333 ms. Do not pass
`--allow-non-realtime` or `--allow-device-resampling` in a formal run.

Test at least these cases for 10 minutes each, followed by a 30-minute closed-enclosure run of the
shipping worst case:

| Case | Required workload |
| --- | --- |
| Baseline | Pass-through/empty preset. |
| Shipping | Shipping NAM, cabinet IR, and normal effects. |
| Worst case | Largest supported NAM, 8192-sample IR, modulation, delay, reverb, compressor, and active UI. |
| Switching | Worst-case preset with repeated slot changes while audio is present. |
| Controls | Worst-case preset with touch, encoder, and footswitch activity. |

Start thermal and CPU evidence capture:

```sh
(
  while :; do
    printf '%s ' "$(date --iso-8601=seconds)"
    vcgencmd measure_temp
    vcgencmd get_throttled
    sleep 1
  done
) >"$RUN_DIR/thermal.log" & THERMAL_PID=$!

(
  while :; do
    printf '\n%s\n' "$(date --iso-8601=seconds)"
    mpstat -P ALL 1 1 2>/dev/null || true
    vcgencmd measure_clock arm 2>/dev/null || true
    sleep 1
  done
) >"$RUN_DIR/cpu.log" & CPU_PID=$!
```

Run Ardor and retain stderr:

```sh
cd /opt/ardor
./build-pi/pedal-poc --realtime --data-root /opt/ardor/data --bank 0 --slot 0 \
  --sample-rate 48000 --block-size 64 \
  --capture-device <capture-index> --playback-device <playback-index> \
  --input-channel left 2>&1 | tee "$RUN_DIR/pedal.log"
```

At startup, require all of the following:

- `Callback scheduler: SCHED_FIFO (1), priority 70`;
- native 48 kHz capture and playback; and
- no device-resampling or normal-priority override.

Capture independent scheduler evidence while the process is running:

```sh
pid="$(pidof ardor-pedal)"
for tid in /proc/"$pid"/task/*; do chrt -p "${tid##*/}"; done \
  | tee "$RUN_DIR/scheduler.txt"
```

Capture kernel evidence separately; Ardor’s `over` count measures callback-budget overruns, not
ALSA xruns:

```sh
sudo journalctl -kf -o short-iso >"$RUN_DIR/kernel.log" & KERNEL_PID=$!
# Run the test, then stop the journal follower with: sudo kill "$KERNEL_PID"
rg -ni 'xrun|underrun|overrun|snd_pcm|i2s|bcm|codec' "$RUN_DIR/kernel.log" || true
```

At the end, stop the application and samplers, then record the final state:

```sh
kill "$THERMAL_PID" "$CPU_PID"
wait "$THERMAL_PID" "$CPU_PID" 2>/dev/null || true
vcgencmd measure_temp | tee "$RUN_DIR/temp-after.txt"
vcgencmd get_throttled | tee "$RUN_DIR/throttled-after.txt"
amixer -c <codec-card> contents | tee "$RUN_DIR/codec-mixer-after.txt"
```

## 6. Release checklist

Mark the run **PASS** only when every item is true:

- [ ] The calibration used the shipping codec gain/mixer/cabling and has two linearity readings
  within 1 dB.
- [ ] The exact `codec_input_0dBFS_dBu` value, units, method, source voltage, WAV files, and
  mixer dump are in the evidence directory.
- [ ] Each tested NAM model records metadata, calculated correction, and an audible/reference
  comparison; models without metadata explicitly use 0 dB correction.
- [ ] `throttled=0x0` before and after the run, with no throttle bits in `thermal.log`.
- [ ] The audio callback runs at `SCHED_FIFO/70`; both devices are natively 48 kHz.
- [ ] No new Ardor `over` events, callback gaps, effect-bypass activation, ALSA XRUN state, or
  kernel xrun/underrun evidence occurs during the 30-minute worst-case run.
- [ ] Average callback time is below 65% of 1.333 ms (0.867 ms) and sampled p99, when available,
  is below 80% (1.067 ms).
- [ ] No clicks, dropouts, unwanted muting, drive mismatch, or control/switching failure is heard.

If any item fails, preserve the entire run directory and write the earliest observed timestamp in
`notes.txt`. Do not overwrite the failed evidence with a rerun.
