#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
version_file="$repo_dir/buildroot/buildroot-version.env"

[ -f "$version_file" ] || {
  echo "benchmark-nam-device: missing version manifest: $version_file" >&2
  exit 1
}
# shellcheck disable=SC1090
. "$version_file"

usage() {
  cat <<'EOF'
Usage:
  scripts/benchmark-nam-device.sh [options] <pi-host-or-ip>

Options:
  --block-sizes LIST  Comma-separated frame counts. Default: 8,16,32,64,128
  --warmup N          Untimed blocks per case. Default: 200
  --iterations N      Timed blocks per case. Default: 2000
  --tiers MODE        all, full, or nano. Default: all
  --cpu N             Pi CPU used by the benchmark. Default: 2
  --output PATH       Local CSV output path
  -h, --help          Show this help

Environment:
  ARDOR_PI_HOST          Host/IP when no positional host is passed
  ARDOR_SSH_USER         SSH user on the pedal. Default: root
  ARDOR_SSH_OPTS         Extra options passed to ssh/scp
  ARDOR_SKIP_BUILD=1     Reuse ./pedal-nam-bench-pi
  ARDOR_BUILDROOT_VOLUME Buildroot Docker volume
  ARDOR_DOCKER_IMAGE     Build container. Default: ubuntu:24.04

The live pedal service is stopped for the isolated benchmark and restarted by
a remote EXIT trap. Models and the benchmark executable live only under /tmp.
The script never stores a password; use an SSH key or the OpenSSH prompt.
EOF
}

die() {
  echo "benchmark-nam-device: $*" >&2
  exit 2
}

block_sizes=8,16,32,64,128
warmup=200
iterations=2000
tiers=all
cpu=2
output=
host=

while [ "$#" -gt 0 ]; do
  case "$1" in
    --block-sizes) [ "$#" -ge 2 ] || die "--block-sizes requires a value"; block_sizes=$2; shift 2 ;;
    --warmup) [ "$#" -ge 2 ] || die "--warmup requires a value"; warmup=$2; shift 2 ;;
    --iterations) [ "$#" -ge 2 ] || die "--iterations requires a value"; iterations=$2; shift 2 ;;
    --tiers) [ "$#" -ge 2 ] || die "--tiers requires a value"; tiers=$2; shift 2 ;;
    --cpu) [ "$#" -ge 2 ] || die "--cpu requires a value"; cpu=$2; shift 2 ;;
    --output) [ "$#" -ge 2 ] || die "--output requires a value"; output=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*) die "unknown option: $1" ;;
    *) [ -z "$host" ] || die "expected only one host/IP"; host=$1; shift ;;
  esac
done

host=${host:-${ARDOR_PI_HOST:-}}
[ -n "$host" ] || { usage >&2; exit 2; }
case "$tiers" in all|full|nano) ;; *) die "--tiers must be all, full, or nano" ;; esac

models_dir="$repo_dir/models"
model_count=$(find "$models_dir" -type f -name '*.nam' ! -name '._*' | wc -l)
[ "$model_count" -gt 0 ] || die "no NAM files found under $models_dir"

benchmark_bin="$repo_dir/pedal-nam-bench-pi"
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
      cp output/build/ardor-pedal-1.0/pedal-nam-bench /ardor/pedal-nam-bench-pi
    '
fi
[ -x "$benchmark_bin" ] || die "benchmark binary is missing: $benchmark_bin"

if [ -z "$output" ]; then
  output="$repo_dir/nam-benchmark-$(date +%Y%m%d-%H%M%S).csv"
fi

ssh_user=${ARDOR_SSH_USER:-root}
ssh_opts=${ARDOR_SSH_OPTS:-}
ssh_target="$ssh_user@$host"
remote_dir=/tmp/ardor-nam-bench

echo "Uploading benchmark and $model_count models to $ssh_target"
# shellcheck disable=SC2086
ssh $ssh_opts "$ssh_target" "rm -rf '$remote_dir' && mkdir -p '$remote_dir'"
# shellcheck disable=SC2086
scp -O $ssh_opts "$benchmark_bin" "$ssh_target:$remote_dir/pedal-nam-bench"
tar -C "$repo_dir" --exclude='._*' --exclude='.DS_Store' -cf - models \
  | ssh $ssh_opts "$ssh_target" "tar -C '$remote_dir' -xf -"

echo "Running isolated model sweep; CSV will be written to $output"
# shellcheck disable=SC2086
ssh $ssh_opts "$ssh_target" 'sh -s' \
  "$remote_dir" "$block_sizes" "$warmup" "$iterations" "$tiers" "$cpu" > "$output" <<'REMOTE'
set -eu
remote_dir=$1
block_sizes=$2
warmup=$3
iterations=$4
tiers=$5
cpu=$6
service=/etc/init.d/S99ardor-pedal
was_running=0

cleanup() {
  rm -rf "$remote_dir"
  if [ "$was_running" = 1 ]; then
    "$service" start >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT HUP INT TERM

if pidof ardor-pedal >/dev/null 2>&1; then
  was_running=1
  "$service" stop >/dev/null
  sleep 1
fi
for policy in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
  [ -w "$policy" ] && echo performance > "$policy"
done

chrt -f 60 "$remote_dir/pedal-nam-bench" \
  --cpu "$cpu" \
  --block-sizes "$block_sizes" \
  --warmup "$warmup" \
  --iterations "$iterations" \
  --tiers "$tiers" \
  "$remote_dir/models"
REMOTE

echo "Benchmark complete: $output"
python3 "$script_dir/analyze-nam-benchmark.py" "$output"
