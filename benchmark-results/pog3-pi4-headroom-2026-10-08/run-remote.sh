#!/bin/sh
set -eu
cd /tmp/pog3-headroom-20261008
was_running=0
if pidof ardor-pedal >/dev/null; then was_running=1; fi
cleanup() {
  if [ "$was_running" = 1 ]; then
    /etc/init.d/S99ardor-pedal start > service-restart.log 2>&1
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      if pidof ardor-pedal > restored-service-pid.txt; then return; fi
      sleep 1
    done
    return 1
  fi
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
sha256sum probe shared.wisdom /opt/ardor-pedal/presets/bank-000/preset-0.json /opt/ardor-pedal/models/Supro_1695TJ-P12Q_M201b_421738.nam > input-sha256.txt
/etc/init.d/S99ardor-pedal stop > service-stop.log 2>&1
if pidof ardor-pedal >/dev/null; then exit 1; fi
printf 'label,return_code\n' > receipt.csv
run() {
  label=$1; mode=$2; seconds=$3; profile=$4; io=${5:-alsa}
  result=0
  ./probe "$mode" "$seconds" "$profile" 128 shared.wisdom /opt/ardor-pedal "$label" "$io" > "$label.summary.csv" 2> "$label.log" || result=$?
  printf '%s,%s\n' "$label" "$result" >> receipt.csv
  cat "$label.summary.csv"
  metadata > "device-after-$label.txt"
  # Exit 3 records the first real xrun, with its partial callback trace.
  [ "$result" = 0 ] || [ "$result" = 3 ] || exit "$result"
}
run noop-1 noop 8 none
run chain-1 chain 30 none
run core-1 core 30 none
run combined-1 combined 12 none
run combined-offline-1 combined 12 none offline
run core-sample core 20 sample
run core-count core 20 count
run combined-2 combined 12 none
run combined-offline-2 combined 12 none offline
run core-2 core 30 none
run chain-2 chain 30 none
metadata > device-after.txt
