# Ardor Scenes — Implementation Plan

Status: Active implementation plan
Date: 2026-09-16
Product contract: [Scenes specification](scenes-specification.md)

## Progress

- [x] Phase 1.1 — C++ version-4 model, validation, round-trip, UI-model preservation.
- [x] Phase 1.2 — Manager daemon validation and version capability advertisement.
- [x] Phase 1.3 — Manager types, validation, and scene-set factory helpers.
- [x] Phase 2.1 — capability registry, ranges, transition laws, and bypass classification.
- [x] Phase 2.2 — immutable prepared target plan and activation preflight.
- [x] Phase 2.3 — admission accounting for retained tail state.
- [x] Phase 3.1 — realtime-safe latest-request mailbox and interruptible sample-time transition core.
- [x] Phase 3.2 — numeric DSP target dispatch and post-rig scene trim integration.
- [x] Phase 3.3 — bypass tail policy and transition admission accounting.
- [x] Phase 4.1 — qualified effect bypass eligibility and 10 ms Cut crossfade.
- [x] Phase 4.2 — separable wet-path Let ring and bounded tail lifecycle for
  hosted delays, hosted reverbs, and convolution reverb.
- [x] Phase 5.1 — dedicated scene footswitch layer, 60 ms two-pair gesture
  arbitration, tuner preservation, 600 ms layer chord, and local scene recall.
- [x] Phase 5.2 — named MIDI scene actions, per-target controller pickup,
  target-only transition overrides, and 20 Hz live scene telemetry.
- [x] Phase 6.1 — four-plate device Scenes performance screen with live,
  pending, transitioning, altered/pedal, timing, trim, and unsaved states.
- [x] Phase 6.2 — device scene creation and editor workflows.
  - [x] Create four valid scenes from the current rig, select/recall an editing
    scene, and edit name, timing, trim, default, and preset Open-in behavior.
  - [x] Copy, swap, and disable scene sets with one-step undo and explicit
    confirmation; preserve IDs, names, defaults, and ID-based bindings.
  - [x] Add the device scene strip and full-width settings sheet; validate it
    with LVGL interaction tests and a 1280×720 visual review.
  - [x] Add per-control This scene/Shared ownership editing and settled-runtime
    current-sound capture.
- [x] Phase 7.1 — manager scene protocol and workspace foundation.
  - [x] Publish scene capabilities, active generation, and live scene identity;
    reject stale recalls and keep recall commands out of the durable queue.
  - [x] Bind Apply and Save & Apply to the editing scene while preserving
    Save-only behavior and reporting partial apply failures through the existing flow.
  - [x] Add editing-only scene tabs, explicit Recall on pedal, timing/trim/name/
    default/Open-in settings, a compact comparison, keyboard navigation, and
    the narrow 2 × 2 layout.
  - [x] Add ownership-aware numeric parameter and block-enabled editing with
    explicit This scene/Shared conversion across all four scene definitions.
- [x] Phase 7.2 — manager copy/swap actions, complete comparison matrix,
  revision persistence across reconnect, and connection-loss/partial-failure
  browser coverage.
  - [x] Add confirmed copy-to and identity-preserving slot swap operations as
    single undo steps.
  - [x] Expand Compare to real target-address rows with differences-only and
    show-all views plus the responsive per-parameter layout.
  - [x] Persist an acknowledged active preset fingerprint through managerd and
    the pedal runtime so clean matching drafts can recall after reconnect while
    Save immediately invalidates the old fingerprint.
  - [x] Cover transient status loss and saved-then-failed-apply behavior in
    browser tests.
  - [x] Add friendly block/control labels, physical-unit formatting, shared
    settings view, and row-level copy-across editing to Compare.
- [ ] Phase 8 — integration, documentation, measured headroom, and release qualification.
  - [x] Preserve complete version-4 scene documents through preset copy,
    backup/restore, hosted read/write, and looper source snapshots.
  - [x] Centralize explicit scene flattening and device scene-disable behavior,
    including trim folding, target resolution, mapping policy, and legacy version selection.
  - [x] Update user-facing documentation, MIDI reference, examples, and release notes.
  - [ ] Record production-Pi worst-transition headroom and soak evidence.

## Goal

Deliver four named scenes inside one prepared preset. Scene recall changes a
coherent set of runtime parameters and bypass states without rebuilding the
audio graph. The pedal provides a dedicated Scenes performance layer; the
manager provides detailed scene authoring and explicit live recall.

