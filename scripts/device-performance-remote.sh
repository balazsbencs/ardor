#!/bin/sh
#
# BusyBox-compatible runtime probe for ardor-pedal. This script is normally
# streamed to the device by measure-device-performance.sh; it may also be
# copied to the pedal and run there directly.

set -eu

duration=${1:-10}
interval=${2:-1}
process_name=${3:-ardor-pedal}
audio_cpu=${4:-2}
worker_cpu=${5:-3}
require_worker=${6:-0}
telemetry_file=${7:-/run/ardor-pedal.telemetry}

usage_error() {
  echo "device-performance: $*" >&2
  exit 2
}

is_uint() {
  case "$1" in
    ''|*[!0-9]*) return 1 ;;
    *) return 0 ;;
  esac
}

is_uint "$duration" || usage_error "duration must be a positive integer"
is_uint "$interval" || usage_error "interval must be a positive integer"
is_uint "$audio_cpu" || usage_error "audio CPU must be a non-negative integer"
is_uint "$worker_cpu" || usage_error "worker CPU must be a non-negative integer"
[ "$duration" -gt 0 ] || usage_error "duration must be greater than zero"
[ "$interval" -gt 0 ] || usage_error "interval must be greater than zero"
[ "$audio_cpu" -ne "$worker_cpu" ] || usage_error "audio and worker CPUs must differ"
case "$require_worker" in
  0|1) ;;
  *) usage_error "require-worker must be 0 or 1" ;;
esac

app_pid=$(pidof "$process_name" 2>/dev/null | awk '{print $1}')
[ -n "$app_pid" ] || {
  echo "device-performance: process not running: $process_name" >&2
  exit 1
}

tmp_dir=$(mktemp -d /tmp/ardor-performance.XXXXXX)
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT HUP INT TERM

snapshot_cores() {
  awk '
    /^cpu[0-9]+ / {
      total = 0
      for (field = 2; field <= NF; ++field) total += $field
      idle = $5 + $6
      cpu = $1
      sub(/^cpu/, "", cpu)
      print cpu, total, idle
    }
  ' /proc/stat > "$1"
}

snapshot_threads() {
  destination=$1
  : > "$destination"
  [ -d "/proc/$app_pid/task" ] || return 1

  for task_dir in /proc/"$app_pid"/task/*; do
    [ -d "$task_dir" ] || continue
    tid=${task_dir##*/}
    [ -r "$task_dir/stat" ] || continue
    [ -r "$task_dir/sched" ] || continue
    [ -r "$task_dir/status" ] || continue

    exec_ms=$(awk '/^se.sum_exec_runtime/ {print $3; exit}' "$task_dir/sched")
    migrations=$(awk '/^se.nr_migrations/ {print $3; exit}' "$task_dir/sched")
    affinity=$(awk '/^Cpus_allowed_list:/ {print $2; exit}' "$task_dir/status")
    name=$(awk '/^Name:/ {print $2; exit}' "$task_dir/status")
    last_cpu=$(awk '{print $39}' "$task_dir/stat")
    rt_priority=$(awk '{print $40}' "$task_dir/stat")
    policy=$(awk '{print $41}' "$task_dir/stat")

    [ -n "$exec_ms" ] || continue
    [ -n "$migrations" ] || migrations=0
    [ -n "$affinity" ] || affinity=unknown
    [ -n "$name" ] || name=unknown
    [ -n "$last_cpu" ] || last_cpu=unknown
    [ -n "$rt_priority" ] || rt_priority=unknown
    [ -n "$policy" ] || policy=unknown

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$tid" "$exec_ms" "$last_cpu" "$affinity" "$policy" \
      "$rt_priority" "$migrations" "$name" >> "$destination"
  done
}

snapshot_telemetry() {
  [ -r "$telemetry_file" ] || return 1
  cp "$telemetry_file" "$1"
}

telemetry_metric() {
  awk -v key="$2" '
    {
      for (field = 1; field <= NF; ++field) {
        split($field, pair, "=")
        if (pair[1] == key) {
          sub(/ms$/, "", pair[2])
          print pair[2]
          exit
        }
      }
    }
  ' "$1"
}

