# Four-Track Looper — Implementation Specification

Date: 2026-08-30
Status: Implemented in software; Raspberry Pi 4 acceptance and physical-pedal validation pending

## Goal

Add a performance looper that turns Ardor's four footswitches into a complete,
stage-usable transport for four synchronized tracks. A player chooses a preset,
enters Looper mode, records the processed stereo sound, builds and mutes tracks
independently, overdubs with one-level undo/redo per track, and can save and
recall the loop set.

The looper is part of the live audio host, not an effect block in preset JSON.
One preset is locked for the lifetime of an open loop session. This keeps
recorded layers stable, keeps the preset chain at one instance, and prevents a
preset replacement from discarding realtime loop memory.

## Settled decisions

1. **Four synchronized tracks.** One shared loop length and playhead; no
   independent or polyrhythmic track lengths in v1.
2. **Processed stereo capture.** The recorded source is the active preset's
   stereo program output, after preset output gain and before master volume and
   the safety limiter.
3. **One locked preset per open session.** Preset switching, structural edits,
   and Manager-driven preset apply commands are rejected until the session is
   closed. A paused or saved session is still open and still owns the lock.
4. **One-level undo/redo per track.** Each track keeps its accumulated base and
   its most recent take separately. A later overdub folds the previous take
   into the base while recording the new take, so work and memory stay bounded.
5. **RAM recording, explicit persistence.** No SD-card access occurs while
   recording or from the callback. Saving is allowed only while transport is
   paused and uses a background I/O worker plus atomic publication.
6. **Dedicated Operate-mode UI.** The pedal screen is a glanceable transport,
   not a waveform editor or miniature DAW. Detailed library management can be
   added to the Manager after the on-device flow is proven.
7. **No silent dry-mode fallback.** A Pi benchmark compares dry and processed
   placement, but processed stereo remains the product contract unless its
   hardware gate fails and this specification is amended.

## Scope

Version 1 includes:

- four processed-stereo tracks;
- first-loop length capture and synchronized recording for later tracks;
- overdub, play/mute, per-track level and stereo balance;
- one-level undo/redo on every track;
- global pause/resume, new session, clear track, save, load, and close session;
- dedicated footswitch behavior and preservation of the tuner chord;
- an LVGL Looper screen and saved-loop library overlay;
- persistent 48 kHz float WAV loop sets with the source preset snapshot;
- offline correctness tests, LVGL/control tests, a target benchmark, and a
  full-engine Pi 4 soak.

Explicitly deferred:

- reverse, half speed, replace recording, fades, one-shot playback, and track
  effects;
- independent track lengths, tempo detection, beat grids, count-in,
  quantization smaller than the master loop boundary, and time stretching;
- MIDI clock, MIDI looper commands, and external synchronization;
- importing arbitrary audio files or changing sample rate;
- background saving while loops continue playing;
- Manager library UI, cloud synchronization, and sharing;
- storing the looper as a preset-chain block;
- persistence of undo history. A recalled track starts with no undoable take.

## Existing platform constraints

- Raspberry Pi 4B, AArch64, 48 kHz; preferred block size 64, fallback 128.
- The audio callback is `SCHED_FIFO/70` and normally pinned to CPU 2.
- Anything reachable from the callback has no allocation, locks, filesystem
  I/O, exceptions, sleeps, or unbounded loops.
- `PedalEngine::processBlock()` currently owns preset processing, master gain,
  bypass mixing, output metering, and the final safety limiter.
- `MiniaudioBackend` already performs a click-free prepared-engine handoff and
  keeps raw capture in a separate SPSC monitor ring for the tuner.
- The control loop services evdev and LVGL at roughly 5 ms cadence. It is the
  single producer for realtime looper commands.
- Preset replacement builds another `PedalEngine` off the callback and swaps it
  through `MiniaudioBackend::replaceEngine()`.
- The current tuner gesture is an FS1+FS2 chord held for one second. It remains
  available in Looper mode.
- The UI is authored on the existing fixed 1280 x 720 LVGL canvas and must be
  readable from standing height in low light and glare.

