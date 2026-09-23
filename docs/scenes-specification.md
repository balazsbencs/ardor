# Ardor Scenes — Product and Interaction Specification

Status: Approved direction; implementation in progress. See
[the implementation plan](scenes-implementation-plan.md).
Date: 2026-09-15.
Scope: Device operation, editing, manager, MIDI/expression, audio behavior,
persistence, compatibility, delivery sequence, and acceptance criteria.

## 1. Product decision

**A preset defines the rig. A scene defines how that rig sounds at a particular
moment in a song.**

Each scene-enabled preset has four named scenes. All four use the same blocks,
amp models, cabinet assets, and routing. Scenes recall supported parameter
values, effect bypass states, and a scene output trim without loading another
rig. The four built-in footswitches select the four scenes in Scenes mode.

Example: one preset named “Afterglow” contains Verse, Chorus, Solo, and Outro.
The guitarist presses one switch to bring in drive, raise the output slightly,
and increase delay. Selecting Verse restores its defined values regardless of
which scene came before it.

The user confirmed dedicated Scenes mode and both instant switching and timed
transitions. The remaining interaction details and numerical defaults below
are proposed design decisions. Existing Panel styling and the physical switch
map are retained.

### Reading guide

- Sections 3–6: what scenes contain and how they sound.
- Sections 7–10: footswitches, device screens, editing, and manager wireframes.
- Sections 11–14: controllers, looper/tuner, storage, and synchronization.
- Sections 15–18: engineering constraints, acceptance, and delivery order.

### Outcomes

- Reach any of four sounds with one switch while playing.
- Identify the sounding scene and the switch assignments in one glance.
- Recall the same parameter targets from any preceding scene.
- Preserve the running audio graph during scene recall.
- Create a useful four-scene preset without learning a modulation matrix.
- Keep saving, live performance, and temporary controller movement distinct.

### First-release boundaries

Include four scenes, names, a default scene, scene editing and copying, instant
recall, timed transitions, qualified effect tails, MIDI scene selection, existing
expression integration, local manager operation, and all supported routing
families after their individual acceptance gates pass.

Defer scene-specific NAM/IR replacement, different block order per scene,
scene-to-scene expression morphing, momentary scene stacks, more than four
scenes, sequenced scenes, tempo-quantized recall, cross-preset spillover, and
different controller assignments for each scene. Ordinary expression control
continues to work in the first release.

## 2. Existing foundation and required changes

Repository inspection establishes the following starting points:

| Existing behavior | Consequence for scenes |
| --- | --- |
| Four preset slots and four footswitches | Add a clearly identified control layer. |
| Screen maps switches as 1/3 above 2/4 | Use that same arrangement for scenes. |
| FS1 + FS2 held for one second opens the muted tuner | Preserve the gesture and exit behavior. |
| Current left switches have a 150 ms chord window; right switches act immediately | Any new chord on the right changes timing and needs physical validation. |
| MIDI bindings can interpolate multiple targets or toggle two endpoint sets | Preserve those bindings; named scenes need a separate identity and recall operation. |
| Engine setters exist for many parameters, EQ, bypass, and WDW mix | Reuse the capabilities, but separate setters do not establish atomic scene recall. |
| Dual Rig and WDW have their own processing paths | Scene changes must reach each lane coherently. |
| Preset replacement fades out, swaps at silence, and fades in | Scene recall must use a different path that retains the engine. |
| Looper stores processed stereo and locks its preset | Scenes may change the live sound within that same locked rig. |
| Preset formats currently include versions 1–3 | Scene presets need explicit compatibility negotiation. |

Some older prose describes a narrower MIDI target set than the current engine
supports. The scene capability registry and tests must be authoritative; a
working low-level setter alone is insufficient proof that a target is safe.

## 3. Mental model and terminology

| Term | Meaning |
| --- | --- |
| Preset / rig | Shared block graph, assets, routing, and four scene definitions. |
| Scene | A named set of performance parameter targets within that rig. |
| Default scene | Scene recalled when the preset is activated or the engine restarts. |
| Live scene | Last scene recall accepted by the audio engine; during a transition it is the destination. |
| Editing scene | Scene whose stored draft values are being edited. It can differ from the live scene in the manager. |
| Transition | Movement from current audible parameter values to another scene's targets. |
| Unsaved changes | Edits to preset or scene definitions that are not on disk. |
| Altered | Temporary performance controls differ from the live scene definition. |
| Shared | A setting has one value for all four scenes. |

Scene selection never creates an unsaved edit. MIDI and expression movement
never silently rewrite scene definitions. Scene 1 has no special inheritance
role; any scene can be the default.

Names contain 1–24 Unicode characters after trimming, with no line breaks or
control characters. Preserve entered case in storage and in the manager; use
the device's existing uppercase presentation. Slots are always numbered 1–4.
Duplicate names are allowed because slot numbers remain visible.

## 4. What a scene stores

### Parameter ownership

On enabling scenes, copy the current preset's eligible performance values into
all four scenes. They initially sound identical. This is a copy of the preset
definition, not an accidental capture of an expression pedal's current position.

Each eligible target has one explicit ownership setting: **Per scene** or
**Shared**. Eligible performance targets start Per scene. Structural targets
are always Shared and have no scene ownership toggle.

For every Per scene target, all four scenes store an explicit value. Selecting
a scene never inherits a missing value from the previously sounding scene.
Shared targets live once in the rig definition.

Changing a target from Per scene to Shared offers “Use Verse value for all
scenes,” naming the editing scene. Show a confirmation only if values differ;
the operation is undoable. Changing Shared to Per scene copies the shared value
to all four scenes. Scope is edited through the parameter's secondary menu,
not a row of extra switches on every performance screen.

### Eligibility contract

