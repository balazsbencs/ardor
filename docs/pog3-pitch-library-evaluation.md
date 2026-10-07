# POG3 pitch-library CPU screening

Updated 2026-10-07. The user requested trying the polyphonic pitch-rendering
libraries first, then supplied Terrarium and the ERB-PS2 thesis. This checkpoint
adds a reproducible optional comparison, not a production renderer replacement.
The existing FFTW POG3 remains the control and CPU admission remains open.

## Decision

The tested Signalsmith configurations save CPU only with a larger hop or longer
latency, and fail the settled-tone tuning screen. Rubber Band's tested separate
voice instances exceed the full-block CPU goal before Attack/freeze/filter/space
work. Terrarium's shared filter-bank approach is a useful reference already
ported in Ardor, with fewer voices and reduced source bandwidth. The next focused
prototype is the thesis's ERB-PS2 design; the isolated octave-up reference is now
implemented. See [its CPU/audio checkpoint](pog3-erb-ps2-reference-results.md)
and [the shared multi-voice experiment plan](pog3-erb-ps2-evaluation-plan.md).
The [implemented eight-path CPU checkpoint](pog3-erb-shared-cpu-results.md)
now shows that full-rate shared phase voices, even with bounded table math,
do not retain the single octave-up CPU advantage or meet full-block admission.
The later [counted-cadence experiment](pog3-erb-cadence-cpu-results.md) reduces
wide-bank demand to 20.879–21.694%; full required-stage cost and audio admission
remain open.

These observations apply to the pinned implementations, options and adapters
below. They do not establish that every possible configuration or a fork sharing
analysis would have the same result. No candidate is admitted as a complete POG3
backend by this screening.

## Pinned inputs and build

| Input | Revision / configuration |
| --- | --- |
| Ardor control | `87204ea26068ae3d2da9cd0e1efb06d826ca3b33`, existing Release `ardor_pog3` |
| Signalsmith Stretch | `a670068d9aeb64913331d5cc29337b19a457a7df`, version 1.3.2 |
| Signalsmith Linear | `de55e6a50ffcf6f8f43f649692d94691c7025151`, tag 0.6.4 |
| Rubber Band | `e4296ac80b1170018a110bc326fd0d45a0eb27d6`, version 4.0.0 |
| Terrarium inspected upstream | `bc7ddf35362cf4b0e9d46b77bc21e490f79f9ed1` |
| Host / compiler | Intel Core i3-8100T, GCC 14.2.0, ordinary `-O3 -DNDEBUG -std=c++20` |
| FFTW | system single-precision 3.3.10, same runtime as production control |

The separate `tests/pog3-library-trial/CMakeLists.txt` accepts local source paths
and existing Ardor static libraries. It does not fetch dependencies or add them
to Ardor's normal build, CI or package. No worker threads, fast-math or
architecture-specific compiler flags are enabled. Signalsmith uses its supplied
Linear backend; Rubber Band's official single-file compilation unit uses
single-precision FFTW and its built-in BQ resampler. Upstream code is unmodified.
Signalsmith is MIT; Rubber Band's GPL linkage fits the user's accepted dependency
policy. The libraries are not vendored into the production tree.

## Workload and adapter contracts

Every CPU row processes the same four-second 48 kHz stereo low-guitar chord plus
deterministic noise as the existing benchmark. There are 64/128-sample callbacks,
a warm run, reset, and a measured run. Dynamic rows move Warp targets every
480 samples with callback-granularity updates and a 20 ms extent ramp. Production
Ardor additionally retains its own frame snapshots/smoothing. Thus this is
comparable demand screening, not sample-identical control automation.

Five-voice rows contain -24, -12, +7, +12, +24 stereo pitch shifts. Eight-path rows
add processed unison and two warm upper variants, corresponding to the current
six long plus two short stereo render paths. The historical library run kept
indices 4/5 fixed and moved indices 6/7.
That reversed the production long/short upper assignment: production long
upper paths follow Warp, and short upper paths stay fixed. The shared ERB
checkpoint corrects the helper for subsequent runs; those earlier CSVs remain
unchanged, including their dynamic numbers. Static results are unaffected.
Ardor always runs its complete
eight internal paths and publishes its six mixed voices. The trial times the
library voices separately; it does not align/mix Focus paths or implement Attack,
freeze/gliss, filter, detune, spread or pan for those libraries. Their costs are
therefore not full-block admission numbers. Existing Ardor interpretation and
ownership updates remain part of its production-bank measurement.

