# Ardor Manager Pedalboard Redesign Implementation Plan

> **For the implementation subagent:** Execute this plan serially, one task and one verification gate at a time. Do not spawn additional subagents. The worktree may already contain unrelated user changes; inspect and preserve them. Never reset, discard, reformat, or commit unrelated files.

**Goal:** Replace the current form-based Tauri manager with a polished desktop pedalboard editor in which every runtime-supported preset block can be added, configured, reordered, bypassed, duplicated, deleted, saved, and applied safely.

**Primary outcome:** A user can connect to `ardor-managerd`, choose any bank and slot, build a maximum-ten-block serial chain from all 39 supported choices, edit every supported parameter through purpose-built controls, understand validation problems before saving, and deliberately choose either **Save** or **Save & Apply**.

**Architecture:** Keep `ardor-managerd` as the saved-preset and asset boundary. Build a versioned manager effect catalog that mirrors the runtime catalog, then enforce parity with a focused C++ test. Replace `App.tsx` state sprawl with a device-session layer and a history-aware preset editor reducer. Render the editor as a preset sidebar, signal-chain canvas, and selected-block inspector. Unknown future blocks and fields remain losslessly preserved and receive a guarded advanced editor instead of being silently rewritten.

**Tech stack:** React 18, TypeScript, Vite, Tauri 2, Tailwind CSS, Radix Dialog/Tooltip, Lucide icons, dnd-kit, Vitest, Testing Library, Playwright, existing Go `ardor-managerd`, existing C++20 DSP catalog.

---

## 1. Non-negotiable product rules

The implementation subagent must preserve these rules throughout the work:

1. Editing is offline. Moving a control changes only the local draft.
2. **Save** performs one preset `PUT` and does not request activation.
3. **Save & Apply** saves first, waits for the canonical saved response, then sends the apply request for that exact bank and slot.
4. **Apply** is never available for a dirty draft. Do not let the visible draft differ from the preset being applied.
5. A preset/slot switch with unsaved changes must offer **Save**, **Discard**, and **Cancel**.
6. Unknown root fields, global fields, block fields, parameters, and unknown block types must survive load/edit/save unchanged unless the user explicitly edits or deletes them.
7. `global.safetyLimitDb` remains preserved but is not a normal tone control. Show it read-only under Advanced preset settings. Do not expose casual editing in this milestone.
8. The runtime supports a maximum of ten blocks in the hardware UI; enforce the same maximum in the manager.
9. At most one enabled `nam`, `cab`, `mod`, `delay`, or `reverb` block may exist in a valid applyable chain.
10. Enabled NAM and Cabinet blocks must appear before the first enabled stereo Daisy block (`mod`, `delay`, or `reverb`) because `EngineLoader` rejects mono asset blocks after stereo is established.
11. Missing assets and unsupported blocks may be saved for forward compatibility, but they block **Save & Apply** and **Apply**.
12. Do not claim that a preset is currently active after reconnect unless the daemon returns authoritative `device.active` data. In the current protocol, label successful activation as **Applied this session**.
13. All destructive asset and block actions require an explicit, scoped confirmation or a five-second undo toast as specified below.
14. The primary UI must never show raw JSON for known supported blocks.

---

## 2. Supported block matrix

The block browser must expose exactly these 39 addable definitions.

| Category | Count | Definitions |
|---|---:|---|
| Amp | 1 | NAM model |
| Cabinet | 1 | Cabinet IR |
| Dynamics | 1 | Compressor |
| EQ | 1 | Five Band Parametric EQ |
| Modulation | 13 | Chorus, Flanger, Rotary, Vibe, Phaser, Vintage Trem, Poly Octave, Pattern Trem, Auto Swell, Filter, Formant, Quadrature, Destroyer |
| Delay | 10 | Digital, Tape, Dual, Filter, Lo-fi, Bucket Brigade, Duck, Pattern, Swell, Tremolo |
| Reverb | 12 | Room, Hall, Plate, Spring, Bloom, Cloud, Shimmer, Chorale, Nonlinear, Swell, Magneto, Reflections |

Runtime identifiers must match `src/daisyfx/DaisyFxCatalog.cpp` exactly. Never derive a block's JSON `type` from its display category:

- Modulation definitions write `type: "mod"` and their catalog `params.mode`.
- Delay definitions write `type: "delay"` and their catalog `params.mode`.
- Reverb definitions write `type: "reverb"` and their catalog `params.mode`.
- Compressor writes `type: "dynamics"`, `params.mode: "compressor"`.
- EQ writes `type: "eq"`, `params.mode: "parametric_eq_5"`.

---

## 3. Target user experience

### Desktop layout at 1180 px and above

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ Ardor  ● Connected · Ardor Pedal       Undo Redo       Save  Save & Apply  │
├───────────────┬──────────────────────────────────────┬──────────────────────┤
│ PRESETS       │ Empty 1 · Bank 000 / Slot 1         │ Vintage Trem         │
│ Bank [000 ▾]  │                                      │ On  [toggle]         │
│ ┌───────────┐ │ Input → [Comp] → [NAM] → [Cab] → +  │ Speed   ─────●  35% │
│ │ 1 Clean   │ │                                      │ Depth   ─────●  70% │
│ │ 2 Crunch  │ │ Selected block summary/warnings      │ Mix     ─────● 100% │
│ │ 3 Ambient │ │                                      │ ...                  │
│ │ 4 Empty   │ │                                      │ Reset · Duplicate   │
│ └───────────┘ │                                      │ Delete               │
├───────────────┴──────────────────────────────────────┴──────────────────────┤
│ Workspace   Assets                                      Status / version    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Responsive rules

- `>= 1180px`: fixed 248 px preset sidebar, flexible canvas, 340 px inspector.
- `960px..1179px`: 220 px preset sidebar, inspector becomes a 360 px right drawer.
- `< 960px` browser fallback: preset sidebar and inspector are drawers; the Tauri window still retains its configured 960 px minimum.
- The chain scrolls horizontally inside its own canvas. The page shell itself must not acquire horizontal overflow.
- The top action bar and preset identity remain visible while the chain or inspector scrolls.