| Setting | Scene policy |
| --- | --- |
| Supported drive, wah, dynamics, modulation, delay, reverb, and widener controls | Per scene if the exact parameter supports safe runtime recall. |
| EQ band gain, frequency, Q, and eligible band enable | Per scene after stable coefficient-transition validation. |
| Cabinet mix and level; convolution reverb mix and level | Per scene where the processing path supports them. |
| Effect block enable | Per scene for qualified effects with defined bypass behavior. |
| Preset input gain | Eligible; changing it changes how hard the rig is driven. It is not hardware input calibration. |
| Preset output gain | Shared rig balance. Use the separate scene trim for scene-to-scene level differences. |
| Scene output trim | Always per scene, −12 to +6 dB, 0.1 dB steps, initially 0 dB. |
| Supported Dual Rig lane levels and WDW level/pan/width | Per scene after lane-coherence validation. |
| Lane mute | Only qualified output mute; never a routing rebuild or raw-input bypass. |
| NAM, cabinet, Dual Amp, or Dual Rig structural enable | Shared in the first release. |
| Models, IR files, full/nano selection, algorithm identity | Shared. |
| Block order, routing topology, block insertion/deletion | Shared. |
| Input channel selection, polarity, EQ filter slope, oversampling or latency-changing choices | Shared in the first release. |
| Master volume, hardware calibration, limiter, audio buffer, Wi-Fi, palette | Outside scenes. |
| MIDI/expression assignments and endpoint ranges | Shared preset configuration in the first release. |
| Looper transport, recorded audio, tuner state | Outside scenes. |

Every parameter is classified as continuous, safely stepped, or shared-only.
Numeric representation does not imply continuous behavior: an enum stored as a
number must not be interpolated. An unsupported control is labeled **Shared**,
with an explanation in its detail view. Saving an imported scene that claims
an unsupported target fails validation; it is not silently dropped.

Scene trim is applied after the complete rig mix and before looper capture,
master volume, and the safety limiter. Existing loop playback therefore keeps
its recorded level when the live guitar scene changes.

## 5. Scene recall and transitions

### Recall sequence

1. Resolve the requested scene against the active preset generation.
2. Validate the complete target set before changing anything.
3. Publish one bounded scene command to the running audio program.
4. Start all participating targets at one logical audio boundary.
5. Acknowledge the destination and command identity to device and manager.
6. Complete the transition and acknowledge its settled state.

Recall does not stop the device, load assets, rebuild the chain, reset amp
state, clear delay buffers, write files, or change master volume. Processing
continues throughout.

### Destination-owned timing

Each scene has an **Enter time**:

- **Instant**, the default: no intentional musical ramp, with mandatory short
  smoothing for controls that would otherwise click.
- **Timed**, 0.1–10.0 seconds in 0.1-second steps.
- Initial presets in the editor: Instant, 0.25 s, 0.5 s, 1 s, 2 s, 4 s.

Entering Solo uses Solo's time. Returning to Verse uses Verse's time. A preset's
default scene is initialized before its first audible output; startup does not
fade through an unrelated scene.

There is one transition curve in the first release: linear progress in a
parameter-appropriate domain. Gain moves in dB; frequency moves logarithmically;
mix and pan use their documented control law. The engine must account for its
existing smoothing so a displayed one-second transition does not secretly
take several seconds to settle.

Safely stepped targets change once at transition start with their required
local crossfade. They are labeled “Switches at start” in the editor. Delay-time
changes are eligible only after their pitch/glitch behavior is explicitly
classified and auditioned; until then, delay time stays Shared. A future
musical pitch-glide option is a separate feature.

### Interruption and repeated presses

- Select another scene mid-transition: start toward the newest destination
  from current rendered values, using the new destination's full Enter time.
- Select the destination again while transitioning: do nothing; do not restart
  its timer.
- Select an already settled, unaltered live scene: do nothing.
- Select an altered live scene: clear scene-owned temporary changes and restore
  its definition using its Enter time. This is the predictable “get me back” action.
- Rapid requests may supersede intermediate pending recalls. Never build an
  audible backlog of old scenes. Report superseded requests honestly.

Parameter recall is deterministic; existing reverb/delay history and the
player's input naturally make the resulting waveform history-dependent.

## 6. Bypass, tails, and sound continuity

**No engine reload** is mandatory. It does not by itself guarantee pleasant
bypass or preserved tails.

For ordinary qualified effects, bypass uses a short validated crossfade,
initial target 10 ms. Required processor state stays allocated. Do not use a
hard boolean bypass as the whole transition strategy.

Retain each processor's declared path latency through bypass, using its
prepared dry-path alignment where necessary. A scene must not change lane
alignment or produce a comb-filtered crossfade by mixing misaligned dry and
processed paths. This preserves the rig's existing latency rather than adding
a scene-specific buffer.

Delay and reverb blocks expose a Shared **On scene bypass** choice:

| Choice | Behavior |
| --- | --- |
| Cut | Fade out the effect contribution over 10 ms; stop feeding its wet path. |
| Let ring | Fade out new wet input over 10 ms; retain the existing wet history and let it decay. |

Let ring is the default only for algorithms whose wet contribution can be
separated correctly. Others offer Cut and explain that tails are unavailable.
Do not claim universal spillover. The block's underlying dry/wet law must
prevent doubled dry signal while a bypassed tail is mixed back in.

Additional rules:

- Bypassed wet parameters are stored for the next enable. Under Let ring, old
  tail parameters remain in use until re-enable or tail termination.
- Re-enable while a tail is sounding reuses that state and ramps in new input;
  it does not allocate another delay/reverb instance.
- An enabled effect whose parameters change continues processing its current
  history with those changes. Old and new decay/feedback settings do not run
  as separate effects.
- First-release tail lifetime is bounded to 30 seconds after bypass, with a
  100 ms final fade. State is then cleared outside any unbounded operation.
  Earlier cleanup is permitted only when the algorithm can prove its remaining
  output is inaudible; a quiet gap between repeats is insufficient evidence.
- Cut must clear stale history before a subsequent enable can reveal it.
  Clearing large buffers must be bounded; if re-enabled during cleanup, retain
  safe bypass until the block is ready and report the brief pending state.
- Shared whole-lane mute and any qualified scene lane mute fade the entire
  lane contribution, including its tails. Show this in the lane mute help.
- Downstream effects and gain changes process surviving tails normally.
- Switching presets has the existing preset handoff behavior; this feature
  makes no cross-preset tail-preservation promise.

Timed scene transitions govern continuous targets. Bypass input gating and
short safety fades happen at transition start; they do not stretch to the
whole Enter time. A player wanting a gradual effect entrance should automate
its mix between scenes while leaving the block enabled.

