# LVGL Flat Knob Redesign

## Goal

Redesign the Ardor pedal LVGL interface for its 1280×720 touch display. The
UI should feel like a focused hardware editor: flat, restrained, and fast to
operate on stage or while shaping a sound. The redesign is visual and
interaction-focused; the preset format, UI model, and audio engine remain
unchanged.

## Approved visual direction

- Canvas: solid black (`#000000`).
- Surfaces: charcoal/dark grey (`#242424` family) with no visible borders,
  shadows, gradients, or raised-card effects. Every block-like surface has a
  5px corner radius.
- Functional accent: acid green (`#43F05A`) only for an active value arc, the
  selected chain block, the focused parameter label, and attention states such
  as an unsaved preset. It is not decorative panel fill or default text color.
- Primary typeface: Open Sans. Bundle LVGL-compatible Open Sans font assets
  with the application so the target and desktop simulator render identically;
  use regular weight for normal text and semibold only for headings/value
  emphasis. Remove all Montserrat use from `LvglUi`.
- Use high-contrast near-white text on black/charcoal, with muted grey labels.

## Screen structure

### Shared shell

The design coordinate system changes from 800×480 to 1280×720. Existing
uniform display scaling and inverse-transformed touch handling remain, so the
desktop simulator continues to work at other resolutions.

Each screen has a thin header: preset/bank identity on the left or centre,
status and actions on the opposite side. Header actions use flat charcoal
buttons with a 5px radius and no border. There are no shadowed or coloured
panels outside their functional states.

### Preset mode

The preset screen retains the existing bank and four-preset selection model.
Preset tiles are flat charcoal blocks. The active preset has a thin acid-green
indicator and clear text contrast. Edit and master-volume entry points remain
available without crowding the grid.

### Edit mode: Studio Chain

Edit mode uses the selected Studio Chain hierarchy:

1. Header with current preset, Presets, Global, Save, and Blocks actions.
2. A full-width left-to-right signal chain from Input to Output. Each enabled
   block is a charcoal module with category, block name, and asset name. The
   selected module uses acid-green category text and a small indicator; disabled
   modules are visibly dimmed but readable.
3. A selected-block control panel below the chain. It displays the block
   name/category and compact `PAGE n / total` indicator at the top, with a
   bottom-aligned row of up to six compact knobs.

Blocks retain tap-to-select, drag-to-reorder, and the existing drawer-driven
asset insertion flow. Block, global, and parameter drawers inherit the same
black/charcoal flat system.

### Parameters and paging

Every adjustable continuous parameter is displayed and changed through a
virtual knob. Effects that expose more than six parameters use pages of at
most six knobs. The page indicator is always visible. Left/right touch
targets and a horizontal swipe move between pages; page navigation is not a
parameter control and therefore is not represented by a knob.

Global input and output gain use exactly the same control panel and knob
language. Boolean state such as block enable/bypass remains an explicit on/off
switch, not an artificial knob. The bypass switch sits beside the page
indicator in the selected-block control-panel header.

## Knob design and interaction

The approved knob directly follows the supplied reference:

- A 270° sweep with a dark unfilled outer track.
- A slim acid-green arc from the minimum position to the current value.
- A dark charcoal centre with a black inner rim.
- A crisp white radial pointer showing the current position.
- Parameter name and formatted value below the dial; no numeric readout inside
  the dial.

Knobs are compact, with generous spacing in their six-control row. The
active/focused knob has an acid-green label and a subtle flat selection state;
it does not get a drop shadow, outline, or decorative glow. Values remain
readable below every knob, including unselected controls.

Touching a knob focuses it. A vertical drag over that knob changes it
continuously: up increases, down decreases. Updates are clamped to the
parameter range and immediately update the displayed arc, pointer, formatted
value, dirty state, and existing model value. The physical encoder fine-tunes
the last touched/focused knob. If no parameter is focused, it retains its
current master-volume behavior until the user focuses a control.

## Implementation boundaries

Implement the redesign in the existing LVGL renderer and preserve `UiState`,
the preset store, the UI-model operations, audio processing, and preset JSON
format.

Introduce reusable renderer helpers and metadata for:

- flat panels and action buttons;
- chain modules and selection states;
- Open Sans text styles;
- parameter descriptors (label, key, unit, range, step, formatting);
- virtual knob geometry, value rendering, focus, drag handling, and encoder
  adjustments.

Replace the current `+`/`−` parameter handlers with the virtual-knob control
path. Parameter descriptors must use the existing validated ranges and
setters, rather than duplicating or bypassing model validation.

## Error handling and accessibility

- Clamp all touch and encoder deltas through the existing parameter setters.
- Ignore drag events without a valid selected parameter; never dereference a
  missing selected block.
- Preserve a visible focus state for the encoder target.
- Keep text, current values, and enabled/bypassed state legible without relying
  solely on acid green.
- Use bundled fonts and explicit fallbacks so missing target system fonts do
  not alter the layout.

## Verification

- Build the project and run the existing UI and preset smoke tests.
- Run the LVGL simulator at native 1280×720 and verify the preset, chain,
  drawers, save state, reorder behavior, and telemetry display.
- Manually verify knob vertical drag, range clamping, formatted units, focus
  transfer, physical-encoder adjustment, and parameter-page navigation.
- Confirm no parameter editing control still renders as `+` or `−`.
- Inspect the screens for black backgrounds, borderless 5px-radius charcoal
  blocks, no shadows, Open Sans rendering, and acid green limited to
  functional state.
