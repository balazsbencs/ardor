# Marshall HyperWaveNet on the Pi 4 — 4 November 2026

**Follow-up:** the [16-channel NEON optimization](../hyperwavenet-hd-pi4-20261104/README.md)
now runs the same HD file on this Pi with a cabinet. The results below document
the initial generic implementation, before that optimization.

HyperWaveNet 1.0.0 support is implemented and installed on `192.168.88.12`.
The supplied **Lite CPU** model works at 48 kHz / 128 frames, including a real
Codec Zero audio test with the existing 8,192-sample cabinet. The supplied **HD**
model loads correctly but exceeds this Pi's processing budget. Use Lite CPU on
this device; HD needs further optimization or faster hardware.

## Files and architecture

Both original attachments already existed in the device's model library. Their
SHA-256 hashes matched exactly; neither was replaced or duplicated there.

| Model | Device filename | SHA-256 |
| --- | --- | --- |
| Lite CPU | `Marshall_1987X_Reissue_-_Lite_CPU_parametric.nam` | `83b0ad46d094bfe771362d856b01887187c2b09123b2fca2e92bd46aeefa70eb` |
| HD | `Marshall_1987X_Reissue-hd.nam` | `80c653b21088487cf6cd443af6867cd569473cfbb3a12ff86887b9f7b4475803` |

Both are mono, 48 kHz, `HyperWaveNet` 1.0.0 with a 23-layer WaveNet and a
16-unit hidden hypernetwork using rank-2 deltas. Lite uses eight audio channels;
HD uses sixteen. Both expose Presence, Bass, Middle, Treble, Volume I (High Treb)
and Volume II (Normal), each 0–10 with default 5.

Weight layout checks found 12,146 base + 25,762 hypernetwork values for Lite and
47,202 base + 51,394 hypernetwork values for HD. Values were finite, and target
ranges were nonoverlapping and within the complete base network.

The user's Marshall files remain external test inputs, not repository fixtures.
The checked-in synthetic fixture and Python reference outputs come from the
MIT-licensed parametric core fork, with provenance beside the fixture.

## Implementation and tests

The adapter uses phillipmself/NeuralAmpModelerCoreParametric commit
`fddcbc91e74ba79066a43c3b73cc690073529232`. It preserves the stock NAM core,
uses explicit dispatch, checks base weight counts before iterator-based loading,
and validates hypernetwork dimensions, low-rank shapes, target ranges and finite
weights. Both full and low-rank deltas are supported. Nested condition DSPs,
packed/slimmable inner networks and HyperWaveNet containers are rejected.

Ardor's existing parameter mailbox delivers validated targets between audio
blocks. The hypernetwork regenerates and installs weights once when the vector
changes, using prepared storage and retaining audio history. Controls apply at a
block boundary without smoothing, following upstream behavior. Audible steps
while turning knobs remain possible; this work does not claim to solve them.

Validation passed:

- Python reference parity at 13, 64 and 128 frames: maximum absolute error
  `1.08033e-7` on the host and `9.68575e-8` on ARM.
- Ten malformed-model cases: truncated/nonfinite weights, invalid/out-of-bounds/
  overlapping targets, inconsistent low-rank dimensions, oversized hidden layers,
  unknown mode, nested conditioning, wrong version and slimmable inner layers.
- Both exact Marshall files: production loader, 10,000 measured audio blocks,
  625 all-control updates, finite output, parameter restoration and invalid-target
  rejection. No audited C/C++ allocations or frees occurred during setters/audio.
  Linux linker wrappers cover linked malloc/calloc/realloc/free references and
  Eigen scratch allocation, not allocations internal to shared libraries.
- Four host NAM/parametric CTests, including concurrency and new native
  HyperWaveNet discovery, controls, engine dispatch and preset roundtrip checks.
- ARM parametric regression and LVGL UI smoke, including the already installed
  learned-control panel cache fix.
- Manager asset/preset/server packages and 74 manager frontend tests. Frontend
  coverage includes HyperWaveNet module selection and control editing/reset.
- Real multipart uploads of both attachments to the ARM manager on a temporary,
  loopback-only API: HTTP 201, `supported: true`, all six control definitions.
  Production authentication remained enabled. The embedded manager HTML and
  asset names match the previously installed frontend.

See the logs and `device-upload-results.json` in this directory. The initial
host timing log predates C allocator wrappers; the ARM measurement includes them.
An initial device invocation could not find `taskset`; the application and its
selection were restored automatically, then the test was rerun using the smoke
program's own CPU affinity option. The recorded `device-smoke.log` is that
successful rerun. An initial new regression used capitalized names for a lowercase
synthetic fixture; correcting the test names produced the recorded passing runs.

