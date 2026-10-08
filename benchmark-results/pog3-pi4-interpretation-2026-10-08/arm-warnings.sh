#!/bin/sh
set -eu
for freeze in 0 1; do
 /buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Werror -pedantic -DARDOR_POG3_EXPERIMENTAL_FREEZE="$freeze" -I/ardor/src -I/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -I/probe/fftw/include -c /ardor/src/daisyfx/pog3/PolyphonicPitchBank.cpp -o /probe/frame-arm-warnings-$freeze.o
done
/buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Werror -pedantic -I/ardor/src -c /ardor/tests/pog3_frame_birth_slots.cpp -o /probe/frame-arm-test-warnings.o
