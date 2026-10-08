#!/bin/sh
set -eu
for freeze in 0 1; do
 /buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Werror -pedantic -DARDOR_POG3_EXPERIMENTAL_FREEZE="$freeze" -I/ardor/src -I/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -I/probe/fftw/include -c /ardor/src/daisyfx/pog3/PolyphonicPitchBank.cpp -o /probe/phasecache-arm-warnings-$freeze.o
done
/buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Werror -pedantic -DARDOR_POG3_EXPERIMENTAL_FREEZE=0 -I/ardor/src -I/buildroot/output/build/ardor-pedal-1.0/_deps/neuralampmodelercore-src/Dependencies -I/probe/fftw/include -c /ardor/tests/pog3_pitch_quality.cpp -o /probe/phasecache-arm-test-warnings.o
