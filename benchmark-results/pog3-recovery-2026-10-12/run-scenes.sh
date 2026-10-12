#!/bin/sh
set -eu
cd /tmp/pog3-recovery-scenes-20261012
mkdir .running
mkdir -p data/runtime/live-commands
rm -f telemetry.txt return-code.txt
child=''
restore=0
if pidof ardor-pedal > service-before.txt; then restore=1; fi
cleanup() {
  code=$?
  trap - EXIT HUP INT TERM
  set +e
  echo 0 > /sys/class/leds/ardor:audio-output-enable/brightness
  if [ -n "$child" ] && kill -0 "$child" 2>/dev/null; then kill -TERM "$child"; wait "$child"; fi
  if [ "$restore" = 1 ]; then
    /etc/init.d/S99ardor-pedal start > service-restore.log 2>&1
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      if pidof ardor-pedal > service-restored.txt; then break; fi
      sleep 1
    done
    test -s service-restored.txt || code=1
  fi
  printf '%s\n' "$code" > return-code.txt
  rmdir .running
  exit "$code"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
sha256sum ardor-pedal quality libpedal-pog3-malloc-probe.so /usr/share/ardor-pedal/pog3-fftw.wisdom data/models/* > input-sha256.txt
/etc/init.d/S99ardor-pedal stop > service-stop.log 2>&1
LD_PRELOAD="$PWD/libpedal-pog3-malloc-probe.so" ./quality --allocation > target-quality.log 2>&1
if pidof ardor-pedal >/dev/null; then exit 1; fi
export ALSA_CARD=Zero
./ardor-pedal --realtime --ui --data-root "$PWD/data" --bank 0 --slot 3 --sample-rate 48000 --block-size 128 --parallel-rigs --audio-cpu 2 --rig-worker-cpu 3 --wdw-dry-worker-cpu 0 --wdw-wet-worker-cpu 1 --telemetry-file "$PWD/telemetry.txt" --clip-debug > app.log 2>&1 &
child=$!
ready=0
for attempt in $(seq 1 150); do
  if grep -q 'callbacks=[1-9]' telemetry.txt 2>/dev/null; then ready=1; break; fi
  kill -0 "$child" 2>/dev/null || break
  sleep .1
done
[ "$ready" = 1 ] || { echo 'App did not start audio.'; tail -n 15 app.log; exit 1; }
# Outputs stay muted during the automatic stress check.
cat /proc/asound/card0/pcm0p/sub0/hw_params > hw-params.txt
cat /proc/asound/card0/pcm0p/sub0/status > pcm-start.txt
sample() {
  label=$1; seconds=$2
  kill -0 "$child"
  cp telemetry.txt "$label.start.txt"
  grep -q 'worker_underflow=0 ' telemetry.txt
  grep -q 'worker_submit_miss=0 ' telemetry.txt
  grep -q 'bypassed=0 ' telemetry.txt
  grep -q 'pog3_bypassed=0 ' telemetry.txt
  grep -q 'pog3_recoveries=0 ' telemetry.txt
  sh device-performance-remote.sh "$seconds" 1 ardor-pedal 2 3 1 "$PWD/telemetry.txt" > "$label.performance.txt" 2>&1
  cp telemetry.txt "$label.end.txt"
  cp data/runtime/active-preset.json "$label.active.json"
  grep -q '"bank":[[:space:]]*0' "$label.active.json"
  cat "$label.end.txt"
}
sample baseline 2

generation=$(sed -n 's/.*"generation":[[:space:]]*\([0-9]*\).*/\1/p' data/runtime/active-preset.json)
[ -n "$generation" ]
for scene in b c d a; do
  printf '{"type":"recall_scene","generation":%s,"sceneId":"%s","requestId":"endurance-%s"}\n' "$generation" "$scene" "$scene" > data/runtime/live-commands/request.tmp
  mv data/runtime/live-commands/request.tmp data/runtime/live-commands/request.json
  selected=0
  for attempt in $(seq 1 100); do
    if grep -q "\"liveSceneId\":[[:space:]]*\"$scene\"" data/runtime/active-preset.json; then selected=1; break; fi
    kill -0 "$child" || exit 1
    sleep .1
  done
  [ "$selected" = 1 ] || { echo 'Scene recall failed.'; tail -n 15 app.log; exit 1; }
  sleep 1
  sample "scene-$scene" 10
done
