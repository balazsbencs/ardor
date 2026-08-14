# LVGL Touch UI Redesign — Specification

Status: **proposed**, not implemented. Mockups are in `mockups/lvgl-redesign/`.

This document specifies a replacement visual system for the device touch UI. It changes
appearance, layout and state semantics. It does **not** change any product capability,
preset schema field, hardware contract, or audio behaviour.

---

## 1. Why this exists

The current UI derives depth from a top-lit gradient (`src/ui/LvglUiStyle.h:21-31`, the
`bg → panel` ramp from `#07090b` to `#18232a`). That looks correct in the SDL simulator and
bands visibly on the device.

This is not a tuning problem. `lv_conf.h:13` sets `LV_COLOR_DEPTH 16` under
`ARDOR_UI_BACKEND_FBDEV` because the Pi DSI framebuffer is RGB565; the simulator builds at
`LV_COLOR_DEPTH 32`. RGB565 provides 32 red levels, 64 green, 32 blue. A dark, low-contrast
ramp stretched across 720 px crosses a handful of quantisation steps, and LVGL applies no
dithering in its render path. A smooth result is not reachable.

**The whole visual system is therefore built from flat planes.** That constraint is the
origin of the design, not a limitation applied afterwards.

---

## 2. Design laws

These are binding. A change that violates one is a change to this specification.

1. **No gradients.** No `lv_grad_dsc_t`, no multi-stop fills, no soft shadows, no glows, no
   feathered edges. Depth comes from flat planes at different values, 1 px rules, and hard
   edges.
2. **Value separation over hue separation.** Two adjacent surfaces must differ by a step
   readable at a steep angle in low light, not only by hue.
3. **One reserved LIVE colour per palette.** It marks the running patch and the selected
   parameter. It appears nowhere else — not on buttons, not on success states, not on errors.
4. **Faults are never carried by hue alone.** Every fault state carries at least three
   signals: a lamp, a distinct border, and a diagonal hatch or explicit legend text.
5. **Secondary lettering is tinted from the plate hue.** Never neutral grey on a tinted plate.
6. **Every touch target is ≥ 44 px** in its smallest dimension, including text-only controls
   (`lv_obj_set_ext_click_area`).
7. **Tiled images, not object arrays, for repeating texture.** Hatches and grids ship as
   small RGB565 tiles.

---

## 3. Direction

**Panel** — the nomenclature plate of an instrument. Not a rendered panel with simulated
knobs; the engraved graphic system itself: bracketed function groups, printed travel scales
with numerals, patch-point circles, section legends, indicator lamps.

Rejected alternatives, retained in `mockups/lvgl-redesign/` for reference: Floor (stage-floor
setlist), Plate (one-bit dithered), Sheet (engineering drafting), Cyc (lighting cue stack),
Sleeve (record-sleeve restraint).

Mockup files:

| File | Contents |
|---|---|
| `panel.html` | Preset, edit chain, split, tuner, parameters — original olive palette |
| `panel-palettes.html` | The three candidate palettes on identical geometry |
| `panel-system.html` | Module drawer, toasts, unsaved dialog, settings, EQ editor |
| `panel-states.html` | Keyboard, asset library, factory reset, setup code, empty states, bypass, boot |
| `index.html` | Index across all directions |

Screen inventory — every screen the device draws is specified:

| Screen | Mockup | Phase |
|---|---|---|
| Preset (2 × 2) | `panel.html` § 1 | 4 |
| Edit chain | `panel.html` § 2 | 5 |
| Split lanes | `panel.html` § 2b | 5 |
| Module drawer | `panel-system.html` § A | 5 |
| Asset library | `panel-states.html` § 2 | 5 |
| Parameters | `panel.html` § 4 | 6 |
| Parametric EQ | `panel-system.html` § E | 6 |
| Tuner | `panel.html` § 3 | 6 |
| Settings + palette | `panel-system.html` § D | 7 |
| Text entry / keyboard | `panel-states.html` § 1 | 7 |
| Toasts | `panel-system.html` § B | 7 |
| Unsaved dialog | `panel-system.html` § C | 7 |
| Factory reset | `panel-states.html` § 3 | 7 |
| First-boot setup code | `panel-states.html` § 4 | 7 |
| Empty bank | `panel-states.html` § 5 | 7 |
| No captures installed | `panel-states.html` § 6 | 7 |
| Overload bypass + clip | `panel-states.html` § 7 | 7 |
| Boot | `panel-states.html` § 8 | 7 |

---

## 4. Palette system

Palette is a **runtime theme**, not a colour picker. The player selects a named palette;
they never assemble one from individual colours, because the relationships between LIVE,
plate, lettering and the six family colours are the thing being chosen.

### 4.1 Tokens

Eleven tokens plus six family colours. Every screen is built from these and nothing else.

| Token | Role |
|---|---|
| `plate` | Screen ground |
| `plate2` | Module and card body, one value step up |
| `plate3` | Recess: headers, list grounds, lamp cells |
| `engrave` | Primary lettering |
| `engrave_lo` | Secondary lettering, legends, tick marks |
| `engrave_off` | Disabled and bypassed lettering |
| `rule` | 1 px rules and borders |
| `lamp` | **LIVE — reserved** |
| `warn` | Caution: sequential fallback, truncated IR, missing-asset legend |
| `faultline` | Fault borders |
| `faulttext` | Fault lettering. `danger` maps here; there is no separate token |
| `laneL` / `laneR` | Split lane identity (replaces `accent` / `rigRight`) |
| family × 6 | `amp`, `cab`, `util`, `mod`, `dly`, `verb` |

### 4.2 The three palettes

**Slate — default.** Cool neutral graphite, signal red.

```
plate #212528  plate2 #2a2f33  plate3 #191c1f
engrave #e2e4e3  engrave_lo #8d9499  engrave_off #5b6266  rule #3b4247
lamp #d8422f   warn #c9973f  faultline #6b463c  faulttext #bb9186
laneL #7fa6c8  laneR #c9a06a
amp #a8814e  cab #939a9e  util #5f7f9c  mod #5d8f80  dly #8175a0  verb #a8785c
```

**Ink.** Deep navy, ice cyan. Most legible at a steep angle.

```
plate #10161f  plate2 #182130  plate3 #0b1017
engrave #dde6ee  engrave_lo #7e8fa3  engrave_off #4d5b6b  rule #2a3646
lamp #5fd0e8   warn #d9a04e  faultline #5c3946  faulttext #c08e97
laneL #6d8fd0  laneR #c99050
amp #c99050  cab #8296ab  util #6d8fd0  mod #4fa89a  dly #8d7fc4  verb #cf7a86
```

**Sodium.** True black, warm bone, sodium amber. Preserves dark adaptation across a long set.