## Terminology

- **Session:** the in-memory four-track looper state plus its source preset.
- **Loop set:** a persisted session on disk.
- **Master length:** the frame count established by the first recording.
- **Base:** a track's first take plus every overdub older than the latest one.
- **Last take:** the most recent overdub, held separately for undo/redo.
- **Open:** a session owns the preset lock, whether running or paused.
- **Paused:** the playhead does not advance and no loop audio is emitted; live
  guitar continues through the locked preset.
- **Muted:** one track is inaudible while the shared playhead continues.

## Player workflow

### Enter

The preset screen gains a `Looper` action. Foot-only entry is a one-second hold
on the currently active preset footswitch. Holding an inactive preset switch
continues to select that preset normally; it never enters Looper mode.

Entry is rejected while an engine preview is pending. If the active preset has
unsaved edits, the existing Save / Discard / Cancel flow resolves them first.
On success the host snapshots the audible preset, prepares the memory tier,
opens an empty session, locks preset navigation, and shows Looper mode.

### First recording

The initially selected track is Track 1. The player may select another empty
track before recording; the first recorded track, not necessarily Track 1,
establishes the master length.

1. FS3 starts recording immediately at the next processed audio block.
2. Live guitar remains audible and is written into the selected track's base.
3. FS3 again closes the loop at the next block boundary and starts playback
   from frame zero.
4. Reaching the tier's maximum frame count closes the loop automatically and
   reports `MAX LENGTH` without dropping audio.

A loop shorter than two configured audio blocks is discarded as an accidental
tap. The minimum therefore follows the active block size and does not impose a
musical duration.

### Later tracks

With a master length present, FS3 on an empty selected track arms recording.
The track enters `ARMED`, begins at the next master wrap, records exactly one
master cycle, then enters `PLAY`. Pressing FS3 while armed cancels the arm.

This wait is deliberate: all tracks stay sample-aligned without a tempo grid,
time stretching, or a second stop gesture.

### Overdub and undo

FS3 on a populated selected track arms an overdub for the next master wrap.
The overdub always runs for one complete master cycle. FS3 during the cycle is
ignored and the screen says `OVERDUB ENDS AT LOOP`; FS1 after completion is the
way to reject the take.

FS1 toggles the selected track's latest take between audible and undone. It has
no effect when the track has no last take, is armed, or is recording. Starting
a new overdub commits the previous last take in its current audible/undone
state and makes the new overdub the undoable take.

### Pause, save, load, and close

- `STOP ALL` pauses the shared playhead. Pressing it again resumes from the
  same frame. It does not change per-track mute states.
- Save, Load, New, and Close require a paused transport. Their buttons explain
  `STOP ALL FIRST` instead of silently pausing a performance.
- `EXIT` pauses and returns to the preset screen but leaves the session open.
  The preset grid is disabled and shows `LOOP SESSION PAUSED` with Resume and
  Close actions.
- Closing releases the preset lock and discards only the in-memory session.
  A saved loop set remains on disk. An unsaved or changed session requires
  `Save / Discard / Cancel` confirmation.
- Loading a loop set prepares its stored preset snapshot and audio in a new
  engine off the realtime thread, then uses the existing fade-out/swap/fade-in
  handoff. The previous bank/slot is remembered and restored when the loaded
  session closes.

## Footswitch contract

Looper mode uses the physical corner layout already established by Ardor:

| Switch | Short action | Hold/chord behavior |
| --- | --- | --- |
| FS1, upper left | Undo/redo selected track | FS1+FS2 for 1 s enters tuner |
| FS2, lower left | Select next populated-or-empty track, wrapping 4 to 1 | Hold 1.5 s clears the selected track; visible countdown; release cancels |
| FS3, upper right | Record / finish first loop / arm overdub | None; acts on press for rhythmic accuracy |
| FS4, lower right | Play/mute selected track | None; acts on press for rhythmic accuracy |