## Delivery principles

- Land the persistence contract before the runtime or either UI depends on it.
- Keep versions 1–3 byte-compatible in meaning. Scenes are opt-in version 4.
- Keep scene definitions, live scene state, and controller overrides separate.
- Publish a whole scene at one audio boundary; never send a series of unrelated
  UI setter calls and call that atomic recall.
- Admit only targets whose realtime behavior has been classified and tested.
- Validate serial, Dual Rig, and WDW independently before enabling each in UI.
- Every phase leaves tests passing and has a useful, reviewable boundary.

## Version 4 document contract

Version 4 preserves the existing `routing`, `blocks`, and optional `wdw`
structure. It adds one `sceneSet` object:

```json
{
  "version": 4,
  "sceneSet": {
    "defaultSceneId": "scene-1",
    "openIn": "scenes",
    "scenes": [
      {
        "id": "scene-1",
        "name": "Verse",
        "enterTimeMs": 0,
        "outputTrimDb": 0.0,
        "targets": [
          { "target": "inputGainDb", "value": -3.0 },
          { "target": "blockEnabled", "blockId": "drive-1", "value": false },
          { "target": "parameter", "blockId": "delay-1", "parameter": "mix", "value": 0.15 },
          { "target": "wdwLane", "lane": "wet", "parameter": "width", "value": 1.0 }
        ]
      }
    ]
  }
}
```

The actual file contains exactly four scenes. Each scene contains the same set
of target addresses and an explicit value for every address. That repeated,
self-describing representation is slightly larger than positional arrays but
is safer to inspect, edit, migrate, and recover.

`enterTimeMs` is `0` for Instant or `100..10000` in 100 ms steps. Scene trim is
`-12..+6 dB`. Target values retain their JSON type: numeric parameters use a
finite number and enabled states use a boolean.

Version 4 supports serial, serial with Dual Rig, and WDW. Versions 1–3 reject
`sceneSet`; version 4 requires it. Version 4 does not change routing semantics.

## Phase 1 — persistence and validation foundation

### 1.1 C++ preset model

- Add typed scene structures to `src/preset/Preset.h`.
- Parse and serialize `sceneSet` in `src/preset/Preset.cpp`.
- Validate four unique scene IDs, names, default ID, timing, trim, target shape,
  existing block references, target uniqueness, and equal target address sets.
- Generalize topology version checks so version 4 can wrap serial, Dual Rig,
  or WDW without weakening the older version contracts.
- Add version-4 round-trip and rejection coverage to `tests/preset_smoke.cpp`.

### 1.2 Manager daemon boundary

- Accept version 4 and validate the same structural scene invariants before
  persisting or applying it.
- Continue rejecting version 4 when scene data is missing or malformed.
- Advertise `supportedPresetVersion: 4` only after daemon validation exists.
- Add Go tests for valid serial and WDW scenes plus malformed/default/target
  mismatch cases.

### 1.3 Manager types and draft validation

- Add scene types to the manager API model.
- Validate version/routing/scene structure in the editor before save.
- Add factory helpers to enable scenes by copying eligible defined values into
  four initially identical scenes. Eligibility remains conservative until the
  runtime registry lands.
- Add TypeScript tests for all scene-level validation messages.

Exit: the same version-4 fixture round-trips through C++, Go, and TypeScript;
versions 1–3 remain accepted unchanged; no UI advertises scenes yet.

## Phase 2 — scene capability registry and prepared plan

- Create one canonical registry that classifies every address as continuous,
  stepped, enabled-with-cut, enabled-with-tail, or shared-only.
- Give each continuous target a value domain and transition law: linear,
  logarithmic frequency, or dB.
- Validate target ranges against effect metadata and special processors.
- Build an immutable `PreparedSceneSet` when a preset is activated. Resolve
  block IDs to bounded runtime handles outside the callback.
- Charge all scene-addressable processors and qualified tail states during
  admission. Disabled scene effects remain prepared.
- Reject unsupported targets explicitly; never ignore them.

Exit: a prepared scene contains no strings, JSON, dynamic lookup, or allocation
needed by the audio callback, and every stored target has one tested runtime
classification.

## Phase 3 — atomic realtime recall

- Add a latest-wins scene command mailbox carrying preset generation, request
  ID, destination scene, and transition duration.
- At one logical audio boundary, snapshot current rendered values and publish
  all new target endpoints together.