```
plate #0c0b09  plate2 #16140f  plate3 #070605
engrave #f0e4cd  engrave_lo #9a8f7a  engrave_off #5e574a  rule #302b22
lamp #ffb01f   warn #c98a3c  faultline #5d3a2e  faulttext #c19183
laneL #7f9ab5  laneR #c9924f
amp #b06a3a  cab #9a8f7a  util #6f8296  mod #5f9280  dly #8b7aa5  verb #b3705f
```

> **Slate's LIVE colour is red**, so error states in Slate must not be red. This is why law 4
> exists. Verify the fault treatment in all three palettes, not just the default.

### 4.3 Implementation

`src/ui/LvglUiStyle.h` currently exposes `extern std::uint32_t accent` plus eight
`inline constexpr int` colours. Replace both with one theme struct:

```cpp
struct PanelPalette {
  std::uint32_t plate, plate2, plate3;
  std::uint32_t engrave, engraveLo, engraveOff, rule;
  std::uint32_t lamp, faultLine, faultText;
  std::uint32_t family[6];   // amp, cab, util, mod, dly, verb
};
const PanelPalette& palette();          // current
void setPalette(PaletteId id);          // triggers a full rebuild
```

`categoryColor()` (`LvglUiStyle.h:49`) keeps its signature and reads `palette().family[]`.

Persistence extends `DeviceSettings` in `src/ui/GlobalSettings.h`:

- Add `PaletteId paletteId = PaletteId::Slate;`
- Add `bool savePalette(PaletteId, std::string& error) const;` to `GlobalSettingsStore`

A palette change is a full-scene rebuild. It is a settings action, never on an audio path,
so the cost is irrelevant — but it must go through the same deferral as any other structural
rebuild (`endInteraction()`, per `src/ui/README.md`).

### 4.4 Retiring `accentColor` — decided, and larger than it looks

**Decision: the user-selectable accent colour is removed entirely.** Palettes replace it.
There is no migration and no mapping to a "nearest palette"; a stored `accentColor` is
ignored and the device starts on Slate.

**No external coupling.** `accentColor` appears only in device-local code. It is not in the
preset schema, not in the managerd REST surface, and not in the Tauri or hosted manager. The
`accent` hits under `services/managerd/internal/webui/dist/` are the built web bundle's own
CSS custom property and are unrelated. Retiring it cannot break a client.

**But `accent` is overloaded.** It is referenced at roughly sixty call sites across fifteen
files, and it is doing the work of at least five distinct semantic roles. Phase 1 is not a
find-and-replace; it is a token disambiguation. Every site must be classified:

| Current use | Example | New token |
|---|---|---|
| Active preset card | `LvglUiPreset.cpp:129-130, 200, 205, 215` | `lamp` |
| Tuner needle and centre tick | `LvglUiTuner.cpp:68, 83` | `lamp` |
| Tuner "in tune" verdict | `LvglUiTuner.cpp:118` | `lamp` (warn/fault for the other two) |
| Selected block / drawer item | `LvglUiEdit.cpp:466`, `LvglUiDrawer.cpp:528-530` | `engrave` |
| Drag indicator | `LvglUiDrag.cpp:33, 53` | `engrave` |
| Pressed-state border on every control | `LvglUiStyle.cpp:54` | `engrave` |
| Dirty / unsaved Save label | `LvglUiPreset.cpp:114`, `LvglUiEdit.cpp:325` | `warn` |
| MIDI / expression connected lamp | `LvglUiStatus.cpp:121, 128, 188, 198` | `mod` (ok) |
| Status text, non-error | `LvglUiStatus.cpp:138, 218, 230` | `engrave` |
| Status text, error | same lines, `danger` branch | `faulttext` |
| Split lane L | `LvglUiEdit.cpp:207, 564` | `laneL` |
| Split lane R (`rigRight`) | `LvglUiEdit.cpp:207, 564` | `laneR` |
| EQ band enabled | `LvglUiParameters.cpp:390`, `LvglUiEqRenderer.cpp:438` | `lamp` if selected, else `engrave_lo` |
| Parameter slider fill / outline | `LvglUiParameterRenderer.cpp:188, 415, 541` | `engrave`, `lamp` when selected |
| Settings section highlight | `LvglUiSettings.cpp:286` | `engrave` on `plate2` |
| Keyboard and focused field | `LvglUiSettings.cpp:367, 415` | `engrave` |
| `categoryColor()` fallback | `LvglUiStyle.cpp:72` | `engrave_lo` — **not** a family colour |

> The single most common mistake here is mapping everything to `lamp` because it "was the
> accent". Most of these are *emphasis*, not *live*, and belong on `engrave`. Design law 3
> means `lamp` must end up on the active preset, the selected parameter, the tuner's in-tune
> state and nothing else.

**Delete outright:**

- `kAccentColors` and the six-swatch picker (`LvglUiSettings.cpp:17-35, 301-341`)
- `LvglUi::selectAccentColor` (`LvglUiSettings.cpp:170`, decl `LvglUi.h:160`)
- `onAccentColorClicked` (`LvglUiSettings.cpp:38`)
- `Actions::saveAccentColor` (`LvglUi.h:57`)
- `GlobalSettingsStore::saveAccentColor` (`GlobalSettings.cpp:210`, decl `GlobalSettings.h:29`)
- `accentColor` field and `kDefaultAccentColor` (`GlobalSettings.h:9, 12`)
- The `accentColor` read in `GlobalSettings.cpp:169-170` and write at `:217`
- `extern std::uint32_t accent` (`LvglUiStyle.h:36`, defn `LvglUiStyle.cpp:5`)
- `rigRight` (`LvglUiStyle.h:29`) — superseded by `laneR`
- The accent initialisation in `LvglUi.cpp:95-96`

**Signature change.** `makeTelemetryPresentation(state, accent)` takes an accent colour
(`UiStatusPresentation.h:16`, `.cpp:16, 38`). Change it to take the `PanelPalette` reference,
or to return a semantic enum the view maps to a token — the latter is better, because the
presentation layer should not know about colours at all.

**Tests to update.** These assert the retired behaviour and will fail:

- `tests/global_settings_smoke.cpp:37, 46` — assert `accentColor` persists as `0x67a6ff`.
  Replace with equivalent assertions on `paletteId`.
- `tests/lvgl_ui_smoke.cpp:631` — *"success status should render in accent green"*. The new
  expectation is the palette's `mod` token.

Record the removal in `CHANGELOG.md`: it is a visible behaviour change for anyone who set a
custom accent.

---

## 5. Typography

Two families replace `OpenSansRegular` / `OpenSansSemibold`.

| Role | Face | Weights | Sizes (px) |
|---|---|---|---|
| Nomenclature, legends, names, buttons | Saira Condensed | 500, 600 | 11, 12, 13, 14, 17, 19, 23, 26, 38 |
| Numerals, readouts, measured values | Saira | 300, 400 | 12, 13, 19, 23, 44, 46 |
| Tuner note designator | Saira Condensed 600 | 600 | 184 |

