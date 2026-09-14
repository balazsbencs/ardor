# Wet/dry/wet UI layout mockup

The static page [`mockups/lvgl-redesign/wdw.html`](../mockups/lvgl-redesign/wdw.html)
is the visual contract for the first WDW UI pass. The manager now implements this
layout as the version-3 editor surface; the LVGL surface keeps the same two-lane
summary and exposes the lane mix controls without adding a raw-input escape hatch.

It shows two surfaces:

* **Device, 1280 × 720:** a glanceable split/join diagram with a complete dry
  mono contribution and a complete stereo wet contribution. The dry lane is
  pannable; the wet lane keeps its stereo identity after the cab and exposes a
  width control. The diagram never offers an always-on raw-input path.
* **Manager, desktop:** the same two lanes opened for authoring. Insertion points,
  mix controls, measured latency, CPU placement, pair-ring depth, and admission
  failures are visible here rather than crowding the stage display.

The mockup inherits the existing Panel direction in
[`docs/lvgl-ui-redesign-spec.md`](lvgl-ui-redesign-spec.md): flat slate planes,
one-pixel rules, Saira lettering, and family colour bars. “Dry” and “Wet” replace
the old left/right lane vocabulary for this topology; the colours identify signal
role, not output channel.

## Persisted contract

WDW presets use `version: 3`, `routing: "wdw"`, an empty top-level `blocks` array,
and two explicit lanes:

```json
{
  "version": 3,
  "routing": "wdw",
  "blocks": [],
  "wdw": {
    "dry": {"blocks": [], "levelDb": 0, "pan": 0, "enabled": true},
    "wet": {"blocks": [], "levelDb": 0, "width": 1, "enabled": true}
  }
}
```

The dry lane is mono at its lane boundary and may contain drive/utility stages;
the wet lane preserves stereo time-based stages. Each lane needs exactly one
enabled NAM; a cabinet IR is optional because a NAM capture may already include
the cab response. When present, the cab sits between NAM and time-based stages.
The runtime admission path remains the final authority for asset availability
and topology.

The device model represents the two lanes as a single split/join summary block so
existing touch navigation, bypass, and preview transactions remain safe. A newly
inserted WDW block starts with two empty lanes: the device lets the player author
the draft incrementally, but defers live preview until both lanes contain one
enabled, installed NAM. An installed cab may be added after NAM when a separate
IR is desired. After that point,
valid additions (such as Wet delay/reverb) preview normally; invalid Dry/Wet
placements are rejected at the lane drawer. Loading and saving that summary
round-trips the explicit WDW schema; it never serializes the summary as a legacy
version-2 Dual Rig.