## 7. Built-in controls

### Physical layout

The screen must mirror the hardware:

```text
       FS1                  FS3
       FS2                  FS4
```

Never number the scene grid in ordinary row-major order.

### Control layers

| Input | Presets mode | Scenes mode |
| --- | --- | --- |
| FS1 | Preset slot 1 | Scene 1 |
| FS2 | Preset slot 2 | Scene 2 |
| FS3 | Preset slot 3 | Scene 3 |
| FS4 | Preset slot 4 | Scene 4 |
| Hold FS1 + FS2 for 1 s | Muted tuner | Muted tuner |
| Hold FS3 + FS4 for 600 ms | Enter Scenes for the sounding preset | Return to Presets |
| Encoder turn | Master volume | Master volume |
| Touch a scene plate | Not applicable | Recall that scene |
| Touch Presets / Scenes | Change layer | Change layer |

No encoder push action is assumed; current control events only establish
rotation. No long-press scene overwrite or destructive foot gesture exists.

Touch scene buttons fire on a valid release inside the same plate and cancel
on an out-of-bounds drag. Mark the interaction pending immediately, but mark
the destination live only after audio acknowledgment. Leaving a finger on a
plate does not repeatedly recall it.

Changing layer changes the switch assignments and display only. It preserves
the live scene and audio. Returning to Scenes does not recall the default.

Scene-enabled presets have an **Open in** setting, Scenes by default or
Presets. Loading the preset initializes its default scene and uses that
setting. A manual layer change lasts until the next successful preset load.
Presets without scenes retain existing operation; the right chord is inactive
and their switch timing is unchanged. Failed preset selection keeps the
previous preset, scene, and layer.

### Gesture arbitration — a deliberate tradeoff

Scene-enabled presets need both left and right pairs recognized without
accidentally firing either individual switch. Proposed recognition window:
**60 ms from the first press** for either pair, replacing the current 150 ms
left-pair window while this feature is active.

- A lone press fires at release if released within 60 ms, otherwise at 60 ms.
- If the matching partner arrives within 60 ms while the first remains down,
  suppress both individual actions and start the pair hold from the second press.
- Release a recognized pair early: cancel it with no individual action.
- Fire a completed hold once; consume both releases and require both switches
  to be up before the pair can retrigger.
- A partner pressed after the window is a separate press, not a retroactive
  chord. Test and document this limitation with real footwear.
- Three or four overlapping switches cancel any unfinished chord and all
  still-pending single actions. Already executed actions cannot be undone.
- While either pair is recognized, do not start a second chord.

This introduces up to 60 ms of gesture delay before scene recall. That delay
is separate from audio round-trip latency and is a release acceptance concern.
Touch and MIDI recalls do not use this footswitch chord window.

Provide a device setting **Scene layer chord: On / Off**, On by default. Off
restores the existing gesture parser, so FS3/FS4 remain immediate and FS1/FS2
retain the tuner window. Layer switching then uses touch or an explicit MIDI
mode action. Do not market the chord-enabled path as instantaneous.

The 60 ms/600 ms values are proposed measurable defaults. Stage trials must
verify that the pair can be hit reliably and single-scene timing is acceptable;
revise this specified contract before release if either fails.

### Hands-free song flow

Play scene 3 → hold right pair → see preset slots → select another preset →
its default scene loads → Scenes mode opens if that preset requests it.

Bank navigation remains the existing touch/MIDI operation. Hands-free bank
navigation without MIDI is a separate control feature; this plan does not
invent an additional chord or repurpose the volume encoder.

## 8. Device performance screen

Visitor mode: Operate. Audience: standing guitarist, approximately 1.5 m above
the pedal, bad light, brief glances, both hands occupied.

Inherit actual LVGL Panel tokens and fonts. DESIGN.md primarily documents the
website; device code and the device redesign specification are the visual
authority here. No new theme, scene rainbow, gradients, shadows, or decorative
animation is introduced.

### Layout at the existing 1280 × 720 design size

- Top identity rail: 52 px.
- Four scene plates: 580 px content region, two columns and two rows, matching
  the existing preset map.
- Bottom control rail: 88 px.

```text
┌─────────────────────────────────────────────────────────────────────┐
│ SCENES       AFTERGLOW                         BANK 03 / PRESET 2    │
├─────────────────────────────────┬───────────────────────────────────┤
│ FS 1                            │ FS 3 · LIVE                       │
│                                 │                                   │
│ VERSE                           │ SOLO                              │
│                                 │                                   │
│ Instant                         │ +2.0 dB · Instant                 │
├─────────────────────────────────┼───────────────────────────────────┤
│ FS 2                            │ FS 4                              │
│                                 │                                   │
│ CHORUS                          │ OUTRO                             │
│                                 │                                   │
│ 0.5 s                           │ 2.0 s                             │
├─────────────────────────────────┴───────────────────────────────────┤
│ PRESETS  TUNER  LOOPER  EDIT           BUFFER 38% USED   MASTER 80   │
└─────────────────────────────────────────────────────────────────────┘
```

All names, levels, and meter readings in this wireframe are illustrative.
The wireframe specifies hierarchy and placement, not a finished screenshot.

### Visual hierarchy

1. Scene names: existing Saira Condensed Semibold 72 where they fit; 52 for
   longer names. At most two lines, no scrolling marquee. If needed, truncate
   at a grapheme boundary with an ellipsis; show the full name in the editor.
2. Switch number and LIVE state: 28 px, consistent placement on every plate.
3. Preset identity: 28 px in the top rail; preset number remains visible if
   its name truncates.
4. Enter time and nonzero scene trim: 22–28 px secondary text. They are optional
   glance information, never prerequisites for choosing the right switch.

The live plate uses the palette's live lamp for its header and a 3 px border.
Inactive plates use neutral rules. The name remains high-contrast text. A
printed LIVE label and stronger border communicate state without color.

### States

