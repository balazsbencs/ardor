# Full Marshall HD model on Pi 4 — 4 November 2026

The original full HD model now runs on `192.168.88.12` at **48 kHz / 128 frames**,
with the existing cabinet and all six learned controls. The optimized build is
installed. It uses one audio core and introduces no extra buffering latency.
No model reduction, retraining, quantization or sample-rate change was required.

This supersedes the HD performance limitation of the
[initial generic implementation](../hyperwavenet-pi4-20261104/README.md).

## What changed

`src/namparam/wavenet/mutable_a2.cpp` implements the exact 16-channel, 23-layer
A2 topology with an ARM64 NEON kernel. It processes four consecutive frames in
registers, reusing each weight across those frames. Compact history rings mirror
only the three frames needed for a tile that crosses a ring boundary. The
validated hypernetwork delta ranges determine whether convolution kernels need
reloading after a control edit; unchanged kernels stay prepared.

The same floating-point weights, activations, skip head, control encoding and
history are retained. Arithmetic reduction order changes slightly. The original
generic runtime remains available and handles other shapes. Production hosts
without the ARM64 kernel keep their generic backend. Shape checks require all
A2 invariants before selecting the specialization.

The supplied file is unchanged:

```
/opt/ardor-pedal/models/Marshall_1987X_Reissue-hd.nam
SHA-256 80c653b21088487cf6cd443af6867cd569473cfbb3a12ff86887b9f7b4475803
```

Its Presence, Bass, Middle, Treble, Volume I (High Treb) and Volume II (Normal)
controls still span 0–10 with default 5. Controls apply at block boundaries as
before; this optimization does not add smoothing or remove possible audible
stepping while turning knobs.

## Numerical and memory validation

The exact HD weights were compared with the generic runtime at 13, 64, 128 and
129 frames, including mixed shorter blocks, ring wraps, resets and live parameter
changes. ARM maximum absolute error was `1.36346e-6`, with error RMS
`8.44794e-8` against reference RMS `0.126378`. Output is sample-aligned with the
generic runtime, so no additional algorithmic delay was introduced.

The in-memory synthetic A2 fixture also changes dilated convolution weights,
covering the conditional reload logic, and combines full and low-rank deltas.
Shape mutations verify fallback for near-matching architectures. Existing Python
reference, malformed-file, controls, preset restoration and C/C++ allocation
audits continue to pass. No allocation or free was observed in the timed
setter/audio path.

Four targeted host NAM CTests and ARM parametric/native UI regression executables
passed. Standalone host ASan/UBSan and ARM UBSan checks passed variable-block,
reset, ring-wrap and weight-update tests at capacities 1–1,024 frames.
`mutable-a2-memory.cpp` is that standalone helper. Its shared success message
mentions both sanitizers; the ARM UBSan build enabled UBSan only. An ARM ASan
attempt could not reserve its allocator's virtual address layout before user
code ran; `arm-sanitizers.log` preserves that initialization failure. UBSan vptr
instrumentation was disabled for the standalone link, which does not include the
WaveNet parser's RTTI-defining translation unit.

## Pi processor measurements

Same Raspberry Pi 4 Rev 1.5 / 1 GB, performance governor, 1.5 GHz, AArch64
Buildroot GCC 13.4, `-O2 -DNDEBUG`, without fast-math or overclocking. The processor
smoke was pinned to CPU 2 / SCHED_FIFO 60, with the normal audio service stopped.
Each measurement used 1,000 warmup and 10,000 measured blocks. Every sixteenth
measured block changed all six controls. The deadline is 2,666.67 µs.

| Runtime / block kind | Blocks | p99.9 µs | Maximum µs | Deadline misses |
| --- | ---: | ---: | ---: | ---: |
| Original generic / steady | 9,375 | 3,748.24 | 3,776.00 | 9,375 |
| Optimized / steady | 9,375 | 1,959.91 | 2,059.04 | 0 |
| Original generic / all controls change | 625 | 4,250.17 | 4,250.17 | 625 |
| Optimized / all controls change | 625 | 2,398.63 | 2,398.63 | 0 |

[Timing chart](timings.html) · [Machine-readable results](measurements.json).
The original figures are the earlier generic-runtime measurements on this same
Pi, not a simultaneous controlled experiment. At 625 observations, nearest-rank
p99.9 equals the maximum. `first-device-smoke.log` records the first kernel
before the additional history/reload refinements; `refined-device-tests.log`
records the final result.

## Real audio and normal application

Both tests used the real Codec Zero capture/playback device, 48 kHz / 128 frames,
CPU 2 / SCHED_FIFO 70, the unchanged HD model and the existing G12T-75 cabinet IR
limited to 8,192 samples.

- A three-minute hardware control sweep through the production engine/backend
  completed **67,498 callbacks and 10,800 setter calls**, changing all six
  parameters every 100 ms. Average processing was `2.0293 ms`, maximum
  `2.45556 ms`, with zero overruns, gaps, nonfinite blocks or size mismatches.
- The normal touchscreen application then ran a separate three-minute process
  observation. Including startup/prewarm, the last observed callback count was
  **65,539**. Average was about `1.99 ms`, maximum `2.21 ms`, with zero overruns,
  gaps, worker faults, bypasses, nonfinite samples or block mismatches.

The final native binary hash matches the binary used in those tests. CPU frequency
remained 1.5 GHz; observed temperature was approximately 55 °C. No second model
worker or extra frame of buffering was needed. Test presets/assets lived under
`/tmp`, and controls were disabled for the static application test.

Physical output was muted during diagnostics and restored afterward. These are
processing and numerical checks, not a listening session or analog round-trip
latency measurement. This qualification covers one HD amp plus cabinet at
128 frames; additional effect chains, other periods and simultaneous HD instances
need their own capacity checks.

## Installed build and rollback

```
/opt/ardor-pedal/system/current
  -> releases/0.1.99-hyperwavenet-hd.dev.20261104

Native build: 74efef6c-hyperwavenet-hd-dev
```

The source is an isolated archive of base commit
`74efef6c06e91bc4b07d3cdb95cb4902c6186478`, the HyperWaveNet adapter, this
optimization and the already installed learned-control UI cache fix. Unrelated
workspace changes were excluded. `source.patch` records that candidate, and
`manifest.json` records its hash and the installed executable hashes. The manager
binary is unchanged; its active-release API identifies the new build through the
manifest. Numeric version `0.1.54` and bundle metadata still describe the original
base package. This is a local development deployment, not a published/signed OTA
release.

All 36 files in the preset/model/settings checksum baseline passed after
activation. Existing model files and saved presets were preserved; the original
selection, bank 4 / slot 0 (`Empty 1`, zero-based), was restored and the output
relay re-enabled. Refresh the manager and select the existing HD model to use it.

The previous working HyperWaveNet release is intact. Restore it with:

```sh
sh /opt/ardor-pedal/deploy-backup-hyperwavenet-hd-20261104/rollback.sh
```

Rollback also recalls the selection recorded before this deployment, which can
change whichever slot is selected when rollback is run later.

The historical `source.patch` is retained byte-for-byte to match the deployed
manifest hash. Its fixture entries lack a separating newline;
To apply it, insert a newline and the standard `\ No newline at end of file`
marker between the two fixture entries (`+}--- /dev/null`). The pull request
contains the source files directly; their contents match the deployed candidate.
