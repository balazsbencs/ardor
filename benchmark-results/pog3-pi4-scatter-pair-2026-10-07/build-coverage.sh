#!/bin/sh
set -eu
# compiler patched_checkout json_include fftw_include candidate_library fftw_library output_dir
# coverage_sources.py must already have generated coverage.h, pitch.cpp, bench.cpp there.
compiler=$1
repo_root=$2
json_include=$3
fftw_include=$4
candidate_library=$5
fftw_library=$6
output_dir=$7
"$compiler" -O3 -DNDEBUG -std=c++20 -DARDOR_POG3_EXPERIMENTAL_FREEZE=0 \
  -I"$repo_root/src" -I"$repo_root/tests" -I"$fftw_include" -I"$json_include" \
  -isystem "$repo_root/third_party/cycfi-q/include" \
  "$output_dir/bench.cpp" "$output_dir/pitch.cpp" \
  "$repo_root/src/daisyfx/hosted/dsp/pitch_shifter.cpp" \
  "$candidate_library" "$fftw_library" -pthread -o "$output_dir/coverage-candidate"