| State | Device treatment |
| --- | --- |
| Stable live scene | One plate marked LIVE. |
| Transition | Destination marked GOING TO; a thin progress rule advances on that plate. Source loses LIVE; no two solid live lamps. |
| Waiting for engine acceptance | Destination shows a neutral pending marker; current live plate remains live. |
| Temporary controller change | Live plate also shows ALTERED, unless only the assigned continuous pedal is active, in which case show PEDAL. |
| Unsaved scene edits | Small persistent UNSAVED label in the top rail; no extra full-screen popup on recall. |
| Scene unavailable | Distinct fault border, UNAVAILABLE text, and an inspectable reason. Do not mark it live. |
| Tuner/looper lock or other rejection | Brief explicit reason; preserve current live indication. |

Progress starts from engine acknowledgment, not from a button animation.
Update visual progress at a bounded UI rate, around 20 Hz; DSP timing does not
depend on display refresh. Reduced-motion mode uses the text state and a
static destination marker; audio timing stays unchanged.

The footer offers Presets, Tuner, Looper, and Edit as separated touch buttons
with at least 60 px height. Reserve approximately the rightmost 320 px for
master volume, with buffer status above it. Settings remains reachable from
Presets; do not squeeze another icon into the scene rail.

Performance plates only recall scenes. Editing requires the labeled Edit
action; tapping or holding a plate never enters a rename or overwrite flow.

## 9. First-time setup and device scene editing

### Enable scenes

1. Open a preset's existing editor and choose Scenes.
2. A compact setup panel explains: “Four sounds using this rig. Footswitches
   select scenes in Scenes mode.”
3. Choose **Create four scenes**. All four copy the preset's current defined
   performance values, receive names Scene 1–Scene 4, Instant Enter time,
   and 0 dB trim. Scene 1 becomes default.
4. Prepare any required scene-capable runtime resources while existing audio
   continues. A preparation failure leaves the preset and audio unchanged.
5. Enter the scene editor with Scene 1 selected and an unsaved draft.
6. Rename, copy, and edit scenes, then Save preset.

This setup does not automatically create a loud Solo scene or enable effects.
Prepared-engine handoff may be required during initial setup; ordinary recall
after preparation must not require it.

### Editor structure

The existing chain canvas and parameter pages remain the workspace. Add a
scene strip directly below the editor header, approximately 64 px tall. Order
the compact strip 1, 2, 3, 4 with explicit FS labels; the performance view alone
needs the physical 2 × 2 layout.

The header reads **EDITING · SCENE 3 · SOLO**, shows UNSAVED when appropriate,
and offers Back and Save preset. A labeled Scene settings action opens name,
Enter time, trim, default-scene selection, and copy/reset operations.

Parameter controls show the editing scene's stored value and a small scope
label, **This scene** or **Shared**. The block enable control follows the same
scope rule. A secondary menu changes scope. Shared structural controls state
“Applies to all four scenes.”

Scene settings use a full-width native edit panel, not a small popover over
the live plates:

```text
┌─────────────────────────────────────────────────────────────────────┐
│ BACK          SCENE 3 · SOLO                        SAVE PRESET      │
├─────────────────────────────────────────────────────────────────────┤
│  1 Verse       2 Chorus       3 Solo [EDITING]       4 Outro         │
├──────────────────────────────────┬──────────────────────────────────┤
│ NAME                             │ ENTER TIME                       │
│ Solo                      RENAME │ [Instant] [Timed]        0.5 s  │
├──────────────────────────────────┼──────────────────────────────────┤
│ SCENE TRIM                       │ DEFAULT ON PRESET LOAD           │
│ −12 dB ─────────●────── +6 dB    │ [Make Solo default]              │
│                         +2.0 dB │ Current default: Verse           │
├──────────────────────────────────┴──────────────────────────────────┤
│ COPY TO…       SWAP SLOTS…       MORE…                               │
└─────────────────────────────────────────────────────────────────────┘
```

The numeric Enter time is disabled while Instant is selected. More contains
the uncommon scope-independent operations, including explicit sound capture
and Disable scenes. Open in is in a labeled Preset settings section there.
Rename opens the existing native text-entry pattern; no new keyboard design
is required. The settings view does not turn the volume encoder into a value
encoder. Illustrative values above are not factory defaults.

### Live preview on the device

- Choosing an editing scene recalls that draft scene. Its configured Enter
  time applies; show the transition as usual.
- Parameter edits change that scene's draft and preview with ordinary safe
  edit smoothing. They do not repeatedly restart the scene's Enter time.
- Editing a shared value changes the common draft once and previews it for
  the live scene.
- Hardware scene presses change both live and editing scenes while editing,
  unless a modal text entry or confirmation is open.
- If external recall arrives during a modal, audio may change, but the modal's
  editing target remains explicit and fixed. Finishing rename affects that
  named target; then the editor follows the current live scene.
- Tuner and safety actions always retain priority. Scene presses in a device
  confirmation dialog follow the dialog's displayed roles, never hidden scene
  actions.

### Edit and save rules

Scene edits stay in the preset draft when switching to another scene. No
Save/Discard prompt appears between scenes. Save preset persists all four scene
definitions, their settings, the shared rig, and mapping configuration together.

Leaving the editor for performance preserves the audible draft and UNSAVED
indicator. Loading another preset or bank with a dirty draft follows the
existing Save / Discard / Cancel flow; it must offer a foot-operable choice.
Specifically: FS1 Save and continue, FS2 Discard and continue, FS4 Cancel;
FS3 has no action. Consume releases before restoring the normal layer.

Undo/redo changes authored definitions only. Performance scene presses are not
undo-history entries. Undoing an edit to the live scene updates its preview;
undoing an edit to an inactive scene does not change the live sound.

### Scene operations

| Operation | Contract |
| --- | --- |
| Rename | Change name only. |
| Copy scene to… | Replace destination values, Enter time, and trim. Preserve destination ID, slot, name, default flag, and external bindings. Confirm overwrite; one undo step. |
| Reset scene to… | Choose another scene as source; same copy contract, clearly displayed. No ambiguous factory reset for a scene. |
| Swap slots | Move scene identities and names to the chosen physical slots. Default-scene and ID-based MIDI references follow identity. Preview the new footswitch map before committing; undoable. |
| Make default | Only changes which scene preset activation initializes. No immediate audio change. |
| Copy current sound to scene | Advanced explicit capture of supported, settled scene-owned runtime values; lists affected targets. Disabled during a transition. Does not capture master, mappings, tails, or looper state. |
| Disable scenes | Choose which scene to keep as the ordinary preset sound, fold its scene trim into preset output gain if representable, and confirm removal of scene data/bindings. Otherwise require resolving the gain range first. Undoable before save. |

