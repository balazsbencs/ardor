# Raspberry Pi Realtime Audio Validation Runbook

Use this runbook on the actual Raspberry Pi and Codec Zero before calling an Ardor release stage-ready. It measures three different things that must not be conflated:

- DSP callback budget (`over` in Ardor telemetry): the application took longer than the callback's time budget.
- ALSA/device xruns: the kernel or PCM device failed to keep its hardware buffer supplied or consumed.
- Thermal throttling: the Pi reduced clock/voltage or reached a thermal limit, which can cause either of the above.

Run the normal and stress cases with the final enclosure closed, final power supply, display enabled, and the same preset/model/IR that will ship. An open-board desktop test is only a smoke test.

## 1. Prepare the Pi

Record the software and hardware baseline before every formal run:

```sh
RUN_DIR="$HOME/ardor-validation/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN_DIR"

uname -a | tee "$RUN_DIR/uname.txt"
git -C /opt/ardor rev-parse HEAD | tee "$RUN_DIR/git-revision.txt"
vcgencmd measure_temp | tee "$RUN_DIR/temp-before.txt"
vcgencmd get_throttled | tee "$RUN_DIR/throttled-before.txt"
for governor in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
  [ -r "$governor" ] && printf '%s ' "$governor" && cat "$governor"
done | tee "$RUN_DIR/governor-before.txt"
aplay -l | tee "$RUN_DIR/aplay-devices.txt"
arecord -l | tee "$RUN_DIR/arecord-devices.txt"
```

`get_throttled` must read `throttled=0x0` before a formal run. If it does not, power-cycle and correct the power or cooling problem first; historic throttle bits make a test result ambiguous.

Use a stable system: disable package updates, browsers, compilers, SSH file transfers, and other CPU or I/O-heavy jobs. Keep the display/UI enabled if it is part of the product configuration.

For a headroom characterization only, the `performance` governor is useful. It is not a substitute for the normal-product test, which must use the intended governor.

```sh
# Optional characterization run; restore the intended governor afterwards.
for governor in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
  echo performance | sudo tee "$governor"
done
```

Install diagnostic tools if the image provides packages:

```sh
sudo apt install -y stress-ng rt-tests trace-cmd linux-perf
```

On Buildroot or another minimal image, use the equivalent image packages. Do not install tools or rebuild the application during a recorded run.

## 2. Select the test matrix

Use 48 kHz and 64 frames throughout. The callback budget is `64 / 48000 = 1.333 ms`.

Run each case for at least 10 minutes; run the shipping worst case for 30 minutes after the 10-minute gate passes.

| Case | Chain and interaction | Purpose |
| --- | --- | --- |
| Baseline | Empty/pass-through preset | Establish device and OS floor. |
| Shipping preset | NAM + 8192-sample cab + shipping effects | Normal use. |
| Worst case | Largest supported NAM, 8192-sample cab, modulation, delay, reverb, compressor | CPU headroom. |
| Switching | Worst case while changing slots repeatedly | Engine handoff and filesystem/control-thread pressure. |
| UI/control | Worst case with display, touch, encoder, and footswitch activity | Product configuration. |

Use a repeatable input. A continuous guitar looper or reamped DI track is preferable to silence: it exercises NAM, convolution, delay, and output paths. Set monitor volume safely before starting.

## 3. Capture callback-load and thermal evidence

Start independent samplers first. The first records temperature and throttle bits once per second; the second records CPU utilization and frequency once every two seconds.

```sh
(
  while :; do
    printf '%s ' "$(date --iso-8601=seconds)"
    vcgencmd measure_temp
    vcgencmd get_throttled
    sleep 1
  done
) >"$RUN_DIR/thermal.log" &
THERMAL_PID=$!

(
  while :; do
    printf '\n%s\n' "$(date --iso-8601=seconds)"
    mpstat -P ALL 1 1 2>/dev/null || true
    vcgencmd measure_clock arm 2>/dev/null || true
    sleep 1
  done
) >"$RUN_DIR/cpu.log" &
CPU_PID=$!
```

If `mpstat` is unavailable, record `top -b -n 1` instead. Do not use a GUI system monitor; it changes the workload.

Run the selected preset and send stderr to a log. Substitute the installed paths and device indices:

```sh
cd /opt/ardor
./build-pi/pedal-poc --realtime --data-root /opt/ardor/data --bank 0 --slot 0 \
  --sample-rate 48000 --block-size 64 \
  --capture-device <capture-index> --playback-device <playback-index> \
  --input-channel left 2>&1 | tee "$RUN_DIR/pedal.log"
```

At startup Ardor must report `Callback scheduler: SCHED_FIFO (1), priority 70`.
Capture the audio-thread policy as independent evidence; a normal-policy fallback is
not a production result:

```sh
pid="$(pidof ardor-pedal)"
for tid in /proc/"$pid"/task/*; do
  chrt -p "${tid##*/}"
done | tee "$RUN_DIR/scheduler.txt"
```

`--allow-non-realtime` is only for desktop/Linux development without
`CAP_SYS_NICE`; do not use it in a Pi validation or release image.
Likewise, do not use `--allow-device-resampling`: startup must report native
48 kHz capture and playback for a production result.

For slot-switch testing, enter `0`, `1`, `2`, `3` repeatedly at a controlled cadence (for example, one change every five seconds) while audio is present. Note the exact test pattern in `$RUN_DIR/notes.txt`.

