#!/bin/sh
set -eu
export TMPDIR=/home/bbalazs/.cache/ardor-pog3-device-build-20261007/compiler-tmp
repo=/home/bbalazs/projects/ardor-pog3
probe=/tmp/ardor-pog3-device-build
out=$repo/benchmark-results/pog3-pi4-phase-cache-2026-10-08/bulk-validity
builder=$repo/benchmark-results/pog3-pi4-render-schedule-2026-10-07/build-automation.sh
json=/home/bbalazs/projects/ardor/build-ci/_deps/neuralampmodelercore-src/Dependencies
fftinc=/tmp/pog3-vector-fft/fftw-root/usr/include
fftlib=/usr/lib/x86_64-linux-gnu/libfftw3f.so.3
for mode in host optin; do
  freeze=0
  if [ "$mode" = optin ]; then freeze=1; fi
  for build in baseline candidate; do
    root=$repo
    if [ "$build" = baseline ]; then root=$probe/phasebulk-baseline-source; fi
    sh "$builder" c++ "$root" "$json" "$fftinc" "$probe/phasebulk-$build-$mode/libdevice_pog3.a" "$fftlib" "$probe/phasebulk-automation-$build-$mode" "$freeze"
    "$probe/phasebulk-automation-$build-$mode" "$probe/phase-host-shared.wisdom" > "$probe/phasebulk-automation-$build-$mode.f32" 2> "$out/host-automation-$build-$mode.log"
  done
  cmp "$probe/phasebulk-automation-baseline-$mode.f32" "$probe/phasebulk-automation-candidate-$mode.f32"
  sha256sum "$probe/phasebulk-automation-baseline-$mode.f32" "$probe/phasebulk-automation-candidate-$mode.f32"
  wc -c "$probe/phasebulk-automation-baseline-$mode.f32" "$probe/phasebulk-automation-candidate-$mode.f32"
done
"$probe/phasebulk-baseline-host/full-baseline" --freeze-trace "$probe/phase-host-shared.wisdom" "$probe/phasebulk-host-full-baseline.f32" 2> "$out/host-full-baseline.log"
"$probe/phasebulk-candidate-host/pog3-device-full" --freeze-trace "$probe/phase-host-shared.wisdom" "$probe/phasebulk-host-full-candidate.f32" 2> "$out/host-full-candidate.log"
cmp "$probe/phasebulk-host-full-baseline.f32" "$probe/phasebulk-host-full-candidate.f32"
sha256sum "$probe/phasebulk-host-full-baseline.f32" "$probe/phasebulk-host-full-candidate.f32"
wc -c "$probe/phasebulk-host-full-baseline.f32" "$probe/phasebulk-host-full-candidate.f32"

for build in baseline candidate; do
  root=$repo
  if [ "$build" = baseline ]; then root=$probe/phasebulk-baseline-source; fi
  c++ -O3 -DNDEBUG -std=c++20 -DARDOR_POG3_EXPERIMENTAL_FREEZE=0 -I"$root/src" -I"$json" -I"$fftinc" "$out/phasebulk-trace.cpp" "$probe/phasebulk-$build-host/libdevice_pog3.a" "$fftlib" -pthread -o "$probe/phasebulk-direct-$build-host"
  "$probe/phasebulk-direct-$build-host" "$probe/phase-host-shared.wisdom" > "$probe/phasebulk-direct-$build-host.bin" 2> "$out/host-direct-$build.log"
done
cmp "$probe/phasebulk-direct-baseline-host.bin" "$probe/phasebulk-direct-candidate-host.bin"
sha256sum "$probe/phasebulk-direct-baseline-host.bin" "$probe/phasebulk-direct-candidate-host.bin"
wc -c "$probe/phasebulk-direct-baseline-host.bin" "$probe/phasebulk-direct-candidate-host.bin"