### Visual direction

- Dark studio theme is the default; persist light/dark preference in local storage.
- Use system fonts; do not load web fonts or depend on a network connection.
- Use an acid-green primary accent consistent with the pedal UI, restrained to selected/active states and primary actions.
- Category accents: Amp amber, Cabinet orange, Dynamics cyan, EQ violet, Modulation magenta, Delay blue, Reverb indigo, Unknown gray.
- Numeric values use tabular figures.
- Avoid skeuomorphic rotary knobs. Horizontal sliders plus precise numeric inputs are easier to scan, compare, and control with keyboard input.

---

## 4. Final frontend file structure

The subagent may make small naming adjustments, but must preserve these boundaries.

```text
apps/manager/src/
  App.tsx
  main.tsx
  styles.css
  api/
    client.ts
    client.test.ts
    errors.ts
    types.ts
  app/
    AppShell.tsx
    AppTopBar.tsx
    AppNavigation.tsx
    providers.tsx
  components/
    Button.tsx
    ConfirmDialog.tsx
    Drawer.tsx
    EmptyState.tsx
    IconButton.tsx
    NumberField.tsx
    ParameterSlider.tsx
    SegmentedControl.tsx
    StatusBadge.tsx
    ToastRegion.tsx
    Toggle.tsx
    Tooltip.tsx
  connection/
    ConnectionDialog.tsx
    deviceSession.tsx
    deviceSession.test.tsx
  effects/
    catalog.v1.json
    catalog.ts
    catalog.test.ts
    types.ts
  presets/
    editor/
      editorReducer.ts
      editorReducer.test.ts
      editorTypes.ts
      presetFactory.ts
      presetValidation.ts
      presetValidation.test.ts
      recovery.ts
      recovery.test.ts
    browser/
      BankPicker.tsx
      PresetSidebar.tsx
      PresetSlotCard.tsx
    chain/
      ChainCanvas.tsx
      ChainBlockCard.tsx
      ChainInsertionPoint.tsx
      chainA11y.ts
    block-browser/
      BlockBrowser.tsx
      BlockDefinitionCard.tsx
    inspector/
      BlockInspector.tsx
      GenericParameterControls.tsx
      AssetInspector.tsx
      CompressorInspector.tsx
      DaisyInspector.tsx
      EqInspector.tsx
      EqResponseGraph.tsx
      UnknownBlockInspector.tsx
      PresetInspector.tsx
    workspace/
      PresetWorkspace.tsx
      UnsavedChangesDialog.tsx
  assets/
    AssetLibrary.tsx
    AssetTable.tsx
    AssetUploadDropzone.tsx
    AssetDeleteDialog.tsx
  test/
    fixtures.ts
    render.tsx
    setup.ts
```

Do not recreate one large `App.tsx`. It should wire providers and render `AppShell`; business rules belong in the editor/session modules.

---

## 5. Core data contracts

### Effect catalog TypeScript model

Create a discriminated control union rather than switching on arbitrary parameter names:

```ts
export type EffectCategory =
  | "amp" | "cabinet" | "dynamics" | "eq"
  | "modulation" | "delay" | "reverb";

export type NumberControl = {
  kind: "number";
  key: string;
  label: string;
  minimum: number;
  maximum: number;
  step: number;
  unit: "percent" | "db" | "ms" | "hz" | "ratio" | "plain";
  defaultValue: number;
};

export type ChoiceControl = {
  kind: "choice";
  key: string;
  label: string;
  choices: Array<{ value: string; label: string }>;
  defaultValue: string;
};

export type ToggleControl = {
  kind: "toggle";
  key: string;
  label: string;
  defaultValue: boolean;
};

export type AssetControl = {
  kind: "asset";
  label: string;
  assetKind: "models" | "irs";
};

export type EqControl = { kind: "parametric-eq-5" };

export type EffectDefinition = {
  id: string;                         // e.g. "mod:vintage_trem"
  blockType: string;                  // JSON block.type
  mode?: string;                      // JSON params.mode
  name: string;
  description: string;
  category: EffectCategory;
  constraintGroup?: "nam" | "cab" | "mod" | "delay" | "reverb";
  maxEnabledInGroup?: number;
  controls: Array<NumberControl | ChoiceControl | ToggleControl | AssetControl | EqControl>;
};
```

`catalog.ts` must validate the imported JSON at module initialization with explicit type guards. Do not cast unvalidated JSON with `as EffectCatalog`. A malformed catalog should throw a descriptive startup error during development and fail its unit test.

### Editor state

```ts
export type PresetLocation = { bank: number; slot: number };

export type PresetHistory = {
  past: Preset[];
  present: Preset;
  future: Preset[];
};

export type EditorState = {
  location: PresetLocation;
  saved: Preset;
  history: PresetHistory;
  selectedBlockId?: string;
  recoveryAvailable?: Preset;
};
```

Dirty state is `!deepEqual(state.saved, state.history.present)`. Do not store a second mutable `dirty` boolean that can drift.

All preset mutations push the previous `present` onto `past`, cap history at 100 entries, and clear `future`. Selection-only actions do not affect history.

### Editor actions

Implement and test this explicit action set:

```ts
type EditorAction =
  | { type: "load"; location: PresetLocation; preset: Preset }
  | { type: "select-block"; blockId?: string }
  | { type: "set-name"; name: string }
  | { type: "set-global"; key: "inputGainDb" | "outputGainDb"; value: number }
  | { type: "add-block"; definitionId: string; index: number; initialAsset?: string }
  | { type: "move-block"; blockId: string; index: number }
  | { type: "toggle-block"; blockId: string; enabled: boolean }
  | { type: "duplicate-block"; blockId: string }
  | { type: "remove-block"; blockId: string }
  | { type: "set-block-asset"; blockId: string; asset: string }
  | { type: "set-block-param"; blockId: string; key: string; value: unknown }
  | { type: "set-eq-band"; blockId: string; band: number; patch: Partial<EqBand> }
  | { type: "change-definition"; blockId: string; definitionId: string }
  | { type: "reset-block"; blockId: string }
  | { type: "replace-present"; preset: Preset }
  | { type: "mark-saved"; preset: Preset }
  | { type: "undo" }
  | { type: "redo" };
```