FS1 and FS2 actions occur on release so the existing chord can suppress them.
FS2's short action is suppressed once its clear hold threshold is crossed. A
clear does not change the master length while any track remains. Clearing the
last populated track returns the session to the no-master empty state.

FS3 and FS4 act on the downstroke and do not participate in tap/hold detection.
`PLAY/MUTE` is the accurate behavior: muting a track never stops the shared
clock, and restoring it returns at the current synchronized phase.

While the tuner is active, any footswitch exits as it does today. The looper
pauses before tuner output mute is applied, remembers the global running state,
and returns to Looper mode. It resumes only if it was running before the tuner.

## UI specification

### Main Looper screen

The screen keeps the flat Panel language, square geometry, and One Lamp Rule.
It has three bands:

1. **Top legend rail:** `LOOPER`, locked preset name, master length, current
   position, memory tier/remaining time, and saved/modified state.
2. **Track field:** four large track plates in Ardor's column-major physical
   order (`1 / 3` above `2 / 4`). Each shows track number, state, level,
   balance, last-take/undo status, and a single high-contrast progress rule.
3. **Bottom action rail:** `Stop All`/`Resume`, `New`, `Save`, `Load`, and
   `Exit`. Destructive confirmations are overlays, not transient toast text.

The fixed footswitch legends sit nearest their physical corners: `UNDO`,
`TRACK`, `REC / DUB`, and `PLAY / MUTE`. They do not move when track selection
changes.

Track interaction by touch:

- tapping a plate selects it;
- the selected plate exposes large `MUTE`, level, and balance controls;
- tapping another plate selects it without changing its audible state;
- clear is available in the selected-track controls with the same confirmation
  as the FS2 hold;
- no waveform editing, trimming, zooming, or draggable loop boundaries exist.

### Visual states

| State | Treatment |
| --- | --- |
| Empty | recessed graphite, `EMPTY` |
| Armed | warning amber header and `ARMED · NEXT LOOP` |
| Recording / overdubbing | the single live-red state, `REC` or `OVERDUB` |
| Playing | bone text, bright progress rule, no red fill |
| Muted | disabled engraving, explicit `MUTED` |
| Selected | strong rule/brackets; selection alone does not spend live red |
| Fault | warning border plus recovery text; never color alone |

The progress rule is driven by sampled telemetry, not rebuilt widgets. State
transitions update immediately; position may publish at 30 Hz. The UI does not
animate an inferred clock because it must not drift from the audio playhead.

### Saved-loop library

`Load` opens a touch-first overlay listing local loop sets by name. Each row
shows source preset, duration, populated track count, and saved date. Missing
or invalid audio is shown as `UNAVAILABLE` with a reason and is not loadable.
The overlay supports load and delete; renaming and bulk management are deferred
to the Manager.

## Audio placement and PedalEngine refactor

`PedalEngine::processBlock()` is refactored into the following logical order:

```
raw mono input
  -> input sanitization
  -> preset input gain
  -> RuntimeChain
  -> preset bypass equal-power mix
  -> preset output gain
  -> RealtimeLooper (records program; adds loop tracks)
  -> master volume
  -> output/looper-bus metering
  -> safety limiter
  -> stereo output
```

Today master gain is multiplied into both bypass branches before the limiter.
Factoring the common master multiplication after the new looper stage is
algebraically equivalent while the looper is inactive. A regression test must
prove inactive output remains sample-equivalent within floating-point
tolerance for wet, bypassed, and bypass-transition cases.

The recorded source includes preset output gain but excludes master volume and
the final limiter. Therefore:

- changing the physical master volume scales live guitar and all loop tracks
  together;
- the safety limiter protects the sum of live monitoring plus every track;
- a hot take may contain float samples outside `[-1, 1]`, which float WAV can
  preserve;
- playback is not limited once per track and then limited again at the bus.

The looper always monitors the current processed program at unity. It records
only that current program, never the loop bus, so overdubbing cannot
accidentally feed every existing track back into the selected track.

No automatic mix normalization is applied as tracks are added. It would make
existing layers change level and pump with mute operations. Track levels and
the final limiter are the explicit headroom controls; looper-bus limiter
activity is exposed in telemetry.

