# Wet/dry/wet UI layout mockup

The static page [`mockups/lvgl-redesign/wdw.html`](../mockups/lvgl-redesign/wdw.html)
is the visual contract for the first WDW UI pass. It is intentionally not wired to
LVGL, the manager, or a preset schema yet.

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

The next design pass should settle the persisted lane schema, exact touch gestures,
and manager API before any production UI code is changed.