Both are SIL Open Font License, compatible with the project's MIT licence provided the
licence file ships alongside.

### Font budget

Each size is a separate compiled LVGL font. Rules:

- Subset the 184 px tuner face to `A B C D E F G ♯ 0-9` — twelve note letters plus octave
  digits. A full ASCII set at that size is several hundred KB.
- Subset the Saira numeral sizes to digits, `.`, `-`, `+`, `%`, `k`, `H`, `z`, `d`, `B`,
  `m`, `s`.
- Compile at 1bpp where the design uses solid lettering on a flat plate; reserve 4bpp for
  the two largest display sizes only. On a 16-bit panel, 4bpp antialiasing on small type
  produces colour fringing that 1bpp does not.
- Enable `LV_USE_FONT_COMPRESSED` — already set at `lv_conf.h:9`.

**Verify on hardware before committing.** Thin stems at large sizes and antialiasing
behaviour on the DSI panel cannot be judged in the SDL simulator.

---

## 6. Layout

Design grid stays **1280 × 720** (`LvglUiStyle.h:14-15`). The existing fixed-canvas-plus-scale
approach in `LvglUiUi.cpp:110` is unchanged.

| Element | Size |
|---|---|
| Top legend rail | 52 px, 1 px bottom rule, 28 px side padding |
| Bottom control rail | 88 px, 1 px top rule, 28 px side padding |
| Content area | 580 px between rails |
| Module drawer | 480 px (keeps `kBlockDrawerWidth`) |
| Spacing scale | 4, 8, 11, 14, 18, 22, 28 px |
| Rules and borders | 1 px, always |

`kStatusBarHeight` (48) and `kHeaderButtonHeight` (60) are superseded by the rail heights
above. `kHeaderBlocksButtonWidth` (164) is superseded — buttons size to their label.

---

## 7. Hardware mapping

`enclosure.openscad:3` and `:91-95` place four footswitches at the four corners of the top
panel, inset 22 mm, with the display centred between them.

**The preset screen is a 2 × 2 grid whose quadrants correspond to the physical switch
corners.** Each tile carries its switch label in its own outer corner with a bracket tick.
This removes the translation step between "patch 3" and "which pedal is that".

Mapping — **confirmed**, and now recorded in `docs/hardware-assembly.md` § Footswitch
physical positions:

```
   FS 1  ┌─────────┬─────────┐  FS 3
         │ Patch 1 │ Patch 3 │
         ├─────────┼─────────┤
   FS 2  │ Patch 2 │ Patch 4 │  FS 4
         └─────────┴─────────┘
```

| Switch | Corner | Keycode | Slot | Grid cell |
|---|---|---|---:|---|
| FS 1 | Top-left | `KEY_F1` | 1 | row 1, col 1 |
| FS 2 | Bottom-left | `KEY_F2` | 2 | row 2, col 1 |
| FS 3 | Top-right | `KEY_F3` | 3 | row 1, col 2 |
| FS 4 | Bottom-right | `KEY_F4` | 4 | row 2, col 2 |

Slot order in the grid is therefore **1, 3 / 2, 4** — reading order does not apply, because
the geometry answers to the enclosure rather than to a list.

`LvglUiPreset.cpp:196-197` currently assigns cells row-major (`i % 2` column, `i / 2` row),
producing 1, 2 / 3, 4. The two expressions swap. See § 13.1a.

The left column being FS1 + FS2 is consistent with the existing tuner gesture in `README.md`
(*"Hold the two left switches (`KEY_F1` + `KEY_F2`)"*), and with FS1 approving / FS4
cancelling a factory reset — approve and cancel sit on opposite corners, which is a good
property to preserve.

Footswitch parity is unchanged: preset switching and tuner entry remain reachable without
touching the screen, per `PRODUCT.md` § Accessibility.

---

## 8. Component library

Build these once in `LvglUiStyle.cpp` and reuse. Several replace ad-hoc constructions.

### 8.1 Lamp
11 × 11 px solid square. States: off (`rule`), live (`lamp`), fault (`verb`), ok (`mod`).
No radius, no glow — a glow is a ramp.

### 8.2 Legend text
Uppercase, letter-spaced 0.26 em, `engrave_lo`. LVGL has no letter-spacing property on a
label style beyond `text_letter_space`; set it explicitly per style.

### 8.3 Rail + Thumb
A recessed 18 px horizontal rail with a solid fill and a 44 × 54 px grab handle. `MIN` and
`MAX` legends anchor the direction without competing with the live value above. The visible
thumb and full control card both accept the drag, so the affordance is recognizable while
the touch target stays forgiving. Discrete choices use the same card anatomy with 60 px
segmented targets. The existing 3 × 2 parameter-page layout remains unchanged.

### 8.4 Module tile (preset screen)
Header strip 26 px with legend and lamp; body with numeral, name, and a nomenclature row of
family ticks (26 × 3 px rule above each label). Live tile: border and header take `lamp`,
numeral takes `lamp`.

### 8.5 Block card (edit screen)
150–168 px wide, 64 px family-coloured header, body ≥ 250 px with name, readout, and a
3-segment group rule. The full header is a labeled `DRAG` surface rather than containing a
small grip; its 64 px height is deliberate for reliable touchscreen acquisition. A tap on
either header or body still opens the block. Compact Dual Rig lane cards use the same pattern
with a 52 px header. States: normal, selected (`engrave` border), bypassed (`plate3` body,
`rule` header, `engrave_off` text).

### 8.6 Patch point
15 px circle, 2 px `engrave_lo` border, on a 1 px connecting rule. Insertion variant is
19 px with a `+` glyph and a `rule` border.

### 8.7 Bypass jumper
An `lv_line` drawn above the module layer in `lamp`, entering and leaving at the patch points
either side of the bypassed block, with a `BYP` legend. Requires correct z-order during drag —
this is the one component that touches `LvglUiDrag.cpp`.

### 8.8 Annunciator toast
See § 10.

### 8.9 Dialog
660 px wide, centred, `engrave_lo` border, 40 px legend header, body, action row. Committing
actions grouped left, Cancel separated by flexible space.

### 8.10 Keyboard key
82 × 44 px plate, 1 px `rule` border, `plate2` fill, engraved cap. Commit key (Enter) takes
`engrave` fill with `plate` text. Modifier and secondary keys (Shift, Del, 123) keep
`engrave_lo` lettering at 12 px. Never a rounded or shadowed key — it must not read as a
borrowed OS keyboard.

### 8.11 Icons — there is no icon library

This design is **lettering-first by construction**. Controls are named, not pictured: `Edit`,
`Tuner`, `Bank −`, `Save`, `Modules`, `Done`. That is a deliberate property of a nomenclature
plate and it removes an entire asset pipeline.

Only four glyphs appear anywhere, and all four are text:

| Glyph | Use | Source |
|---|---|---|
| `+` | Insertion point on a rail | Compiled font |
| `✕` | Drawer close | Compiled font |
| `◀` `▶` | Prev / next parameter page | Compiled font |