There are always four valid scenes after enabling. No empty scene slot and no
Delete scene action exist in this release. An unchanged slot remains useful as
a duplicate or fallback sound.

## 10. Manager interface

Visitor mode: Operate. Detailed editing belongs here. Extend the current
workspace, preset sidebar, chain canvas, inspector, and Save / Apply controls.

### Desktop composition

```text
┌──────────────┬──────────────────────────────────────────────────────┐
│ PRESETS      │ AFTERGLOW       UNSAVED        SAVE    SAVE & APPLY  │
│              │ Live: SOLO       Editing: CHORUS                     │
│ Bank 03      ├──────────────────────────────────────────────────────┤
│ 1 ...        │ 1 Verse   2 Chorus [EDITING]   3 Solo [LIVE]  4 Outro │
│ 2 Afterglow  │ [Recall on pedal]   [Scene settings]   [Compare]     │
│ 3 ...        ├────────────────────────────────┬─────────────────────┤
│ 4 ...        │ Shared rig / signal chain      │ Selected parameter  │
│              │                                │ This scene / Shared │
│              │                                │ Value and units     │
└──────────────┴────────────────────────────────┴─────────────────────┘
```

Scene tabs select the editing scene only. **Recall on pedal** is the explicit
audio action. Live and editing indicators use distinct language and treatments:
the live lamp means sounding; editing uses a neutral underline/focus treatment.
The browser must not change stage audio just because someone inspects a tab.

### Save and apply semantics

- Save writes all draft scene and rig definitions to the device store. It
  does not activate them or recall a scene.
- Recall on pedal selects a scene from the acknowledged active runtime
  revision. Disable it when the edited document has unsaved changes or does
  not match that active revision, with “Apply this version to recall it.”
- Save & Apply saves, prepares, and activates the edited document, explicitly
  selecting the editing scene. The button's accessible description names it.
- Ordinary Apply also activates the editing scene in this workspace. External
  preset Program Change and ordinary preset selection use the default scene.
- If only scene data changed and the graph/resources are compatible, applying
  the new scene bank uses an atomic prepared update without replacing DSP.
  A graph or resource change uses the existing preset activation path.
- A failed save leaves the draft intact. A successful save followed by failed
  apply displays “Saved; pedal still playing the previous version.”

First release does not require live streaming of every unsaved browser slider
movement. On-device edits remain live previews; manager editing retains an
explicit Save & Apply workflow.

### Scene settings and comparison

Scene settings contain Name, Enter time, Scene trim, Default scene, Open in,
Copy to, and Swap slots. Preset-wide settings such as Open in are labeled as
applying to the whole preset, even when reached through scene settings.

Compare opens a parameter matrix grouped by lane and block:

| Parameter | Verse | Chorus | Solo | Outro |
| --- | --- | --- | --- | --- |
| Drive · enabled | Off | On | On | Off |
| Drive · amount | 25% | 40% | 55% | 25% |
| Delay · mix | 10% | 20% | 28% | 40% |
| Scene trim | 0 dB | 0 dB | +2 dB | −1 dB |
| Enter time | Instant | 0.5 s | Instant | 2 s |

Example values are illustrative. Default view shows differences only. Offer
Show all and Shared settings. Cells have physical units, labels, and keyboard
editing; copy one value across scenes through an explicit row action. A hidden
row with identical values is not an absent scene target.

### Smaller screens and accessibility

- Below roughly 1000 CSS px, collapse the preset sidebar to a drawer and use
  a full-width inspector sheet.
- Below roughly 700 CSS px, show scenes in a numbered 2 × 2 chooser. The
  inspector becomes a separate view with a persistent scene identity header.
- The comparison matrix becomes one parameter with four labeled values per
  group; avoid a tiny horizontally scrolling spreadsheet on a phone.
- Touch actions target at least 44 × 44 CSS px; device controls use their own
  larger native targets.
- All manager controls have keyboard access, visible focus, accessible names,
  and semantic selection states. Arrow keys navigate scene tabs; Enter/Space
  selects the editing tab. Actual recall remains the explicit Recall button.
- Announce accepted live scene changes accessibly, but do not announce every
  transition frame or expression sample.
- Preserve focus during live-status updates. At browser zoom and with long
  translated labels, controls wrap without hiding Save or scene identity.

## 11. MIDI and expression

### Named scene actions

Add explicit learn targets **Select scene 1/2/3/4** and **Show Presets/Scenes**.
These are distinct from existing parameter Continuous and Toggle bindings.
Rename the existing editor option “Toggle / Scene” to **Toggle values** so two
different concepts do not share the Scene label. Existing stored bindings keep
their meaning.

Named scene bindings reference stable scene IDs. A direct scene-select CC fires
on the low-to-high edge, threshold 64, consuming the release. Repeated high
values do not retrigger a transition. Optional compact **Scene number** binding
maps exactly CC values 0, 1, 2, 3 to the four current slots; other values are
ignored. Slot-based and ID-based bindings are visibly distinguished.

No new CC number is reserved by default. Existing bank select, Program Change,
tuner actions, and learned mappings remain available. A learn operation that
conflicts with another action on an overlapping channel/CC must offer Replace
or Cancel, naming the old action. A single CC cannot both select a scene and
alter its parameters. Import validation detects the same conflict.

Explicit Show Presets/Scenes actions set a layer rather than toggling it, so
retries are idempotent. They do not change the sound. Program Change still
selects presets and initializes their default scene.

### Controller ownership

One ordered control authority resolves device, MIDI, expression, and manager
commands. Scene recall writes the scene-owned baseline. Later accepted
performance control input may temporarily override its individual targets.
Recalling a scene clears those scene-owned overrides. Shared-target overrides
retain their existing behavior because scenes do not own those targets.

For two commands in the same control cycle, process local performance input
after network requests so a coincident local action wins. Otherwise accepted
arrival order wins. Scene requests include a runtime generation so a command
for an old preset cannot affect a newly loaded preset.

### Continuous controller pickup

