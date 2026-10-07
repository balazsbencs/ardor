#!/bin/sh
set -eu
# compiler checkout json_include fftw_include library fftw_library output freeze [fallback_fft_library]
compiler=$1
repo_root=$2
json_include=$3
fftw_include=$4
library=$5
fftw_library=$6
output=$7
freeze=$8
fallback=${9:-}
set -- "$library"
if [ -n "$fallback" ]; then set -- "$@" "$fallback"; fi
"$compiler" -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Werror -pedantic -DARDOR_POG3_EXPERIMENTAL_FREEZE="$freeze" \
  -I"$repo_root/src" -I"$json_include" -I"$fftw_include" \
  "$repo_root/benchmark-results/pog3-pi4-render-schedule-2026-10-07/automation-trace.cpp" \
  "$@" "$fftw_library" -pthread -o "$output"
