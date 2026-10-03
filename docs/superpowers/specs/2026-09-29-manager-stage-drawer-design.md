# Manager redesign: Stage and Drawer

Date: 2026-09-29. Status: approved direction (mockup 1), ready to build.

## Source of truth

- Visual and interaction reference: `mockups/manager-taste/1-stage-drawer.html` (Edit view) and `1-stage-drawer.html#assets` (Assets view). Shared mockup code: `shared.css`, `assets-view.css`, `render.js`, `interact.js`, `assets-view.js`.
- Audit and rationale: `mockups/manager-taste/index.html`.
- Visual system: `DESIGN.md` (Lamp Black `device-*` tokens), device values in `src/ui/LvglUiStyle.cpp` (`kSlate`).
- Product rules: `PRODUCT.md`.

The mockup is a visual and interaction target, not code to copy. The React app keeps its domain layer (`api/`, `connection/`, `presets/editor/*`, `effects/*`, `presets/scenes/sceneRevision.ts`, `cloud/`, `localAuth/`, `tone3000/` API code) and replaces the presentation layer.

## Goal

Rebuild the manager UI in the pedal's Lamp Black language. The chain looks and behaves like the touch screen edit screen, and it stays reorderable. Add an Assets view in the same grammar.

## Layout (desktop, 1440 × 900 reference)

1. **App bar** (56 px): red mark and "Ardor", an Edit / Assets view switch, the preset name with `BANK 00 · FS 1`, a MODIFIED tag when the draft differs from the saved slot, the live state, the connection pill, and Settings.
2. **Bank bar** (Edit view): bank stepper `BANK 00` to `BANK 99`, then the four slot tiles FS 1 to FS 4 in one row. Each tile shows the name and the family-colour chain strip. The live preset tile is flooded lamp red with a glow; the tile open in the editor has a bone outline.
3. **Stage**:
   - *Overview* (no block open): stage head (EDIT, block count, scene scope switch), the chain of device cards between IN and OUT jacks, with `+` insert points. Dual Rig renders SPLIT, lane A and lane B, JOIN. WDW routing renders DRY and WET lanes.
   - *Drawer* (a block open): the chain folds into the chip strip (view transition), and the block drawer fills the stage. It has a family-colour top edge, the block name, BLOCK ON, duplicate, reset, delete, the scene scope switch, and large controls.
4. **Rail** (bottom, 72 px): Save, Undo, Redo, Add block, Global, Scenes. In the drawer, the focused control's context appears: label, value, fine −/+, Assign EXP, Reset. Right side: the live-state action and Done.

**Assets view:** the bank bar becomes three kind tiles (NAM models, cabinet IRs, reverb IRs) with counts and sizes. The stage is the file list (search, sort, select, used-in chips). A file drawer opens under the list. The rail holds Upload, Browse TONE3000 (models only), bulk delete, and the delete confirmation.

**Phone (390 px wide):** tiles scroll sideways, cards shrink to 148 × 232, the drawer shows one control per row, the rail shows icons only, the asset file drawer becomes a bottom sheet.

## Decisions that differ from the mockup

| Mockup | Build | Why |
|---|---|---|
| "Audition": hear edits before saving | Removed. Live state offers **Save and load** when dirty, **Load on pedal** when clean and not live, and a **LIVE ON PEDAL** tag when the slot is live and its stored revision matches. | `applyPreset` loads the saved slot (`deviceSession.applyCurrent`). The pedal cannot play an unsaved draft. |
| MIDI learn button | Removed. | No MIDI mapping UI or API flow exists. |
| BANK 01 (demo) | `BANK 00` to `BANK 99` and `FS 1` to `FS 4`. | The device prints `BANK %02d` with the 0-based bank (`src/ui/LvglUiPreset.cpp:71`). |
| Adding a second delay turns the first off | The module drawer disables the row and shows the reason ("Disable Tape Delay first"). | Keep the existing constraint logic in `PresetWorkspace` `disabledDefinitions`. |
| Upload shows a percentage | Indeterminate progress bar. | `fetch` uploads report no progress. |
| Used-in chips from local data | New read-only endpoint `GET /api/assets/usage` on `managerd`. The hosted cloud build hides the column until its relay supports the route. | The manager only has preset summaries; the pedal already scans presets on rename. |

## Behaviour rules

- **One lamp:** lamp red means live (preset, scene) or the focused control. Buttons never use it.
- **Family colours** (cap, bars, chip top edge, code squares): amp and drive `#d2923f`, cab `#aab2b7`, dynamics/EQ/wah/stereo `#5f95c9`, modulation `#3fb08c`, delay `#9a82d6`, reverb and IR reverb `#d07a5a`.
- **Card caps** use the device labels from `labelForBlockType` in `src/ui/UiModel.cpp`: Neural Amp, Cab, Dual Amp, Dual Rig, Modulation, Delay, Reverb, Dynamics, EQ, Wah, Drive, Stereo.
- **Card values:** two main values per block, the same pairs as the mockup (`render.js` `MAIN`).
- **Reorder:** drag the card cap with a mouse (6 px threshold), long-press 250 ms on touch, Alt + Left/Right on a focused card, dnd-kit keyboard sensor (Space to lift). Drops across lanes of the same Dual Rig, and between the WDW lanes, are allowed where the reducer supports them. Top level ↔ lane drops are not allowed.
- **Undo:** one pointer drag or one key burst (900 ms) on a control is one undo step.
- **Scenes:** in scene scope, the first edit of a value makes all four scenes own that address (`set-scene-scope` adds it to every scene with the current value), and the open scene gets the new value. Later edits set the scene value. A SCENE tag with a **Share** button makes the value shared again: the preset takes the open scene's value and every scene drops the address (reducer behaviour). BLOCK ON works the same way. The first scene edit is one undo step.
- **Draft survives view switches:** the editor state lives above the Edit and Assets views.
- **Assets:** rename updates saved presets (server behaviour, `updatedPresetCount`). Delete, rename, upload and replace are not undoable, and delete asks in the rail. "Try in preset" sets the file on the first matching block of the open draft and is undoable. A preset that references a path with no file shows MISSING FILE on its tile and FILE MISSING on its card.
- **HTTP:** the LAN build keeps saying HTTP in the connection pill (PRODUCT.md). The hosted build does not show it.
- **Safety limiter:** shown as fixed −1 dBFS protection, never a control.

## Non-goals

- New pedal features (audition, MIDI learn, bank names).
- A light theme (the four palettes stay; the default Slate palette gets the Lamp Black values).
- Changes to the device LVGL UI.

## Acceptance

- All current manager tests keep passing or move to the new components with the same assertions.
- New unit tests for every new module; one Playwright E2E suite with a mocked API covers connect, reorder, edit, save, assets upload with a conflict, rename, and delete.
- Screens at 1440 × 900 and 390 × 844 match the mockup layout.
- `npm run build`, `npm run build:device`, `npm run build:hosted`, and `go test ./...` in `services/managerd` pass.