temperature_milli_c() {
  if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
    cat /sys/class/thermal/thermal_zone0/temp
  else
    echo 0
  fi
}

current_frequency_khz() {
  frequency_file=
  for candidate in \
    /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq \
    /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq; do
    if [ -r "$candidate" ]; then
      frequency_file=$candidate
      break
    fi
  done
  if [ -n "$frequency_file" ]; then
    cat "$frequency_file"
  else
    echo 0
  fi
}

policy_name() {
  case "$1" in
    0) echo OTHER ;;
    1) echo FIFO ;;
    2) echo RR ;;
    *) echo "$1" ;;
  esac
}

command_line=$(tr '\000' ' ' < "/proc/$app_pid/cmdline")
start_uptime=$(awk '{print $1}' /proc/uptime)
snapshot_cores "$tmp_dir/cores.start"
snapshot_threads "$tmp_dir/threads.start"
telemetry_at_start=0
if snapshot_telemetry "$tmp_dir/telemetry.start"; then
  telemetry_at_start=1
fi

start_temp=$(temperature_milli_c)
max_temp=$start_temp
start_frequency=$(current_frequency_khz)
min_frequency=$start_frequency
max_frequency=$start_frequency

echo "Ardor pedal performance probe"
echo "  pid:        $app_pid"
echo "  command:    $command_line"
echo "  sample:     ${duration}s (${interval}s interval)"
printf "  temperature at start: %.1f C\n" "$(awk -v value="$start_temp" 'BEGIN {print value / 1000.0}')"
if [ "$start_frequency" -gt 0 ] 2>/dev/null; then
  printf "  CPU clock at start:   %.0f MHz\n" "$(awk -v value="$start_frequency" 'BEGIN {print value / 1000.0}')"
fi
echo

remaining=$duration
printf "Sampling"
while [ "$remaining" -gt 0 ]; do
  step=$interval
  if [ "$step" -gt "$remaining" ]; then
    step=$remaining
  fi
  sleep "$step"
  remaining=$((remaining - step))

  sample_temp=$(temperature_milli_c)
  sample_frequency=$(current_frequency_khz)
  if [ "$sample_temp" -gt "$max_temp" ] 2>/dev/null; then
    max_temp=$sample_temp
  fi
  if [ "$sample_frequency" -gt 0 ] 2>/dev/null; then
    if [ "$min_frequency" -eq 0 ] 2>/dev/null || [ "$sample_frequency" -lt "$min_frequency" ]; then
      min_frequency=$sample_frequency
    fi
    if [ "$sample_frequency" -gt "$max_frequency" ]; then
      max_frequency=$sample_frequency
    fi
  fi
  printf "."
done
echo

current_pid=$(pidof "$process_name" 2>/dev/null | awk '{print $1}')
if [ "$current_pid" != "$app_pid" ]; then
  echo "FAIL: $process_name restarted during the sample (was $app_pid, now ${current_pid:-stopped})" >&2
  exit 1
fi

snapshot_cores "$tmp_dir/cores.end"
snapshot_threads "$tmp_dir/threads.end"
telemetry_ready=0
if [ "$telemetry_at_start" = 1 ] && snapshot_telemetry "$tmp_dir/telemetry.end"; then
  telemetry_ready=1
fi
end_uptime=$(awk '{print $1}' /proc/uptime)
elapsed=$(awk -v start="$start_uptime" -v end="$end_uptime" 'BEGIN {print end - start}')

echo
echo "Per-core utilization"
echo "  core   busy     headroom"
awk '
  NR == FNR {
    start_total[$1] = $2
    start_idle[$1] = $3
    next
  }
  {
    total_delta = $2 - start_total[$1]
    idle_delta = $3 - start_idle[$1]
    busy = total_delta > 0 ? (total_delta - idle_delta) * 100.0 / total_delta : 0
    printf "  CPU%-2s %6.1f%%   %6.1f%%\n", $1, busy, 100.0 - busy
  }
' "$tmp_dir/cores.start" "$tmp_dir/cores.end"