Subset these into the 14 px Saira Condensed face. **Do not add `lv_symbol` / FontAwesome**;
its glyphs are a different drawing language and will look pasted on. If a future control
genuinely cannot be named in two words, draw it with `lv_line` in the panel's own 1 px
weight — the bracket ticks on the preset tiles are the reference for how that should look.

Two existing decorative marks are retired: the settings gear icon
(`LvglUiStatus.cpp:210`) becomes the word `Setup`, and the category accent bar in the drawer
(`LvglUiDrawer.cpp:303, 516`) becomes the 26 × 3 px family tick from § 8.4.

---

## 9. Screens

| Screen | Source file | Change |
|---|---|---|
| Preset | `LvglUiPreset.cpp` | Grid cells transposed to column-major; card restyled (§ 13.1) |
| Edit chain | `LvglUiEdit.cpp`, `LvglChainLayout.cpp` | Restyle; bypass becomes a jumper |
| Split lanes | `LvglUiEdit.cpp` | Restyle as a bracketed module group |
| Module drawer | `LvglUiDrawer.cpp` | Restyle; filters wrap to 4 + 3 rows; footer count |
| Parameters | `LvglUiParameters.cpp`, `LvglUiParameterRenderer.cpp` | Sliders become travel scales |
| EQ editor | `LvglUiEqRenderer.cpp` | Curve stroke only, no filled area |
| Tuner | `LvglUiTuner.cpp` | Engraved centre-zero meter, three-lamp verdict row |
| Settings | `LvglUiSettings.cpp` | **New section** — palette picker; accent picker retired |
| Text entry | `LvglUiSettings.cpp` | Restyle the existing `lv_keyboard` per § 8.10 |
| Asset library | **new** — new nav state in `UiModel` | Full-screen; not the 480 px drawer (§ 13.5e) |
| Status | `LvglUiStatus.cpp`, `UiStatusPresentation.cpp` | Folds into the two rails; clip annunciator to the top rail |
| Cloud claim | `CloudClaimOverlay.cpp` | Adopt the dialog component |
| Toasts | **new** — queue in `UiModel`, view in `PanelComponents` | New subsystem (§ 13.7a) |
| Factory reset | **new** | Touch disabled; FS1 approves, FS4 cancels |
| Setup code | **new** | First boot, short-lived pairing code |
| Boot | **new** | Discrete progress blocks, no spinner |
| Empty bank / no captures | `LvglUiPreset.cpp`, `LvglUiEdit.cpp` | Empty states (§ 13.7e) |

### Notes on specific screens

**Preset.** Four tiles. Live tile differs by lit lamp, full-chroma header, `lamp` numeral and
`lamp` border. Fault tile takes `faultline` border, a `verb` lamp and a legend chip naming
the missing asset. Master volume and headroom live in the bottom rail as an engraved scale.

**Module drawer.** 480 px from the right, dimming the chain behind rather than covering it,
so the chosen insertion point stays visible as a dashed slot on the rail. Seven filters in
4 + 3 rows. Footer carries the visible/total count.

**EQ editor.** Response drawn as a 2.5 px stroke, **no filled area under the curve** — a soft
fill is a ramp and would band exactly like the old gradients. Selected band takes `lamp` in
both graph handle and selector chip. Disabled band greys to `engrave_off` and its handle
hollows. Interaction model in § 9.1.

### 9.1 EQ interaction — three parameters on two axes

A band has frequency, gain and Q; the graph offers only x and y. Q therefore gets three
routes, in order of precedence.

**1. Shoulder grips (spatial).** When a band is selected it grows two grips sitting on the
curve at its **−3 dB points**, joined by a dashed bandwidth span carrying the Q value in both
Q and octaves. Dragging either grip inward narrows the band, outward widens it; the pair is
symmetric about the centre frequency, so one finger is enough. The grips sit exactly on the
bandwidth edges they control, so the gesture and the physics are the same operation rather
than a borrowed convention.

- Grip: 13 × 26 px, `lamp` border, `plate` fill, with a 44 px extended click area.
- Only the selected band shows grips. Five bands' worth would be unusable.
- Clamp to the band's real range (`q` 0.1–18 per `README.md` § Supported Parameters).

**2. Band editor strip (reliable).** The selected band opens a full-width strip with Freq,
Gain and Q as three engraved travel scales. This is the path that requires no discovery,
survives a boot on the foot, and works when the curve is too crowded to hit a grip
accurately. **It is the primary path; the grips are the fast path.**

**3. Encoder (fine).** The rotary encoder adjusts whichever control is highlighted, with the
band editor highlighting Q by default. Q is precisely the parameter that wants a fine
continuous control rather than a 200 px drag, and the encoder is otherwise idle on this
screen.

> Encoder context switching is new behaviour. Today the encoder is master volume everywhere
> (`README.md` § Realtime Run). Scope it: the encoder retargets **only** while a parameter or
> EQ editor is open, and reverts to master volume on exit. Master volume must stay reachable —
> it is a live performance control and cannot be captured by an editing screen without a way
> back. Implement in `LvglUiNavigation.cpp` alongside the existing screen state.

Handle drag continues to set frequency and gain together. Band type (peaking, shelf, pass)
is a control-rail action rather than a gesture.

**Tuner.** Note designator at 184 px. Centre-zero engraved meter with major ticks at
±50/±25/0. Verdict as a three-lamp row (Flat / In tune / Sharp) so the state is readable
without parsing a number.

---

## 10. Toasts

Rendered as **annunciator strips**, not floating pills: a plate with a lamp cell on the left,
matching the caution annunciators on the instruments this world derives from.

| Class | Lamp | Border | Extra | Use |
|---|---|---|---|---|
| `info` | `engrave_lo` | `rule` | — | Live edit applied, module inserted |
| `ok` | `mod` | `rule` | — | Patch saved, settings written |
| `warn` | `amp` | `amp` | — | Sequential fallback, IR truncated, block-size mismatch |
| `fault` | `verb` | `faultline` | diagonal hatch | Asset missing, save failed, engine bypassed |
| neutral | `engrave_off` | `rule` | action button | Undoable actions |

Rules:

- Width fixed at 660 px. Position centred, 104 px from the bottom, above the control rail.
- Maximum **two** visible; a third replaces the oldest.
- Dismiss after 4 s (`info`, `ok`, neutral). `warn` and `fault` persist until acknowledged
  or the condition clears.
- A toast with an action button (Undo, Details, Choose) does not auto-dismiss.
- Toasts never use `lamp`. That colour means LIVE only.

Real events worth wiring, drawn from existing telemetry: parallel-worker realtime-policy
failure (`README.md` § Realtime Run), overload bypass latch, missing preset asset, save
success and failure, live-parameter publish confirmation.

---

## 11. Motion

One authored moment, not scattered effects.