## Realtime module

New module: `src/looper/`.

| File | Responsibility |
| --- | --- |
| `LooperTypes.h` | Commands, track/session enums, telemetry, persisted metadata value types |
| `LooperSpsc.h` | Fixed-capacity, trivially-copyable SPSC ring used in each direction |
| `RealtimeLooper.{h,cpp}` | Prepared buffers, audio-thread state machine, playback, record, overdub, undo, gain/balance, seam correction |
| `LooperController.{h,cpp}` | Control-thread selected track, gesture mapping, command sequencing, acknowledgements, tuner pause/restore, preset lock |
| `LooperStore.{h,cpp}` | Manifest validation, atomic save/load, enumeration and deletion |
| `LooperWav.{h,cpp}` | 48 kHz stereo float WAV encoder/decoder for loop audio only |

`RealtimeLooper` is owned by `PedalEngine`. It is host state, not part of
`RuntimeChain`, and `PedalEngine::replacePreparedProgram()` must not replace it.
Ordinary prepared-engine pointer swaps are forbidden while a session is open;
loading a saved loop constructs a complete new engine containing both the
snapshot preset and the prepared session.

### Public realtime-facing interface

The implementation may refine names, but not responsibilities:

```cpp
class RealtimeLooper {
public:
  bool prepare(float sampleRate, std::size_t blockSize,
               std::size_t memoryBudgetBytes, std::string& error);
  bool tryEnqueue(const LooperCommand& command) noexcept;
  bool tryReadTelemetry(LooperTelemetry& telemetry) noexcept;
  void processBlock(float* programLeft, float* programRight,
                    std::size_t frames) noexcept;

  // Control/I/O-thread access only after a Pause command acknowledgement.
  LooperSessionView pausedSessionView() const;
};
```

`processBlock()` is in-place: on return the two program buffers contain live
program plus audible loop tracks. Inactive and paused sessions leave the live
program unchanged.

### Command handoff

Discrete commands travel control-to-audio through a fixed 64-entry SPSC ring.
Each command contains a monotonically increasing sequence number, command type,
track index where applicable, and scalar payload. The audio thread drains at
most the ring capacity at the start of a block, so work is strictly bounded.

Command types:

- `OpenEmpty`, `RecordOrOverdub`, `ToggleTrackAudible`, `ToggleUndo`;
- `ClearTrack`, `Pause`, `Resume`, `ResetSession`;
- `SetTrackLevel`, `SetTrackBalance` if these are not published through
  dedicated atomics;
- `AdoptPreparedSession` only if saved-loop loading does not use a complete
  prepared-engine swap.

Queue overflow is returned to the caller and shown as `LOOPER BUSY`; commands
are never silently dropped. High-rate level/balance updates are either
coalesced before enqueue or published as per-track atomic targets so a slider
cannot starve transport commands.

The audio thread publishes state changes and 30 Hz position samples through a
separate fixed SPSC telemetry ring. If that ring fills, it may drop intermediate
position samples but must retain the latest state/error transition. Telemetry
includes `lastAppliedCommandSequence`, allowing Save/Load/Tuner orchestration to
wait for a pause acknowledgement without blocking the callback.

### Track buffers and undo

Each track owns four preallocated float arrays of `maximumFrames`:

```
baseLeft, baseRight, lastTakeLeft, lastTakeRight
```

The first recording writes the base. The first overdub writes `lastTake` while
the old base plays. Undo/redo toggles whether `lastTake` contributes to output.

When another overdub begins, each frame is updated once as the playhead passes:

```
effectiveOld = base + (lastTakeAudible ? lastTake : 0)
base         = effectiveOld
lastTake     = currentProcessedProgram
output       = effectiveOld + currentProcessedProgram
```

After one cycle the prior undo decision is fully committed and the new take is
the independently undoable layer. This avoids a full-loop copy, keeps callback
work constant per frame, and supports unlimited sequential overdubs with one
level of history.

Undo, redo, mute, and level/balance changes are rejected during record or
overdub. They become available on the boundary that completes the take.