| Adapter | Window / hop | Streaming latency reported by trial |
| --- | --- | --- |
| Ardor bank | long 2048/256, short 1024/128, low 4096/512 | long 2304 samples (48 ms), short 1152 (24 ms) |
| `ss-default` | default preset, split computation | 7200 samples (150 ms) |
| `ss-cheap` | cheaper preset, split computation | 6720 samples (140 ms) |
| `ss-pog3` | 2048/256 plus 1024/128, split computation | long 2304 (48 ms), short 1152 (24 ms) |
| `ss-balanced` | 2048/512 plus 1024/256, split computation | long 2560 (53.33 ms), short 1280 (26.67 ms) |
| `rb-r2` | real-time, faster, short, channels together, pitch high consistency | 4096-sample extra FIFO reservoir after preferred startup pad/trim; observed underruns |
| `rb-r3` | real-time, finer, short, channels together, pitch high consistency | same reservoir and observed underruns |
| `rb-live` | short, channels together, fixed 512-sample processing | maximum queried initial ratio delay plus 512-sample wrapper buffer: 7296 (152 ms) |
| `terrarium-48` / `terrarium-80` | related analytic filter bank at 8 kHz | frequency-dependent filter/resampler delay; scalar latency field is not applicable |

Rubber Band's variable-output adapters use a fixed-capacity FIFO and count every
underrun/overflow. They never feed future input to conceal an underrun. Their
reservoir proved insufficient for these streams; those rows are failed streaming
trials, not functioning production replacements. The Live API has no FIFO faults
but concentrates work on every 512th input frame. Startup latency depends on
initial pitch ratio; the table reports the worst voice.

Terrarium reference rows have only three fixed octave outputs (-24, -12, +12),
exposed separately in stereo with separate interpolators. They reuse Ardor's
ported filter/multirate code with the original 80-band or current 48-band center
spacing, and omit production mix, EQ, tone and swell. This costs more interpolation
than upstream's early mixed output, but supplies independent voice observations.
It is not a five/eight-path CPU competitor and cannot run dynamic Warp. Source
band centers span approximately 60–1685 Hz; the multirate design restricts the
source/output bands further than the POG3 bank. No FFTW or pitch-library code is
involved in those reference rows.

## Final CPU evidence

Two complete timing passes, **128 successful invocations**, execute sequentially
with the second case order reversed, after builds and quality/allocation phases
finish. Each pass has 64 rows. A preliminary incomplete run was retained under
`build-pog3-library-trial/preliminary-interrupted` and is excluded; it predates
the balanced/Terrarium cases and overlapped a warning compilation. No final
timing pass overlaps a build, quality workload or allocation probe.

The table gives the range of mean callback-period demand across both callback
sizes and both passes for **eight warm stereo paths**. Terrarium rows instead
have only **three** fixed stereo octave voices and are explicitly separate
references. These are bank/adapter timings, not complete POG3 processor costs.

| Adapter | Static mean period demand | Moving Warp mean demand | Worst retained callback (µs) |
| --- | ---: | ---: | ---: |
| Current Ardor bank | 27.184–27.645% | 27.688–27.756% | 1920.010 |
| Signalsmith default | 17.920–18.200% | 18.033–18.152% | 1519.450 |
| Signalsmith cheaper | 11.413–11.689% | 11.537–11.634% | 645.325 |
| Signalsmith POG3 sizes | 29.551–30.249% | 29.900–30.121% | 3587.930 |
| Signalsmith balanced | 15.465–15.686% | 15.634–15.799% | 1217.240 |
| Rubber Band R2 | 98.277–100.208% | 123.288–125.226% | 7962.790 |
| Rubber Band R3 | 103.822–105.202% | 109.847–110.549% | 7776.940 |
| Rubber Band Live | 83.753–83.801% | 99.467–101.763% | 17208.700 |
| Terrarium 48 bands, **3 voices** | 4.634–4.738% | unsupported | 290.143 |
| Terrarium 80 bands, **3 voices** | 7.506–7.532% | unsupported | 324.633 |