- Drawer: single-axis slide, 160 ms, ease-out. Nothing else animates during it.
- Toast: 120 ms entry along one axis. No scale, no fade-in from zero opacity.
- Patch change: no transition. The screen changes at once — a player switching mid-song must
  not wait on an animation.
- Never animate colour across a hue. On RGB565 a colour tween is a visible staircase.

---

## 12. Legibility and touch

- Body and legend text ≥ 4.5:1 against its plate; large display text ≥ 3:1. Measure on
  device, not in the simulator — panel gamma and viewing angle change the result.
- All touch targets ≥ 44 px including text-only controls.
- Colour is never the only carrier of a state. Live also has a lamp and a border; bypassed
  also has a recessed body and a jumper; fault also has hatch and legend text.
- Verify at a steep angle in low light, standing, with the pedal on the floor. This is the
  primary operating scene per `PRODUCT.md` § Operating Context.

---

## 13. Implementation phases

**Phase 1 — Theme foundation.** `PanelPalette` struct, three palettes, `setPalette()`,
`GlobalSettings` persistence and `accentColor` retirement. No visual change beyond colour.
Existing screens keep working. *Verifies independently: run `pedal-ui-sim`, switch palettes.*

**Phase 2 — Fonts.** Compile the Saira subsets, replace the Open Sans references, check on
the device panel. Revert to Open Sans is a one-line change if the hardware check fails.

**Phase 3 — Shared components.** Lamp, legend, travel scale, block card, patch point,
toast, dialog in `LvglUiStyle.cpp`. Migrate `button()` and `label()` callers.

**Phase 4 — Preset screen.** Cell transpose plus restyle. See § 13.1 for the detailed
breakdown. Highest user-visible value; ship it alone if nothing else lands.

**Phase 5 — Edit, drawer, split.** Block cards, patch points, bypass jumper, drawer
restyle. Touches `LvglUiDrag.cpp` for jumper z-order.

**Phase 6 — Parameters, EQ, tuner.** Travel scales, curve stroke, engraved meter. Includes
the EQ interaction model (§ 9.1): shoulder grips, band editor strip, and encoder retargeting.
Land the band editor strip **before** the grips — it is the path that must always work, and
the grips are an optimisation on top of it.

**Phase 7 — Settings, toasts, dialogs.** Palette picker UI, toast queue, unsaved-changes
dialog, `CloudClaimOverlay` adoption.

### Dependency graph

```
1 Theme ──┬── 3 Components ──┬── 4 Preset
          │                  ├── 5 Edit / drawer / split
          │                  ├── 6 Parameters / EQ / tuner
          │                  └── 7 Settings / toasts / first run
          └── (2 Fonts is independent — slot it anywhere)
```

- **Phase 1 blocks everything.** Nothing else can reference a token before it exists.
- **Phase 2 blocks nothing.** Every layout works in Open Sans; it just lacks the engraved
  character. Do it when a device session is available.
- **Phase 3 blocks 4–7.** Building the preset tile before the lamp and legend exist means
  writing them twice.
- **Phases 4, 5, 6, 7 are mutually independent** and can be done in any order or in parallel.
- **§ 13.1a (the cell transpose) has no dependencies at all** and can ship on its own today.

Each phase is independently shippable and independently revertible.

---

### 13.1 Phase 4 in detail — preset screen

All work is in `src/ui/LvglUiPreset.cpp` and the member declarations in `src/ui/LvglUi.h`.

#### What is already true

The preset screen is **already a 2 × 2 grid**. `renderPresetMode()` builds an
`LV_LAYOUT_GRID` container with `{FR(1), FR(1)}` columns and rows
(`LvglUiPreset.cpp:186-190`). The four cards live in a fixed
`std::array<lv_obj_t*, 4>` (`LvglUi.h:248-251`) and are value-synced without a rebuild by
`syncPresetCards()`. The retained-view architecture this phase needs is in place.

The redesign does **not** restructure this screen. It transposes the cell assignment,
restyles the card, and folds the loose header buttons into rails.

#### 4a. Transpose the cell assignment

`LvglUiPreset.cpp:196-197` currently reads:

```cpp
lv_obj_set_grid_cell(preset, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i % 2), 1,
                     LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i / 2), 1);
```

That is row-major: slots land 1, 2 / 3, 4. The switch corners require column-major:

```cpp
// Column-major: slot index maps to the physical footswitch corner, not reading
// order. FS1 top-left, FS2 bottom-left, FS3 top-right, FS4 bottom-right.
// See docs/hardware-assembly.md, "Footswitch physical positions".
lv_obj_set_grid_cell(preset, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i / 2), 1,
                     LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i % 2), 1);
```

The two expressions swap. Nothing else in the file depends on cell position — the arrays are
indexed by slot, not by cell — so `syncPresetCards()`, the click handler, and the missing-asset
logic are all unaffected.

**This single change is independently shippable and independently valuable**, before any
restyle. It is also the one change a player would notice immediately.

Add a comment at the declaration. A future contributor "tidying" this to reading order would
silently break the hardware correspondence, and the failure is invisible in code review.

#### 4b. Remove the glow

`LvglUiPreset.cpp:219-222` gives the active indicator a 16 px shadow at 40 % opacity in the
accent colour. That is a soft radial ramp and it is a direct instance of the banding problem
this redesign exists to fix. Delete the four `shadow_*` calls.

The whole indicator goes with it: the redesign marks the live tile with a lamp, a full-chroma
header strip and a border, not a 4 px glowing bar. Remove `presetIndicators_` and its
`std::array` member, along with the show/hide logic in `syncPresetCards()`.

#### 4c. Replace the font scaling hack

`LvglUiPreset.cpp:209-211` fakes a 56 px preset name by applying
`transform_scale(2 * LV_SCALE_NONE)` to a 28 px font. Bitmap glyphs scaled 2× are soft and
will look worse on the device than in the simulator.

If Phase 2 has landed, use the real 38 px Saira Condensed face and delete the three transform
calls. If Phase 2 has not landed, keep the hack — it is not a regression — and leave a
`TODO` referencing this section. **Phase 4 does not depend on Phase 2.**

#### 4d. Card interior

Rebuild the card body per § 8.4:

| Element | Spec |
|---|---|
| Header strip | 26 px, `plate3`, legend `Patch N` + lamp right |
| Numeral | Saira 300, 44 px, `engrave_lo` |
| Name | Saira Condensed 600, 38 px, `engrave`, wraps to 2 lines |
| Nomenclature row | family ticks, 26 × 3 px rule above each label |
| Switch label | `FS N` in the tile's **outer** corner with a 16 px bracket tick |

Switch label position is per-cell, not per-slot: top-left tile gets its label top-left, and so
on outward. Derive it from the grid cell, not the slot index, so 4a and this stay consistent.

New members replacing `presetIndicators_`:

