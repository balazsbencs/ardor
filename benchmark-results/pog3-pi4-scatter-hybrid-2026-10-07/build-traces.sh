#!/bin/sh
set -eu
# compiler checkout json_include baseline_library candidate_library fftw_library output_dir [fallback_fft_library]
# Build both libraries from source.txt's base/candidate using the retained CMake context first.
compiler=$1
repo_root=$2
json_include=$3
baseline_library=$4
candidate_library=$5
fftw_library=$6
output_dir=$7
fallback_fft_library=${8:-}
mkdir -p "$output_dir"
for build in baseline candidate; do
  library=$baseline_library
  if [ "$build" = candidate ]; then library=$candidate_library; fi
  set -- "$library"
  if [ -n "$fallback_fft_library" ]; then set -- "$@" "$fallback_fft_library"; fi
  "$compiler" -O3 -DNDEBUG -std=c++20 -DARDOR_POG3_EXPERIMENTAL_FREEZE=0 \
    -I"$repo_root/src" -I"$json_include" \
    "$repo_root/benchmark-results/pog3-pi4-scatter-hybrid-2026-10-07/live-trace.cpp" \
    "$@" "$fftw_library" -pthread -o "$output_dir/live-$build"
done
