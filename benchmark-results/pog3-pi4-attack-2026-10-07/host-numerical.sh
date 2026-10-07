#!/bin/sh
set -eu
export TMPDIR=/home/bbalazs/.cache/ardor-pog3-device-build-20261007/compiler-tmp
repo=/home/bbalazs/projects/ardor-pog3
probe=/tmp/ardor-pog3-device-build
out=$repo/benchmark-results/pog3-pi4-attack-2026-10-07
builder=$repo/benchmark-results/pog3-pi4-render-schedule-2026-10-07/build-automation.sh
json=/home/bbalazs/projects/ardor/build-ci/_deps/neuralampmodelercore-src/Dependencies
fftinc=/tmp/pog3-vector-fft/fftw-root/usr/include
fftlib=/usr/lib/x86_64-linux-gnu/libfftw3f.so.3
for mode in default optin; do
  freeze=0
  baseline=$probe/attack-baseline-host/libardor_pog3.a
  fallback=$probe/attack-baseline-host/libardor_realtime_fft.a
  candidate=$probe/attack-candidate-host/libdevice_pog3.a
  if [ "$mode" = optin ]; then
    freeze=1
    baseline=$probe/attack-baseline-optin/libdevice_pog3.a
    fallback=
    candidate=$probe/attack-candidate-optin/libdevice_pog3.a
  fi
  sh "$builder" c++ "$probe/attack-baseline-source" "$json" "$fftinc" "$baseline" "$fftlib" "$probe/attack-automation-baseline-$mode-host" "$freeze" "$fallback"
  sh "$builder" c++ "$repo" "$json" "$fftinc" "$candidate" "$fftlib" "$probe/attack-automation-candidate-$mode-host" "$freeze"
  "$probe/attack-automation-baseline-$mode-host" "$probe/phase-host-shared.wisdom" > "$probe/attack-automation-baseline-$mode-host.f32" 2> "$out/host-automation-baseline-$mode.log"
  "$probe/attack-automation-candidate-$mode-host" "$probe/phase-host-shared.wisdom" > "$probe/attack-automation-candidate-$mode-host.f32" 2> "$out/host-automation-candidate-$mode.log"
  cmp "$probe/attack-automation-baseline-$mode-host.f32" "$probe/attack-automation-candidate-$mode-host.f32"
  sha256sum "$probe/attack-automation-baseline-$mode-host.f32" "$probe/attack-automation-candidate-$mode-host.f32"
  wc -c "$probe/attack-automation-baseline-$mode-host.f32" "$probe/attack-automation-candidate-$mode-host.f32"
done
"$probe/attack-baseline-host/pedal-pog3-bench" --freeze-trace "$probe/phase-host-shared.wisdom" "$probe/attack-host-full-base.f32" 2> "$out/host-full-baseline.log"
"$probe/attack-candidate-host/pog3-device-full" --freeze-trace "$probe/phase-host-shared.wisdom" "$probe/attack-host-full-candidate.f32" 2> "$out/host-full-candidate.log"
cmp "$probe/attack-host-full-base.f32" "$probe/attack-host-full-candidate.f32"
sha256sum "$probe/attack-host-full-base.f32" "$probe/attack-host-full-candidate.f32"
wc -c "$probe/attack-host-full-base.f32" "$probe/attack-host-full-candidate.f32"
