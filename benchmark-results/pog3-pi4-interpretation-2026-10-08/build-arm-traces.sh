#!/bin/sh
set -eu
for build in baseline candidate; do
  root=/ardor
  if [ "$build" = baseline ]; then root=/probe/frame-baseline-source; fi
  sh /ardor/benchmark-results/pog3-pi4-render-schedule-2026-10-07/build-automation.sh /buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ "$root" /buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies /probe/fftw/include /probe/frame-$build-arm/libdevice_pog3.a /probe/fftw/lib/libfftw3f.a /probe/frame-automation-$build-arm 0
  /buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ -O3 -DNDEBUG -std=c++20 -DARDOR_POG3_EXPERIMENTAL_FREEZE=0 -I"$root/src" -I/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -I/probe/fftw/include /ardor/benchmark-results/pog3-pi4-interpretation-2026-10-08/frame-trace.cpp /probe/frame-$build-arm/libdevice_pog3.a /probe/fftw/lib/libfftw3f.a -pthread -o /probe/frame-direct-$build-arm
 done