Both 64/128 callback budgets are represented (1333.333/2666.667 µs). One
Signalsmith POG3-sized eight-path callback overruns its period. The corresponding
R2/R3/Live eight-path rows have 8490/8789/3020 total period overruns. Other
eight-path configurations and the Terrarium references have none in these runs.
Those host observations do not prove dependable target-device tails. Five-voice
rows, individual callback sizes, all p99/max values, FIFO faults and checksums
remain in the CSVs; no slow callback or failed streaming row is removed.

The ignored final artifacts are in `build-pog3-library-trial/final-artifacts`:

| CSV | SHA-256 |
| --- | --- |
| `timing-1.csv` | `8934c9d6aae16571a41fe35f825fbb14fb78b5a55362636f259f00b8903ee047` |
| `timing-2.csv` | `32b460603826cdf9279a76a07ccffcaed9cdc6867c059f382483eee9803f6bd8` |
| `allocation-1.csv` | `a280f68036ba8445d6e60202628508576af1d89f861ba42a54a426be7ebd2608` |
| `quality-1.csv` | `6d8ee5293a04f2d22ba29014a61fd8260356bf67f86ea502121e18f7edc8add4` |

## Audio screening and allocation findings

One settled 329.628 Hz sine screens every exposed voice. A 65536-point Hann FFT
with log-peak interpolation estimates tuning. The estimator gives at most 0.041
cents on the unchanged Ardor control, sufficient to identify the much larger
candidate errors but not certify sub-cent precision. Nearest-peak gain has the
usual Hann interpolation bias. A separate two-second Hann projection measures
energy exactly at each requested frequency; detuning reduces that projection
without necessarily reducing the audible peak's amplitude. The CSV keeps those
two gain measurements separate.

The existing production tone/continuous-Warp gates require absolute tuning error
below 3 cents. All four tested Signalsmith configurations exceed that limit by a
wide margin in this single-tone screen, before a full matrix or feature admission.

| Configuration | Largest absolute settled-tone error | FIFO / stream faults in this fixture |
| --- | ---: | ---: |
| Ardor | 0.041 cents | 0 |
| Signalsmith default | 13.682 cents | 0 |
| Signalsmith cheaper | 10.972 cents | 0 |
| Signalsmith POG3 sizes | 18.130 cents | 0 |
| Signalsmith balanced | 18.182 cents | 0 |
| Rubber Band R2 | 32.536 cents | 21 |
| Rubber Band R3 | 0.041 cents | 18 |
| Rubber Band Live | 0.041 cents | 0 |
| Terrarium 48 / 80 band references | 0.033 cents | 0 |

The trial also records each requested partial for a resolved four-tone chord, a
close 82.4069/87.3071 Hz pair, and rejection of upward out-of-band shifts for a
17003.2 Hz input. There are **544 quality rows** across 40 invocations. Chord rows
do not report inferred tuning from an ambiguous neighboring peak. Exact-frequency
projection and spur measurements are screening diagnostics, not perfect note
separation or a replacement for the existing comprehensive DSP suites. Several
candidates and the control struggle with the pure close low pair; do not treat
their target projections as a new equivalently measured legacy chord acceptance
threshold. Terrarium's raw tone gain is roughly -4 to -5 dB and its close low
partial projections show greater loss; output EQ/calibration was deliberately
omitted from the reference.

A separate glibc interposer invocation checks C allocation/free during all 64
prepared callback/control workloads. Timings from this phase are excluded from
CPU conclusions. Reset itself is outside this probe; startup after reset is
included. This is a callback check, not total memory accounting.