Changing between modes in the same family preserves shared normalized control values and replaces `params.mode`. It fills newly required values from the target defaults and preserves unknown parameter keys. Changing across block families is not supported in-place; the user deletes/adds instead.

### Validation result

```ts
type ValidationIssue = {
  severity: "warning" | "error";
  code: string;
  message: string;
  blockId?: string;
  field?: string;
};

type PresetValidationResult = {
  issues: ValidationIssue[];
  canSave: boolean;
  canApply: boolean;
};
```

- Structural invalidity, non-finite values, invalid asset paths, invalid IDs, more than ten blocks, and out-of-range known values set `canSave = false`.
- Missing assets, unsupported blocks/modes, duplicate constrained enabled blocks, and mono blocks after stereo set `canApply = false`.
- Missing/unsupported blocks are warnings for Save but visually prominent.
- Validation must never delete or normalize unknown data as a side effect.

---

## 6. Detailed task sequence

### Task 0: Establish a safe baseline

**Files:** No modifications.

- [ ] Read the repository `AGENTS.md` before taking any implementation action and follow any more-specific instructions discovered below the files being changed.
- [ ] Run `git status --short` and save the output in the implementation notes. Expect unrelated modifications; do not clean them.
- [ ] Inspect diffs for every file before modifying it: `git diff -- <path>`.
- [ ] Run the current manager tests:

  ```bash
  cd apps/manager
  npm ci
  npm test
  npm run build
  ```

- [ ] Run the daemon tests:

  ```bash
  cd services/managerd
  go test ./...
  ```

- [ ] Build/run the runtime catalog smoke test if the existing build tree is usable:

  ```bash
  cmake --build build --target pedal-daisy-fx-catalog-smoke -j2
  ./build/pedal-daisy-fx-catalog-smoke
  ```

- [ ] Record any pre-existing failure. Do not fix unrelated failures inside this feature.

**Gate:** The subagent knows the baseline and which touched files already contain user edits.

---

### Task 1: Add frontend dependencies and the test harness

**Files:**

- Modify: `apps/manager/package.json`
- Modify: `apps/manager/package-lock.json`
- Modify: `apps/manager/vite.config.ts`
- Modify: `apps/manager/tsconfig.json`
- Create: `apps/manager/src/test/setup.ts`
- Create: `apps/manager/src/test/render.tsx`

- [ ] Install runtime dependencies:

  ```bash
  npm install @dnd-kit/core @dnd-kit/sortable @dnd-kit/utilities \
    @radix-ui/react-dialog @radix-ui/react-tooltip lucide-react
  ```

- [ ] Install development dependencies:

  ```bash
  npm install --save-dev @testing-library/react @testing-library/user-event \
    @testing-library/jest-dom jsdom @playwright/test
  ```

- [ ] Configure Vitest with `environment: "jsdom"`, `setupFiles: ["./src/test/setup.ts"]`, and CSS enabled.
- [ ] Import `@testing-library/jest-dom/vitest` from setup.
- [ ] Add scripts:

  ```json
  "test:watch": "vitest",
  "test:e2e": "playwright test",
  "typecheck": "tsc"
  ```

- [ ] Create a `renderWithProviders` helper that initially wraps only Radix Tooltip provider; later tasks may add app providers.
- [ ] Add a one-line smoke component test proving jsdom and jest-dom work.
- [ ] Run `npm test` and `npm run typecheck`.

**Gate:** Component tests can render, query accessible roles, and send keyboard/pointer input.

---

### Task 2: Create the versioned effect catalog

**Files:**

- Create: `apps/manager/src/effects/types.ts`
- Create: `apps/manager/src/effects/catalog.v1.json`
- Create: `apps/manager/src/effects/catalog.ts`
- Create: `apps/manager/src/effects/catalog.test.ts`

- [ ] Write failing tests requiring:

  - exactly 39 definitions;
  - unique `id` values;
  - 13 `mod`, 10 `delay`, and 12 `reverb` mode definitions;
  - unique `(blockType, mode)` pairs;
  - seven numeric controls for every Daisy definition;
  - `0..1` ranges for all Daisy stored parameters;
  - the compressor's eleven controls and exact defaults from `UiModel.cpp`;
  - exactly five default EQ bands at 80, 250, 800, 2500, and 8000 Hz;
  - NAM uses a `models` asset control;
  - Cabinet uses an `irs` asset control plus `levelDb` and `mix`;
  - every known definition creates a complete preset block.

- [ ] Fill the catalog from these authoritative sources:

  - Daisy IDs, labels and defaults: `src/daisyfx/DaisyFxCatalog.cpp`.
  - Compressor defaults: `defaultCompressorParams()` in `src/ui/UiModel.cpp`.
  - Compressor ranges: `src/ui/ParameterControls.cpp`.
  - Cabinet ranges/defaults: `src/ui/ParameterControls.cpp` and README.
  - EQ ranges/defaults: `src/equalizer/EqParameters.h/.cpp`.

- [ ] Use semantic mode-specific labels for Daisy's algorithm parameters exactly as the C++ catalog does.
- [ ] Store Daisy values as normalized `0..1`; format them as percentage in the manager. Do not invent physical time/frequency mappings not exposed by the runtime.
- [ ] Add helpers:

  ```ts
  allEffectDefinitions(): EffectDefinition[]
  findEffectDefinition(block: PresetBlock): EffectDefinition | undefined
  getEffectDefinition(id: string): EffectDefinition
  definitionsForCategory(category: EffectCategory): EffectDefinition[]
  createBlockFromDefinition(id: string, existingBlocks: PresetBlock[], asset?: string): PresetBlock
  defaultsForDefinition(id: string): Record<string, unknown>
  ```

- [ ] `createBlockFromDefinition` generates `block-N`, where `N` is one larger than the greatest existing numeric `block-N`; it falls back to the first unused positive integer when IDs are nonstandard.
- [ ] Preserve catalog order as the intentional block-browser order.
- [ ] Run catalog tests and build.