```cpp
std::array<lv_obj_t*, 4> presetHeaderStrips_{};
std::array<lv_obj_t*, 4> presetLamps_{};
std::array<lv_obj_t*, 4> presetNumerals_{};
std::array<lv_obj_t*, 4> presetNomenRows_{};
```

#### 4e. State treatment

| State | Treatment |
|---|---|
| Live | Border, header strip and numeral take `lamp`; lamp lit; header legend adds `· Running` |
| Normal | `rule` border, `plate3` header, `engrave_lo` numeral, unlit lamp |
| Fault | `faultline` border, `verb` lamp, name in `faulttext`, legend chip naming the missing asset |
| Empty | Hidden, as today |

The existing `presetHasUnavailableAssets()` call already drives the fault case; it only needs
its presentation swapped. Replace the `"!  MISSING ASSET"` label with the legend chip and
name the asset — the mockup shows `Asset not found`, and naming the file is better where
width allows.

#### 4f. Rails

`renderPresetMode()` currently positions Tuner, Edit, Bank − and Bank + as free-floating
buttons using `kHeaderButtonTop`, `kHeaderEdgeInset`, `kHeaderTunerButtonWidth`,
`kHeaderBankButtonWidth`, `kHeaderEditX`, `kHeaderBankUpX`, `kHeaderBankDownX`
(`LvglUiPreset.cpp:16-25`). Replace all seven constants with the two rails from § 6:

- **Top legend rail, 52 px** — wordmark, bank name (`presetBankLabel_`), engine legend, MIDI lamp.
- **Bottom control rail, 88 px** — Edit, Tuner, Bank −, Bank +, Setup, then master volume as an
  engraved travel scale on the right.

Keep the existing behaviour exactly: Edit fires on `LV_EVENT_PRESSED` with its comment intact
(`LvglUiPreset.cpp:157-159` — a deliberate touchscreen accommodation, not an oversight), Bank
buttons fire on `LV_EVENT_CLICKED`, and the `kMinBank` / `kMaxBank` disabled states in
`syncHeaderView()` still apply.

`onSettingsClicked` already exists in `LvglUiNavigation.cpp:63`. Verify whether the preset
screen currently wires a Settings control — `renderPresetMode()` does not appear to, though
`README.md` describes a gear between Bank + and Edit. If it is missing, the bottom rail's
Setup button closes that gap; if it exists elsewhere, reuse its handler.

#### 4g. Tests

Extend `tests/lvgl_ui_smoke.cpp`:

- Assert slot → grid cell mapping is column-major. Guards against a future tidy-up reverting 4a.
- Assert exactly one card carries the live treatment after `selectPreset`.
- Assert a rebuild leaves no dangling `UiEventContext` in `UiContextRegion::Preset` — the
  existing `remove_if` in `rebuildPresetView()` covers this; the test pins it.
- Assert the fault treatment appears when `presetHasUnavailableAssets()` is true.

#### Definition of done

1. Slots occupy corners matching the physical switches.
2. No `shadow_*` style anywhere in the file.
3. Live, normal, fault and empty states all correct in **all three palettes** — Slate's LIVE
   red must not be confusable with the fault treatment.
4. `pedal-lvgl-ui-smoke` passes.
5. Checked on the device, at a steep angle, in low light.

#### Risk

Low. The change is confined to one file plus member declarations, touches no audio path, no
preset schema and no hardware contract, and 4a alone is a two-token edit that can ship and
revert on its own.

---

### 13.2 Phase 1 in detail — theme foundation

Largest phase by file count, smallest by conceptual difficulty. It is a mechanical migration
across ~60 call sites in 15 files, and its risk is doing it *thoughtlessly* — see the warning
in § 4.4 about mapping everything to `lamp`.

**New file: `src/ui/PanelPalette.h` / `.cpp`.** Do not grow `LvglUiStyle.h`; it is already
carrying layout constants, style helpers and colour.

```cpp
namespace ardor::lvgl_ui {
enum class PaletteId : std::uint8_t { Slate = 0, Ink = 1, Sodium = 2 };
struct PanelPalette {
  std::uint32_t plate, plate2, plate3;
  std::uint32_t engrave, engraveLo, engraveOff, rule;
  std::uint32_t lamp, warn, faultLine, faultText;
  std::uint32_t laneL, laneR;
  std::uint32_t family[6];             // amp, cab, util, mod, dly, verb
};
const PanelPalette& palette();
PaletteId paletteId();
void setPalette(PaletteId);            // sets current; caller triggers the rebuild
const char* paletteName(PaletteId);    // "Slate" / "Ink" / "Sodium"
}
```

Values are in § 4.2 — transcribe them exactly; they are chosen as a set and individually
adjusting one breaks the LIVE / family separation.

**Order of work.** Do it in this order or the intermediate states will not compile cleanly:

1. Add `PanelPalette.h/.cpp` with the three tables. Nothing references it yet.
2. Add `paletteId` to `DeviceSettings` and `savePalette()` to `GlobalSettingsStore`; load it
   in `LvglUi.cpp` where accent is currently initialised (`:95-96`).
3. Migrate file by file using the § 4.4 table. Keep `accent` alive during this so the tree
   always builds; each file is one small commit.
4. When no references remain, delete `accent`, `rigRight`, and the accent picker
   (full deletion list in § 4.4).
5. Update `tests/global_settings_smoke.cpp:37, 46` and `tests/lvgl_ui_smoke.cpp:631`.

**Switching before Phase 7 exists.** Phase 7 owns the palette picker UI. To verify Phase 1
alone, read `paletteId` from the settings JSON at boot — editing the file and restarting is a
sufficient test. Do not build a throwaway picker.

**`setPalette()` must not restyle in place.** Every screen rebuilds. Route it through the same
deferral as any structural change (`endInteraction()`, `src/ui/README.md`), and let the
existing revision counters drive it — do not call `lv_obj_clean` from the settings event
handler directly.

**Definition of done.** No `accent` or `rigRight` symbol remains; all three palettes selectable
via the settings file; `pedal-lvgl-ui-smoke` and `global_settings_smoke` pass; the UI looks
unchanged in structure, only recoloured.

---

### 13.3 Phase 2 in detail — fonts

**Faces.** Saira Condensed (500, 600) and Saira (300, 400), both SIL OFL. Vendor the OFL text
alongside the generated `.c` files and add it to the Buildroot licence manifest.

**Generate with `lv_font_conv`.** One invocation per size. Sizes and subsets from § 5:

```
# nomenclature — full Latin, small sizes, 1bpp
lv_font_conv --font SairaCondensed-SemiBold.ttf --size 14 --bpp 1 --format lvgl \
  -r 0x20-0x7F -o ardor_font_saira_cond_14.c

# tuner designator — 12 note letters, sharp sign, digits only, 4bpp
lv_font_conv --font SairaCondensed-SemiBold.ttf --size 184 --bpp 4 --format lvgl \
  -r 0x41-0x47 -r 0x23 -r 0x30-0x39 -o ardor_font_saira_cond_184.c

# numerals — digits, units, punctuation, 1bpp
lv_font_conv --font Saira-Light.ttf --size 44 --bpp 1 --format lvgl \
  -r 0x2B -r 0x2D -r 0x2E -r 0x30-0x3A -r 0x25 -r 0x6B,0x48,0x7A,0x64,0x42,0x6D,0x73 \
  -o ardor_font_saira_44.c
```

