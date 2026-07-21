#!/bin/sh
cmake -S . -B build-sdl -DARDOR_UI_BACKEND=sdl -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdl --target pedal-ui-sim -j
./build-sdl/pedal-ui-sim --data-root . --bank 0