**Gate:** The frontend can create valid default JSON for all 39 choices without hard-coded UI switches in `App.tsx`.

---

### Task 3: Add runtime/frontend catalog parity coverage

**Files:**

- Create: `tests/manager_effect_catalog_smoke.cpp`
- Modify: `CMakeLists.txt`

- [ ] Add a C++ smoke executable linked with the library that owns `daisyFxCatalog()` and with nlohmann/json include access.
- [ ] Pass the absolute catalog path as a compile definition rather than relying on the test working directory:

  ```cmake
  target_compile_definitions(pedal-manager-effect-catalog-smoke PRIVATE
    ARDOR_MANAGER_EFFECT_CATALOG="${CMAKE_CURRENT_SOURCE_DIR}/apps/manager/src/effects/catalog.v1.json"
  )
  ```

- [ ] Parse the manager JSON and assert for all 35 Daisy entries:

  - the `(blockType, mode)` pair exists in `daisyFxCatalog()`;
  - display names match;
  - parameter key order matches;
  - labels match;
  - defaults compare within `0.0001f`;
  - there are no extra or missing Daisy definitions.

- [ ] Assert the manager catalog's compressor and EQ identifiers match the runtime identifiers.
- [ ] Register the executable with CTest.
- [ ] Run:

  ```bash
  cmake --build build --target pedal-manager-effect-catalog-smoke -j2
  ./build/pedal-manager-effect-catalog-smoke
  ```

**Gate:** Any future DSP catalog change that is not reflected in the desktop editor fails CI.

---

### Task 4: Replace the draft helpers with the editor reducer

**Files:**

- Create: `apps/manager/src/presets/editor/editorTypes.ts`
- Create: `apps/manager/src/presets/editor/presetFactory.ts`
- Create: `apps/manager/src/presets/editor/editorReducer.ts`
- Create: `apps/manager/src/presets/editor/editorReducer.test.ts`
- Retain temporarily: `apps/manager/src/presets/presetDraft.ts`

- [ ] Write reducer tests before implementation for every action listed in Section 5.
- [ ] Include explicit preservation tests with future root, global, block and parameter fields.
- [ ] Include ID collision tests using standard and nonstandard IDs.
- [ ] Verify add at index 0, middle and end.
- [ ] Verify move uses final destination semantics and selection follows the moved block.
- [ ] Verify duplicate inserts immediately after the source, receives a unique ID, and becomes selected.
- [ ] Verify delete selects the nearest surviving neighbor.
- [ ] Verify a reset restores only known fields/parameters and preserves unknown block fields and unknown params.
- [ ] Verify mode changes within a family preserve shared normalized values and unknown parameters while changing semantic definition.
- [ ] Verify EQ band edits clone nested arrays/objects and always retain exactly five bands.
- [ ] Verify undo/redo, history truncation after a new edit, and the 100-entry cap.
- [ ] Clamp values in the reducer through catalog metadata; reject non-finite numbers by leaving state unchanged.
- [ ] `mark-saved` uses the daemon response as both `saved` and current `present`, because the server response is canonical.
- [ ] Keep all operations immutable. `structuredClone` is acceptable for preset snapshots.
- [ ] Run focused tests.

**Gate:** All preset editing behavior exists independently of React components and API calls.

---

### Task 5: Implement complete preset validation

**Files:**

- Create: `apps/manager/src/presets/editor/presetValidation.ts`
- Create: `apps/manager/src/presets/editor/presetValidation.test.ts`

- [ ] Add table-driven tests for:

  - preset version and serial routing;
  - name length up to 120 characters;
  - finite globals in API/runtime ranges;
  - nonempty unique block IDs up to 80 characters;
  - maximum ten blocks;
  - valid relative asset paths with no absolute path, backslash, `.` or `..` segment;
  - required NAM/Cab assets;
  - known parameter type/range checks;
  - unknown block warning;
  - unsupported known-family mode warning;
  - missing asset warning using the current asset lists;
  - maximum one enabled NAM/Cab/Mod/Delay/Reverb;
  - enabled NAM/Cab after an enabled Daisy stereo effect;
  - disabled duplicates not blocking Apply;
  - canonical five-band EQ shape;
  - warnings tied to the correct `blockId`.

- [ ] Keep Save and Apply decisions distinct.
- [ ] Sort issues in stable chain order; preset-level issues come first.
- [ ] Add `issuesForBlock(result, blockId)` and `firstBlockingIssue(result)` helpers.
- [ ] Do not mutate or normalize the preset during validation.
- [ ] Run focused tests.

**Gate:** Save/Apply buttons can rely on one deterministic validation result.

---

### Task 6: Improve API errors and complete asset client methods

**Files:**

- Create: `apps/manager/src/api/errors.ts`
- Modify: `apps/manager/src/api/types.ts`
- Modify: `apps/manager/src/api/client.ts`
- Modify: `apps/manager/src/api/client.test.ts`

- [ ] Add `ArdorApiError` with `status`, `code`, `message`, and optional `details`.
- [ ] When a response is not OK, attempt to parse the OpenAPI `Error` shape; fall back to status text without throwing a second parse error.
- [ ] Add timeout/abort support to the client constructor or each request. Use a 10-second default for JSON calls and no short timeout for uploads.
- [ ] Add `deleteAsset(kind, assetId)` using URL encoding and `DELETE`.
- [ ] Retain the existing `overwrite` upload argument and expose it in the UI later.
- [ ] Ensure `204 No Content` works without calling `response.json()`.
- [ ] Expand `DeviceStatus.active` as optional in TypeScript, but never invent it when absent.
- [ ] Add tests for structured 400, plain-text 500, 204 delete, encoded asset ID, abort, and successful apply.
- [ ] Run tests and build.

**Gate:** UI surfaces actionable daemon messages such as asset conflict and invalid preset path.

---

### Task 7: Build the device-session and data-loading layer

**Files:**

- Create: `apps/manager/src/connection/deviceSession.tsx`
- Create: `apps/manager/src/connection/deviceSession.test.tsx`
- Create: `apps/manager/src/connection/ConnectionDialog.tsx`
- Modify: `apps/manager/src/api/types.ts`

