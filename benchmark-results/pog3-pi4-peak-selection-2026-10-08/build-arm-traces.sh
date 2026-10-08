#!/bin/sh
set -eu
for build in baseline candidate; do
  root=/ardor
  if [ "$build" = baseline ]; then root=/probe/peaks-baseline-source; fi
  sh /ardor/benchmark-results/pog3-pi4-render-schedule-2026-10-07/build-automation.sh /buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ "$root" /buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies /probe/fftw/include /probe/peaks-$build-arm/libdevice_pog3.a /probe/fftw/lib/libfftw3f.a /probe/peaks-automation-$build-arm 0
  /buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ -O3 -DNDEBUG -std=c++20 -DARDOR_POG3_EXPERIMENTAL_FREEZE=0 -I"$root/src" -I/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -I/probe/fftw/include /ardor/benchmark-results/pog3-pi4-peak-selection-2026-10-08/peaks-trace.cpp /probe/peaks-$build-arm/libdevice_pog3.a /probe/fftw/lib/libfftw3f.a -pthread -o /probe/peaks-direct-$build-arm
 done
