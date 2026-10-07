#!/bin/sh
set -eu
export TMPDIR=/home/bbalazs/.cache/ardor-pog3-device-build-20261007/compiler-tmp
repo=/home/bbalazs/projects/ardor-pog3
probe=/tmp/ardor-pog3-device-build
out=$repo/benchmark-results/pog3-pi4-attack-gain-2026-10-07
builder=$repo/benchmark-results/pog3-pi4-render-schedule-2026-10-07/build-automation.sh
json=/home/bbalazs/projects/ardor/build-ci/_deps/neuralampmodelercore-src/Dependencies
fftinc=/tmp/pog3-vector-fft/fftw-root/usr/include
fftlib=/usr/lib/x86_64-linux-gnu/libfftw3f.so.3
for mode in host optin; do
  freeze=0
  if [ "$mode" = optin ]; then freeze=1; fi
  for build in baseline candidate; do
    root=$repo
    if [ "$build" = baseline ]; then root=$probe/gain-baseline-source; fi
    sh "$builder" c++ "$root" "$json" "$fftinc" "$probe/gain-$build-$mode/libdevice_pog3.a" "$fftlib" "$probe/gain-automation-$build-$mode" "$freeze"
    "$probe/gain-automation-$build-$mode" "$probe/phase-host-shared.wisdom" > "$probe/gain-automation-$build-$mode.f32" 2> "$out/host-automation-$build-$mode.log"
  done
  cmp "$probe/gain-automation-baseline-$mode.f32" "$probe/gain-automation-candidate-$mode.f32"
  sha256sum "$probe/gain-automation-baseline-$mode.f32" "$probe/gain-automation-candidate-$mode.f32"
  wc -c "$probe/gain-automation-baseline-$mode.f32" "$probe/gain-automation-candidate-$mode.f32"
done
"$probe/gain-baseline-host/full-baseline" --freeze-trace "$probe/phase-host-shared.wisdom" "$probe/gain-host-full-baseline.f32" 2> "$out/host-full-baseline.log"
"$probe/gain-candidate-host/pog3-device-full" --freeze-trace "$probe/phase-host-shared.wisdom" "$probe/gain-host-full-candidate.f32" 2> "$out/host-full-candidate.log"
cmp "$probe/gain-host-full-baseline.f32" "$probe/gain-host-full-candidate.f32"
sha256sum "$probe/gain-host-full-baseline.f32" "$probe/gain-host-full-candidate.f32"
wc -c "$probe/gain-host-full-baseline.f32" "$probe/gain-host-full-candidate.f32"
