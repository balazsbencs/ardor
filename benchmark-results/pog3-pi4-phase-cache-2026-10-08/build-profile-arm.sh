#!/bin/sh
set -eu
for build in baseline candidate; do
 root=/ardor
 if [ "$build" = baseline ]; then root=/probe/phasecache-baseline-source; fi
 /buildroot/output/host/bin/cmake -S "$root/tests/pog3-library-trial/device" -B /probe/phasecache-profile-$build-arm -DCMAKE_TOOLCHAIN_FILE=/buildroot/output/host/share/buildroot/toolchainfile.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-O3 -DARDOR_FFTW_INCLUDE_DIR=/probe/fftw/include -DARDOR_FFTW_LIBRARY=/probe/fftw/lib/libfftw3f.a -DARDOR_JSON_INCLUDE_DIR=/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -DARDOR_POG3_EXPERIMENTAL_FREEZE=OFF -DARDOR_POG3_INTERPRETATION_PROFILE=ON -DPython3_EXECUTABLE=/buildroot/output/host/bin/python3
 /buildroot/output/host/bin/cmake --build /probe/phasecache-profile-$build-arm --target pog3-device-full -j2
done
