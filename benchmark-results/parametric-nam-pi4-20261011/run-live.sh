#!/bin/sh
set -eu
root=/tmp/ardor-hw-20261011
name=$1
frames=$2
seconds=$3
preset=$4
mode=${5:-headless}
dest="$root/results/$name"
if [ -e "$dest" ]; then echo "FAIL: result directory already exists: $dest" >&2; exit 1; fi
mkdir -p "$dest"
trace="$root/tracing/instances/ardor-parametric-hw"
echo 0 > "$trace/tracing_on"
echo > "$trace/trace"
echo 1 > "$trace/tracing_on"
flags=
if [ "$mode" = ui ]; then flags=--ui; fi
cleanup() {
  kill -TERM "$app" 2>/dev/null || true
  wait "$app" 2>/dev/null || true
  rm -f "$root/test.pid"
  echo 0 > "$trace/tracing_on"
}
trap cleanup EXIT
trap 'exit 143' HUP INT TERM
mkdir -p "$root/data/presets/bank-000"
cp "$preset" "$root/data/presets/bank-000/preset-0.json"
"$root/bin/ardor-hw-pedal" --realtime --bank 0 --slot 0 --data-root "$root/data" \
  --sample-rate 48000 --block-size "$frames" --capture-device -1 --playback-device -1 \
  --audio-cpu 2 --parallel-rigs --rig-worker-cpu 3 --wdw-dry-worker-cpu 0 --wdw-wet-worker-cpu 1 \
  --control-device /dev/null --telemetry-file "$dest/telemetry.txt" --clip-debug $flags > "$dest/audio.log" 2>&1 &
app=$!
echo "$app" > "$root/test.pid"
echo "started=$name pid=$app frames=$frames duration=$seconds"
i=0
while [ ! -r "$dest/telemetry.txt" ] && [ "$i" -lt 120 ]; do
  kill -0 "$app" || { cat "$dest/audio.log"; exit 1; }
  sleep 1
  i=$((i+1))
done
sleep 3
kill -0 "$app" || { cat "$dest/audio.log"; exit 1; }
if grep -E 'Warning: preset|Recovered with preset' "$dest/audio.log"; then
  echo "FAIL: requested test preset did not load" >&2
  exit 1
fi
cp "$root/data/runtime/active-preset.json" "$dest/active-preset.json"
cp "$dest/telemetry.txt" "$dest/start-telemetry.txt"
cat "/proc/$app/status" > "$dest/start-process-status.txt"
mkdir -p "$dest/threads"
for task in /proc/"$app"/task/*; do
  tid=${task##*/}
  cat "$task/status" > "$dest/threads/$tid-status.txt"
  cat "$task/sched" > "$dest/threads/$tid-sched.txt"
  cat "$task/stat" > "$dest/threads/$tid-stat.txt"
done
cat /proc/asound/card0/pcm0p/sub0/hw_params > "$dest/playback-hw-params.txt"
cat /proc/asound/card0/pcm0c/sub0/hw_params > "$dest/capture-hw-params.txt"
sh "$root/device-performance-remote.sh" "$seconds" 1 ardor-hw-pedal 2 3 0 "$dest/telemetry.txt" > "$dest/performance.txt" || true
kill -0 "$app" || { cat "$dest/audio.log"; exit 1; }
cp "$dest/telemetry.txt" "$dest/final-telemetry.txt"
cat "/proc/$app/status" > "$dest/final-process-status.txt"
cat /proc/asound/card0/pcm0p/sub0/status > "$dest/playback-final-status.txt"
cat /proc/asound/card0/pcm0c/sub0/status > "$dest/capture-final-status.txt"
cp "$root/data/runtime/active-preset.json" "$dest/final-active-preset.json"
case "$name" in
  *integration*) ;;
  *) cmp -s "$dest/active-preset.json" "$dest/final-active-preset.json" || { echo "FAIL: active preset changed during test" >&2; exit 1; } ;;
esac
echo PASS > "$dest/validation-status.txt"
echo "completed=$name"
cat "$dest/final-telemetry.txt"
echo 0 > "$trace/tracing_on"
cat "$trace/trace" > "$dest/alsa-xrun-trace.txt"
cat "$trace"/per_cpu/cpu*/stats > "$dest/trace-buffer-stats.txt"
grep -c __snd_pcm_xrun "$dest/alsa-xrun-trace.txt" || true