- [ ] Define session states: `disconnected`, `connecting`, `connected`, `error`.
- [ ] Persist only base URL and theme in local storage. Do not persist bearer tokens in local storage.
- [ ] On Connect, fetch device, models, IRs and preset summaries concurrently after device authentication succeeds.
- [ ] Store the client and loaded resource arrays in context; expose explicit refresh methods.
- [ ] Prevent overlapping connection/save/apply/upload operations of the same kind.
- [ ] If the daemon reports auth enabled and the request is unauthorized, keep the dialog open and focus the token input.
- [ ] On initial successful connection choose the location in this order:

  1. `device.active` when present and loadable;
  2. the last locally selected location for this base URL;
  3. bank 0, slot 0.

- [ ] If the chosen slot does not exist, initialize a local empty preset targeted at that location instead of calling GET repeatedly.
- [ ] Do not automatically reconnect with a previously entered token after application restart.
- [ ] Test success, auth failure, network failure, empty slot, optional active location, and reconnect.

**Gate:** Components consume a coherent session instead of recreating clients and loading flags independently.

---

### Task 8: Implement crash-recovery drafts and navigation guards

**Files:**

- Create: `apps/manager/src/presets/editor/recovery.ts`
- Create: `apps/manager/src/presets/editor/recovery.test.ts`
- Create: `apps/manager/src/presets/workspace/UnsavedChangesDialog.tsx`

- [ ] Store recovery records under a key derived from base URL, bank and slot. A record contains the draft, the serialized saved snapshot it was based on, and a timestamp.
- [ ] Debounce writes by 500 ms and only persist dirty drafts.
- [ ] Remove the record after successful Save/Save & Apply or explicit Discard.
- [ ] On load, offer recovery only when the record's saved snapshot equals the freshly loaded saved preset and its draft differs.
- [ ] If the saved snapshot differs, label the record stale and allow explicit dismissal; never auto-merge.
- [ ] Add an unsaved-navigation controller used for slot changes, bank changes, disconnect, and app-level navigation to Assets.
- [ ] **Save** in the guard completes the save before continuing navigation. A failed save leaves the user on the current preset.
- [ ] Test recovery creation/removal, stale recovery, and all Save/Discard/Cancel branches.

**Gate:** Accidental navigation and app crashes do not silently lose work.

---

### Task 9: Establish the Ardor design system and app shell

**Files:**

- Modify: `apps/manager/src/styles.css`
- Modify: `apps/manager/tailwind.config.cjs`
- Modify: `apps/manager/src-tauri/tauri.conf.json`
- Create: reusable files under `apps/manager/src/components/`
- Create: files under `apps/manager/src/app/`
- Modify: `apps/manager/src/main.tsx`
- Replace: `apps/manager/src/App.tsx`

- [ ] Add CSS variables for background, surfaces, elevated surfaces, borders, text tiers, accent, danger, warning, focus ring, and every effect category.
- [ ] Set dark as first-run default and support light via `[data-theme]` variables.
- [ ] Add visible `:focus-visible` rings; never remove outlines without replacement.
- [ ] Build Button, IconButton, Toggle, StatusBadge, Tooltip, Drawer, ConfirmDialog, NumberField, ParameterSlider, EmptyState and ToastRegion primitives.
- [ ] `ParameterSlider` must pair a labelled range input with a number input, clamp on blur, and announce units.
- [ ] Use Radix for focus-trapped modal/drawer behavior. Escape closes non-destructive dialogs.
- [ ] Build `AppShell` with Workspace and Assets navigation, top bar, connection status, theme control and settings.
- [ ] Update Tauri default size to 1280x800 and minimum to 960x640 only if the current user-modified config has no conflicting intent.
- [ ] Keep `App.tsx` under roughly 100 lines.
- [ ] Remove DaisyUI component classes from all newly created components. Do not remove the DaisyUI package until the old UI is gone.
- [ ] Add component tests for keyboard focus, dialog focus return, tooltip labels and theme persistence.

**Gate:** The empty application shell is responsive, keyboard accessible, and visually distinct from generic DaisyUI.

---

### Task 10: Build the bank and preset sidebar

**Files:**

- Create: `apps/manager/src/presets/browser/BankPicker.tsx`
- Create: `apps/manager/src/presets/browser/PresetSidebar.tsx`
- Create: `apps/manager/src/presets/browser/PresetSlotCard.tsx`
- Add corresponding tests.

- [ ] Bank picker accepts `0..99`, supports keyboard increment/decrement, and displays three digits.
- [ ] Render exactly four slot cards for the selected bank, including empty slots.
- [ ] Slot card displays slot number, preset name or “Empty slot,” dirty marker for the current draft, optional server warning counts, and applied-this-session marker.
- [ ] Clicking an empty slot creates a local empty draft with name `New Preset` targeted at that slot.
- [ ] Clicking another slot goes through the unsaved-navigation controller.
- [ ] Add bank search/jump; do not render a flat list of 400 rows.
- [ ] Persist last selected bank/slot per base URL.
- [ ] Use `aria-current="true"` for the selected slot.
- [ ] Add loading skeleton, empty/error states and retry.
- [ ] Test bank 0/99 boundaries, empty slots, dirty guard and keyboard selection.

**Gate:** Every one of the 400 locations is reachable without a flat, unscannable preset list.

---

### Task 11: Build the signal-chain canvas

**Files:**

- Create files under `apps/manager/src/presets/chain/`
- Add chain component tests.

