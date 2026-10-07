#!/bin/sh
set -eu

# Run over SSH: sh run-remote.sh UNIQUE_TMP_DIRECTORY [CPU]
# Or: sh run-remote.sh UNIQUE_TMP_DIRECTORY CPU --full-probes PROBE...
# The caller uploads probes and retrieves every receipt/log before cleanup.
probe_dir=$1
cpu=${2:-2}
cd "$probe_dir"
service=/etc/init.d/S99ardor-pedal
was_running=0
if pidof ardor-pedal >/dev/null 2>&1; then was_running=1; fi
cleanup() {
  if [ "$was_running" = 1 ]; then
    "$service" start > service-restart.log 2>&1 || return 1
    # The init script can return before its background child has exec'd.
    attempts=0
    while ! pidof ardor-pedal > restored-service-pid.txt; do
      attempts=$((attempts + 1))
      [ "$attempts" -lt 10 ] || return 1
      sleep 1
    done
  fi
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

metadata() {
  date -u
  uname -a
  cat /sys/firmware/devicetree/base/model
  printf '\n'
  cat /proc/cmdline
  cat /sys/class/thermal/thermal_zone0/temp
  cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
  cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq
  cat /proc/meminfo | head -4
}
metadata > device-before.txt
printf 'cpu=%s scheduler=other service_was_running=%s\n' "$cpu" "$was_running" >> device-before.txt

if [ "$was_running" = 1 ]; then "$service" stop > service-stop.log 2>&1; fi
if pidof ardor-pedal >/dev/null 2>&1; then
  echo 'live service did not stop' >&2
  exit 1
fi

# These offline loops run faster than wall-clock audio. Use SCHED_OTHER to
# avoid Linux RT-bandwidth throttling of an unpaced FIFO loop; live ALSA/FIFO
# deadline and xrun admission requires a later paced runtime test.
if [ "${3:-}" = "--full-probes" ]; then
  shift 3
  [ "$#" -gt 0 ] || exit 2
  printf 'probe,return_code\n' > receipt.csv
  for probe in "$@"; do
    result=0
    ./pog3-device-cpu "$cpu" "./$probe" > "$probe.csv" 2> "$probe.log" || result=$?
    printf '%s,%s\n' "$probe" "$result" >> receipt.csv
    metadata > "device-after-$probe.txt"
    [ "$result" = 0 ] || exit "$result"
  done
  metadata > device-after.txt
  exit 0
fi
# Numerical tests precede timing.
./pog3-device-cpu "$cpu" ./pog3-device-foundation > foundation.log 2>&1
./pog3-device-cpu "$cpu" ./pog3-device-quality --short-hold > quality.log 2>&1
printf 'pass,backend,callback,workload,return_code\n' > receipt.csv
for pass in 1 2; do
  backends='ardor erb-cadence-count-32 erb-effects-ownership-32 erb-effects-attack-32 erb-effects-freeze-32 erb-effects-gliss-32'
  callbacks='64 128'
  workloads='static dynamic events'
  if [ "$pass" = 2 ]; then
    backends='erb-effects-gliss-32 erb-effects-freeze-32 erb-effects-attack-32 erb-effects-ownership-32 erb-cadence-count-32 ardor'
    callbacks='128 64'
    workloads='events dynamic static'
  fi
  for backend in $backends; do
    for callback in $callbacks; do
      for workload in $workloads; do
        label="$pass-$backend-$callback-$workload"
        result=0
        ./pog3-device-cpu "$cpu" ./pog3-device-trial "$backend" 8 "$callback" "$workload" > "$label.csv" 2> "$label.log" || result=$?
        printf '%s,%s,%s,%s,%s\n' "$pass" "$backend" "$callback" "$workload" "$result" >> receipt.csv
        [ "$result" = 0 ] || exit "$result"
      done
    done
  done
  # Separate full processor suite; fixtures differ from the ERB stage trial.
  ./pog3-device-cpu "$cpu" ./pog3-device-full > "full-$pass.csv" 2> "full-$pass.log"
  metadata > "device-after-pass-$pass.txt"
done
metadata > device-after.txt