Ardor prints once-per-second telemetry such as:

```text
callbacks=… over=… over%=… max=…ms avg=…ms budget=1.33ms bypassed=0
```

Record the final values and calculate deltas from the beginning/end of the run. `over=0` is the release gate. Until the backend exposes actual ALSA xrun counters, **do not label `over` as an ALSA xrun**; it is application callback-budget telemetry.

At the end of the run, stop the pedal with `Ctrl-C`, then stop samplers and collect the final state:

```sh
kill "$THERMAL_PID" "$CPU_PID"
wait "$THERMAL_PID" "$CPU_PID" 2>/dev/null || true
vcgencmd measure_temp | tee "$RUN_DIR/temp-after.txt"
vcgencmd get_throttled | tee "$RUN_DIR/throttled-after.txt"
```

## 4. Measure ALSA/device xruns separately

First capture kernel messages around the run:

```sh
sudo journalctl -kf -o short-iso >"$RUN_DIR/kernel.log" &
KERNEL_PID=$!
# Run the pedal test, then:
sudo kill "$KERNEL_PID"
```

Search the result without assuming every driver uses the same wording:

```sh
rg -ni 'xrun|underrun|overrun|snd_pcm|i2s|bcm|codec' "$RUN_DIR/kernel.log" || true
```

Where kernel trace events are available, record the PCM xrun event. Discover the exact event name on the target kernel rather than hard-coding one:

```sh
sudo trace-cmd list -e | rg -i 'snd.*(xrun|pcm)|alsa.*xrun'
```

If an event such as `snd_pcm_xrun` exists, record it during the same test:

```sh
sudo trace-cmd record -o "$RUN_DIR/alsa-xrun.dat" -e snd_pcm_xrun -- \
  /opt/ardor/build-pi/pedal-poc --realtime --data-root /opt/ardor/data --bank 0 --slot 0 \
  --sample-rate 48000 --block-size 64
sudo trace-cmd report "$RUN_DIR/alsa-xrun.dat" >"$RUN_DIR/alsa-xrun-report.txt"
```

If the kernel has no suitable tracepoint, use the kernel log plus a PCM status poll as supporting evidence:

```sh
find /proc/asound/card*/pcm*/sub*/status -type f -print
while :; do
  date --iso-8601=seconds
  cat /proc/asound/card*/pcm*/sub*/status
  sleep 1
done >"$RUN_DIR/alsa-status.log"
```

`/proc/asound` polling can miss a short recovery, so it is not proof of zero xruns. It is useful to correlate an observed problem with a PCM state such as `XRUN`.

## 5. Stress and causality checks

Do not run synthetic stress in the release pass. Use it only after a baseline pass to find margin and identify the cause of a failure.

### CPU interference

On a four-core Pi, first leave one core free for audio and load the others:

```sh
stress-ng --cpu 3 --cpu-method matrixprod --timeout 10m --metrics-brief \
  >"$RUN_DIR/stress-ng.log" 2>&1 &
STRESS_PID=$!
# Run the worst-case pedal preset, then kill "$STRESS_PID".
```

If this fails, repeat without stress. A failure only under stress identifies scheduler/CPU margin, not necessarily an audio-engine algorithm defect. If it fails in both cases, inspect callback telemetry, kernel logs, temperature, power supply, and the selected I2S device/period configuration.

### Scheduler latency

Run `cyclictest` only when the pedal is not running, then repeat with the pedal on a separate run if the image supports CPU affinity:

```sh
sudo cyclictest -m -Sp90 -i200 -h400 -q -D 600s | tee "$RUN_DIR/cyclictest.txt"
```

Large scheduler-latency spikes that approach or exceed 1.333 ms are release blockers, even if the short audio test happened not to xrun.

## 6. Pass/fail criteria and report template

Pass the shipping worst-case 30-minute closed-enclosure run only when all of the following are true:

- `throttled=0x0` before and after the run; thermal logs show no throttling bit at any point.
- No ALSA trace event, kernel xrun/underrun message, or observed `XRUN` PCM state.
- Ardor callback telemetry has zero new `over` events; no automatic effects bypass.
- Average callback time is below 65% of 1.333 ms (`< 0.867 ms`) and p99 is below 80% (`< 1.067 ms`). The current runtime prints average and max; obtain p99 from a sampled/per-callback profiler when that instrumentation is added.
- Audio remains free of clicks, dropouts, unexpected muting, and excessive noise through all preset switches and controls.
- The recorded maximum temperature remains below the board/enclosure's approved limit and never causes a throttle event. Treat a sustained temperature near 80 °C on a Pi 4 as a cooling-design warning even if a throttle bit has not yet appeared.

Create one report directory per run containing the generated logs and a `notes.txt` with this summary:

```text
Date/time:
Pi model / OS / kernel:
Codec Zero / power supply / enclosure state:
Ardor git revision:
Preset(s), NAM model(s), IR length, effect chain:
Audio device and actual ALSA settings:
Input source and output monitor path:
Run duration:
Temperature min / max / final:
vcgencmd get_throttled before / after:
Ardor callback count / over delta / average / max:
ALSA xrun evidence (trace, kernel log, or none):
Slot/control switching pattern:
Audible observations:
Result: PASS / FAIL
```

Any failure should preserve the complete log directory. Do not rerun immediately and overwrite evidence; first copy the directory and investigate the earliest correlated event.