### Shared playhead

There is one integer `playheadFrame` in `[0, masterFrames)`. Populated tracks
read and write that index. Later-track record and overdub arms start when it
wraps to zero and stop on the following wrap. Pausing freezes it. Muting does
not.

Commands are applied on audio-block boundaries, so first-loop start and stop
resolution is one configured block: 1.33 ms at 64 frames or 2.67 ms at 128.
No UI or documentation should claim sample-accurate footswitch timing.

### Seam correction

Playback preserves `masterFrames`; it does not shorten the loop for overlap.
At each wrap, each audible track calculates the left/right discontinuity
between its previous output and frame zero. It adds that correction to the
first 32 playback frames with a linear decay from full correction to zero:

```
corrected[n] = stored[n] + discontinuity * (1 - n / 31),  n = 0..31
```

For loops shorter than 64 frames, the correction length is half the loop,
floored at one frame. The correction is playback-only and never mutates saved
audio. Tests cover bounded first difference, unchanged loop length, stereo
independence, and no correction when the seam is already continuous.

### Level and balance

Track level range is `-60..+6 dB`, default `0 dB`, with `-60 dB` treated as
silence. Stereo balance range is `-1..+1`, default center. Balance attenuates
the opposite channel without boosting the favored channel, preserving the
captured stereo field at center. Targets slew over 5 ms to avoid zipper noise.

### Memory tiers

All track and undo arrays are allocated and page-touched on the control thread
before `OpenEmpty` is published. The callback never grows a vector or faults in
an untouched loop page deliberately.

Float stereo base plus float stereo last-take storage costs 64 bytes per master
frame across four tracks. The default budget is derived from physical RAM and
then rounded down to leave non-loop overhead:

| Installed RAM | Loop buffer budget | Advertised maximum at 48 kHz |
| --- | ---: | ---: |
| up to 1 GB | 128 MiB | 40 s |
| 2–3 GB | 256 MiB | 80 s |
| 4 GB or more | 512 MiB | 160 s |

The 512 MiB tier is a hard v1 cap even on an 8 GB unit. SDL/headless tests may
override the budget explicitly. Failure to allocate or lock the selected tier
leaves the ordinary pedal running and reports `LOOPER MEMORY UNAVAILABLE`.

The prepared session reports its actual maximum frames; UI duration is derived
from that value, never from the tier label alone.

## Session and track state machines

Session states:

| State | Meaning |
| --- | --- |
| `Inactive` | no open session; realtime path is transparent |
| `EmptyPaused` | session open, no master length |
| `RecordingMaster` | first track is defining master length |
| `Running` | playhead advances; tracks may arm/record/overdub |
| `Paused` | playhead frozen; save/load/new/close allowed |
| `Faulted` | session retained if safe, recording rejected, recovery text shown |

Track states exposed to UI:

| State | Entered by | Leaves by |
| --- | --- | --- |
| `Empty` | new/clear | first record or armed record |
| `ArmedRecord` | FS3 on empty with master | next wrap or FS3 cancel |
| `Recording` | first record or armed boundary | FS3/max length/next wrap |
| `Playing` | completed take or unmute | FS4 mute, FS3 arm overdub, clear |
| `Muted` | FS4 | FS4, FS3 arm overdub, clear |
| `ArmedOverdub` | FS3 on populated track | next wrap or FS3 cancel |
| `Overdubbing` | armed boundary | next wrap |

The implementation must derive UI states from compact realtime flags rather
than duplicate independent state machines in the UI and audio threads.

## Preset lock and runtime integration

`LooperController` owns the lock. While it is open:

- physical preset selection and bank changes report `CLOSE LOOP SESSION FIRST`;
- touch preset cards and Edit mode are disabled;
- `apply_preset` runtime commands are consumed, rejected, and logged rather
  than deferred indefinitely;
- structural/touch parameter editing is unavailable. Existing expression and
  preset MIDI performance mappings remain live and are captured intentionally
  into new processed takes;
