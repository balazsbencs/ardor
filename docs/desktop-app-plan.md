# Ardor desktop application plan

Status: proposal, 2026-10-05. This is a plan, not an implemented desktop release.

Implementation has started with an opt-in desktop host, per-user library,
persistent interface settings, background preset preparation, live editing,
sequential WDW, a resizable LVGL window, Mac bundle metadata/permission handling,
and a Mac CI build. Linux session and window checks cover the prototype; Mac
hardware validation and public distribution remain outstanding. See
[the prototype README](../apps/ardor-desktop/README.md) for build/use details.

The intended product is a standalone guitar effects application: connect a guitar
through a USB audio interface, launch Ardor, select the interface, and play through
the same effects and presets as the pedal. Start with macOS; keep Windows and a
later VST3/CLAP plugin in the architecture from the beginning.

## Recommended approach

Build a dedicated `ardor-desktop` executable using the existing C++ DSP, preset
format, LVGL interface, SDL window backend, and miniaudio audio backend. Extract
the reusable application/session logic from `apps/pedal-poc/main.cpp` instead of
copying that entire entry point. Keep the pedal, developer tools, and desktop app
as separate hosts of the shared code.

The starting point is `pedal-poc --realtime --ui`, which already connects the UI
to audio, live edits, preset activation, tuner, and looper. `pedal-ui-sim` only
previews the interface and does not process live audio.

The initial desktop UI can retain Ardor's LVGL design. Add desktop audio setup,
asset import, keyboard control, scaling, and application lifecycle behavior.
Evaluate any larger interface rewrite after the audio prototype; it is not a
prerequisite for making an end-user application.

## Existing foundations and gaps

| Area | Existing foundation | Desktop work |
| --- | --- | --- |
| DSP and presets | `src/dsp`, effect libraries, `src/preset`, `src/audio/EngineLoader.*` | Reuse algorithms and schema; separate session management from hardware policy |
| Live UI | `src/ui`, LVGL/SDL, integrated `pedal-poc --ui` | Audio setup, import/export, window scaling, keyboard access, desktop settings |
| Audio | `src/audio/MiniaudioBackend.*`; Core Audio linkage in CMake | Stable device selection, structured errors, restart/reconnect, usable buffer choices |
| Storage | Relative assets under a configurable data root | Per-user library, factory initialization, migrations, portable preset export |
| MIDI | Parser, mappings, MIDI learn, Linux device input | Desktop MIDI device enumeration/input, feeding the existing mappings |
| Plugin | `plugins/whammy-vst`, DPF wrapper producing VST3 and CLAP | Whole-chain processing adapter, project state, automation, embedded editor |
| Release | CMake and firmware CI/releases | Mac application bundle and signing pipeline; later Windows packaging |

Specific constraints found in the current source:

- `MiniaudioBackend::start`, engine loading, and hosted Daisy processing require
  48 kHz. Keep 48 kHz internally for the first desktop release.
- The integrated SDL window currently starts at 800 × 480; the simulator starts
  at 1280 × 720. Review the real application on Retina displays and smaller
  windows; simulator appearance alone does not establish desktop usability.
- `MiniaudioBackend.cpp` includes pthreads and queries its scheduler
  unconditionally. That needs platform guards or a platform abstraction for
  Windows. Scheduling, CPU affinity, and denormal handling also need review.
- Several routing executors have Linux-only worker implementations. Some permit
  a sequential fallback; pipelined WDW explicitly rejects non-Linux platforms.
  Decide and test the desktop execution policy before promising routing parity.
- Preset and looper persistence have Windows branches, but durable writes and
  replacement of existing files still need Windows validation.
- The Whammy wrapper bypasses the full engine's rate restriction by wrapping one
  algorithm directly. It is evidence for the plugin build route, not evidence
  that the entire engine is already ready for a DAW.

## Shared architecture

```text
Pedal host          Desktop host              Plugin host (later)
Linux hardware      LVGL + SDL window         DPF + DAW/editor integration
                    miniaudio device I/O      DAW provides audio and MIDI
       \                 |                    /
        Shared session, preset loading, commands, telemetry
                              |
                Shared Ardor DSP and preset schema
```

The shared session owns prepared engines and preset drafts, prepares structural
changes away from audio processing, publishes bounded commands, and safely
retires old engines. Preserve the existing click-reduction behavior. Disk I/O,
model loading, UI work, and blocking waits stay outside the audio callback.

Expose a processing interface independent of miniaudio and SDL. Put fixed-block
adaptation and, when needed, sample-rate conversion in a reusable audio adapter.
Report its algorithmic latency. A plugin must use its host's callback rather than
open its own audio device, and every plugin instance must have independent state.
Avoid depending on pedal runtime command files for in-process desktop/plugin
control.