| Adapter | C allocation/free calls per four-second row | Stream faults |
| --- | --- | --- |
| Ardor, all Signalsmith configurations, Terrarium references | 0 / 0 | 0 |
| Rubber Band R2 | 7–16 / 7–16 | 20–41 |
| Rubber Band R3 | 0–8 / 0–8 | 18–37 |
| Rubber Band Live | 10–131 / 10–131 | 0 |

These counts concern these adapters' processing after reset and their ratio
changes. They do not establish that other preparation or streaming strategies
would allocate in steady state. Rubber Band's CPU screening is already sufficient
to deprioritize a separate full instance for every voice on this host.

## Reproduction

Prepare immutable local checkouts of the revisions in the table, and build
Ardor's Release `ardor_pog3`, `ardor_realtime_fft`, and allocation probe first.
Example with paths supplied by the operator:

```sh
cmake -S tests/pog3-library-trial -B build-pog3-library-trial \
  -DCMAKE_BUILD_TYPE=Release \
  -DARDOR_BUILD=/absolute/path/to/ardor/build-ci \
  -DSIGNALSMITH_STRETCH_SOURCE=/absolute/path/to/signalsmith-stretch \
  -DSIGNALSMITH_LINEAR_SOURCE=/absolute/path/to/linear \
  -DRUBBERBAND_SOURCE=/absolute/path/to/rubberband \
  -DARDOR_JSON_INCLUDE_DIR=/absolute/path/containing/nlohmann \
  -DARDOR_FFTW_INCLUDE_DIR=/absolute/path/containing/fftw3.h \
  -DARDOR_FFTW_LIBRARY=/absolute/path/to/libfftw3f.so
cmake --build build-pog3-library-trial -j2
python3 tests/pog3-library-trial/run.py \
  build-pog3-library-trial/pog3-pitch-library-trial \
  build-pog3-library-trial/final-artifacts --phase quality
python3 tests/pog3-library-trial/run.py \
  build-pog3-library-trial/pog3-pitch-library-trial \
  build-pog3-library-trial/final-artifacts --phase allocation \
  --probe build-ci/libpedal-pog3-malloc-probe.so
# Finish builds, quality checks and allocation probes before timing.
python3 tests/pog3-library-trial/run.py \
  build-pog3-library-trial/pog3-pitch-library-trial \
  build-pog3-library-trial/final-artifacts --phase timing --passes 2
```

The second timing pass reverses case order. CSVs retain every completed callback's
contribution to means/maxima and every overrun. Invocation receipts and stderr
logs sit alongside them. The runner removes inherited LD_PRELOAD for quality and
timing; allocation mode requires an explicit probe. A missing probe, unsupported
Ardor five-path configuration, and dynamic Terrarium request all fail explicitly.
The standalone harness passes strict warnings with external headers designated
as system includes. The normal production build and DSP code are unchanged.

Upstream API references:
[Signalsmith](https://github.com/Signalsmith-Audio/signalsmith-stretch),
[Rubber Band streaming](https://breakfastquay.com/rubberband/integration.html),
[Rubber Band Live](https://github.com/breakfastquay/rubberband/blob/e4296ac80b1170018a110bc326fd0d45a0eb27d6/rubberband/RubberBandLiveShifter.h).
Their public APIs expose a processor-wide ratio/map, not reusable analysis for
several differently shifted voices. Sharing internals would be a distinct fork
and integration effort and is not benchmarked here.

## Subsequent Attack/freeze checkpoint

**ERB Attack/freeze checkpoint:** the isolated hybrid now prices all three
warm stereo spectral ownership paths, active Attack projected into the counted
ERB bank, and actual freeze/gliss with eight held stereo paths. Overall host
demand is 66.61–70.14%, rising to 82.23–87.09% during the middle held segment,
with frequent callback overruns. Resolved-note and stationary freeze fixtures
pass; close-note independence, raw pitch quality, live stereo phase and fade
coherence remain open. No production renderer is replaced. See
[the complete stage costs and DSP evidence](pog3-erb-attack-freeze-results.md).
The user has clarified that 25% is a chain-budget planning target, not a hard
usability limit. Compare the full FFTW processor and ERB hybrid on hardware now;
actual callback deadlines, intended-chain margin and xruns determine admission.