- master volume remains live because it is outside the recorded source;
- global tuner entry is allowed and performs the pause/restore handshake.

If the existing overload controller bypasses effects, already recorded loop
tracks continue playing through the final host stage, but new record/overdub
commands are rejected with `DSP BYPASSED · RECORDING LOCKED`. There is no
automatic dry recording. Closing the session or explicitly restoring effects
is required before recording resumes.

Audio-device recovery keeps `liveEngine` and therefore loop RAM alive. The
playhead naturally stops while callbacks are absent and continues after the
backend restarts. The backend's existing output ramp protects the restart.
Process termination or power loss discards an unsaved session.

## Persistence

Loop sets live under the data root:

```
loops/<loop-id>/
  manifest.json
  preset-<revision>.json
  track-1-<revision>.wav
  track-2-<revision>.wav
  track-3-<revision>.wav
  track-4-<revision>.wav
```

`loop-id` is a generated lowercase hexadecimal identifier and never contains a
user name. Audio filenames come only from the manifest writer, never directly
from manifest input. All resolved paths must remain beneath the loop directory.

Manifest version 1:

```json
{
  "version": 1,
  "id": "8f0d9c...",
  "name": "Loop 2026-08-30 21:14",
  "savedAt": "2026-08-30T19:14:22Z",
  "sampleRate": 48000,
  "channels": 2,
  "loopFrames": 3840000,
  "presetFile": "preset-a1b2.json",
  "sourcePreset": {"bank": 0, "slot": 2, "name": "Ambient Lead"},
  "tracks": [
    {
      "index": 0,
      "present": true,
      "audioFile": "track-1-a1b2.wav",
      "levelDb": 0.0,
      "balance": 0.0,
      "muted": false
    }
  ]
}
```

All populated WAVs are stereo IEEE float at 48 kHz and exactly `loopFrames`
long. Saving writes each track's current effective audio (`base` plus the last
take only when redo/audible), so recalled tracks have no undo history.

### Atomic publication

Save never overwrites audio named by the current manifest:

1. Require and acknowledge paused transport.
2. Generate a new revision suffix.
3. Write uniquely named temporary preset/audio files in the loop directory.
4. Flush and fsync each, then rename each to its unique final name.
5. Write, flush, and fsync `manifest.json.tmp` referring only to those final
   files.
6. Rename it over `manifest.json` and fsync the directory.
7. Remove files from older revisions only after the new manifest is durable.

A crash before step 6 leaves the prior manifest valid. Orphan temporary or
unreferenced revision files are removed during a later successful save or
library maintenance.

### Load validation

Loading occurs on the I/O/control side before engine activation. Reject:

- unknown manifest versions;
- IDs, preset files, or audio paths that escape their loop directory;
- sample rates other than 48 kHz, channel counts other than two, or non-float
  audio in v1;
- zero length, length above the prepared tier, or track lengths that differ
  from `loopFrames`;
- non-finite samples, missing preset/audio files, malformed preset JSON, or a
  preset whose assets cannot be prepared;
- duplicate/out-of-range track indices or numeric settings outside their
  documented ranges.

A failed load leaves the current engine and session unchanged. Library rows
surface the specific validation failure without offering Load.

## File-level integration map

| Existing path | Change |
| --- | --- |
| `src/dsp/PedalEngine.{h,cpp}` | Own `RealtimeLooper`; split program, looper, master, and limiter stages; expose control/telemetry access |
| `src/audio/PresetActivation.{h,cpp}` | Respect session lock; support prepared engine containing a loaded session |
| `src/audio/MiniaudioBackend.{h,cpp}` | No looper DSP; retain existing engine handoff and tuner monitor; verify device recovery semantics |
| `src/control/ControlEvents.{h,cpp}` | Active-preset hold entry and mode-aware looper/tuner gesture actions |
| `apps/pedal-poc/main.cpp` | Own `LooperController` and I/O worker; route UI/footswitch/runtime commands; enforce preset lock |
| `src/ui/UiModel.{h,cpp}` | Add `UiMode::Looper`, looper telemetry/state, revisions, prompts, and library model |
| `src/ui/LvglUi.{h,cpp}` | Add retained Looper layer and action callbacks |
| `src/ui/LvglUiLooper.cpp` | Render/sync main screen, selected-track controls, confirmations, and library overlay |
| `src/ui/LvglUiPreset.cpp` | Add Looper/Resume action and locked-session treatment |
| `src/audio/WavIo.*` | Remains IR-oriented; looper encoding/validation stays in `src/looper/LooperWav.*` |
| `CMakeLists.txt` | Add looper library, UI source, smoke tests, and non-CTest benchmark target |