echo
echo "Ardor threads"
echo "  role       tid     CPU%   last  affinity  policy/prio  migrations"
awk -v elapsed="$elapsed" -v main_pid="$app_pid" \
    -v audio_cpu="$audio_cpu" -v worker_cpu="$worker_cpu" '
  NR == FNR {
    start_exec[$1] = $2
    start_migrations[$1] = $7
    next
  }
  ($1 in start_exec) {
    cpu = elapsed > 0 ? ($2 - start_exec[$1]) * 100.0 / (elapsed * 1000.0) : 0
    migrations = $7 - start_migrations[$1]
    role = "other"
    if ($1 == main_pid) role = "UI/main"
    if ($4 == audio_cpu) role = "audio"
    if ($4 == worker_cpu) role = "rig-worker"
    policy = $5 == 0 ? "OTHER" : ($5 == 1 ? "FIFO" : ($5 == 2 ? "RR" : $5))
    printf "  %-10s %-6s %6.1f%%   %-4s  %-8s  %s/%-4s      %s\n",
           role, $1, cpu, $3, $4, policy, $6, migrations
    total_cpu += cpu
  }
  END {
    printf "  %-10s %-6s %6.1f%%\n", "TOTAL", "-", total_cpu
  }
' "$tmp_dir/threads.start" "$tmp_dir/threads.end"

echo
echo "Audio callback telemetry"
telemetry_failures=0
if [ "$telemetry_ready" = 1 ]; then
  callbacks_start=$(telemetry_metric "$tmp_dir/telemetry.start" callbacks)
  callbacks_end=$(telemetry_metric "$tmp_dir/telemetry.end" callbacks)
  over_start=$(telemetry_metric "$tmp_dir/telemetry.start" over)
  over_end=$(telemetry_metric "$tmp_dir/telemetry.end" over)
  gaps_start=$(telemetry_metric "$tmp_dir/telemetry.start" gaps)
  gaps_end=$(telemetry_metric "$tmp_dir/telemetry.end" gaps)
  worker_over_start=$(telemetry_metric "$tmp_dir/telemetry.start" worker_over)
  worker_over_end=$(telemetry_metric "$tmp_dir/telemetry.end" worker_over)
  nonfinite_start=$(telemetry_metric "$tmp_dir/telemetry.start" nonfinite)
  nonfinite_end=$(telemetry_metric "$tmp_dir/telemetry.end" nonfinite)
  mismatch_start=$(telemetry_metric "$tmp_dir/telemetry.start" block_mismatch)
  mismatch_end=$(telemetry_metric "$tmp_dir/telemetry.end" block_mismatch)
  average_ms=$(telemetry_metric "$tmp_dir/telemetry.end" avg)
  maximum_ms=$(telemetry_metric "$tmp_dir/telemetry.end" max)
  budget_ms=$(telemetry_metric "$tmp_dir/telemetry.end" budget)
  bypassed=$(telemetry_metric "$tmp_dir/telemetry.end" bypassed)

  callback_delta=$((callbacks_end - callbacks_start))
  over_delta=$((over_end - over_start))
  gap_delta=$((gaps_end - gaps_start))
  worker_over_delta=$((worker_over_end - worker_over_start))
  nonfinite_delta=$((nonfinite_end - nonfinite_start))
  mismatch_delta=$((mismatch_end - mismatch_start))
  average_budget_percent=$(awk -v average="$average_ms" -v budget="$budget_ms" \
    'BEGIN {
      if (budget > 0) printf "%.6f\n", average * 100.0 / budget
      else print 0
    }')

  printf "  callbacks:       %s during sample\n" "$callback_delta"
  printf "  callback time:   avg %.2f ms (%5.1f%% budget), max %.2f ms, budget %.2f ms\n" \
    "$average_ms" "$average_budget_percent" "$maximum_ms" "$budget_ms"
  printf "  deadline faults: callback_over=%s gaps=%s worker_over=%s\n" \
    "$over_delta" "$gap_delta" "$worker_over_delta"
  printf "  DSP faults:      nonfinite=%s block_mismatch=%s bypassed=%s\n" \
    "$nonfinite_delta" "$mismatch_delta" "$bypassed"

  if [ "$over_delta" -gt 0 ] || [ "$gap_delta" -gt 0 ] \
      || [ "$worker_over_delta" -gt 0 ] || [ "$nonfinite_delta" -gt 0 ] \
      || [ "$mismatch_delta" -gt 0 ] || [ "$bypassed" -ne 0 ]; then
    echo "  AUDIO HEALTH: FAIL"
    telemetry_failures=1
  else
    echo "  AUDIO HEALTH: PASS"
  fi