- Add sample-time transition state and parameter-domain interpolation.
- Add post-rig scene trim before looper capture, master volume, and limiter.
- Make interruption begin from current rendered values. Re-selecting a settled
  scene is a no-op unless temporary overrides altered scene-owned targets.
- Carry scene generation through Dual Rig and WDW work items so lane pairs
  remain coherent.
- Publish accepted, transitioning, settled, superseded, and rejected telemetry.

Exit: serial, Dual Rig, and WDW scene recall pass deterministic block-boundary,
interruption, non-finite, and worker-generation tests without engine replacement.

## Phase 4 — bypass and tail policy

- Replace scene-addressable hard bypass with a prepared latency-aligned crossfade.
- Implement Cut first for all admitted blocks.
- Add Let ring only to algorithms with separable input/wet contribution and a
  validated bounded tail lifecycle.
  - Hosted delays, hosted reverbs, and IR convolution reverb persist
    `sceneBypass: "letRing"`, expose wet contribution separately from dry
    output, and cap scene-bypass tail playback at 30 seconds. The manager
    exposes Cut/Let ring only for these qualified blocks while Scenes are
    enabled.
- Preserve histories on ordinary parameter changes; clear stale Cut history
  safely before re-enable.
- Add re-enable-during-tail, long-gap repeat, high-feedback, lane mute, and
  bounded-expiry tests.

Exit: bypass produces no new clicks or dry doubling; the UI exposes Let ring
only for capability-qualified algorithms.

## Phase 5 — controls and controller arbitration

- Extend footswitch gestures with the confirmed dedicated Scenes layer and a
  configurable FS3+FS4 layer chord.
- Keep FS1+FS2 tuner behavior, consume exits, and reset pending gestures on
  every mode change.
- Add named MIDI scene actions and rename legacy “Toggle / Scene” to “Toggle
  values” in UI copy without changing stored legacy meaning.
- Add per-target pickup for expression and continuous MIDI after scene recall.
- Centralize command ordering and preset-generation rejection.
- Preserve looper footswitch transport; allow scene recall through touch/MIDI.

Exit: threshold/bounce/held-switch tests and physical pedal trials satisfy the
60 ms recognition and 600 ms chord proposal, or those constants are revised in
the product specification before release.

## Phase 6 — device UI

- Add the four-plate performance screen in the physical 1/3 over 2/4 map.
- Show live, pending, transitioning, altered/pedal, unavailable, and unsaved states.
- Add scene creation, scene strip, settings, copy/swap/default, ownership scope,
  and explicit current-sound capture to the existing editor.
- Preserve one-second glance readability in all palettes and at device scaling.
- Add tuner/looper overlays and foot-operable dirty-navigation decisions.

Exit: LVGL model/smoke tests pass, simulator screenshots pass one bounded visual
review, and standing-height/glare trials satisfy the specification.

## Phase 7 — manager UI and device protocol

- Extend device status with scene capabilities and live scene telemetry.
- Add idempotent, generation-bound local recall commands; do not persist them in
  the reboot-surviving runtime queue.
- Add scene editing tabs, explicit Recall on pedal, settings, comparison matrix,
  ownership controls, and stored/live revision messaging.
- Implement Save, Apply, and Save & Apply exactly as specified.
- Add responsive and accessible layouts without changing the established Panel
  visual system.

Exit: browser tests cover edit/live separation, stale commands, save/apply
partial failure, keyboard operation, narrow layouts, and connection loss.

## Phase 8 — integration and release qualification

- Round-trip scenes through copy, backup/restore, hosted read/write, and looper
  source snapshots. Keep hosted live recall disabled unless separately designed.
- Add explicit flatten-to-legacy export and scene-disable behavior.
- Update README, user manual, MIDI documentation, example presets, and release notes.
- Measure worst-scene CPU/headroom and run the production Pi soak with repeated
  scene changes, expression, MIDI, UI, and manager activity.

Exit: all acceptance criteria in the product specification have evidence, with
measured claims clearly separated from targets.

## Initial implementation slice

Implementation started with Phase 1 in this order:

1. C++ scene types, parser/serializer, validation, and smoke tests.
2. Go daemon validation and advertised version.
3. TypeScript types and editor validation.

No scene button is exposed until Phase 3 can recall a prepared scene without an
engine reload. This avoids shipping a UI promise before the audio contract exists.
