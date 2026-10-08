#!/bin/sh
set -eu
/buildroot/output/host/bin/cmake -S /ardor/tests/pog3-library-trial/device -B /probe/gain-candidate-arm -DCMAKE_TOOLCHAIN_FILE=/buildroot/output/host/share/buildroot/toolchainfile.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-O3 -DARDOR_FFTW_INCLUDE_DIR=/probe/fftw/include -DARDOR_FFTW_LIBRARY=/probe/fftw/lib/libfftw3f.a -DARDOR_JSON_INCLUDE_DIR=/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -DARDOR_POG3_EXPERIMENTAL_FREEZE=OFF -DARDOR_POG3_PROFILE=OFF -DARDOR_POG3_CALLBACK_PROFILE=OFF -DARDOR_POG3_ATTACK_PROFILE_DETAILS=OFF -DARDOR_POG3_ATTACK_PROFILE=OFF
/buildroot/output/host/bin/cmake --build /probe/gain-candidate-arm --target pog3-device-full pog3-device-pitch-quality pog3-device-cpu -j2
for build in baseline candidate; do
  root=/ardor
  if [ "$build" = baseline ]; then root=/probe/gain-baseline-source; fi
  sh /ardor/benchmark-results/pog3-pi4-render-schedule-2026-10-07/build-automation.sh /buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ "$root" /buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies /probe/fftw/include /probe/gain-$build-arm/libdevice_pog3.a /probe/fftw/lib/libfftw3f.a /probe/gain-automation-$build-arm 0
  /buildroot/output/host/bin/cmake -S "$root/tests/pog3-library-trial/device" -B "/probe/gain-profile-$build-arm" -DCMAKE_TOOLCHAIN_FILE=/buildroot/output/host/share/buildroot/toolchainfile.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-O3 -DARDOR_FFTW_INCLUDE_DIR=/probe/fftw/include -DARDOR_FFTW_LIBRARY=/probe/fftw/lib/libfftw3f.a -DARDOR_JSON_INCLUDE_DIR=/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -DARDOR_POG3_EXPERIMENTAL_FREEZE=OFF -DARDOR_POG3_PROFILE=OFF -DARDOR_POG3_CALLBACK_PROFILE=OFF -DARDOR_POG3_ATTACK_PROFILE_DETAILS=OFF -DARDOR_POG3_ATTACK_PROFILE=ON -DPython3_EXECUTABLE=/buildroot/output/host/bin/python3
  /buildroot/output/host/bin/cmake --build "/probe/gain-profile-$build-arm" --target pog3-device-full -j2
done
