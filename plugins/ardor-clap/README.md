# Ardor CLAP beta: first DSP/state slice

This is a full-chain plugin using the same engine and preset format as the
standalone app. It is separate from the existing Whammy algorithm plugin.
The first slice supports **48 kHz DAW sessions only**, with a host-generated
parameter interface. It is a draft beta, not a completed DAW product.

## Use

The **CLAP beta** workflow uploads Apple Silicon/macOS 15+ and Linux/x64
artifacts after the ABI smoke test passes. Unpack the download and copy
`Ardor.clap` to `~/Library/Audio/Plug-Ins/CLAP/` on Mac or `~/.clap/` on Linux.
Keep the accompanying license notices. Restart/rescan the DAW, create a 48 kHz
session, and insert Ardor on an audio track. Configure the audio interface and
monitoring in the DAW. The plugin never opens its own audio device.

Mac betas are ad-hoc signed and are not notarized. Hosts may block them. Public
Developer ID signing/notarization and real DAW testing remain release steps.

The main input/output buses are stereo. **Guitar input** selects Left (default),
Right or Average as the mono guitar source; Ardor creates the stereo effects
output. Input/output trim and bypass use smoothed gain changes. Automation event
positions are respected; engine scene changes occur at the next 64-frame quantum.
The host receives adapter plus prepared engine latency. Bypass passes the selected
mono source, delayed by that same latency. Effect tails are conservatively reported
as infinite when the preset contains blocks, until algorithm-specific bounds are
verified. Offline rendering uses the same deterministic sequential engine.

**Library preset** loads one of the four slots in bank 1 of the standalone
library. Save chains there using Ardor desktop, then select a slot in the plugin.
Absent slots use Clean, Tremolo, Chorus and Delay fallbacks without creating or
rewriting your library. Selection stages a new engine on the main thread and
requests a host restart; the old engine keeps processing until restart is granted.
This selector is deliberately not automatable. Hosts implementing CLAP preset-load
can also load an ordinary Ardor JSON preset directly.

**Scene** recalls the four authored scenes when present. Parameter scenes are
supported; structural scene graphs are rejected visibly in this first slice.
Serial and fixed WDW chains can use NAM/IR and built-in effects supported by the
shared engine. Each instance owns its own engine, delay buffers and parameters.

DAW state embeds the complete preset JSON and controls, so deleting/editing the
original preset file does not change a reopened project. NAM, cabinet and reverb
assets remain relative paths in the shared user library; they are **not embedded**.
Moving a project requires copying those assets to the corresponding library folders.
A missing/invalid asset rejects the replacement and retains the previous engine.

## Build and verify

```sh
cmake -S . -B build-clap -DARDOR_UI_BACKEND=none \
  -DARDOR_BUILD_CLAP_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-clap --target ardor-clap-host-smoke --parallel 3
ctest --test-dir build-clap --output-on-failure -R '^ardor-clap-host-smoke$'
cmake --install build-clap --prefix "$PWD/build-clap/stage" --component clap-beta
```

The smoke host dynamically loads the actual CLAP module and covers enumeration,
ports/controls, lifecycle/reset, unsupported-rate rejection, varying host blocks,
reported impulse latency, sequential WDW, authored/default scenes and scene restore,
independent instances, sample-offset automation,
short-read/write state streams, rejected state, library selection/restart, real
NAM-plus-delay processing, project restore without the original preset file,
native JSON preset loading, missing-model retention, and in-place processing.
It tracks C++ allocations during the tested callbacks (not every allocator).
It uses an isolated temporary library and opens no audio devices.

Local upstream `clap-validator` 0.4.1 checks are also run. The full suite currently
reports 14 passes, 19 failures at activation because it expects rates beyond 48 kHz,
and 11 skips; this is **not** a full validator pass. Metadata/conversion/invalid-state
checks pass, and the smoke host verifies audio at the supported rate. The validator
is not filtered into a misleading all-clear CI badge.

## Next slices

- Streaming conversion for common host rates (44.1/48/96 kHz), with measured
  conversion quality and latency, then a full upstream validator pass.
- An embedded editor, preset discovery/browser, and portable project assets.
- Structural scene latency accounting and DAW-specific testing.
- Public release signing/notarization, and later Windows and VST3 distribution.

This slice uses the small pinned [CLAP C ABI](https://github.com/free-audio/clap/tree/29ffcc273be7c7c651f6c9953b99e69700e2387a)
so the host's explicit main/audio thread contract controls preparation and restart.
The existing DPF Whammy wrapper stays independent. For VST3, evaluate the official
[CLAP wrapper](https://github.com/free-audio/clap-wrapper) or a DPF adapter around
this host-independent engine lifecycle; do not fork the effects implementation.