The preset JSON schema, managerd preset API, `RuntimeChain`, and effect catalog
do not change.

## Error and interruption behavior

- **Command queue full:** keep current audio state, show `LOOPER BUSY`, permit
  retry; never drop a record/clear silently.
- **Memory preparation failure:** remain in Preset mode; no partial session.
- **Maximum length reached:** close and play the first loop automatically.
- **Save failure:** retain the paused in-memory session and prior saved
  revision; show the filesystem error in plain language.
- **Load failure:** retain the currently audible engine/session.
- **Preset asset missing on load:** mark loop unavailable and identify the
  missing relative asset.
- **Audio device interruption:** retain loop RAM and phase, resume through the
  backend fade after successful restart.
- **DSP overload bypass:** keep playback, lock recording, show recovery.
- **Tuner entry while armed:** cancel the arm, then enter the tuner.
- **Tuner entry while recording/overdubbing:** reject it with `FINISH TAKE
  FIRST`. Aborting an overdub midway would require an unbounded rollback of
  already-folded frames, so no partial-take shortcut is allowed. First-loop
  recording can be finished with FS3 or cancelled on screen.
- **New/clear/close confirmation lost to UI rebuild:** confirmation state lives
  in `UiState`, not only in LVGL widget state.

## Testing

All behavior is written test-first. Timing benchmarks are standalone targets,
not shared-host CTest pass/fail tests.

### `tests/looper_smoke.cpp`

1. Inactive and paused processing are transparent.
2. First recording starts/stops on block boundaries and establishes exact
   master frames; too-short and maximum-length cases behave as specified.
3. Later tracks arm, start at phase zero, record one cycle, and remain aligned.
4. All four tracks play/mute independently against one playhead.
5. Overdub outputs old track plus live monitoring once, then stores the new
   take without loop-bus feedback.
6. Undo/redo toggles only the latest take; a new overdub correctly commits an
   audible or undone previous take.
7. Clear/new reuse stale memory safely and emit no old samples.
8. Pause/resume preserves frame position and per-track mute states.
9. Level and balance reach their targets smoothly and never produce non-finite
   output.
10. Seam correction preserves frame count and bounds the wrap discontinuity.
11. Command ordering and acknowledgements are deterministic; overflow is
    observable.
12. Processing at 64 and 128 frames performs no allocation or locking.

### Control and UI tests

- Extend `tests/control_smoke.cpp` for active-preset hold, FS1+FS2 tuner
  priority, FS2 short/clear-hold distinction, and FS3/FS4 downstroke actions.
- Extend `tests/ui_model_smoke.cpp` for every session/track transition,
  persistent confirmations, preset lock, and tuner return mode.
- Extend `tests/lvgl_ui_smoke.cpp` for the 2 x 2 track field, fixed physical
  legends, visible selected/armed/recording/muted/fault states, disabled Save
  while running, library validation errors, and no color-only status.
- Extend preset activation and reload stress tests to prove an open session
  cannot be replaced and a failed saved-loop activation leaves it intact.

### `tests/looper_store_smoke.cpp`

- float-WAV and manifest round trip;
- effective audio saved with undo state flattened;
- atomic manifest-last publication and preservation of prior revision on each
  injected failure point;
- path traversal, symlink escape, malformed JSON, wrong format/rate/channels,
  differing lengths, non-finite samples, and missing preset asset rejection;
- orphan revision cleanup and safe deletion limited to one validated loop ID.

### Signal-path regression