After scene recall, stationary expression/CC input must not immediately
overwrite recalled values. Continuous bindings that overlap scene-owned targets
use **pickup**: ignore their value until the physical control crosses or comes
within 2% of the recalled target in the binding's normalized domain. For MIDI,
use a minimum tolerance of two CC steps.

Require fresh movement after recall before pickup can occur: at least 2% of
travel, or two MIDI steps, measured from the position observed at recall.
Repeated stationary samples never qualify. During a timed transition, the
pickup marker is the destination's stored value, not a moving intermediate
point. A pickup marker does not chase the pedal.

- Each target in a multi-target continuous binding picks up independently.
- If the scene target is outside the binding's reachable range, pickup occurs
  at the nearest endpoint; take control through a short safe ramp there.
- A binding with equal endpoints takes control only after fresh meaningful
  input following recall; there is no mathematical crossing to wait for.
- On connection/reconnection, observing a position establishes position only;
  it does not count as intentional movement.
- Until pickup, detail views say “Move pedal to pick up” and show a target
  marker. The performance screen does not display calibration instructions.
- Once picked up, the assigned target follows its ordinary controller law.
- If input takes over during a timed transition, cancel the scene ramp for
  that target only. Other targets finish their transition.
- Device parameter editing temporarily owns the edited target and rearms
  controller pickup, preventing a stationary pedal from fighting the slider.

Scene recall resets a legacy toggle binding's endpoint latch to endpoint 1
without emitting endpoint-1 parameter writes. Preserve its input-high state
until a real release is observed. Its next fresh press selects endpoint 2.
This keeps recalled scene values intact and prevents a held switch from
immediately undoing recall.

Only bindings with scene-owned targets are rearmed by recall. If a legacy
binding combines Shared and Per scene targets, split it into separately
tracked ownership groups internally: reset the scene-owned endpoint latch,
preserve the shared-target latch, and process the next input edge for both.
Explain the two scopes in the mapping editor. Do not reset unrelated shared
performance controls as a side effect of selecting a scene.

The UI labels scene-owned temporary divergence ALTERED, or PEDAL for ordinary
assigned continuous movement. Saving never captures either automatically.
Copy current sound to scene is the explicit capture operation.

Multi-target expression morphing between entire scene endpoints is deferred;
it needs a separate model for pedal position, destination identity, and bypass.

## 12. Tuner, looper, and exceptional device states

| Context | Rule |
| --- | --- |
| Enter tuner | Preserve the preset, destination scene, performance layer, and draft. Mute through the existing host path. An in-progress transition may complete under mute. |
| While tuner is open | Reject new scene/preset recall requests with Tuner active; do not queue them for later. Parameter performance writes are suppressed. |
| Exit tuner | Any footswitch exits without selecting a scene. Consume its release. Rearm controller pickup and restore the prior layer. |
| Looper open | Keep FS1–FS4's existing transport roles. Right-pair scene-layer gesture is disabled. |
| Scene selection while looping | Available via MIDI and a touch Scenes chooser opened from the looper. Chooser clearly says Footswitches control looper. |
| Looper Scenes chooser | Compact physical 2 × 2 scene map plus Back to looper; no scene editing. Hardware remains transport-only. |
| Scene recall during recording/overdub | Recorded audio includes the live scene and its transition. Existing recorded tracks are unchanged. |
| Preset lock during looping | Scene recall is permitted because the graph remains fixed. Preset replacement, structural edits, and scene-bank editing stay locked. |
| Save loop set | Include the complete source preset with scenes and the current destination scene ID for live sound on recall. No controller overrides or transition progress are serialized. |
| Load loop set | Initialize its captured source preset and scene before resuming audio. Start from defined values, not a recorded intermediate ramp. |
| Engine overload/fault | Existing safety behavior wins. Do not report new scene recalls as successful while the engine is unable to apply them. |
| Audio buffer change or restart | Reinitialize the preset's default scene, or the loop snapshot's scene when restoring that session; clear transient overrides. |
| Missing controller | Sound remains at the last valid runtime state; do not jump to a fabricated heel/toe value. |

No footswitch gesture for entering/leaving the looper is added here. Looper
access retains the existing touchscreen flow. All looper, tuner, and scene
mode changes clear pending gesture state before reassignment.

## 13. Persistence and compatibility

### Logical stored data — no code schema prescribed here

Scene-enabled presets use **preset format version 4**. Version 4 can contain
the existing serial, Dual Rig, or WDW topology plus the scene feature. It does
not alter their routing meaning.

Persist these concepts:

| Data | Contents |
| --- | --- |
| Shared rig | Existing blocks, stable block IDs, assets, routing, and shared values. |
| Scene target ownership | Explicit list of Per scene targets; all remaining eligible targets are Shared. |
| Scene definitions | Four stable scene IDs, four ordered slots, names, complete owned-target values, Enter time, and trim. |
| Preset scene settings | Default scene ID and Open in layer. |
| MIDI scene bindings | Explicit action type, channel/CC, edge/value behavior, scene ID or slot semantics. |
| Existing controls | Existing MIDI parameter bindings and expression assignment, preserved. |
| Tail policy | Shared per qualified delay/reverb block. |

Stable scene identity is independent of its name and slot. Block targets use
stable block IDs and canonical parameter identifiers, never display labels or
array offsets. Persist ownership so future firmware does not unexpectedly make
a newly supported parameter scene-specific on loading an old document.

Do not persist active scene on every performance press, transition progress,
controller pickup state, live tail buffers, transient overrides, UI selection,
or runtime acknowledgment IDs. Boot behavior is deterministic and performance
does not cause SD-card writes.

### Compatibility rules

- Loading/saving a scene-free version 1–3 preset preserves its format unless
  another explicit feature requires migration. Enabling scenes is opt-in.
- Older device/manager versions must reject unsupported version 4 with an
  explicit update-needed message; they must not flatten or drop scenes.
- Device capability information advertises maximum preset version, scene count,
  supported targets/behaviors, and scene recall availability.
- A newer manager disables unsupported scene actions on older firmware and
  explains the compatibility requirement.
- Existing two-endpoint MIDI bindings are not automatically converted into
  four named scenes. Optional migration assistance can come later.
- Backup/restore, preset copy, hosted read/write, and loop snapshots must
  round-trip scene data losslessly where version 4 is supported.