Use **1bpp** everywhere except the 184 px tuner face and the 44/46 px numerals. On a 16-bit
panel, 4bpp antialiasing on small type produces visible colour fringing that 1bpp does not,
and 1bpp is a quarter of the flash.

**Replace** the two `#include "ui/fonts/OpenSans*.h"` lines in each screen file and the
default argument in `LvglUiStyle.h:39`. Keep the Open Sans files in the tree until the device
check passes — reverting is then a one-line change per file.

**Mandatory device check, before committing.** Build with `ARDOR_UI_BACKEND=fbdev`, flash, and
look at: the 184 px tuner glyph (thin-stem breakup), the 12 px legends at 0.26 em tracking
(letter collision), and the 44 px numerals at a steep angle. The simulator answers none of
these. If the light weights break up, move Saira 300 → 400 and re-measure; that is the
expected fix.

**Definition of done.** No Open Sans reference in `src/ui/`; all sizes present; verified on the
panel, not the simulator.

---

### 13.4 Phase 3 in detail — shared components

**New file: `src/ui/PanelComponents.h` / `.cpp`.** Per the project's file-organisation rule,
do not add these to `LvglUiStyle.cpp`.

```cpp
lv_obj_t* lamp(lv_obj_t* parent, LampState state);        // Off, Live, Ok, Warn, Fault
lv_obj_t* legend(lv_obj_t* parent, const std::string& text, LegendWeight weight);
lv_obj_t* plateButton(lv_obj_t* parent, const std::string& label, ButtonRank rank);
lv_obj_t* railControl(lv_obj_t* parent, const RailControlSpec& spec);
lv_obj_t* blockCard(lv_obj_t* parent, const BlockCardSpec& spec);
lv_obj_t* patchPoint(lv_obj_t* parent, bool insertion);
lv_obj_t* annunciator(lv_obj_t* parent, const ToastSpec& spec);
lv_obj_t* panelDialog(lv_obj_t* parent, const DialogSpec& spec);
```

**`railControl` is the one to get right.** Its recessed track, fill, and wide thumb appear
six times per parameter page and three times in the active EQ band editor. Building it
per-frame is the obvious performance mistake.

- Build the rail, handle, grip, and endpoint legends **once** and never touch them again.
- A value change moves the handle and resizes the fill. Two property writes, no rebuild.
- Cache by control-layout signature. `LvglUiParameterRenderer.cpp` already caches parameter
  views this way; extend that mechanism rather than inventing a second one.

**Migrate `button()` and `label()`** (`LvglUiStyle.h:41-45`) to delegate to `plateButton` and
`legend`. Keep the old signatures as thin wrappers during the phase so screens migrate one at
a time.

**Definition of done.** Components exist, are unit-exercised in `lvgl_ui_smoke`, and at least
one screen has been migrated to prove the interfaces.

---

### 13.5 Phase 5 in detail — edit, drawer, split

Touches `LvglUiEdit.cpp`, `LvglChainLayout.cpp`, `LvglUiDrawer.cpp`, `LvglUiDrag.cpp`,
`LvglUiChainInteraction.cpp`.

**5a. Block cards.** Replace the current card build in `LvglUiEdit.cpp:460-500` with
`blockCard()`. Family colour moves from the left accent bar (`:470, 498`) to the 64 px,
full-width drag header — **delete the left bar**; a coloured left border above 1 px is banned
by the design system. Dual Rig lane cards use a 52 px version. The entire header owns drag,
with a movement threshold suppressing the subsequent click; a stationary tap still opens the
block.

**5b. Lane colours.** `LvglUiEdit.cpp:207` and `:564` use `laneIndex == 0 ? accent : rigRight`.
Becomes `laneL` / `laneR`. This is the only place lane identity is expressed, so it is a
two-line change once the tokens exist.

**5c. Bypass jumper (§ 8.7).** The one genuinely new mechanic. An `lv_line` above the module
layer, entering and leaving at the patch points either side of the bypassed block. Z-order
during drag is the hazard: the jumper must not paint over a card being dragged. Draw it into
the same layer the drag indicator uses (`LvglUiDrag.cpp:33, 53`) and rebuild it on
`endInteraction()`, never mid-drag.

**5d. Drawer restyle.** Filters wrap 4 + 3 (seven will not fit across 480 px at a legible
size). Footer carries visible/total count. The category accent bar at `:303, 516` becomes the
26 × 3 family tick. Scrim the chain behind at ~55 % rather than covering it — the insertion
point must stay visible.

**5e. Asset library — a new screen.** `mockups/lvgl-redesign/panel-states.html` § 2. This is
**not** the 480 px drawer: it is full-screen, because it lists files with long paths and can
run to hundreds of captures. Needs a new navigation state in `UiModel`, a filter rail with
counts, a search field (which opens the keyboard from Phase 7 — until then, filters alone are
acceptable), and missing assets listed rather than hidden so a broken preset is diagnosable
from the pedal.

**Definition of done.** Chain, split, drawer and asset library all in the new system; bypass
reads as a jumper; no left accent bars remain; drag still passes `lvgl_ui_smoke`.

---

### 13.6 Phase 6 in detail — parameters, EQ, tuner

**6a. Parameters.** `LvglUiParameterRenderer.cpp` sliders use the Rail + Thumb control from
§ 8.3. The two-row three-column layout is unchanged; only the control is replaced.

**6b. EQ.** `LvglUiEqRenderer.cpp`. Curve stays a stroke — **no filled area beneath it**; a
soft fill is a ramp and will band. Then § 9.1 in this order:

1. Band editor strip (Freq / Gain / Q as rail controls). Ship this first; it is the path that
   must always work.
2. Shoulder grips at the −3 dB points with the dashed bandwidth span.
3. Encoder retargeting.

**6c. Encoder retargeting.** In `LvglUiNavigation.cpp` beside the existing screen state. The
encoder drives the highlighted control **only** while a parameter or EQ editor is open, and
reverts to master volume on exit. Master volume is a live performance control; if an editor
screen can capture it, there must be an unambiguous way back. Add a smoke test asserting the
revert.

**6d. Tuner.** `LvglUiTuner.cpp`. Engraved centre-zero meter with major ticks at ±50 / ±25 / 0,
184 px designator, and a three-lamp verdict row (Flat / In tune / Sharp) so the state is
readable without parsing a number. The cents → lamp mapping is a pure function — put it beside
`UiStatusPresentation` and unit-test it directly.