- [ ] Render Input and Output terminals around a horizontal sequence of block cards.
- [ ] Block card displays friendly definition name, asset filename or mode subtitle, category accent, bypass state and the highest-severity validation badge.
- [ ] Selecting a card updates the inspector without mutating history.
- [ ] Add insertion buttons before, between and after blocks. Each opens Block Browser with a fixed target index.
- [ ] Configure dnd-kit sortable pointer and keyboard sensors. Use `sortableKeyboardCoordinates`.
- [ ] After a drag, dispatch one `move-block` action. Do not update preset order on every pointer movement.
- [ ] Every card menu provides Move left, Move right, Duplicate, Reset and Delete as accessible alternatives.
- [ ] Bypass toggle must not also select/drag the card accidentally.
- [ ] Delete uses a five-second undo toast. The implementation may dispatch remove immediately and retain the prior preset for undo.
- [ ] Disable Add and Duplicate at ten blocks with a tooltip explaining the limit.
- [ ] Keep disabled blocks in their chain position with reduced contrast and a clear diagonal bypass treatment; do not hide them.
- [ ] Test pointer-independent reorder through buttons/keyboard, selection, bypass, delete undo, validation badges and max blocks.

**Gate:** The chain itself, rather than a long form, is the primary workspace.

---

### Task 12: Build the searchable Block Browser

**Files:**

- Create files under `apps/manager/src/presets/block-browser/`
- Add block-browser tests.

- [ ] Open as a modal on desktop and full-height drawer on narrow viewports.
- [ ] Provide All, Amp, Cabinet, Dynamics, EQ, Modulation, Delay and Reverb category filters.
- [ ] Search names, descriptions and common aliases. Include catalog aliases such as `tremolo`, `BBD`, `bucket`, `octave`, `shimmer`, `compressor`, `IR`, and `NAM`.
- [ ] Display a card for all 39 definitions.
- [ ] Disable a definition when adding it enabled would violate a `maxEnabledInGroup` constraint. Explain the conflicting existing block by name.
- [ ] Do not disable definitions merely because an existing block of the same family is bypassed; validation permits one enabled instance.
- [ ] NAM and Cab cards remain selectable with no assets installed. The created block uses an empty asset and immediately opens its repair state in the inspector.
- [ ] Selecting a definition dispatches add at the captured insertion index, closes the browser, and selects the new block.
- [ ] Restore focus to the insertion button that opened the browser.
- [ ] Test exact definition counts by category, search aliases, constraint messaging and insertion index.

**Gate:** Every supported effect is discoverable and addable through friendly names.

---

### Task 13: Build known-block inspectors

**Files:**

- Create files under `apps/manager/src/presets/inspector/` except EQ files handled in Task 14.
- Add inspector tests.

- [ ] `BlockInspector` resolves the current definition and routes to the appropriate inspector.
- [ ] Header contains definition name, type/category, enable toggle, validation summary and mode selector where the family has multiple modes.
- [ ] Daisy inspector renders catalog-driven percentage controls. Display rounded integers, while preserving the stored float until the user changes it.
- [ ] Mode selector lists only definitions with the same `blockType`. Changing modes retains shared values as defined by the reducer.
- [ ] Compressor inspector renders:

  - Threshold `-60..0 dB`;
  - Ratio `1..20`;
  - Attack `0.1..200 ms`;
  - Release `10..2000 ms`;
  - Knee `0..24 dB`;
  - Makeup `0..24 dB`;
  - Input `-24..24 dB`;
  - Mix `0..100%`;
  - Sidechain HPF `20..500 Hz`;
  - Detector Peak/RMS;
  - Auto Makeup Off/On.

- [ ] NAM/Cab asset inspector provides a searchable native/listbox picker, shows file size, marks missing paths, and has a shortcut to the Assets view.
- [ ] Cabinet also renders Level `-60..12 dB` and Mix `0..100%`.
- [ ] Footer provides Reset, Duplicate and Delete with the same semantics as the card menu.
- [ ] Add collapsed Advanced block details showing immutable ID/type/mode and unknown fields. Known block raw JSON remains read-only.
- [ ] Unknown block inspector permits enable, reorder through chain controls, duplicate, delete and a guarded JSON params editor. Require valid JSON object input before dispatching `replace-present`; preserve all non-params block fields.
- [ ] Test every control kind, boundary values, mode change, missing assets, reset and unknown JSON validation.

**Gate:** NAM, Cab, Compressor and all 35 Daisy modes are completely editable without raw JSON.

---

### Task 14: Build the five-band EQ inspector and response graph

**Files:**

- Create: `apps/manager/src/presets/inspector/EqInspector.tsx`
- Create: `apps/manager/src/presets/inspector/EqResponseGraph.tsx`
- Add focused EQ tests.

- [ ] Render an SVG response graph from 20 Hz to 20 kHz logarithmically and `-18..18 dB` vertically.
- [ ] Port only UI-safe response math needed from `ParametricEqMath`; do not call native code. Use RBJ peaking EQ magnitude with a fixed 48 kHz sample rate for display consistency.
- [ ] Draw grid labels at 20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k and horizontal labels at -18, -12, -6, 0, 6, 12, 18 dB.
- [ ] Draw one colored node per band and the combined response curve.
- [ ] Clicking or focusing a node selects that band.
- [ ] Pointer dragging changes frequency horizontally and gain vertically; Q remains in the form. Dispatch throttled edits at animation-frame cadence, not every raw pointer event.
- [ ] Provide complete non-pointer controls for each band:

  - enabled toggle;
  - frequency `20..20000 Hz`;
  - Q `0.1..18`;
  - gain `-18..18 dB`;
  - reset band.

- [ ] Ensure exactly five canonical bands after every edit.
- [ ] Add unit tests for coordinate mapping, finite curve output, default flat curve, boosted center response, range clamping and keyboard editing.
- [ ] Add an accessible text summary such as “Band 1, 80 Hz, 0 dB, Q 1, enabled.”

**Gate:** The EQ is visually understandable and fully operable without relying on the graph.

---

### Task 15: Wire preset identity, globals, validation and save/apply actions

**Files:**

- Create: `apps/manager/src/presets/inspector/PresetInspector.tsx`
- Create: `apps/manager/src/presets/workspace/PresetWorkspace.tsx`
- Modify: app top bar/session wiring.
- Add workspace integration tests.

- [ ] Workspace header shows editable preset name, bank/slot identity, dirty state and validation summary.
- [ ] Preset inspector exposes Input Gain and Output Gain with `-60..24 dB` bounds, matching runtime clamping/OpenAPI.
- [ ] Show routing `serial`, version `1`, and safety limit under collapsed Advanced metadata. Safety limit is read-only.
- [ ] Save button states:

  - disabled when disconnected, clean, busy or `canSave` is false;
  - label `Save` normally and `Saving…` during the request;
  - on success dispatch `mark-saved` with the response preset, refresh summaries, clear recovery and show a toast;
  - on failure retain the draft and focus/show the error summary.