- Export a scene as an ordinary preset through an explicit flatten operation:
  resolve that scene's values and trim, remove named-scene actions, and keep
  compatible parameter mappings. Preserve existing routing's required version.
- Never reuse a deleted scene ID if future releases permit deletion.

### Structural edit reconciliation

Structural edits remain rig-wide and must reconcile scenes before activation:

- Add block: its eligible targets start with the same defaults in all scenes;
  respect existing routing validation. New effects start bypassed wherever
  permitted to avoid unexpected changes in the other three sounds.
- Delete block: remove its scene targets and affected MIDI/expression bindings
  in the same undoable edit; preview the number of affected references.
- Duplicate block: assign a fresh ID and copy each scene's values. Default the
  duplicate to bypassed when allowed; never duplicate external bindings silently.
- Replace algorithm: retain only targets with explicitly compatible semantics,
  not merely equal names. Show affected scene values; initialize new targets
  consistently across scenes and remove invalid mappings in the same edit.
- Move block/lane: stable IDs preserve scene values, but rerun ordering,
  capability, and whole-preset admission checks.

Every scene must be structurally valid. Mutually exclusive blocks cannot evade
validation merely because different scenes enable them at different times;
the prepared runtime and any tail overlap must also be valid and affordable.

## 14. Manager/device synchronization and command contract

The pedal owns live truth. Browser edit selection is never evidence of the
scene actually sounding.

Expose live preset location, active runtime revision/generation, live scene ID,
destination ID, transition state/progress, temporary override indication, last
accepted recall command ID, and fault/rejection reason. Publish this separately
from the stored preset document and the manager's editing draft.

Recall requests contain a unique request ID, expected active preset generation,
and destination scene ID. Repeated request IDs return the original result;
they do not restart transitions. Generation mismatch, unsupported scene,
tuner state, preset activation in progress, and engine unavailability receive
explicit rejection results. A command acceptance acknowledgment is distinct
from transition completion.

Scene recall is a transient performance action. Do not place it in a durable
queue that can replay stale scene presses after reboot or reconnection. Failed
or disconnected clients show Live state unknown and resynchronize before
enabling another recall.

Saving uses a stored revision check. If another client has edited the preset,
keep the local draft and offer Reload or Save as another preset; never silently
overwrite newer definitions. The device's dirty live draft also blocks an
external apply until resolved through the existing local edit-conflict flow.
Local footswitch scene recall within that draft remains available.

Initial live scene recall is available on the device and local manager. Hosted
manager scene document editing/read/write is capability-gated; hosted live
recall is deferred. Adding it later requires an explicit allowlisted transient
command and existing remote-mutation opt-in, not an accidental extension of
the four current preset operations. The UI labels unavailable remote controls
instead of pretending a cloud Save changed the live scene.

## 15. Audio and resource requirements

These are implementation obligations and proposed release gates, not measured
performance claims.

- Build and validate the scene target table outside realtime processing.
- Retain one rig's processors and required bypass/tail state, not four complete
  amp rigs. Preload assets and all scene-addressable processors before recall.
- Reserve resources for the worst admitted scene transition, simultaneous tails,
  and allowed legacy controller changes. Scene 1's CPU use is insufficient.
- Publish coherent scene generations. Dual Rig lanes must observe the same
  scene boundary; WDW worker jobs must carry a coherent scene generation with
  their audio generation. Old queued audio remains internally coherent until
  the boundary reaches output. Never mix one lane's old scene with another's new one.
- No audio callback or DSP worker allocation, mutex, filesystem access, asset
  decoding, unbounded parameter search, or UI work during recall.
- Continuous transition progress is sample-time based and independent of UI
  polling and browser latency. Discrete target application is bounded.
- Existing safety limiter, output mute, and overload policies retain authority.
- Scene recall adds no extra audio buffering or algorithmic latency to the rig.
- Proposed recognition-to-render target: start in the next available processing
  quantum for serial rigs; WDW includes its already configured pipeline. Measure
  control polling separately. This is not a claim of next-block response to a
  physical foot press or a network request.
- Instant controls should settle within 20 ms after their audible transition
  begins unless a documented, qualified processor requires more. Longer behavior
  must be visible in capabilities and approved before calling it Instant.
- Metered audio acknowledgments should reach the device display within 100 ms
  under normal load. UI lag must never delay sound.
- Initial finite bounds: four scenes, at most 512 scene-owned targets and 256
  MIDI action targets per preset. A host exceeding these limits must reject
  explicitly; no silent truncation. Verify these bounds against every admitted
  existing block configuration before implementation freezes them.

Admission failure at preset preparation keeps the previous rig sounding and
explains the costly blocks or unsupported combination. Never reduce effect
quality or drop a scene silently to make it fit.

## 16. Error and recovery copy

| Situation | Suggested copy / recovery |
| --- | --- |
| Scene preparation fails | “Scenes could not be prepared. Your current sound is unchanged.” Show the concrete reason. |
| Unsupported control | “Shared across scenes. This setting needs the rig to reload.” |
| Tail behavior unsupported | “This effect fades out on bypass. Let ring is unavailable.” |
| Stored/runtime mismatch | “Apply this version to recall its scenes.” |
| Save succeeded, apply failed | “Saved. The pedal is still playing the previous version.” |
| Tuner active | “Exit the tuner to change scenes.” |
| Scene editing with loop open | “Close the loop session to edit scenes. You can still select a scene.” |
| Controller pending pickup | “Move pedal to pick up.” Detail view shows current and target positions. |
| Lost manager connection | “Live state unknown. Reconnecting…” Do not show an old scene as confirmed live. |
| External edit conflict | “This preset changed elsewhere. Your edits are still here.” Offer Reload / Save as. |
| Version mismatch | “This preset uses scenes. Update the pedal to load it.” |

Use existing fault and warning tokens. Performance failures do not open a
screen-blocking modal unless the action inherently requires a decision about
unsaved work. A failed recall leaves the live scene selected.

## 17. Acceptance criteria

### Musical and audio behavior

1. From each of four scenes, recall every other scene and verify all owned
   target values, including values not touched by the preceding scene.
