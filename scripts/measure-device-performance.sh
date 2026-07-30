#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
remote_probe="$script_dir/device-performance-remote.sh"

usage() {
  cat <<'EOF'
Usage:
  scripts/measure-device-performance.sh [options] <pi-host-or-ip>

Options:
  --duration N        Measurement duration in seconds. Default: 10
  --interval N        Temperature/frequency sample interval. Default: 1
  --audio-cpu N       Expected audio callback CPU. Default: 2
  --worker-cpu N      Expected parallel-rig worker CPU. Default: 3
  --require-worker    Fail unless a Dual Amp/Rig worker is currently active
  --process NAME      Target process name. Default: ardor-pedal
  --telemetry-file P  Runtime telemetry snapshot. Default: /run/ardor-pedal.telemetry
  -h, --help          Show this help

Environment:
  ARDOR_PI_HOST       Host/IP when no positional host is passed
  ARDOR_SSH_USER      SSH user on the pedal. Default: root
  ARDOR_SSH_OPTS      Extra options passed to ssh

The script never stores a password. Configure an SSH key or enter the device
password at the normal OpenSSH prompt.
EOF
}

die() {
  echo "measure-device-performance: $*" >&2
  exit 2
}

duration=10
interval=1
audio_cpu=2
worker_cpu=3
require_worker=0
process_name=ardor-pedal
telemetry_file=/run/ardor-pedal.telemetry
host=

while [ "$#" -gt 0 ]; do
  case "$1" in
    --duration)
      [ "$#" -ge 2 ] || die "--duration requires a value"
      duration=$2
      shift 2
      ;;
    --interval)
      [ "$#" -ge 2 ] || die "--interval requires a value"
      interval=$2
      shift 2
      ;;
    --audio-cpu)
      [ "$#" -ge 2 ] || die "--audio-cpu requires a value"
      audio_cpu=$2
      shift 2
      ;;
    --worker-cpu)
      [ "$#" -ge 2 ] || die "--worker-cpu requires a value"
      worker_cpu=$2
      shift 2
      ;;
    --require-worker)
      require_worker=1
      shift
      ;;
    --process)
      [ "$#" -ge 2 ] || die "--process requires a value"
      process_name=$2
      shift 2
      ;;
    --telemetry-file)
      [ "$#" -ge 2 ] || die "--telemetry-file requires a value"
      telemetry_file=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      die "unknown option: $1"
      ;;
    *)
      [ -z "$host" ] || die "expected only one host/IP argument"
      host=$1
      shift
      ;;
  esac
done

host=${host:-${ARDOR_PI_HOST:-}}
[ -n "$host" ] || {
  usage >&2
  exit 2
}
[ -r "$remote_probe" ] || die "missing remote probe: $remote_probe"

ssh_user=${ARDOR_SSH_USER:-root}
ssh_opts=${ARDOR_SSH_OPTS:-}
ssh_target="$ssh_user@$host"

echo "Measuring $process_name on $ssh_target"
# ARDOR_SSH_OPTS is intentionally split into separate ssh arguments.
# shellcheck disable=SC2086
ssh $ssh_opts "$ssh_target" 'sh -s' \
  "$duration" "$interval" "$process_name" "$audio_cpu" "$worker_cpu" \
  "$require_worker" "$telemetry_file" < "$remote_probe"