## Mac v1 experience

1. Download a signed DMG, drag `Ardor.app` to Applications, and launch it.
2. Explain and request audio-input permission. If denied, show a useful recovery
   message and keep the app usable.
3. Choose an audio interface by name, guitar input channel, stereo output, and
   buffer preference. Prefer one interface for both input and output in v1.
4. Show an input meter and explicit audio start control. Open a working factory
   preset, with a small set of assets whose redistribution permission is known.
5. Edit effects, switch presets/scenes, use the tuner, and save without a terminal.
   Provide visible controls and shortcuts for actions otherwise tied to hardware.
6. Import NAM models and cabinet/reverb IRs using a file picker. Validate/copy
   them into the managed library, retaining relative preset asset references.
7. Remember settings and recover gracefully after device unplug, sleep/wake,
   unavailable devices, or a preset with missing assets. Preserve unsaved edits
   during audio recovery and confirm destructive exit when needed.

Use `~/Library/Application Support/Ardor/` for the Mac user library; use the
Windows local application-data equivalent later. Treat application-bundle
resources as read-only factory content. Initialize a writable library once;
updates must preserve presets, models, IRs, loops, and settings. Factory-content
versioning must not overwrite user edits.

Start with native 48 kHz on supported interfaces and test buffers such as 64,
128, and 256 frames. Display the actual device configuration and processing load.
Do not label buffer duration as total guitar-to-output latency: converters,
device buffers, engine pipelines, and resampling all contribute. Recommend
headphones/interface monitoring for setup and avoid automatically enabling a
built-in microphone-to-speaker monitoring path.

Retain the existing looper where it works through shared code, and add desktop
record/play/stop controls before claiming feature parity. USB MIDI foot-controller
support is a useful follow-up; it needs a desktop MIDI input backend even though
the mappings and MIDI learn already exist.

## Implementation milestones and exit criteria

### 1. Mac feasibility prototype

- Build on a real Apple Silicon Mac with one USB audio interface.
- Exercise integrated UI/audio with a simple chain, NAM + IR, preset edits,
  scene switches, looper, and representative split/WDW routes.
- Establish a tested sequential routing path or port the necessary workers;
  preserve branch timing/alignment and report any changed latency.
- Measure CPU headroom, callback gaps, audible behavior, and hardware loopback
  latency at the selected buffer sizes. Compare offline output with the pedal
  engine at 48 kHz using identical assets.

Exit: confirmed scope and performance for a Mac application. A routing feature
that is unavailable must be shown explicitly, rather than silently dropping it.

### 2. Dedicated desktop host

- Extract reusable session logic and add `apps/ardor-desktop` / `ardor-desktop`.
- Add platform paths, first-run library creation, device enumeration/settings,
  and structured startup errors.
- Persist device identity using backend identifiers where suitable, with a
  careful fallback; enumeration indices are not stable across restarts.
- Implement explicit audio start/stop, device switching, and reconnect behavior.
- Add file-picker import and preset export with required assets; validate asset
  rates and normalize them where supported instead of copying unusable files.
- Adapt settings to platform capabilities and add window, keyboard, and shutdown
  behavior. Separate desktop settings from GPIO/codec/firmware controls.

Exit: launch and play from Finder with no CLI flags or source checkout; settings
and edits survive restart, and unavailable audio does not close the application.

### 3. Mac beta and distribution

- Create `Ardor.app` with CMake `MACOSX_BUNDLE`, an icon, bundle identifier,
  version, `Info.plist`, and bundled runtime dependencies/resources.
- Add the microphone usage description and audio-input entitlement needed for
  the signed hardened-runtime build.
- Initially target Apple Silicon; add Intel or a universal bundle only with
  separately verified dependencies and actual Intel testing.
- Build on macOS CI, sign nested code with Developer ID, enable hardened runtime,
  notarize, staple the ticket, and publish a DMG through the release pipeline.
- Test a downloaded artifact on a clean Mac without Homebrew, developer tools,
  or repository files. Include permission denial, unplug/replug, sleep/wake,
  paths with spaces/non-ASCII characters, and upgrade preserving user data.
- Include dependency/font notices and only distributable factory models/IRs.
- Initially support manual download-and-replace upgrades; automatic updating can
  follow after stable library migrations and release packaging.

Exit: an end user can install, launch, configure, and play from the distributed
artifact. Shipping directly from the website/GitHub is the initial proposal;
Mac App Store distribution is a separate decision.