Extend the engine tests to prove:

- looper-inactive output matches the prior wet/bypass behavior;
- master volume scales recorded playback at playback time, not record time;
- the safety limiter sees the combined live-plus-loop bus;
- loop playback continues when effects are safety-bypassed, while recording is
  rejected;
- replacing/recovering the backend cannot leave it pointing at freed loop
  memory.

## Hardware benchmark and acceptance gate

Add `tests/looper_bench.cpp` and a target runner following the existing delay
and NAM CSV benchmark conventions. Cases at 32, 64, and 128 frames:

- inactive, one playing track, four playing tracks;
- first recording, synchronized track recording, and overdub fold/write;
- continuous blocks and wrap/command-transition blocks;
- processed-stereo post-chain mixing and a comparison-only dry-mono pre-chain
  mix using identical buffer lengths.

Report mean, p99, p99.9, maximum, deadline misses, memory bandwidth estimate,
checksum, and percentage of the callback quantum.

Pi 4 acceptance:

1. The isolated processed-stereo looper with four tracks has zero deadline
   misses and p99.9 at or below 5% of the 64-frame quantum (about 66.7 us).
2. A full engine with a representative heavy NAM/cab/effect preset plus four
   tracks has zero processing deadline misses and p99.9 at or below the
   repository's existing 70% viability ceiling.
3. A ten-minute production-backend soak at 48 kHz/64 frames records no new
   over-budget callbacks attributable to the looper. Scheduler gaps are
   reported separately and compared with the no-looper baseline.
4. Every supported RAM tier allocates, page-touches, records to its advertised
   maximum, and releases on the target without invoking the OOM killer.
5. Save/load I/O while paused does not change callback timing or perform disk
   work on the realtime thread.

If the processed path fails gate 1 while the comparison dry path passes, stop
and report measurements. Do not silently ship dry capture; that changes the
musical behavior and requires an explicit spec decision.

## Implementation order

1. Standalone realtime state machine, fixed buffers, tests, and benchmark.
2. Pi dry-versus-processed feasibility measurement before UI work.
3. PedalEngine signal-path refactor with inactive regression tests.
4. Control/telemetry SPSC handoff and mode-aware footswitch state machine.
5. Host integration, preset lock, tuner pause/restore, and device recovery.
6. LVGL state model and main Looper screen.
7. Float-WAV persistence, preset snapshot, atomic revision publication, and
   saved-loop library overlay.
8. Full CTest suite, Pi performance gate, and ten-minute device soak.
9. Documentation in README/TESTING and a manual playability pass with the
   physical pedal before calling v1 complete.

## Implementation validation

The realtime engine, four synchronized tracks, processed-stereo signal path,
controller handoff, preset locking, atomic persistence, loaded-loop engine
activation, on-device UI, and desktop regression coverage are implemented on
the `feat/looper` branch. The standalone host benchmark reports zero deadline
misses at 64- and 128-frame block sizes, and the focused no-UI binaries pass
with AddressSanitizer and UndefinedBehaviorSanitizer enabled.

Those host results are development evidence, not the target acceptance gate.
The Raspberry Pi 4 benchmark, supported-tier allocation checks, ten-minute
production-backend soak, and manual footswitch/playability pass still require
the physical pedal. Version 1 is not considered hardware-validated until all
five gates in the preceding section pass and their measurements are recorded.

## Definition of done

- A player can build four synchronized processed-stereo tracks using only the
  pedal's footswitches for track selection, record/overdub, play/mute, undo,
  clear, and tuner access.
- Every realtime operation is bounded and allocation/lock/I/O-free.
- The screen makes selected, armed, recording, playing, muted, paused, dirty,
  and fault states identifiable at a standing glance.
- Saving and recalling reproduces the effective audio, mix settings, master
  length, and source preset tone; a failed operation never destroys the prior
  engine, in-memory session, or durable revision.
- Preset changes cannot invalidate an open session.
- The target benchmark and soak satisfy every acceptance gate above.
- Deferred features remain absent rather than partially implemented or hidden.
