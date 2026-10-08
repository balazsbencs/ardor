#!/bin/sh
# Silent device timing; the normal app is restored on every exit.
set -eu
cd /tmp/pog3-pipeline2-20261008
mkdir .running
was_running=0
child=''
if pidof ardor-pedal > service-before.txt; then was_running=1; fi
cleanup() {
  result=$?
  trap - EXIT HUP INT TERM
  set +e
  if [ -n "$child" ] && kill -0 "$child" 2>/dev/null; then
    kill -TERM "$child"
    wait "$child"
  fi
  if [ "$was_running" = 1 ]; then
    /etc/init.d/S99ardor-pedal start > service-restart.log 2>&1
    restored=0
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      if pidof ardor-pedal > service-restored.txt; then restored=1; break; fi
      sleep 1
    done
    [ "$restored" = 1 ] || result=1
  fi
  printf '%s\n' "$result" > return-code.txt
  rmdir .running
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
metadata() {
  date -u
  cat /sys/class/thermal/thermal_zone0/temp
  cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq
  cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
}
metadata > device-before.txt
sha256sum pedal-pog3-headroom pedal-pog3-pipeline-quality libpedal-pog3-malloc-probe.so shared.wisdom \
  /opt/ardor-pedal/presets/bank-000/preset-0.json \
  /opt/ardor-pedal/models/Supro_1695TJ-P12Q_M201b_421738.nam > input-sha256.txt
LD_PRELOAD="$PWD/libpedal-pog3-malloc-probe.so" ./pedal-pog3-pipeline-quality --allocation > target-quality.log 2>&1
/etc/init.d/S99ardor-pedal stop > service-stop.log 2>&1
if pidof ardor-pedal >/dev/null; then echo 'Normal service did not stop.' >&2; exit 1; fi
printf 'label,return_code\n' > receipt.csv
run() {
  label=$1; mode=$2; seconds=$3
  ./pedal-pog3-headroom "$mode" "$seconds" none 128 shared.wisdom \
    /opt/ardor-pedal "$label" alsa > "$label.summary.csv" 2> "$label.log" &
  child=$!
  result=0
  wait "$child" || result=$?
  child=''
  printf '%s,%s\n' "$label" "$result" >> receipt.csv
  cat "$label.summary.csv"
  metadata > "device-after-$label.txt"
  # 3 means an explicitly recorded ALSA xrun or worker deadline miss.
  [ "$result" = 0 ] || [ "$result" = 3 ] || exit "$result"
}
run direct-1 combined 12
run one-block-1 pipeline 30
run two-block-1 pipeline2 30
run two-block-2 pipeline2 30
run one-block-2 pipeline 30
run direct-2 combined 12
run core core 15
run chain chain 15
metadata > device-after.txt