**Definition of done.** All three screens migrated; EQ Q settable by all three routes; encoder
reverts to master volume on every exit path.

---

### 13.7 Phase 7 in detail — settings, toasts, first run

The largest surface area, and the only phase containing genuinely new infrastructure.

**7a. Toast queue — new subsystem.** There is no toast mechanism today. Model-side:

```cpp
struct Toast { ToastClass cls; std::string title, detail; std::optional<Action> action; };
void pushToast(UiState&, Toast);
```

Rules from § 10: max two visible, third replaces the oldest, 4 s dismiss for `info` / `ok` /
neutral, `warn` and `fault` persist until acknowledged or the condition clears, and anything
with an action never auto-dismisses. Timer lives in the UI thread, never on an audio path.
Wire real events first: overload bypass latch, missing asset, save success and failure,
parallel-worker realtime-policy failure.

**7b. Settings.** `LvglUiSettings.cpp`. Sections: Wi-Fi, Appearance, Audio, Controls, Manager
& cloud, Security & reset, About. Appearance carries the palette picker (previews the whole
palette — plate, lettering, LIVE lamp and six family colours together, never individual
swatches). Delete the accent picker per § 4.4.

**7c. Keyboard restyle.** The `lv_keyboard` at `LvglUiSettings.cpp:415` gets the § 8.10 key
style. Keys 82 × 44 px. It is a real screen the design system must own, not an OS default
left showing through.

**7d. Dialogs.** Unsaved-changes (Save / Discard / Cancel) naming *what* changed, not just
that something did. `CloudClaimOverlay.cpp:258` adopts `panelDialog`.

**7e. First-run and safety screens.** All in `panel-states.html`:

- Setup code (§ 4) — large code, mDNS hostname beneath, expiry, plain-HTTP warning, and a
  Skip action because the pedal makes sound without an account.
- Factory reset (§ 3) — **touch deliberately disabled**; FS1 approves, FS4 cancels, prompts
  drawn in their real corners. A tappable button here would defeat the physical-presence
  requirement in `README.md`.
- Boot (§ 8) — discrete progress blocks, no spinner.
- Empty bank (§ 5) — slots stay visible; collapsing them would move the tiles and break the
  switch correspondence.
- No captures installed (§ 6) — leads with what still works.

**7f. Clip annunciator.** The `--clip-debug` status (`CLIP` / `LIMIT` / `LEVEL OK` at 1 Hz)
moves into the top rail with the first-crossing stage and peak. The latched overload bypass is
separate and takes the bottom rail with a clear action — different lifetimes, different places.

**Definition of done.** Every screen in the mockups exists; toasts queue and expire correctly;
factory reset cannot be approved by touch.

---

## 14. Testing

- `pedal-lvgl-ui-smoke` covers retained views and interaction; extend it with a palette-swap
  case that rebuilds every screen and asserts no dangling `UiEventContext`.
- Add a pure unit test for the cents → tuner-lamp mapping; it is a small pure function and
  belongs beside `UiStatusPresentation`.
- Do not restyle or rebuild the full scene on an idle refresh tick — `src/ui/README.md`
  performance invariants are unchanged by this work.
- Run `pedal-dsp-bench` only if an audio hot path is touched. Nothing here should touch one.
- **Device check is mandatory** for: font rendering, contrast, and confirmation that no
  surface bands. The simulator cannot answer any of the three.

---

## 15. Open questions

1. **Per-preset colour.** Not in this spec. If wanted later it needs a preset-schema field
   and a version bump; do not add it as a UI-only affordance.
2. **Saira licence file** placement for the Buildroot image.
3. **Encoder retargeting** (§ 9.1). Confirm that capturing the encoder inside editor screens
   is acceptable, and that reverting to master volume on exit is sufficient. Alternative:
   require a press-to-retarget so the encoder is never silently reassigned.
4. **Shoulder-grip hit accuracy.** Two 13 px grips plus a 19 px handle within roughly 100 px
   of curve is tight for a fingertip. Needs a device trial; if it fails, the band editor
   strip already covers the function and the grips can be dropped without loss.

**Resolved:**

- Footswitch corner assignment (§ 7) — FS1 top-left, FS2 bottom-left, FS3 top-right,
  FS4 bottom-right.
- Accent migration (§ 4.4) — `accentColor` is removed entirely. No migration, no nearest-palette
  mapping. Stored values are ignored; the device starts on Slate.

---

## 16. Out of scope

Manager application and website visual systems; preset schema; audio behaviour; the
footswitch and encoder contract; OTA; plugin formats.

---

## 17. Notes for the implementing agent

Read this before starting. It is the context that is not recoverable from the code.

**The mockups are the visual authority.** `mockups/lvgl-redesign/` opens in any browser.
`panel-palettes.html` is the palette study, `panel.html` the four core screens,
`panel-system.html` the drawer / toasts / dialog / settings / EQ, `panel-states.html` the
remaining screens and states. Where this document and a mockup disagree about a *value*, the
document wins; about a *composition*, the mockup wins.

**Do not read the other five directions as guidance.** `floor.html`, `plate.html`,
`sheet.html`, `cyc.html` and `sleeve.html` are rejected alternatives kept for reference. They
have known layout defects that were deliberately not fixed.

**The one thing to internalise:** the device framebuffer is RGB565 and the simulator is
32-bit. Anything that looks like a smooth tonal transition will band on the hardware and look
perfect in the simulator. If you find yourself reaching for `lv_grad_dsc_t`, a shadow, or an
opacity fade over a large area, you have left the design system. § 2 is not stylistic advice.

**Verify on the device, not the simulator**, for anything involving colour, contrast or font
rendering. `docs/device-ssh-access` conventions and
`./scripts/measure-device-performance.sh` already exist for getting onto the pedal.

**Performance.** This work should *reduce* UI draw cost — flat fills are cheaper than the
gradients they replace. Treat a measurable regression as a bug. The invariants in
`src/ui/README.md` are unchanged: do not rebuild the scene on an idle tick, do not delete a
widget while an interaction owns it, keep audio allocation-free. Nothing in this spec should
touch an audio path; if a change seems to require it, stop and ask.

**Terminology.** The product vocabulary is fixed by `PRODUCT.md`: *block, chain, rail, lane,
split, join, bank, slot, preset, rig*. Use **preset**, never "patch" — an early draft of these
mockups used "patch" and it was wrong. "Patch point" and "patch cord" are fine; they name
connectors, not presets.

**Where to start.** § 13.1a — the preset-screen cell transpose. Two tokens, no dependencies,
immediately visible, trivially revertible. It will also tell you whether the footswitch corner
mapping in § 7 survives contact with the real hardware.

**When something in this spec is wrong**, and something will be, fix the spec in the same
change as the code. An out-of-date spec is worse than none. Several claims here were corrected
during authoring after reading the source — `LvglUiPreset.cpp` in particular turned out to
already have the 2 × 2 grid this document originally proposed building.