- [ ] Save & Apply states:

  - disabled when disconnected, busy, `canSave` false or `canApply` false;
  - save first even when the draft is currently clean only if needed;
  - apply the exact saved location after successful save;
  - show `Applied this session` only after accepted response;
  - never mark applied if either request fails.

- [ ] A clean preset may expose a secondary Apply action; it remains disabled when `canApply` is false.
- [ ] Add Undo/Redo buttons and keyboard shortcuts:

  - macOS: `Cmd+Z`, `Cmd+Shift+Z`;
  - Windows/Linux: `Ctrl+Z`, `Ctrl+Shift+Z` and `Ctrl+Y`.

- [ ] Do not steal undo shortcuts while a text input is actively handling native text undo unless the app action history clearly owns the latest change. The simplest acceptable rule is to let input-native undo win while focused.
- [ ] Display validation in three places: top summary, block badge, and field-level message.
- [ ] Test save canonical response, failed save, save/apply ordering, dirty apply prevention, warning-only save, recovery cleanup and shortcuts.

**Gate:** The saved file and active-request semantics are always understandable and race-free.

---

### Task 16: Build the dedicated Asset Library

**Files:**

- Create files under `apps/manager/src/assets/`
- Add asset component tests.

- [ ] Workspace/Assets navigation passes through the dirty navigation guard.
- [ ] Render Models and Cabinet IR tabs with search, filename, type and formatted size.
- [ ] Add drag-and-drop and file-picker upload. Accept `.nam` for Models and `.wav` for IRs before calling the server.
- [ ] Support multiple selected files by uploading sequentially so progress and individual failures remain understandable.
- [ ] On `asset_exists` conflict, open a dialog offering Replace or Skip for that exact filename. Replace retries with `overwrite=true`.
- [ ] After each successful upload refresh the matching asset list and show a non-blocking toast.
- [ ] Delete requires a confirmation naming the asset and warning that existing presets may reference it. On success refresh assets and revalidate the current draft.
- [ ] Do not promise usage counts in this milestone; the daemon has no usage endpoint.
- [ ] Add a “Use in selected block” action only when the user navigated from a missing-asset repair flow and asset kinds match.
- [ ] Test extension rejection, sequential uploads, conflict replace/skip, delete confirmation and missing-reference revalidation.

**Gate:** Connection and upload controls no longer compete with the primary preset editor.

---

### Task 17: Remove the legacy manager implementation

**Files:**

- Delete: `apps/manager/src/presets/presetDraft.ts`
- Delete or rewrite: `apps/manager/src/presets/presetDraft.test.ts`
- Modify: `apps/manager/src/App.tsx`
- Modify: `apps/manager/src/styles.css`
- Modify: `apps/manager/tailwind.config.cjs`
- Modify: `apps/manager/package.json`
- Modify: `apps/manager/package-lock.json`

- [ ] Confirm no imports reference `presetDraft.ts`, `effectParams`, `isKnownEditableBlock`, or the old monolithic editor.
- [ ] Remove DaisyUI classes and plugin configuration after the last old component is gone.
- [ ] Remove the `daisyui` package if no code/config references it.
- [ ] Ensure no built `dist` output is added to version control.
- [ ] Run `rg -n "Read-only|effectParams|isKnownEditableBlock|daisyui" apps/manager/src apps/manager/package.json apps/manager/tailwind.config.cjs` and inspect every remaining match.
- [ ] Run unit tests, typecheck and production build.

**Gate:** There is one editor architecture, not a redesigned shell wrapped around the old form.

---

### Task 18: Add browser end-to-end coverage

**Files:**

- Create: `apps/manager/playwright.config.ts`
- Create: `apps/manager/e2e/manager.spec.ts`
- Create: `apps/manager/e2e/fixtures/` as needed
- Optionally create: a test-only launcher script if it does not write production data.

- [ ] Launch `ardor-managerd` against a temporary data root with auth off and a non-production port.
- [ ] Seed assets and presets by copying small existing test fixtures into the temporary root. Never point destructive E2E tests at the repository root or a real pedal.
- [ ] Launch Vite on port 1420 and run these flows:

  1. Connect and load an existing preset.
  2. Navigate to an empty bank/slot.
  3. Add Compressor, NAM, Cabinet, EQ, one Mod, one Delay and one Reverb.
  4. Edit at least one value in every block family.
  5. Reorder blocks and verify JSON order after Save.
  6. Bypass and duplicate a constrained block; verify validation blocks Apply until one is disabled/deleted.
  7. Save, reload and verify values survive.
  8. Save & Apply and verify the daemon queued the request/returned accepted.
  9. Trigger missing asset repair.
  10. Trigger unsaved navigation and test Cancel then Discard.
  11. Upload, replace and delete a temporary asset.

- [ ] Add viewport checks at 1280x800, 1024x700 and 800x900 browser fallback.
- [ ] Add screenshots for stable major states, but avoid brittle pixel-perfect assertions for native font rendering. Prefer layout/visibility assertions plus manually inspectable artifacts.
- [ ] Ensure test teardown terminates processes and removes the temporary data root.

**Gate:** The entire manager workflow is verified through the same HTTP boundary used by Tauri.

---

### Task 19: Accessibility, performance and final verification

**Files:** Modify only where verification reveals issues.

- [ ] Keyboard-only walkthrough:

  - connect;
  - choose bank/slot;
  - add a block;
  - reorder it;
  - edit parameters;
  - save and apply;
  - navigate to Assets and back.

