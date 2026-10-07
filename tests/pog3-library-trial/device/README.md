# POG3 standalone hardware CPU trial

This optional CMake project builds the existing full FFTW processor benchmark,
the ERB stage benchmark, numerical foundations and a CPU-affinity launcher.
It uses the same DSP/fixture sources and does not build or deploy the application.
`ARDOR_POG3_ONLY` excludes Signalsmith/Rubber Band/Terrarium adapters from the
ERB executable; its measured ERB arithmetic is unchanged. Compilation context
can affect generated code, so host timings from the larger executable are not
an identical-binary comparison.

Supply an AArch64 Buildroot toolchain matching the device, single-precision
FFTW and a local `nlohmann/json.hpp` include directory. For example, inside a
container where the SDK is mounted read-only at `/buildroot`, this checkout at
`/ardor` and an isolated output directory at `/probe`:

```sh
/buildroot/output/host/bin/cmake -S /ardor/tests/pog3-library-trial/device -B /probe/build \
  -DCMAKE_TOOLCHAIN_FILE=/buildroot/output/host/share/buildroot/toolchainfile.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-O3 \
  -DARDOR_FFTW_INCLUDE_DIR=/probe/fftw/include \
  -DARDOR_FFTW_LIBRARY=/probe/fftw/lib/libfftw3f.a \
  -DARDOR_JSON_INCLUDE_DIR=/path/to/Dependencies
/buildroot/output/host/bin/cmake --build /probe/build -j2
```

The 2026-10-07 trial uses GCC 13.4.0 from the existing Buildroot 2025.02.15 SDK.
FFTW 3.3.10 is compiled with that compiler and ordinary `-O3`, `--enable-float
--enable-neon --disable-shared --enable-static --disable-threads --disable-openmp`.
Its source tar SHA-256 is
`56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467`, matching
Buildroot's package manifest. Static linking lets the probes run from `/tmp`
without installing FFTW on the pedal. No fast-math is enabled.

Copy the `pog3-device-*` executables and `run-remote.sh` into a unique
directory under `/tmp` on the device. Record their hashes and the source revision
or patch identity before running. The runner **stops the live audio service**,
tests numerical behavior, runs two sequential timing passes in opposite case
order, and restores a previously running service with an EXIT trap. Retain
the restart log/PID receipt and verify the service after it exits. An untrappable
power loss or SIGKILL cannot execute the trap; reconnect and check the service
if the SSH session is lost. It never modifies presets, firmware or governor.

```sh
sh /tmp/UNIQUE_PROBE_DIRECTORY/run-remote.sh /tmp/UNIQUE_PROBE_DIRECTORY 2
```

Retrieve all CSVs, receipts, numerical logs and before/after device metadata
before deleting that exact temporary directory. Each ERB CSV covers a four-second
measured timeline after four seconds of warmup, with 64/128 frames, eight stereo
paths and static/dynamic/event inputs. Capture and assignment counters are in
its paired log. Two full-suite CSVs cover fourteen workloads each; their fixtures
differ from the ERB stage comparison and must be labeled separately.

Offline loops use normal scheduling, pinned to CPU 2. An unpaced FIFO loop would
consume a core continuously and can trigger Linux RT-bandwidth throttling,
creating artificial callback stalls. These results measure standalone DSP
capacity, not ALSA xruns, paced FIFO behavior or complete-chain usability.
Keep every observed tail. Test the intended live chain and thermal/xrun endurance
after a configuration demonstrates adequate standalone margin. The original
25% goal is a planning target, not a prerequisite for this hardware experiment.

## Frame-job profiling and matched production comparisons

Configure a separate build with `-DARDOR_POG3_PROFILE=ON` for diagnostic timers.
The generator copies four DSP sources and the existing full benchmark into the
build directory; it instruments only those copies. Nested frame-job scopes
report calls and inclusive/exclusive microseconds to stderr. Each category's
percentage uses the sum of measured callback durations. Renderer exclusive time
subtracts FFT and held-render children. FFT scopes include the internal packed
synthesis inverse as well as the public transform entry point. A separate
`FFT_dispatch,workload,callback,complex,r2c,c2r,unaligned` stderr row records
actual backend invocation counts; the last count covers unaligned alternatives
across all three transform types. Uninstrumented work and timer overhead
remain in the residual. This measures approximate cost attribution, not CPU
admission. Production and normal CI targets never include these timers.

The runner can execute uploaded full-suite probes in a caller-selected order:

```sh
sh run-remote.sh /tmp/UNIQUE_PROBE_DIRECTORY 2 --full-probes \
  baseline-1 candidate-1 candidate-2 baseline-2
```

Build both performance probes with profiling **OFF**, using the same toolchain,
flags and FFTW archive. Give repeated copies distinct names so every CSV/log and
metadata receipt survives. Run required numerical checks before this timing-only
mode; it does not run the default ERB/numerical sequence. The added
`pog3-device-pitch-quality --magnitude-range` exercises frame magnitudes across
the float range against the independent library norm. The normal pitch suite
also runs this regression. Preserve both source identities and binary hashes.
