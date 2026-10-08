#!/bin/sh
# Run inside the isolated Ubuntu 24.04 SDK container. Adapt mount paths.
set -eu
cmake=/buildroot/output/host/bin/cmake
deps=/buildroot/output/build/ardor-pedal-1.0/_deps
"$cmake" -S /ardor -B /probe/headroom-arm \
  -DCMAKE_TOOLCHAIN_FILE=/buildroot/output/host/share/buildroot/toolchainfile.cmake \
  -DCMAKE_MAKE_PROGRAM=/usr/local/bin/make \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-O3 \
  '-DCMAKE_CXX_FLAGS_RELEASE= -DNDEBUG' \
  -DARDOR_UI_BACKEND=none -DARDOR_BUILD_POG3_HEADROOM_PROBE=ON \
  -DARDOR_POG3_EXPERIMENTAL_FREEZE=OFF \
  -DARDOR_FFTW_INCLUDE_DIR=/probe/fftw/include \
  -DARDOR_FFTW_LIBRARY=/probe/fftw/lib/libfftw3f.a \
  -DFETCHCONTENT_SOURCE_DIR_MINIAUDIO="$deps/miniaudio-src" \
  -DFETCHCONTENT_SOURCE_DIR_NEURALAMPMODELERCORE="$deps/neuralampmodelercore-src"
"$cmake" --build /probe/headroom-arm --target pedal-pog3-headroom -j4
