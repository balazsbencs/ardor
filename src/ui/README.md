# UI architecture

The UI is split into model, orchestration, and retained LVGL views:

- `UiModel.*` owns UI state transitions and publishes typed revision counters.
- `LvglUi.cpp` owns the root canvas, view layers, revision dispatch, and
  interaction deferral.
- `LvglUiPreset.cpp`, `LvglUiEdit.cpp`, `LvglUiDrawer.cpp`,
  `LvglUiParameters.cpp`, `LvglUiSettings.cpp`, and `LvglUiTuner.cpp` own
  feature-level state and rendering.
- `LvglUiParameterRenderer.cpp` contains generic parameter controls;
  `LvglUiEqRenderer.cpp` contains the parametric-EQ graph and gestures.
- `LvglUiDrag.*`, `LvglChainLayout.*`, `LvglUiStyle.*`, and
  `UiStatusPresentation.*` contain shared mechanics without screen lifecycle
  responsibilities.

## Update rules

Model mutations should update the appropriate `UiRevisionSet` field. The
200 Hz UI loop compares those revisions and returns immediately when nothing
changed. Prefer synchronizing existing widgets for value-only changes; rebuild
a layer only when its topology changes.

LVGL event callbacks retain `UiEventContext` pointers. Contexts therefore live
in `LvglUi::contexts_`, a stable `std::list`, and are removed by
`UiContextRegion` when their owning layer is rebuilt.

Parameter views are cached by control-layout signature. Renderer-only LVGL
visual structs remain private to their renderer and are accessed by the narrow
interfaces in `LvglUiParameterView.h` and `LvglUiParameterWidgets.h`.

## Performance invariants

- Do not rebuild or restyle the full scene on an idle refresh tick.
- Do not delete a widget while an LVGL input interaction owns it; defer the
  rebuild until `endInteraction()`.
- Keep audio processing allocation-free. Snapshot atomic controls once per
  block and aggregate diagnostic counters before publishing them.
- Structural edits may capture a rollback snapshot. Successful live parameter
  changes should not.

Run `pedal-lvgl-ui-smoke` for retained-view and interaction coverage, and
`pedal-dsp-bench` after changing an audio hot path.
