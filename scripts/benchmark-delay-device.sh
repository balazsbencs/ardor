#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
version_file="$repo_dir/buildroot/buildroot-version.env"

[ -f "$version_file" ] || {
  echo "benchmark-delay-device: missing version manifest: $version_file" >&2
  exit 1
}
# shellcheck disable=SC1090
. "$version_file"

usage() {
  cat <<'EOF'
Usage:
  scripts/benchmark-delay-device.sh [options] <pi-host-or-ip>

Options:
  --block-sizes LIST  Comma-separated frame counts. Default: 32,64
  --warmup N          Untimed blocks per case. Default: 500
  --iterations N      Timed blocks per case. Default: 10000
  --cpu N             Pi CPU used by the benchmark. Default: 2
  --output PATH       Local timing CSV output path
  --quality PATH      Local quality CSV output path
  --renders DIR       Retrieve float WAV listening renders into DIR
  -h, --help          Show this help

Environment:
  ARDOR_PI_HOST          Host/IP when no positional host is passed
  ARDOR_SSH_USER         SSH user on the pedal. Default: root
  ARDOR_SSH_OPTS         Extra options passed to ssh/scp
  ARDOR_SKIP_BUILD=1     Reuse ./pedal-delay-bench-pi and ./pedal-delay-quality-pi
  ARDOR_BUILDROOT_VOLUME Buildroot Docker volume
  ARDOR_DOCKER_IMAGE     Build container. Default: ubuntu:24.04

The live pedal service is stopped for the isolated run and restarted by a
remote EXIT trap. Binaries and renders live only under /tmp on the device.
EOF
}

die() {
  echo "benchmark-delay-device: $*" >&2
  exit 2
}

block_sizes=32,64
warmup=500
iterations=10000
cpu=2
output=
quality=
renders=
host=

while [ "$#" -gt 0 ]; do
  case "$1" in
    --block-sizes) [ "$#" -ge 2 ] || die "--block-sizes requires a value"; block_sizes=$2; shift 2 ;;
    --warmup) [ "$#" -ge 2 ] || die "--warmup requires a value"; warmup=$2; shift 2 ;;
    --iterations) [ "$#" -ge 2 ] || die "--iterations requires a value"; iterations=$2; shift 2 ;;
    --cpu) [ "$#" -ge 2 ] || die "--cpu requires a value"; cpu=$2; shift 2 ;;
    --output) [ "$#" -ge 2 ] || die "--output requires a value"; output=$2; shift 2 ;;
    --quality) [ "$#" -ge 2 ] || die "--quality requires a value"; quality=$2; shift 2 ;;
    --renders) [ "$#" -ge 2 ] || die "--renders requires a value"; renders=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*) die "unknown option: $1" ;;
    *) [ -z "$host" ] || die "expected only one host/IP"; host=$1; shift ;;
  esac
done

host=${host:-${ARDOR_PI_HOST:-}}
[ -n "$host" ] || { usage >&2; exit 2; }

benchmark_bin="$repo_dir/pedal-delay-bench-pi"
quality_bin="$repo_dir/pedal-delay-quality-pi"
if [ "${ARDOR_SKIP_BUILD:-0}" != "1" ]; then
  : "${BUILDROOT_VERSION:?BUILDROOT_VERSION is required}"
  volume=${ARDOR_BUILDROOT_VOLUME:-$BUILDROOT_DOCKER_VOLUME}
  image=${ARDOR_DOCKER_IMAGE:-ubuntu:24.04}
  docker run --rm \
    -v "$volume:/buildroot" \
    -v "$repo_dir:/ardor" \
    -w /buildroot \
    -e FORCE_UNSAFE_CONFIGURE=1 \
    "$image" bash -lc '
      set -eu
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq
      apt-get install -y -qq build-essential git curl wget rsync cpio unzip bc \
        python3 python3-dev file pkg-config libssl-dev libelf-dev >/dev/null
      make raspberrypi4_ardor_pedal_defconfig BR2_EXTERNAL=/ardor/buildroot/external
      make ardor-pedal-dirclean BR2_EXTERNAL=/ardor/buildroot/external
      make ardor-pedal BR2_EXTERNAL=/ardor/buildroot/external
      cp output/build/ardor-pedal-1.0/pedal-delay-bench /ardor/pedal-delay-bench-pi
      cp output/build/ardor-pedal-1.0/pedal-delay-quality /ardor/pedal-delay-quality-pi
    '