### 4. Windows standalone

- Add Windows build CI early to expose portability problems, then validate on
  real Windows hardware before a public release.
- Port scheduler queries, threading assumptions, application paths, file
  replacement/durability, window startup, and runtime dependency packaging.
- Start by measuring miniaudio/WASAPI. Miniaudio's documented built-in backends
  do not include ASIO. If WASAPI does not meet the target interfaces' latency
  needs, add a suitable ASIO-capable backend through the audio abstraction.
- Package a GUI executable with its dependencies, installer/uninstaller, icon,
  version metadata, and a release signing approach. Keep user data on upgrade.

Exit: install-and-play and latency/recovery checks pass on named supported
Windows/interface combinations. This phase and the plugin phase can be ordered
according to user demand after the Mac release.

### 5. Full-chain plugin (CLAP is the next implementation)

Use the existing DPF integration as the first candidate. Its current Ardor build
already requests `TARGETS vst3 clap`, so one wrapper can produce both formats.
The chosen next milestone after the Mac beta PR is CLAP. Build
and test VST3 afterward. The main engineering effort is common to
both, rather than the format declaration.

Required work:

- Run the shared engine from host audio callbacks, initially mono guitar input
  to stereo output, with documented supported bus layouts.
- Accept varying host blocks through a preallocated fixed-quantum adapter.
- Support common DAW rates, including 44.1/48/96 kHz. First evaluate bounded
  streaming resampling around the 48 kHz engine; validate conversion quality,
  timing, and latency rather than making every effect rate-independent at once.
- Report buffering/routing/conversion latency and effect tails to the host;
  support host bypass, reset, and offline rendering.
- Save complete per-instance chain/scene state in the DAW project. Define how
  model/IR dependencies travel with projects or can be relinked on another
  machine. Never depend solely on absolute paths on the original computer.
- Begin automation with a fixed set of stable macro parameters, such as input,
  output, expression, and assigned effect controls. Do not expose parameters by
  mutable chain position, which would change recorded automation after edits.
- Verify independent instances, headless operation, reopening projects, and
  state restoration while handling model preparation outside audio processing.
- Prototype embedding the LVGL editor into a host-owned window. SDL's standalone
  window path cannot simply become a plugin editor; lifecycle, resize, focus,
  and event integration must be proved. A host-generated parameter UI is enough
  for the initial DSP spike, but not the complete chain-editing product.

Exit: validators and real DAWs pass, project reopening preserves sound/state,
multiple instances behave independently, and rate/block changes are reliable.
If Logic/GarageBand support becomes a goal, evaluate Audio Unit separately.

## First implementation slice

Create a Mac-only `Ardor.app` prototype that opens the current LVGL UI, uses a
writable user library, offers interface/channel selection, and plays one preset
at 48 kHz with saved settings. Prove audio and routing behavior on actual hardware
before expanding packaging, platform support, or plugin UI integration.

No timing estimate is committed yet: Mac routing performance, interface behavior,
and plugin editor embedding are the main unknowns to resolve with prototypes.

## Primary references

- [miniaudio manual and audio backends](https://miniaud.io/docs/manual/index.html)
- [miniaudio source and platform support](https://github.com/mackron/miniaudio)
- [CMake application bundles](https://cmake.org/cmake/help/latest/prop_tgt/MACOSX_BUNDLE.html)
- [Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [Apple hardened runtime and audio input](https://developer.apple.com/documentation/xcode/configuring-the-hardened-runtime)
- [Apple microphone usage description](https://developer.apple.com/documentation/BundleResources/Information-Property-List/NSMicrophoneUsageDescription)
- [DPF plugin targets](https://github.com/DISTRHO/DPF/blob/main/cmake/DPF-plugin.cmake)
- [DPF plugin lifecycle and state](https://distrho.github.io/DPF/classPlugin.html)

## CLAP first implementation slice

The optional `plugins/ardor-clap` target now runs saved full chains through the
shared engine in a 48 kHz DAW session. It provides host-generated controls,
fixed-quantum buffering of arbitrary host blocks, latency reporting, sequential
WDW, parameter scene recall, native JSON preset loading, and per-instance DAW
state containing the complete preset plus controls. Library selection and state
restoration prepare on the main thread and ask the host for a restart; the audio
callback does not load files or destroy engines.

This first slice uses the pinned native CLAP ABI to make its thread/restart
contract explicit, while leaving the existing DPF Whammy plugin independent.
The embedded editor, common-rate conversion, portable embedded/relinkable assets,
structural scene graphs, full validator pass, and real DAW verification remain
next work. See the plugin README for exact support and validation limitations.