2. Recall under continuous playing without device restart, graph replacement,
   intentional silence, new buffer latency, allocation, or callback blocking.
3. Verify gain staging and looper placement: scene trim affects new recordings
   and live guitar, never already recorded layers or master volume.
4. Verify Instant and timed changes on gain, EQ, drive, mix, and lane controls
   with synthetic signals and listening material. No new clicks, invalid
   samples, unstable filters, or unexplained level discontinuities.
5. Interrupt ramps repeatedly and reselect the destination; confirm continuity,
   latest-request behavior, and correct completion acknowledgments.
6. Cut and Let ring use correct dry/wet behavior. Test bypass/re-enable during
   a tail, high feedback, long gaps between repeats, bounded tail expiry, and
   whole-lane mute. No stale-history burst on re-enable.
7. Stress serial, Dual Rig, and WDW independently with the worst admitted scene
   and controller combinations. Both lanes preserve generation coherence.
8. Run the production Raspberry Pi acceptance benchmark and at least a
   ten-minute soak with repeated recalls, UI use, expression, and manager
   activity; require zero scene-induced overruns, invalid samples, or generation
   mismatches. Compare identical workloads before/after and report actual data.

### Controls and state

9. Test switch timings just below/at/above chord and hold thresholds, bounce,
   late partner presses, canceled holds, three/four presses, tuner exit, and
   mode changes while switches are down.
10. A guitarist wearing ordinary stage footwear can reliably change layers;
    verify scene timing is acceptable with a rhythmic playing task. If the
    chord design fails, revise it explicitly before shipping.
11. Tuner exit never recalls a scene; looper mode never steals transport
    switches. Touch and MIDI scene recall during recording behave as specified.
12. Stationary pedals, duplicate MIDI highs, held toggles, and reconnects do not
    undo recall. Test multi-target pickup, reversed ranges, equal endpoints,
    unreachable scene values, and takeover during a transition.
13. Runtime overrides do not dirty documents; editing does. Reselecting an
    altered scene restores its targets. Capture current sound is explicit.

### Editing, data, and synchronization

14. Enable scenes without a tone change, edit two scenes, switch among them,
    save, restart, and verify exact definitions and default-scene initialization.
15. Copy, swap, scope changes, structural edits, undo/redo, flatten, disable,
    backup/restore, and loop snapshots preserve identity and valid references.
16. Version 1–3 round trips stay compatible. Old clients reject version 4
    explicitly. Invalid/missing/duplicate targets and non-finite values fail
    validation without replacing the active audio program.
17. Browser edit selection never recalls audio. Live markers follow engine
    acknowledgments; stored/active revision mismatches remain visible.
18. Test simultaneous device/browser edits, save success/apply failure,
    connection loss after command acceptance, duplicate IDs, superseded
    requests, generation changes, reboot, and stale network commands.

### Visual and usability checks

19. At standing height, users identify the live scene, its switch, and the
    current layer within one second. Test all device palettes and glare.
20. Check longest supported names, duplicate names, missing glyphs, fault and
    transition states, native scale factors, keyboard focus, screen-reader
    announcements, browser zoom, and narrow manager layouts.
21. First-use trial: a guitarist creates Verse and Solo, changes one bypass and
    output trim, saves, performs both, returns to presets, and reopens the rig
    without coaching. Record failures rather than assuming the flow is clear.

## 18. Delivery sequence

| Phase | Deliverable | Exit condition |
| --- | --- | --- |
| 1. Sound/control feasibility | Atomic scene recall prototype design, target inventory, bypass/tail classification, physical gesture trial | Validate continuity, worst-case resource budget, and switch timing before committing UI. |
| 2. Data and compatibility | Version 4 contract, ownership, scene identities, validation, migration/flatten rules | Round-trip and old-client rejection behavior specified and tested during implementation. |
| 3. Core performance | Four scenes, default recall, Instant and timed transitions, coherent serial/Dual Rig/WDW paths | Audio and interruption acceptance gates pass for each admitted topology. |
| 4. Device workflow | Performance layer, editor, creation/copy/scope/save, tuner/looper interactions | Hands-free and first-use trials pass. |
| 5. Controllers and manager | MIDI scene actions, pickup, live-status synchronization, comparison and explicit Apply | Controller conflict, stale-command, edit/live separation, and accessibility checks pass. |
| 6. Release qualification | Documentation, backups/loops, Pi soak, audio demonstrations, compatibility release notes | Measured acceptance evidence and no unresolved blocker in the contracts above. |

Scenes should ship as one coherent workflow. Early internal milestones can
support a narrower set of parameters, but unavailable controls and routing
families must be explicit. Do not ship a scene button that internally reloads
the rig while describing it as continuous scene switching.

## 19. Design review summary

The defining interaction is four large named plates that correspond to the
four real footswitches. The player chooses a sound; detailed ownership and
transition mechanics stay in editing views. The manager separately shows what
is being edited and what is sounding.

The largest implementation risks are coherent multi-target publication across
worker lanes, correct wet-tail bypass, controller pickup, and the gesture
latency introduced by hands-free layer changes. Their behavior is specified
above and each has an explicit validation gate.

This specification remains the product contract rather than an implementation
claim. The version-4 persistence foundation is implemented; realtime recall,
controls, and scene interfaces are still planned. No hardware measurements are
claimed.

## Repository references

- [Product context](../PRODUCT.md)
- [Device Panel design](lvgl-ui-redesign-spec.md)
- [Current preset screen](../src/ui/LvglUiPreset.cpp)
- [Native style tokens](../src/ui/LvglUiStyle.h)
- [Current footswitch gestures](../src/control/ControlEvents.cpp)
- [Preset model](../src/preset/Preset.h)
- [MIDI/expression contract](midi-expression-control.md)
- [MIDI mapping behavior](../src/control/Midi.cpp)
- [Runtime chain](../src/dsp/RuntimeChain.cpp)
- [Audio engine](../src/dsp/PedalEngine.cpp)
- [Current engine replacement contract](../src/audio/MiniaudioBackend.h)
- [WDW architecture](two-lane-wdw-dsp-design.md)
- [Looper specification](superpowers/specs/2026-08-30-looper-design.md)
- [Manager workspace](../apps/manager/src/presets/workspace/PresetWorkspace.tsx)