fi
[ -x "$benchmark_bin" ] || die "benchmark binary is missing: $benchmark_bin"
[ -x "$quality_bin" ] || die "quality binary is missing: $quality_bin"

stamp=$(date +%Y%m%d-%H%M%S)
output=${output:-$repo_dir/benchmark-results/delay-pi4-$stamp.csv}
quality=${quality:-$repo_dir/benchmark-results/delay-quality-pi4-$stamp.csv}
mkdir -p "$(dirname -- "$output")" "$(dirname -- "$quality")"
if [ -n "$renders" ]; then mkdir -p "$renders"; fi

ssh_user=${ARDOR_SSH_USER:-root}
ssh_opts=${ARDOR_SSH_OPTS:-}
ssh_target="$ssh_user@$host"
remote_dir=/tmp/ardor-delay-bench

echo "Uploading delay probes to $ssh_target"
# shellcheck disable=SC2086
ssh $ssh_opts "$ssh_target" "rm -rf '$remote_dir' && mkdir -p '$remote_dir/renders'"
# shellcheck disable=SC2086
scp -O $ssh_opts "$benchmark_bin" "$quality_bin" "$ssh_target:$remote_dir/"

echo "Running isolated delay quality and timing probes"
# shellcheck disable=SC2086
ssh $ssh_opts "$ssh_target" 'sh -s' \
  "$remote_dir" "$block_sizes" "$warmup" "$iterations" "$cpu" <<'REMOTE'
set -eu
remote_dir=$1
block_sizes=$2
warmup=$3
iterations=$4
cpu=$5
service=/etc/init.d/S99ardor-pedal
was_running=0
governors="$remote_dir/governors"
: > "$governors"

cleanup() {
  while IFS=' ' read -r policy governor; do
    [ -n "$policy" ] && [ -w "$policy/scaling_governor" ] && echo "$governor" > "$policy/scaling_governor" || true
  done < "$governors"
  if [ "$was_running" = 1 ]; then "$service" start >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT HUP INT TERM

if pidof ardor-pedal >/dev/null 2>&1; then
  was_running=1
  "$service" stop >/dev/null
fi
for policy in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -r "$policy/scaling_governor" ] || continue
  echo "$policy $(cat "$policy/scaling_governor")" >> "$governors"
  [ -w "$policy/scaling_governor" ] && echo performance > "$policy/scaling_governor"
done

uname -a > "$remote_dir/device.txt"
cat /sys/firmware/devicetree/base/model >> "$remote_dir/device.txt" 2>/dev/null || true
printf '\n' >> "$remote_dir/device.txt"
cat /sys/class/thermal/thermal_zone0/temp >> "$remote_dir/device.txt" 2>/dev/null || true

chrt -f 60 "$remote_dir/pedal-delay-quality-pi" --render-dir "$remote_dir/renders" \
  > "$remote_dir/quality.csv"
chrt -f 60 "$remote_dir/pedal-delay-bench-pi" \
  --cpu "$cpu" --block-sizes "$block_sizes" --warmup "$warmup" --iterations "$iterations" \
  > "$remote_dir/benchmark.csv"
REMOTE

# shellcheck disable=SC2086
scp -O $ssh_opts "$ssh_target:$remote_dir/benchmark.csv" "$output"
# shellcheck disable=SC2086
scp -O $ssh_opts "$ssh_target:$remote_dir/quality.csv" "$quality"
# shellcheck disable=SC2086
scp -O $ssh_opts "$ssh_target:$remote_dir/device.txt" "${output%.csv}-device.txt"
if [ -n "$renders" ]; then
  # shellcheck disable=SC2086
  scp -O $ssh_opts "$ssh_target:$remote_dir/renders/"'*.wav' "$renders/"
fi
# shellcheck disable=SC2086
ssh $ssh_opts "$ssh_target" "rm -rf '$remote_dir'"

echo "Timing CSV: $output"
echo "Quality CSV: $quality"
python3 "$script_dir/analyze-delay-benchmark.py" "$output"
