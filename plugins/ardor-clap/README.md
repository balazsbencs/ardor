# Ardor CLAP beta

This is a full-chain plugin using the same engine and preset format as the
standalone app. It is separate from the existing Whammy algorithm plugin.
The beta supports **48 kHz DAW sessions only**. Beta 2 adds the shared Ardor
editor in an embedded Cocoa view on Mac. Linux still uses host-generated
parameter controls. This is an early beta; real DAW testing remains necessary.

## Use

Download a versioned beta from [GitHub Releases](https://github.com/balazsbencs/ardor/releases).

The **CLAP beta** workflow uploads Apple Silicon/macOS 15+ and Linux/x64
artifacts after the ABI smoke test passes. Unpack the download and copy
`Ardor.clap` to `~/Library/Audio/Plug-Ins/CLAP/` on Mac or `~/.clap/` on Linux.
Keep the accompanying license notices. Restart/rescan the DAW, create a 48 kHz
session, and insert Ardor on an audio track. Configure the audio interface and
monitoring in the DAW. The plugin never opens its own audio device.

In REAPER, open Ardor from the track's FX window and use its normal plugin view.
If REAPER shows the generic parameter list, toggle the **UI** button to return
to the plugin editor after installing/rescanning beta 2. Beta 1 has no custom UI.
The editor includes the preset/chain view, effect and asset browser, live effect
parameters, EQ controls, scenes, and an input/output trim, guitar channel and
bypass toolbar. Trim fields support keyboard editing. NAM/IR assets come from
the shared desktop library; use the desktop app to import them first.

Opening the editor does not create an audio device or write library settings.
Each plugin instance has its own editor display and project draft. Normal
parameter changes update the engine's realtime controls and DAW project state;
structural/scene-definition edits prepare a replacement on the main thread and
wait for the DAW's restart. **Save** writes the current preset to its bank-1
library slot; project-state saving does not overwrite that library. Device tuner,
looper, footswitch/bank navigation, and device master controls are omitted.
The six CLAP host parameters remain stable; effect controls are stored in project
state but are not individually exposed as DAW automation parameters yet.

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
cmake -S . -B build-clap -DARDOR_UI_BACKEND=memory \
  -DARDOR_BUILD_CLAP_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-clap --target ardor-clap-host-smoke ardor-clap-editor-smoke --parallel 3
ctest --test-dir build-clap --output-on-failure -R '^ardor-clap-(host|editor|gui-host)-smoke$'
cmake --install build-clap --prefix "$PWD/build-clap/stage" --component clap-beta
```

Use `ARDOR_UI_BACKEND=none` for a DSP-only build without editor dependencies.
The `memory` backend renders LVGL directly to an instance-owned bitmap; it does
not link SDL. The Mac host view uses Cocoa logical sizes and Retina backing
pixels. UI rendering, input, timers, and DSP preparation stay on the main thread.
Closing an editor deletes only its display/input/view resources; other instances
and the audio engine keep running. Reopening restores its current project draft.

CI also checks renderer/input behavior, live effect edits, draft persistence,
structural restart, independent displays, and the shared UI regression suite.
On Mac, a smoke DAW dynamically loads the real module and checks the CLAP GUI
extension, unsupported APIs, native parenting/show/hide/recreate, actual mouse
control changes, resize bounds, and multiple independent native editors. This
checks Cocoa embedding without claiming a completed REAPER compatibility test.

The smoke host dynamically loads the actual CLAP module and covers enumeration,
ports/controls, lifecycle/reset, unsupported-rate rejection, varying host blocks,
reported impulse latency, sequential WDW, authored/default scenes and scene restore,
audio-level scene recall after host reset,
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
- Linux/Windows native editor embedding, preset discovery, and portable project assets.
- Structural scene latency accounting and DAW-specific testing.
- Public release signing/notarization, and later Windows and VST3 distribution.

This slice uses the small pinned [CLAP C ABI](https://github.com/free-audio/clap/tree/29ffcc273be7c7c651f6c9953b99e69700e2387a)
so the host's explicit main/audio thread contract controls preparation and restart.
The existing DPF Whammy wrapper stays independent. For VST3, evaluate the official
[CLAP wrapper](https://github.com/free-audio/clap-wrapper) or a DPF adapter around
this host-independent engine lifecycle; do not fork the effects implementation.