- [ ] Verify every icon-only button has an accessible name and tooltip.
- [ ] Verify drawers/dialogs trap focus, close appropriately, and return focus.
- [ ] Verify color is never the sole indicator for selected, bypassed, warning or error states.
- [ ] Verify range inputs expose labels, current values, min/max and units.
- [ ] Verify no main-thread work loops over all 400 preset slots on every editor keystroke.
- [ ] Use React Profiler or targeted instrumentation to confirm dragging one slider does not rerender all slot cards and asset rows. Memoize catalog/slot rows only when evidence shows it is needed.
- [ ] Run the complete verification matrix:

  ```bash
  cd apps/manager
  npm test
  npm run typecheck
  npm run build
  npm run test:e2e

  cd ../../services/managerd
  go test ./...

  cd ../..
  cmake --build build --target pedal-manager-effect-catalog-smoke -j2
  ./build/pedal-manager-effect-catalog-smoke
  ctest --test-dir build --output-on-failure
  ```

- [ ] Run Tauri development smoke test on macOS:

  ```bash
  cd apps/manager
  npm run tauri dev
  ```

- [ ] Manually verify minimum window size, theme persistence, file picker behavior, connection failure, a real seeded preset and clean process shutdown.
- [ ] Review `git diff --check` and the final scoped diff. Do not include unrelated worktree changes.

**Gate:** All acceptance criteria below are satisfied and there are no known regressions in the daemon or existing DSP/runtime tests.

---

## 7. Component-level behavior details

### Block card interaction

- Single click selects.
- Space on a focused card selects it.
- Drag handle alone begins pointer drag; clicking sliders/toggles never drags.
- `Shift+ArrowLeft/Right` may move the focused block as an optional shortcut; menu buttons are mandatory regardless.
- Bypass remains a preset edit and participates in undo/redo.
- Validation badge opens/focuses the inspector's issue list.

### Parameter editing

- Sliders update the draft continuously.
- Numeric inputs keep an intermediate string while focused so the user can type `-`, `.`, or replace the full value without reducer churn.
- Commit valid numeric input on blur or Enter; Escape restores the prior value.
- Daisy values display `Math.round(value * 100)%`; number input accepts `0..100` and converts to stored `0..1`.
- Do not rewrite untouched floats merely because their display is rounded.
- Reset Block restores catalog defaults with one undo entry.

### Mode changes

- The inspector's mode menu only changes within the same block family.
- Preserve shared parameter values because all Daisy family modes share the same stored keys.
- Fill any missing catalog key with the new mode's default.
- Preserve unknown keys.
- Update the friendly card name immediately.

### Validation presentation

- Error: Save blocked; red summary and field/block indicator.
- Warning that blocks Apply: amber summary and block indicator; Save remains available.
- Informational state such as bypass: neutral badge, not a warning.
- Top summary uses actionable text, e.g. “Cannot apply: Cabinet IR is missing in block 4,” not only an error code.

### Toasts

- Success toasts dismiss after four seconds.
- Error toasts remain until dismissed when the same error is not already visible inline.
- Delete Block toast includes Undo for five seconds.
- Do not use toasts as the only source of save/apply state.

---

## 8. Deferred work — explicitly outside this implementation

Do not expand this redesign into these items unless the user separately authorizes them:

- Live parameter streaming to the running audio engine.
- An authoritative runtime status publication channel for active bank/slot after reconnect.
- Pairing-code onboarding or OS credential-store integration.
- Preset cloud sync, import/export bundles or sharing.
- Mobile-specific navigation.
- Parallel routing; V1 remains serial.
- Asset usage-count API.
- Preset delete/copy/move endpoints. A future “Save As” can use existing GET/PUT semantics.
- Editing `global.safetyLimitDb` as a tone control.
- Reworking DSP parameter mappings or effect algorithms.

The manager may consume `DeviceStatus.active` when present because the OpenAPI schema already allows it, but this plan does not add the runtime machinery that makes it authoritative.

---

## 9. Acceptance criteria

The implementation is complete only when all conditions hold:

- [ ] The Block Browser exposes exactly 39 supported definitions.
- [ ] All 35 Daisy modes have semantic names and seven editable normalized controls.
- [ ] NAM, Cabinet, Compressor and Five Band EQ are fully editable.
- [ ] Blocks can be inserted anywhere, reordered, selected, bypassed, duplicated, reset, deleted and undone.
- [ ] Known blocks never require raw JSON editing.
- [ ] Unknown fields and blocks survive a known-field edit and save byte-for-value at the JSON data level.
- [ ] The manager prevents more than ten blocks.
- [ ] Validation catches missing assets, unsupported modes, constrained duplicates and mono-after-stereo ordering before Apply.
- [ ] Save never applies; Save & Apply always saves first.
- [ ] Dirty drafts cannot be accidentally discarded by slot, bank or page navigation.
- [ ] Empty slots in all banks `0..99` can be created and saved.
- [ ] Asset upload, overwrite conflict and delete work from the Assets view.
- [ ] The app works at the configured Tauri minimum size and has a usable browser fallback below it.
- [ ] All primary actions are keyboard accessible with visible focus.
- [ ] Light/dark themes persist; dark is the first-run default.
- [ ] Frontend unit/component tests, E2E tests, Go tests, catalog parity test and production builds pass.
- [ ] No unrelated dirty-worktree changes are overwritten or included.

---

## 10. Recommended commit boundaries

If the user has authorized commits, use small, reviewable commits in this order. Otherwise leave changes uncommitted and report the same boundaries in the handoff.

1. `test(manager): add component and browser test harness`
2. `feat(manager): define complete effect catalog`
3. `test: enforce manager and runtime catalog parity`
4. `feat(manager): add history-aware preset editor reducer`
5. `feat(manager): validate saved and applyable presets`
6. `feat(manager): add device session and rich API errors`
7. `feat(manager): add draft recovery and navigation guards`
8. `feat(manager): introduce Ardor desktop shell`
9. `feat(manager): add bank and preset navigation`
10. `feat(manager): build sortable signal chain and block browser`
11. `feat(manager): add complete block inspectors`
12. `feat(manager): add five-band EQ editor`
13. `feat(manager): wire safe save and apply workflows`
14. `feat(manager): add asset library workflows`
15. `test(manager): cover end-to-end desktop workflows`
16. `refactor(manager): remove legacy form editor`

Do not create commits automatically unless the user has explicitly requested commits.