else
  echo "  UNAVAILABLE: $telemetry_file was not readable at both sample boundaries"
  echo "  Deploy a build configured with --telemetry-file to enable deadline checks."
fi

echo
echo "Scheduling checks"
failures=0

audio_record=$(awk -v cpu="$audio_cpu" '$4 == cpu {print; exit}' "$tmp_dir/threads.end")
if [ -z "$audio_record" ]; then
  echo "  FAIL audio: no thread is pinned to CPU $audio_cpu"
  failures=$((failures + 1))
else
  audio_tid=$(echo "$audio_record" | awk '{print $1}')
  audio_policy=$(echo "$audio_record" | awk '{print $5}')
  audio_priority=$(echo "$audio_record" | awk '{print $6}')
  if [ "$audio_policy" = 1 ] && [ "$audio_priority" = 70 ]; then
    echo "  PASS audio: TID $audio_tid, CPU $audio_cpu, $(policy_name "$audio_policy")/$audio_priority"
  else
    echo "  FAIL audio: TID $audio_tid is on CPU $audio_cpu but policy/prio is $(policy_name "$audio_policy")/$audio_priority"
    failures=$((failures + 1))
  fi
fi

worker_record=$(awk -v cpu="$worker_cpu" '$4 == cpu {print; exit}' "$tmp_dir/threads.end")
if [ -n "$worker_record" ]; then
  worker_tid=$(echo "$worker_record" | awk '{print $1}')
  worker_policy=$(echo "$worker_record" | awk '{print $5}')
  worker_priority=$(echo "$worker_record" | awk '{print $6}')
  if [ "$worker_policy" = 1 ] && [ "$worker_priority" = 69 ]; then
    echo "  PASS worker: TID $worker_tid, CPU $worker_cpu, $(policy_name "$worker_policy")/$worker_priority"
  else
    echo "  FAIL worker: TID $worker_tid is on CPU $worker_cpu but policy/prio is $(policy_name "$worker_policy")/$worker_priority"
    failures=$((failures + 1))
  fi
else
  case " $command_line " in
    *" --parallel-rigs "*)
      if [ "$require_worker" = 1 ]; then
        echo "  FAIL worker: parallel mode is configured, but no worker exists; activate a Dual Amp/Rig preset"
        failures=$((failures + 1))
      else
        echo "  IDLE worker: parallel mode is configured; activate a Dual Amp/Rig preset to create the CPU $worker_cpu thread"
      fi
      ;;
    *)
      echo "  FAIL worker: --parallel-rigs is missing from the process command line"
      failures=$((failures + 1))
      ;;
  esac
fi

echo
end_temp=$(temperature_milli_c)
if [ "$end_temp" -gt "$max_temp" ] 2>/dev/null; then
  max_temp=$end_temp
fi
printf "Thermal: start %.1f C, end %.1f C, sampled max %.1f C\n" \
  "$(awk -v value="$start_temp" 'BEGIN {print value / 1000.0}')" \
  "$(awk -v value="$end_temp" 'BEGIN {print value / 1000.0}')" \
  "$(awk -v value="$max_temp" 'BEGIN {print value / 1000.0}')"
if [ "$max_frequency" -gt 0 ] 2>/dev/null; then
  printf "CPU clock range: %.0f-%.0f MHz\n" \
    "$(awk -v value="$min_frequency" 'BEGIN {print value / 1000.0}')" \
    "$(awk -v value="$max_frequency" 'BEGIN {print value / 1000.0}')"
fi

failures=$((failures + telemetry_failures))
if [ "$failures" -gt 0 ]; then
  echo "RESULT: FAIL ($failures check(s))"
  exit 1
fi

echo "RESULT: PASS"
if [ "$telemetry_ready" != 1 ]; then
  echo "CPU and scheduling passed; audio deadline health was not available."
fi
