#!/bin/sh
set -eu
export TMPDIR=/home/bbalazs/.cache/ardor-pog3-device-build-20261007/compiler-tmp
repo=/home/bbalazs/projects/ardor-pog3
probe=/tmp/ardor-pog3-device-build
json=/home/bbalazs/projects/ardor/build-ci/_deps/neuralampmodelercore-src/Dependencies
fftinc=/tmp/pog3-vector-fft/fftw-root/usr/include
for freeze in 0 1; do
  c++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Werror -pedantic -DARDOR_POG3_EXPERIMENTAL_FREEZE="$freeze" -I"$repo/src" -I"$json" -I"$fftinc" -c "$repo/src/daisyfx/pog3/PolyphonicAttack.cpp" -o "$probe/gain-warnings-$freeze.o"
done
cmake --build "$repo/build-pog3-sanitize" --target ardor_pog3 -j2
c++ -O1 -g -std=c++20 -fsanitize=address,undefined -fno-omit-frame-pointer -DARDOR_POG3_EXPERIMENTAL_FREEZE=0 -I"$repo/src" -I"$json" -I"$fftinc" "$repo/benchmark-results/pog3-pi4-render-schedule-2026-10-07/automation-trace.cpp" "$repo/build-pog3-sanitize/libardor_pog3.a" "$repo/build-pog3-sanitize/libardor_realtime_fft.a" /usr/lib/x86_64-linux-gnu/libfftw3f.so.3 -pthread -o "$probe/gain-automation-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$probe/gain-automation-sanitize" "$probe/phase-host-shared.wisdom" > /dev/null