## Pi timing and real audio

Hardware: Raspberry Pi 4 Model B Rev 1.5, 1 GB RAM, Cortex-A72 at 1.5 GHz,
performance governor, Buildroot 2025.02.15, Linux 6.18.37-v8, AArch64.
The processor smoke used CPU 2 / SCHED_FIFO 60 with the normal audio service
stopped. It measured 1,000 warmup + 10,000 timed blocks per model, with every
sixteenth timed block changing all controls. At 128 frames / 48 kHz the deadline
is 2,666.67 µs. Changing controls adds weight-generation and installation work.

| Model / block kind | Blocks | p99.9 µs | Maximum µs | Deadline misses |
| --- | ---: | ---: | ---: | ---: |
| Lite steady | 9,375 | 1,482.59 | 1,657.83 | 0 |
| Lite all controls change | 625 | 1,711.13 | 1,711.13 | 0 |
| HD steady | 9,375 | 3,748.24 | 3,776.00 | 9,375 |
| HD all controls change | 625 | 4,250.17 | 4,250.17 | 625 |

[Timing chart](timings.html) · [Machine-readable measurements](measurements.json).
At 625 samples, the nearest-rank p99.9 is the measured maximum.

The native application was also run against the real Codec Zero capture/playback
device, one model plus the user's cabinet IR, 48 kHz / 128 frames. Test presets
and copied assets lived under `/tmp`; the normal user's presets were not edited.
External controls were disabled for those diagnostic runs. The output relay was
muted by the normal service stop and restored afterward; this was an audio-device
processing test, not a listening or analog loopback measurement.

Lite's one-minute process observation included load/prewarm and reached 20,637
callbacks. Telemetry reported about 1.44 ms average / 1.74 ms maximum, zero
overruns, gaps, worker faults, nonfinite samples or block mismatches. HD's brief
observation reported about 3.40 ms average / 3.67 ms maximum; all 31 observed
callbacks overran, four gaps occurred, and the runtime entered bypass.
`live-audio.log` retains both the external samples and the application's log.

The pinned stock A2 optimized path only exposes fixed 3-/8-channel models and
has no runtime weight-update API; it cannot be reused directly for this HD file.
A dedicated mutable 16-channel optimized path would be additional work. These
measurements do not qualify arbitrary HyperWaveNet files, other periods, longer
soaks, additional effects, or capture/tone quality.

## Development deployment and rollback

The installed native and manager binaries are selected by:

```
/opt/ardor-pedal/system/current
  -> releases/0.1.99-hyperwavenet.dev.20261104
```

The build used an isolated source archive of device base commit
`74efef6c06e91bc4b07d3cdb95cb4902c6186478`, this HyperWaveNet change and the
already installed learned-control panel fix. Unrelated pending workspace changes
were excluded. `source.patch` records that candidate against the base commit;
its SHA-256 is in `manifest.json`. Native code uses the production Buildroot
AArch64 GCC 13.4 toolchain and `-O2 -DNDEBUG`; FFTW 3.3.10 is linked statically
in single-precision NEON form. The manager uses CGO-disabled Linux ARM64 Go.

This is a local development installation, not a published release or signed OTA
bundle. The manifest retains numeric version `0.1.54` for updater compatibility;
its bundle metadata describes the original base package. `buildCommit` is
`74efef6c-hyperwavenet-dev`, and `developmentPatch` identifies this candidate.
The manifest's executable sizes and hashes describe the installed binaries.

All 36 preexisting files in the preset/model/settings checksum baseline passed
verification after activation. The previous selection, bank 4 / slot 0
(`Empty 1`, zero-based), was restored, and the output relay was re-enabled.
Post-deployment observations show the selected application running with zero
overruns, gaps or DSP fault counters. No user model or saved preset was changed.

The previous release is intact. To roll back both executables and restore the
selection captured before this deployment:

```sh
sh /opt/ardor-pedal/deploy-backup-hyperwavenet-20261104/rollback.sh
```

The backup directory also contains the previous symlink target and selection.
Rollback restores that recorded selection, so running it later can change the
currently selected slot.

The historical `source.patch` is retained byte-for-byte to match the deployed
manifest hash. Its fixture entries lack a separating newline;
To apply it, insert a newline and the standard `\ No newline at end of file`
marker between the two fixture entries (`+}--- /dev/null`). The pull request
contains the source files directly; their contents match the deployed candidate.
