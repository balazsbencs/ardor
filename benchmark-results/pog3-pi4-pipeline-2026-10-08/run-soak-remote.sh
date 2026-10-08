#!/bin/sh
# Run only after both short two-block tests passed and the earlier runner exited.
set -eu
cd /tmp/pog3-pipeline2-20261008
mkdir .running
was_running=0
child=''
if pidof ardor-pedal > soak-service-before.txt; then was_running=1; fi
cleanup() {
  result=$?
  trap - EXIT HUP INT TERM
  set +e
  if [ -n "$child" ] && kill -0 "$child" 2>/dev/null; then
    kill -TERM "$child"
    wait "$child"
  fi
  if [ "$was_running" = 1 ]; then
    /etc/init.d/S99ardor-pedal start > soak-service-restart.log 2>&1
    restored=0
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      if pidof ardor-pedal > soak-service-restored.txt; then restored=1; break; fi
      sleep 1
    done
    [ "$restored" = 1 ] || result=1
  fi
  printf '%s\n' "$result" > soak-return-code.txt
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
metadata > soak-device-before.txt
sha256sum pedal-pog3-headroom shared.wisdom \
  /opt/ardor-pedal/presets/bank-000/preset-0.json \
  /opt/ardor-pedal/models/Supro_1695TJ-P12Q_M201b_421738.nam > soak-input-sha256.txt
/etc/init.d/S99ardor-pedal stop > soak-service-stop.log 2>&1
if pidof ardor-pedal >/dev/null; then echo 'Normal service did not stop.' >&2; exit 1; fi
./pedal-pog3-headroom pipeline2 180 none 128 shared.wisdom \
  /opt/ardor-pedal two-block-soak alsa > two-block-soak.summary.csv 2> two-block-soak.log &
child=$!
result=0
wait "$child" || result=$?
child=''
printf 'label,return_code\ntwo-block-soak,%s\n' "$result" > soak-receipt.csv
metadata > soak-device-after.txt
cat two-block-soak.summary.csv
exit "$result"
