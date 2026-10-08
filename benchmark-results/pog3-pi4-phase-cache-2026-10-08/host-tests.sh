#!/bin/sh
set -eu
export TMPDIR=/home/bbalazs/.cache/ardor-pog3-device-build-20261007/compiler-tmp
repo=/home/bbalazs/projects/ardor-pog3
out=$repo/benchmark-results/pog3-pi4-phase-cache-2026-10-08
for freeze in OFF ON; do
  cmake -S "$repo" -B "$repo/build-ci" -DARDOR_POG3_EXPERIMENTAL_FREEZE=$freeze
  cmake --build "$repo/build-ci" --target pedal-pog3-frame-birth-slots pedal-pog3-attack-birth-slots pedal-pog3-controls pedal-pog3-quality pedal-pog3-pitch-quality pedal-pog3-attack-quality pedal-pog3-voice-stages pedal-pog3-expression-quality pedal-pog3-freeze-quality pedal-pog3-bench pedal-pog3-malloc-probe -j2
  ctest --test-dir "$repo/build-ci" -R '^pedal-pog3-' --output-on-failure > "$out/host-$freeze-ctest.log"
done
cmake -S "$repo" -B "$repo/build-ci" -DARDOR_POG3_EXPERIMENTAL_FREEZE=OFF
