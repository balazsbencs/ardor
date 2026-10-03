# Manager Stage and Drawer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the manager web app UI (`apps/manager`) as mockup 1 "Stage and Drawer" plus its Assets view, in the pedal's Lamp Black language, with a reorderable device-style chain.

**Architecture:** Keep the domain layer (API client, device session, editor reducer, validation, catalog, scenes revision, cloud, local auth, TONE3000 API). Move the editor logic out of `PresetWorkspace` into a `usePresetEditor` hook behind an `EditorProvider` above both views, so the draft survives view switches. Build new presentation modules under `src/ui/` (tokens and primitives), `src/stage/` (Edit view) and `src/assets/` (Assets view). Add one read-only `managerd` endpoint for asset usage.

**Tech Stack:** React 19, TypeScript 7, Vite 8, Vitest 5 + Testing Library (jsdom), dnd-kit (core 6, sortable 10), Radix Dialog, lucide-react, Fontsource, Playwright 1.63; Go (`services/managerd`, stdlib `net/http` mux).

**Spec:** `docs/superpowers/specs/2026-09-29-manager-stage-drawer-design.md`. Visual reference: `mockups/manager-taste/1-stage-drawer.html` and `1-stage-drawer.html#assets` (serve with `python3 -m http.server 8765` from `mockups/manager-taste/`).

## Global Constraints

- Lamp Black tokens only; values come from `kSlate` in `src/ui/LvglUiStyle.cpp`. No new literal colours in CSS; tints use `color-mix(in srgb, var(--token) N%, transparent)`.
- Lamp red (`--lamp`) means live preset, live scene, or the focused control. Never a button fill or hover colour.
- Square corners (`border-radius: 0`) on every panel, button, card, chip, tile and input.
- Fonts: Saira Condensed 500–800 for scan labels and values, Saira 400–600 for prose, JetBrains Mono 500 only for chain-strip and module codes.
- Bank and slot labels: `BANK 00` to `BANK 99` (0-based, two digits), `FS 1` to `FS 4`.
- The LAN build shows `HTTP` in the connection pill. The hosted build (`VITE_ARDOR_HOSTED_MODE=true`) does not.
- The safety limiter is shown as fixed `-1 dBFS` protection. It is never an editable control.
- UI copy follows Simplified Technical English: one idea per sentence, active voice, no em or en dashes in visible text. Units keep their case (ms, kHz, dB).
- Immutable updates only (spread / `structuredClone` on copies); no mutation of props or state.
- Files under 400 lines where possible, 800 maximum. No `console.log`.
- Minimum test coverage 80% for new modules (`npx vitest run --coverage` if the coverage provider is installed; otherwise every new module has its own test file).
- Keep existing behaviour of save, apply, recall, unsaved-changes guard, block constraints, WDW policy and scene ownership.
- Work on branch `feat/manager-stage-drawer`. Commit messages: `<type>: <description>`, types feat, fix, refactor, docs, test, chore, perf, ci.

## Review Focus

1. **Undo after a drag.** A person drags a slider across its range and presses ⌘Z once. Expected: the value returns to where the drag started, not one step back. Covered by the gesture test in Task 3 and the TravelScale gesture test in Task 7.
2. **Unsaved draft across views.** A person edits a value, opens Assets, and comes back to Edit. Expected: the edit is still there and MODIFIED still shows. Covered by the EditorProvider test in Task 5 and the StageWorkspace test in Task 14.
3. **Scene edit of a value the scene does not own.** In scene 3, a person turns Mix. Expected: all four scenes own Mix, scene 3 gets the new value, the others keep the old one, and the preset value does not change. Covered by the `editParameter` scene test in Task 11.
4. **Drop that the reducer cannot do.** A person drags a top-level block into a Dual Rig lane, or a lane block to the top level. Expected: nothing moves, and no error. Covered by the `resolveDrop` tests in Task 9.
5. **Upload name conflict and wrong file type.** A person drops `Brown Sound.nam` (already on the pedal) and `notes.txt`. Expected: a Replace / Skip row for the first, a rejection row for the second, and no upload of either until the person chooses. Covered by the upload queue tests in Task 15 and the E2E test in Task 18.

---

### Task 0: Branch and commit the design sources

**Files:**
- Add: `mockups/manager-taste/` (all files; generated scratch files excluded)
- Add: `docs/superpowers/specs/2026-09-29-manager-stage-drawer-design.md`
- Add: `docs/superpowers/plans/2026-09-29-manager-stage-drawer.md`

- [ ] **Step 1: Create the branch**

```bash
cd /Users/bbalazs/Documents/Ardor
git checkout -b feat/manager-stage-drawer
```

- [ ] **Step 2: Check the mockup folder has no stray files**

Run: `git status --short mockups/manager-taste | head -40`
Expected: only `.html`, `.js`, `.css` files and the `current/` and `thumbs/` `.webp` images.

- [ ] **Step 3: Commit**

```bash
git add mockups/manager-taste docs/superpowers/specs/2026-09-29-manager-stage-drawer-design.md docs/superpowers/plans/2026-09-29-manager-stage-drawer.md
git commit -m "docs: add manager Stage and Drawer mockups, spec and plan"
```

---

### Task 1: `managerd` asset usage endpoint

**Files:**
- Modify: `services/managerd/internal/presets/presets.go` (add after `replaceAssetInBlocks`, near line 880)
- Modify: `services/managerd/internal/server/server.go` (add a route before `mux.HandleFunc("GET /api/assets/{kind}"`, line 422)
- Test: `services/managerd/internal/presets/presets_test.go`, `services/managerd/internal/server/server_test.go`

**Interfaces:**
- Produces: `func (s Store) AssetUsage() (map[string][]AssetUse, error)`; `type AssetUse struct { Bank int; Slot int; Name string }`; HTTP `GET /api/assets/usage` → `200 {"usage":[{"path":"models/a.nam","presets":[{"bank":0,"slot":0,"name":"Clean"}]}]}`, sorted by `path`, presets in bank then slot order.

- [ ] **Step 1: Write the failing store test**

Add to `presets_test.go`:

```go
func TestAssetUsageListsPresetsPerPath(t *testing.T) {
	store := NewStore(t.TempDir())
	first := validPreset()
	first["name"] = "Clean"
	first["blocks"] = []any{
		map[string]any{"id": "nam-1", "type": "nam", "enabled": true, "asset": "models/clean.nam", "params": map[string]any{}},
		map[string]any{"id": "cab-1", "type": "cab", "enabled": true, "asset": "irs/2x12.wav", "params": map[string]any{}},
	}
	second := validPreset()
	second["name"] = "Lead"
	second["blocks"] = []any{
		map[string]any{"id": "nam-2", "type": "nam", "enabled": true, "asset": "models/clean.nam", "params": map[string]any{}},
	}
	if _, err := store.Save(0, 0, first); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Save(2, 3, second); err != nil {
		t.Fatal(err)
	}

	usage, err := store.AssetUsage()
	if err != nil {
		t.Fatal(err)
	}
	clean := usage["models/clean.nam"]
	if len(clean) != 2 || clean[0] != (AssetUse{Bank: 0, Slot: 0, Name: "Clean"}) || clean[1] != (AssetUse{Bank: 2, Slot: 3, Name: "Lead"}) {
		t.Fatalf("models/clean.nam usage = %#v", clean)
	}
	if cab := usage["irs/2x12.wav"]; len(cab) != 1 || cab[0].Name != "Clean" {
		t.Fatalf("irs/2x12.wav usage = %#v", cab)
	}
	if len(usage) != 2 {
		t.Fatalf("usage has %d paths, want 2: %#v", len(usage), usage)
	}
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd services/managerd && go test ./internal/presets -run TestAssetUsageListsPresetsPerPath`
Expected: FAIL, `store.AssetUsage undefined` and `undefined: AssetUse`.

- [ ] **Step 3: Implement the store scan**

Add to `presets.go` after `replaceAssetInBlocks`:

```go
// AssetUse is one saved preset that references an asset path.
type AssetUse struct {
	Bank int    `json:"bank"`
	Slot int    `json:"slot"`
	Name string `json:"name,omitempty"`
}

// AssetUsage maps every asset path that a saved preset references to the
// presets that use it. It walks the same fields as ReplaceAssetReferences.
// Paths with no file on disk are included, so the manager can show missing
// files.
func (s Store) AssetUsage() (map[string][]AssetUse, error) {
	usage := map[string][]AssetUse{}
	for bank := 0; bank < 100; bank++ {
		for slot := 0; slot < 4; slot++ {
			loaded, err := s.Load(bank, slot)
			if errors.Is(err, os.ErrNotExist) {
				continue
			}
			if err != nil {
				return nil, fmt.Errorf("load bank %d slot %d: %w", bank, slot, err)
			}
			seen := map[string]bool{}
			if blocks, ok := loaded.Preset["blocks"].([]any); ok {
				collectAssetPaths(blocks, seen)
			}
			if wdw, ok := loaded.Preset["wdw"].(map[string]any); ok {
				for _, laneName := range []string{"dry", "wet"} {
					lane, _ := wdw[laneName].(map[string]any)
					children, _ := lane["blocks"].([]any)
					collectAssetPaths(children, seen)
				}
			}
			name, _ := loaded.Preset["name"].(string)
			for path := range seen {
				usage[path] = append(usage[path], AssetUse{Bank: bank, Slot: slot, Name: name})
			}
		}
	}
	return usage, nil
}

func collectAssetPaths(blocks []any, seen map[string]bool) {
	for _, item := range blocks {
		block, ok := item.(map[string]any)
		if !ok {
			continue
		}
		if asset, _ := block["asset"].(string); asset != "" {
			seen[asset] = true
		}
		if block["type"] == "dualAmp" {
			params, _ := block["params"].(map[string]any)
			for _, key := range []string{"leftNamAsset", "leftIrAsset", "rightNamAsset", "rightIrAsset"} {
				if asset, _ := params[key].(string); asset != "" {
					seen[asset] = true
				}
			}
		}
		if lanes, ok := block["lanes"].(map[string]any); ok {
			for _, laneName := range []string{"left", "right"} {
				lane, _ := lanes[laneName].(map[string]any)
				children, _ := lane["blocks"].([]any)
				collectAssetPaths(children, seen)
			}
		}
	}
}
```

- [ ] **Step 4: Run the store test**

Run: `go test ./internal/presets -run TestAssetUsageListsPresetsPerPath`
Expected: PASS.

- [ ] **Step 5: Write the failing server test**

Add to `server_test.go`:

```go
func TestAssetUsageReportsPresetsPerAsset(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: false})
	preset := []byte(`{"version":1,"name":"Uses model","routing":"serial","global":{},"blocks":[{"id":"nam-1","type":"nam","enabled":true,"asset":"models/clean.nam","params":{}}]}`)
	save := httptest.NewRecorder()
	handler.ServeHTTP(save, httptest.NewRequest(http.MethodPut, "/api/presets/banks/2/slots/1", bytes.NewReader(preset)))
	if save.Code != http.StatusOK {
		t.Fatalf("save status=%d body=%s", save.Code, save.Body.String())
	}

	recorder := httptest.NewRecorder()
	handler.ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/assets/usage", nil))
	if recorder.Code != http.StatusOK {
		t.Fatalf("usage status=%d body=%s", recorder.Code, recorder.Body.String())
	}
	var body struct {
		Usage []struct {
			Path    string `json:"path"`
			Presets []struct {
				Bank int    `json:"bank"`
				Slot int    `json:"slot"`
				Name string `json:"name"`
			} `json:"presets"`
		} `json:"usage"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if len(body.Usage) != 1 || body.Usage[0].Path != "models/clean.nam" || len(body.Usage[0].Presets) != 1 {
		t.Fatalf("usage = %s", recorder.Body.String())
	}
	if got := body.Usage[0].Presets[0]; got.Bank != 2 || got.Slot != 1 || got.Name != "Uses model" {
		t.Fatalf("usage preset = %#v", got)
	}
}
```

- [ ] **Step 6: Run it to see it fail**

Run: `go test ./internal/server -run TestAssetUsageReportsPresetsPerAsset`
Expected: FAIL with `usage status=404` (the `{kind}` route rejects `usage` as an asset kind).

- [ ] **Step 7: Add the route**

In `server.go`, before `mux.HandleFunc("GET /api/assets/{kind}", ...)`. The literal path is more specific than `{kind}`, so the Go 1.22 mux picks it first.

```go
	mux.HandleFunc("GET /api/assets/usage", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		usage, err := presetStore.AssetUsage()
		if err != nil {
			writeError(w, http.StatusInternalServerError, "asset_usage_failed", err.Error())
			return
		}
		type entry struct {
			Path    string             `json:"path"`
			Presets []presets.AssetUse `json:"presets"`
		}
		entries := make([]entry, 0, len(usage))
		for path, uses := range usage {
			entries = append(entries, entry{Path: path, Presets: uses})
		}
		sort.Slice(entries, func(i, j int) bool { return entries[i].Path < entries[j].Path })
		writeJSON(w, http.StatusOK, map[string]any{"usage": entries})
	})
```

Add `"sort"` to the imports of `server.go` if it is not there. Check that `presets` is the imported package name for `internal/presets` (it is used as `presets.Preset` in the PUT handler).

- [ ] **Step 8: Run all `managerd` tests**

Run: `go test ./...`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add services/managerd/internal/presets services/managerd/internal/server
git commit -m "feat: add asset usage endpoint to managerd"
```

---

### Task 2: Manager API and session support for asset usage

**Files:**
- Modify: `apps/manager/src/api/types.ts` (append)
- Modify: `apps/manager/src/api/transport.ts` (interface `ManagerTransport`)
- Modify: `apps/manager/src/api/client.ts` (after `renameAsset`)
- Modify: `apps/manager/src/connection/deviceSession.tsx` (`DeviceSessionValue`, provider state, `connect`, `refreshAssets`, `disconnect`, value memo)
- Test: `apps/manager/src/api/client.test.ts`, `apps/manager/src/connection/deviceSession.test.tsx`

**Interfaces:**
- Consumes: `GET /api/assets/usage` from Task 1.
- Produces: `type AssetUse = { bank: number; slot: number; name?: string }`, `type AssetUsageEntry = { path: string; presets: AssetUse[] }`; `ManagerTransport.getAssetUsage?(): Promise<AssetUsageEntry[]>`; `ArdorApiClient.getAssetUsage()`; `DeviceSessionValue.assetUsage?: AssetUsageEntry[]` (undefined when the transport does not support it) and `refreshAssetUsage(): Promise<void>`.

- [ ] **Step 1: Write the failing client test**

Add to `client.test.ts`, following the file's existing `fetchImpl` mock pattern:

```ts
it("reads asset usage from the pedal", async () => {
  const fetchImpl = vi.fn(async () => new Response(JSON.stringify({
    usage: [{ path: "models/clean.nam", presets: [{ bank: 2, slot: 1, name: "Uses model" }] }],
  }), { status: 200, headers: { "Content-Type": "application/json" } }));
  const client = new ArdorApiClient({ baseUrl: "http://pedal.local", fetchImpl });
  await expect(client.getAssetUsage()).resolves.toEqual([
    { path: "models/clean.nam", presets: [{ bank: 2, slot: 1, name: "Uses model" }] },
  ]);
  expect(fetchImpl).toHaveBeenCalledWith("http://pedal.local/api/assets/usage", expect.anything());
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd apps/manager && npx vitest run src/api/client.test.ts`
Expected: FAIL, `client.getAssetUsage is not a function`.

- [ ] **Step 3: Add the types, transport method and client method**

`types.ts` (append):

```ts
export type AssetUse = { bank: number; slot: number; name?: string };
export type AssetUsageEntry = { path: string; presets: AssetUse[] };
```

`transport.ts`: import `AssetUsageEntry` and add to `ManagerTransport` after `renameAsset`:

```ts
  /** Optional: the hosted relay does not forward this route yet. */
  getAssetUsage?(): Promise<AssetUsageEntry[]>;
```

`client.ts`: import `AssetUsageEntry` and add after `renameAsset`:

```ts
  async getAssetUsage(): Promise<AssetUsageEntry[]> {
    const response = await this.request<{ usage: AssetUsageEntry[] }>("/api/assets/usage");
    return response.usage;
  }
```

- [ ] **Step 4: Run the client test**

Run: `npx vitest run src/api/client.test.ts`
Expected: PASS.

- [ ] **Step 5: Write the failing session test**

Add to `deviceSession.test.tsx`, following its existing `clientFactory` harness:

```ts
it("loads asset usage on connect and after an asset refresh", async () => {
  const usage = [{ path: "models/clean.nam", presets: [{ bank: 0, slot: 0, name: "Clean" }] }];
  const client = fakeClient({ getAssetUsage: vi.fn(async () => usage) });
  const { result } = renderSession(client);
  await act(() => result.current.connect("http://pedal.local"));
  expect(result.current.assetUsage).toEqual(usage);
  await act(() => result.current.refreshAssets("models"));
  expect(client.getAssetUsage).toHaveBeenCalledTimes(2);
});

it("leaves asset usage undefined when the transport cannot report it", async () => {
  const { result } = renderSession(fakeClient({ getAssetUsage: undefined }));
  await act(() => result.current.connect("http://pedal.local"));
  expect(result.current.assetUsage).toBeUndefined();
});
```

If the file has no `fakeClient` / `renderSession` helpers with these names, use the helpers it has (it builds a `ManagerTransport` object and renders `DeviceSessionProvider` with `clientFactory`); keep the assertions as written.

- [ ] **Step 6: Run it to see it fail**

Run: `npx vitest run src/connection/deviceSession.test.tsx`
Expected: FAIL, `assetUsage` is undefined after connect.

- [ ] **Step 7: Add usage state to the session**

In `deviceSession.tsx`:

```ts
// DeviceSessionValue
  assetUsage?: AssetUsageEntry[];
  refreshAssetUsage(): Promise<void>;
```

```ts
// provider state, next to reverbIrs
  const [assetUsage, setAssetUsage] = useState<AssetUsageEntry[]>();
```

```ts
// helper next to loadReverbIrInventory
async function loadAssetUsage(client: ManagerTransport): Promise<AssetUsageEntry[] | undefined> {
  return client.getAssetUsage ? client.getAssetUsage() : undefined;
}
```

In `connect`, add `loadAssetUsage(nextClient)` to the `Promise.all` list and call `setAssetUsage(nextUsage)` with the others. In `refreshAssets`, after the list refreshes, add `setAssetUsage(await loadAssetUsage(client));`. Add:

```ts
  const refreshAssetUsage = async () => {
    if (!client) return;
    setAssetUsage(await loadAssetUsage(client));
  };
```

In `disconnect`, add `setAssetUsage(undefined);`. Add `assetUsage` and `refreshAssetUsage` to the memoised value and its dependency list.

- [ ] **Step 8: Run the manager tests and type check**

Run: `npx vitest run && npx tsc -p .`
Expected: all tests PASS (496 plus the new ones), no type errors. `CloudTransport` compiles because the method is optional.

- [ ] **Step 9: Commit**

```bash
git add apps/manager/src/api apps/manager/src/connection
git commit -m "feat: read asset usage in the manager session"
```

---

### Task 3: One undo step per gesture

**Files:**
- Modify: `apps/manager/src/presets/editor/editorTypes.ts` (`EditorState`, five actions)
- Modify: `apps/manager/src/presets/editor/editorReducer.ts` (`withMutation` line 46, `setBlockParam` line 193, `setEqBand` line 173, cases `set-scene-parameter` 320, `set-scene-input-gain` 343, `set-global` 429, `set-block-param` 675, `set-eq-band` 682, `undo` 719, `redo`, `load` 215)
- Test: `apps/manager/src/presets/editor/editorReducer.test.ts`

**Interfaces:**
- Produces: `EditorState.gesture?: string`; optional `gesture?: string` on actions `set-block-param`, `set-scene-parameter`, `set-scene-scope`, `set-scene-input-gain`, `set-global`, `set-eq-band`. Consecutive mutations with the same non-empty `gesture` replace `present` without a new history entry. Any other mutation, `undo`, `redo` and `load` clear `gesture`.

- [ ] **Step 1: Write the failing test**

Add to `editorReducer.test.ts` (it already has a `preset()` factory with a compressor `block-1`):

```ts
it("keeps one undo step for a gesture and starts a new step for the next gesture", () => {
  let state = createEditorState({ bank: 0, slot: 0 }, preset());
  for (const value of [-20, -22, -24]) {
    state = editorReducer(state, { type: "set-block-param", blockId: "block-1", key: "threshold_db", value, gesture: "drag-1" });
  }
  expect(state.history.past).toHaveLength(1);
  expect(state.history.present.blocks[0].params.threshold_db).toBe(-24);

  state = editorReducer(state, { type: "set-block-param", blockId: "block-1", key: "threshold_db", value: -30, gesture: "drag-2" });
  expect(state.history.past).toHaveLength(2);

  state = editorReducer(state, { type: "undo" });
  expect(state.history.present.blocks[0].params.threshold_db).toBe(-24);
  state = editorReducer(state, { type: "undo" });
  expect(state.history.present.blocks[0].params.threshold_db).toBe(-18);
});

it("does not merge a gesture into an edit without a gesture", () => {
  let state = createEditorState({ bank: 0, slot: 0 }, preset());
  state = editorReducer(state, { type: "set-block-param", blockId: "block-1", key: "threshold_db", value: -20, gesture: "g" });
  state = editorReducer(state, { type: "set-name", name: "Renamed" });
  state = editorReducer(state, { type: "set-block-param", blockId: "block-1", key: "threshold_db", value: -21, gesture: "g" });
  expect(state.history.past).toHaveLength(3);
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `npx vitest run src/presets/editor/editorReducer.test.ts`
Expected: FAIL on the TypeScript action shape (`gesture` does not exist) or `expected length 1, received 3`.

- [ ] **Step 3: Add the field and the optional action property**

`editorTypes.ts`: add `gesture?: string;` to `EditorState`, and `gesture?: string` to these action variants:

```ts
  | { type: "set-scene-parameter"; sceneId: string; blockId: string; parameter: string; value: number; gesture?: string }
  | { type: "set-scene-input-gain"; sceneId: string; value: number; gesture?: string }
  | { type: "set-scene-scope"; sceneId: string; blockId: string; parameter?: string; scope: "shared" | "scene"; value: number | boolean; gesture?: string }
  | { type: "set-global"; key: "inputGainDb" | "outputGainDb"; value: number; gesture?: string }
  | { type: "set-block-param"; blockId: string; key: string; value: unknown; gesture?: string }
  | { type: "set-eq-band"; blockId: string; band: number; patch: Partial<EqBand>; gesture?: string }
```

- [ ] **Step 4: Coalesce in `withMutation`**

Replace `withMutation` in `editorReducer.ts`:

```ts
function withMutation(
  state: EditorState,
  update: (present: Preset) => Preset | undefined,
  selectedBlockId: string | undefined = state.selectedBlockId,
  gesture?: string,
): EditorState {
  const next = update(state.history.present);
  if (!next || deepEqual(next, state.history.present)) return state;
  // One pointer drag or key burst on a control is one undo step.
  if (gesture !== undefined && state.gesture === gesture) {
    return { ...state, selectedBlockId, gesture, history: { ...state.history, present: next, future: [] } };
  }
  const past = [...state.history.past, clonePreset(state.history.present)].slice(-historyLimit);
  return { ...state, selectedBlockId, gesture, history: { past, present: next, future: [] } };
}
```

- [ ] **Step 5: Pass the gesture through**

- `setBlockParam(state, blockId, key, value, gesture?: string)`: pass `state.selectedBlockId, gesture` as the third and fourth arguments of its `withMutation` call. The `set-block-param` case calls `setBlockParam(state, action.blockId, action.key, action.value, action.gesture)`.
- `setEqBand(state, blockId, band, patch, gesture?: string)`: same change; the `set-eq-band` case passes `action.gesture`.
- `set-scene-parameter`, `set-scene-scope`, `set-scene-input-gain`, `set-global`: add `, state.selectedBlockId, action.gesture` as the last arguments of their `withMutation(...)` calls. With the gesture on `set-scene-scope`, the first scene edit of a value (take ownership, then set it) is one undo step.
- `undo`, `redo`: add `gesture: undefined,` to the returned object.
- `load`: make sure the returned state has no `gesture` (it builds a new state with `createEditorState`; if it spreads `state`, add `gesture: undefined`).

- [ ] **Step 6: Run the reducer tests**

Run: `npx vitest run src/presets/editor`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add apps/manager/src/presets/editor
git commit -m "feat: group a control gesture into one undo step"
```

---

### Task 4: Scene view helpers

**Files:**
- Create: `apps/manager/src/presets/scenes/sceneView.ts`
- Modify: `apps/manager/src/presets/workspace/PresetWorkspace.tsx` (replace local `blocksForScene` at lines 22-31 and the `inspectorBlock` memo)
- Test: `apps/manager/src/presets/scenes/sceneView.test.ts`

**Interfaces:**
- Produces:
  - `applySceneToBlock(block: PresetBlock, scene?: PresetScene): PresetBlock` (params and enabled from the scene targets; returns the same object when no target matches)
  - `applySceneToBlocks(blocks: PresetBlock[], scene?: PresetScene): PresetBlock[]` (recursive into `lanes.left/right`)
  - `sceneOwns(scene: PresetScene | undefined, blockId: string, parameter?: string): boolean`

- [ ] **Step 1: Write the failing test**

```ts
import { describe, expect, it } from "vitest";

import type { PresetBlock, PresetScene } from "../../api/types";
import { applySceneToBlock, applySceneToBlocks, sceneOwns } from "./sceneView";

const delay: PresetBlock = { id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", mix: 0.25 } };
const scene: PresetScene = {
  id: "solo", name: "Solo", enterTimeMs: 0, outputTrimDb: 0,
  targets: [
    { target: "parameter", blockId: "d1", parameter: "mix", value: 0.4 },
    { target: "blockEnabled", blockId: "c1", value: false },
  ],
};

describe("sceneView", () => {
  it("applies scene values to one block without changing the source", () => {
    const shown = applySceneToBlock(delay, scene);
    expect(shown.params.mix).toBe(0.4);
    expect(delay.params.mix).toBe(0.25);
  });

  it("returns the same block when the scene does not touch it", () => {
    const other: PresetBlock = { ...delay, id: "x" };
    expect(applySceneToBlock(other, scene)).toBe(other);
    expect(applySceneToBlock(delay, undefined)).toBe(delay);
  });

  it("applies block enabled inside Dual Rig lanes", () => {
    const chorus: PresetBlock = { id: "c1", type: "mod", enabled: true, asset: "", params: {} };
    const rig: PresetBlock = { id: "r1", type: "dualRig", enabled: true, asset: "", params: {}, lanes: { left: { blocks: [chorus] }, right: { blocks: [] } } };
    const [shown] = applySceneToBlocks([rig], scene);
    expect(shown.lanes?.left.blocks[0].enabled).toBe(false);
  });

  it("tells which addresses a scene owns", () => {
    expect(sceneOwns(scene, "d1", "mix")).toBe(true);
    expect(sceneOwns(scene, "d1", "time")).toBe(false);
    expect(sceneOwns(scene, "c1")).toBe(true);
    expect(sceneOwns(undefined, "d1", "mix")).toBe(false);
  });
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `npx vitest run src/presets/scenes/sceneView.test.ts`
Expected: FAIL, module not found.

- [ ] **Step 3: Implement**

```ts
import type { PresetBlock, PresetScene } from "../../api/types";

/** The block as the scene plays it: scene-owned params and enabled replace the preset values. */
export function applySceneToBlock(block: PresetBlock, scene?: PresetScene): PresetBlock {
  if (!scene) return block;
  let params = block.params;
  let enabled = block.enabled;
  let touched = false;
  for (const target of scene.targets) {
    if (target.target === "parameter" && target.blockId === block.id) {
      params = { ...params, [target.parameter]: target.value };
      touched = true;
    } else if (target.target === "blockEnabled" && target.blockId === block.id) {
      enabled = target.value;
      touched = true;
    }
  }
  return touched ? { ...block, params, enabled } : block;
}

export function applySceneToBlocks(blocks: PresetBlock[], scene?: PresetScene): PresetBlock[] {
  if (!scene) return blocks;
  return blocks.map((block) => {
    const shown = applySceneToBlock(block, scene);
    if (!shown.lanes) return shown;
    return {
      ...shown,
      lanes: {
        left: { ...shown.lanes.left, blocks: applySceneToBlocks(shown.lanes.left.blocks, scene) },
        right: { ...shown.lanes.right, blocks: applySceneToBlocks(shown.lanes.right.blocks, scene) },
      },
    };
  });
}

export function sceneOwns(scene: PresetScene | undefined, blockId: string, parameter?: string): boolean {
  return scene?.targets.some((target) => (parameter !== undefined
    ? target.target === "parameter" && target.blockId === blockId && target.parameter === parameter
    : target.target === "blockEnabled" && target.blockId === blockId)) ?? false;
}
```

- [ ] **Step 4: Use the helpers in `PresetWorkspace`**

Delete the local `blocksForScene` (lines 22-31). Replace `displayedBlocks` with `useMemo(() => applySceneToBlocks(present.blocks, editingScene), [present.blocks, editingScene])`, the two WDW lane lines inside `displayedWdw` with `applySceneToBlocks(routing.dry.blocks, editingScene)` and the wet equivalent, and `inspectorBlock` with `useMemo(() => (selected ? applySceneToBlock(selected, editingScene) : undefined), [selected, editingScene])`. Replace the body of `sceneScopeFor` with `sceneOwns(editingScene, blockId, parameter) ? "scene" : "shared"`. Remove the now unused `enabledById` memo.

- [ ] **Step 5: Run all tests**

Run: `npx vitest run && npx tsc -p .`
Expected: PASS. The `PresetWorkspace` tests still pass: this is a pure refactor.

- [ ] **Step 6: Commit**

```bash
git add apps/manager/src/presets/scenes apps/manager/src/presets/workspace
git commit -m "refactor: extract scene view helpers"
```

---

### Task 5: `usePresetEditor` hook and `EditorProvider`

**Files:**
- Create: `apps/manager/src/presets/editor/usePresetEditor.ts`
- Create: `apps/manager/src/presets/editor/EditorContext.tsx`
- Modify: `apps/manager/src/presets/workspace/PresetWorkspace.tsx` (keep only the JSX; read everything from the context)
- Modify: `apps/manager/src/app/AppShell.tsx` (wrap both views in `EditorProvider`)
- Modify: `apps/manager/src/presets/workspace/PresetWorkspace.test.tsx` (render inside `EditorProvider`)
- Test: `apps/manager/src/presets/editor/EditorContext.test.tsx`

**Interfaces:**
- Consumes: `useDeviceSession()`, `editorReducer`, `validatePreset`, `applySceneToBlock(s)`, `sceneOwns` (Task 4).
- Produces: `usePresetEditor(): PresetEditor`; `EditorProvider({ children })`; `usePresetEditorContext(): PresetEditor` (throws `Error("usePresetEditorContext needs an EditorProvider")` outside a provider). `PresetEditor` has exactly these members, moved from `PresetWorkspace` lines 34-366 with the same logic:

```ts
export type AddTarget =
  | { kind: "top"; index: number }
  | { kind: "lane"; rigId: string; lane: "left" | "right"; index: number }
  | { kind: "wdw"; lane: "dry" | "wet"; index: number };

export type ExpressionTarget = { block: PresetBlock; name: string; parameters: NumberControl[] };

export type PresetEditor = {
  editor: EditorState;
  dispatch: Dispatch<EditorAction>;
  present: Preset;
  dirty: boolean;
  validation: PresetValidationResult;
  allBlocks: PresetBlock[];
  editingScene?: PresetScene;
  displayedBlocks: PresetBlock[];
  displayedWdw?: WdwRouting;
  inspectorBlock?: PresetBlock;
  displayedInputGain: number;
  sceneInputOwned: boolean;
  sceneScopeFor(blockId: string, parameter?: string): "shared" | "scene";
  editBlockEnabled(blockId: string, enabled: boolean): void;
  editParameter(blockId: string, parameter: string, value: unknown, gesture?: string): void;
  editWdwMix(lane: "dry" | "wet", key: "levelDb" | "pan" | "width" | "enabled", value: number | boolean): void;
  expressionTargets: ExpressionTarget[];
  expressionTarget?: ExpressionTarget;
  expressionParameter?: NumberControl;
  enableExpression(): void;
  patchExpression(patch: Partial<NonNullable<Preset["expression"]>>): void;
  addTarget?: AddTarget;
  setAddTarget(target?: AddTarget): void;
  disabledDefinitions: Map<string, string>;
  selectLocation(location: PresetLocation): void;
  pendingLocation?: PresetLocation;
  resolveNavigation(choice: "save" | "discard" | "cancel"): Promise<void>;
  save(): Promise<boolean>;
  apply(savedFirst?: boolean): Promise<boolean>;
  saveAndApply(): Promise<void>;
  recallScene(): Promise<void>;
  saving: boolean;
  recallingScene: boolean;
  actionError?: string;
  setActionError(message?: string): void;
  applied?: PresetLocation;
  runtimeMatchesDraft: boolean;
  sharedComparisonRows: SharedComparisonRow[];
  presentSceneTarget(target: PresetSceneTarget, value: number | boolean): { label: string; value: string };
};
```

- [ ] **Step 1: Write the failing provider test**

```tsx
import { act, render, renderHook, screen } from "@testing-library/react";
import type { ReactNode } from "react";
import { describe, expect, it, vi } from "vitest";

import type { Preset } from "../../api/types";
import { EditorProvider, usePresetEditorContext } from "./EditorContext";

const preset: Preset = {
  version: 1, name: "Clean", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "b1", type: "dynamics", enabled: true, asset: "", params: { mode: "compressor", threshold_db: -18 } }],
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset, exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, models: [], irs: [], reverbIrs: [], presets: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../../connection/deviceSession", () => ({ useDeviceSession: () => session }));

const wrapper = ({ children }: { children: ReactNode }) => <EditorProvider>{children}</EditorProvider>;

describe("EditorProvider", () => {
  it("shares one draft between two consumers, so a view switch keeps edits", () => {
    const first = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => first.result.current.editParameter("b1", "threshold_db", -30));
    expect(first.result.current.dirty).toBe(true);
    function Probe() { return <p>{String(usePresetEditorContext().present.blocks[0].params.threshold_db)}</p>; }
    render(<EditorProvider><Probe /></EditorProvider>);
    expect(screen.getByText("-18")).toBeInTheDocument();
  });

  it("holds a dirty navigation until the person chooses", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.editParameter("b1", "threshold_db", -30));
    act(() => result.current.selectLocation({ bank: 0, slot: 1 }));
    expect(result.current.pendingLocation).toEqual({ bank: 0, slot: 1 });
    expect(session.selectLocation).not.toHaveBeenCalled();
  });

  it("fails loudly outside a provider", () => {
    expect(() => renderHook(() => usePresetEditorContext())).toThrow("usePresetEditorContext needs an EditorProvider");
  });
});
```

The first test proves the draft lives in the provider: consumers of the same provider share it, and a separate provider starts from the session preset. The StageWorkspace test in Task 14 checks the view switch end to end.

- [ ] **Step 2: Run it to see it fail**

Run: `npx vitest run src/presets/editor/EditorContext.test.tsx`
Expected: FAIL, module not found.

- [ ] **Step 3: Move the logic into the hook**

Create `usePresetEditor.ts`. Move `PresetWorkspace.tsx` lines 34-366 (from `const session = useDeviceSession();` to the `recallHint` constant) into `export function usePresetEditor(): PresetEditor { ... }`, unchanged except:
- `editParameter` gets a fourth parameter `gesture?: string` and passes it as `gesture` in both dispatches.
- Compute `const runtimeMatchesDraft = activeRevisionMatchesDraft(session.device, editor.location, dirty);` inside the hook.
- Return an object with every `PresetEditor` member listed above.

Keep the offline early return, `locationLabel`, `applyBlocked` and `recallHint` in `PresetWorkspace` for now (Task 14 replaces that file).

- [ ] **Step 4: Create the context**

```tsx
import { createContext, useContext, type ReactNode } from "react";

import { usePresetEditor, type PresetEditor } from "./usePresetEditor";

const EditorContext = createContext<PresetEditor | undefined>(undefined);

/** Holds the preset draft above the Edit and Assets views, so a view switch keeps unsaved edits. */
export function EditorProvider({ children }: { children: ReactNode }) {
  const editor = usePresetEditor();
  return <EditorContext.Provider value={editor}>{children}</EditorContext.Provider>;
}

export function usePresetEditorContext(): PresetEditor {
  const editor = useContext(EditorContext);
  if (!editor) throw new Error("usePresetEditorContext needs an EditorProvider");
  return editor;
}
```

- [ ] **Step 5: Use it**

`PresetWorkspace`: replace the moved code with `const { editor, dispatch, present, ... } = usePresetEditorContext();` destructuring every name the JSX uses. `AppShell`: wrap the `{view === "workspace" ? ... : ...}` expression in `<EditorProvider>...</EditorProvider>`. `PresetWorkspace.test.tsx`: render `<EditorProvider><PresetWorkspace ... /></EditorProvider>` in every test.

- [ ] **Step 6: Run all tests**

Run: `npx vitest run && npx tsc -p .`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add apps/manager/src
git commit -m "refactor: move preset editor logic into a shared provider"
```

---

### Task 6: Lamp Black tokens, fonts and primitives

**Files:**
- Modify: `apps/manager/src/theme/accent.ts` (palette values and extra fields)
- Create: `apps/manager/src/ui/tokens.css`
- Create: `apps/manager/src/ui/family.ts`
- Create: `apps/manager/src/ui/format.ts`
- Create: `apps/manager/src/effects/display.ts` (move `displayValue` from `components/ParameterSlider.tsx`; re-export it there until Task 17)
- Create: `apps/manager/src/ui/Tag.tsx`
- Modify: `apps/manager/src/main.tsx` (fonts, `tokens.css`)
- Modify: `apps/manager/package.json` (add `@fontsource/jetbrains-mono`)
- Test: `apps/manager/src/ui/family.test.ts`, `apps/manager/src/ui/format.test.ts`, `apps/manager/src/theme/accent.test.ts`

**Interfaces:**
- Produces:
  - `type Family = "amp" | "cab" | "util" | "mod" | "dly" | "rev" | "unknown"`; `familyOf(blockType: string): Family`; `capFor(blockType: string): string`
  - `bankLabel(bank: number): string` → `"BANK 07"`; `slotLabel(slot: number): string` → `"FS 1"` for slot 0; `splitDisplay(text: string): { value: string; unit: string }`; `fileSize(bytes: number): string`; `fileStem(filename: string): string`
  - `displayValue(control: NumberControl, value: number): string` from `src/effects/display.ts`
  - `<Tag tone="warn" | "live" | "ink" | "line" | "danger" | "scene">`
  - CSS classes `.fam-amp` … `.fam-rev` set `--fam`; tokens `--plate-hi`, `--lamp-ink`, `--danger`, `--danger-rule`, `--lift`, `--warn-ink` added to `paletteVariables`.

- [ ] **Step 1: Write the failing tests**

`family.test.ts`:

```ts
import { describe, expect, it } from "vitest";

import { allEffectDefinitions } from "../effects/catalog";
import { capFor, familyOf } from "./family";

describe("family", () => {
  it("gives every catalog block type a family and a device cap label", () => {
    for (const definition of allEffectDefinitions()) {
      expect(familyOf(definition.blockType), definition.blockType).not.toBe("unknown");
      expect(capFor(definition.blockType), definition.blockType).not.toBe(definition.blockType);
    }
  });

  it("uses the pedal's labels and colour families", () => {
    expect([familyOf("nam"), capFor("nam")]).toEqual(["amp", "Neural Amp"]);
    expect([familyOf("distortion"), capFor("distortion")]).toEqual(["amp", "Drive"]);
    expect([familyOf("eq"), capFor("eq")]).toEqual(["util", "EQ"]);
    expect([familyOf("irreverb"), capFor("irreverb")]).toEqual(["rev", "Reverb"]);
    expect(familyOf("futureThing")).toBe("unknown");
  });
});
```

`format.test.ts`:

```ts
import { describe, expect, it } from "vitest";

import { bankLabel, fileSize, fileStem, slotLabel, splitDisplay } from "./format";

describe("format", () => {
  it("formats banks like the pedal header", () => {
    expect(bankLabel(0)).toBe("BANK 00");
    expect(bankLabel(99)).toBe("BANK 99");
    expect(slotLabel(0)).toBe("FS 1");
  });

  it("splits a display value into number and unit", () => {
    expect(splitDisplay("412 ms")).toEqual({ value: "412", unit: "ms" });
    expect(splitDisplay("-1.0 dB")).toEqual({ value: "-1.0", unit: "dB" });
    expect(splitDisplay("4:1")).toEqual({ value: "4:1", unit: "" });
    expect(splitDisplay("Dotted 8ths")).toEqual({ value: "Dotted 8ths", unit: "" });
  });

  it("formats file sizes and stems", () => {
    expect(fileSize(96044)).toBe("94 KB");
    expect(fileSize(2310400)).toBe("2.2 MB");
    expect(fileStem("Glass Clean.nam")).toBe("Glass Clean");
  });
});
```

`accent.test.ts`:

```ts
import { describe, expect, it } from "vitest";

import { paletteVariables } from "./accent";

describe("palettes", () => {
  it("gives Slate the Lamp Black values from src/ui/LvglUiStyle.cpp", () => {
    const vars = paletteVariables("slate") as Record<string, string>;
    expect(vars["--bg"]).toBe("#0b0c0d");
    expect(vars["--surface"]).toBe("#16181a");
    expect(vars["--lamp"]).toBe("#e8472f");
    expect(vars["--plate-hi"]).toBe("#202326");
    expect(vars["--lamp-ink"]).toBe("#1a0b08");
    expect(vars["--delay"]).toBe("#9a82d6");
  });
});
```

- [ ] **Step 2: Run them to see them fail**

Run: `npx vitest run src/ui src/theme`
Expected: FAIL, modules not found and Slate values differ.

- [ ] **Step 3: Implement `family.ts` and `format.ts`**

```ts
// family.ts
export type Family = "amp" | "cab" | "util" | "mod" | "dly" | "rev" | "unknown";

const familyByType: Record<string, Family> = {
  nam: "amp", dualAmp: "amp", dualRig: "amp", distortion: "amp",
  cab: "cab",
  dynamics: "util", eq: "util", wah: "util", stereo: "util",
  mod: "mod", delay: "dly", reverb: "rev", irreverb: "rev",
};

// Mirrors labelForBlockType in src/ui/UiModel.cpp, so a card cap reads like the pedal.
const capByType: Record<string, string> = {
  nam: "Neural Amp", cab: "Cab", dualAmp: "Dual Amp", dualRig: "Dual Rig", mod: "Modulation",
  delay: "Delay", reverb: "Reverb", dynamics: "Dynamics", eq: "EQ", wah: "Wah",
  distortion: "Drive", irreverb: "Reverb", stereo: "Stereo",
};

export const familyOf = (blockType: string): Family => familyByType[blockType] ?? "unknown";
export const capFor = (blockType: string): string => capByType[blockType] ?? blockType;
```

```ts
// format.ts
export const bankLabel = (bank: number): string => `BANK ${String(bank).padStart(2, "0")}`;
export const slotLabel = (slot: number): string => `FS ${slot + 1}`;

/** Splits "412 ms" so the number and the unit can use different type. */
export function splitDisplay(text: string): { value: string; unit: string } {
  const match = /^([+-]?[\d.,:]+)\s*([^\d\s].*)?$/.exec(text.trim());
  return match ? { value: match[1], unit: match[2] ?? "" } : { value: text, unit: "" };
}

export function fileSize(bytes: number): string {
  if (bytes < 1024 * 1024) return `${Math.max(1, Math.round(bytes / 1024))} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

export const fileStem = (filename: string): string => filename.replace(/\.(nam|wav)$/i, "");
```

`effects/display.ts`: move `displayValue` verbatim from `ParameterSlider.tsx`; in `ParameterSlider.tsx` replace the function with `export { displayValue } from "../effects/display";` and import it for local use.

- [ ] **Step 4: Update the palettes**

In `accent.ts`, add six fields to `Palette` after `reverb`: `plateHi`, `lampInk`, `danger`, `dangerRule`, `lift`, `warnInk`. Set every palette from `src/ui/LvglUiStyle.cpp` (field order in the C++ struct: plate, plate2, plate3, engrave, engraveLo, engraveOff, rule, lamp, warn, faultLine, faultText, laneL, laneR, families {amp, cab, utility, modulation, delay, reverb}, plateHi, lampInk, danger, dangerRule, lift, warnInk). Slate becomes:

```ts
  { id: "slate", name: "Slate", plate: "#0b0c0d", plate2: "#16181a", plate3: "#121416", engrave: "#eceeed", engraveLo: "#9aa1a6", engraveOff: "#5c6368", rule: "#2b2f33", lamp: "#e8472f", warn: "#e0a53c", faultLine: "#6b463c", faultText: "#d19a8c", laneL: "#7fa6c8", laneR: "#c9a06a", amp: "#d2923f", cab: "#aab2b7", utility: "#5f95c9", modulation: "#3fb08c", delay: "#9a82d6", reverb: "#d07a5a", plateHi: "#202326", lampInk: "#1a0b08", danger: "#f0a497", dangerRule: "#6b3a32", lift: "#040505", warnInk: "#1b1305" },
```

Ink extras: `#222d3f, #06222a, #e8a0a8, #5c3946, #05080c, #1b1305`. Sodium: `#211e17, #1f1400, #e0a58f, #5d3a2e, #030302, #1b1305`. Nord: `#434c5e, #1c2a33, #e3a0a8, #6b4148, #1d2129, #2a2210`. Check the other existing values of Ink, Sodium and Nord against the C++ file and fix any drift.

In `paletteVariables`, set `"--danger": palette.danger`, `"--line-strong": palette.engraveOff`, and add `"--plate-hi"`, `"--lamp-ink"`, `"--danger-rule"`, `"--lift"`, `"--warn-ink"`.

- [ ] **Step 5: Tokens, family classes, fonts, Tag**

`ui/tokens.css`:

```css
/* Lamp Black type and family scopes. Colour values come from paletteVariables(). */
:root {
  --f-cond: "Saira Condensed", "Arial Narrow", sans-serif;
  --f-body: "Saira", system-ui, sans-serif;
  --f-mono: "JetBrains Mono", ui-monospace, monospace;
  --ease: cubic-bezier(0.16, 1, 0.3, 1);
  --gut: 24px;
  --bar-h: 56px;
}
@media (max-width: 720px) { :root { --gut: 16px; --bar-h: 52px; } }

.fam-amp { --fam: var(--amp); }
.fam-cab { --fam: var(--cabinet); }
.fam-util { --fam: var(--utility); }
.fam-mod { --fam: var(--modulation); }
.fam-dly { --fam: var(--delay); }
.fam-rev { --fam: var(--reverb); }
.fam-unknown { --fam: var(--disabled); }

.lb-tag { display: inline-flex; align-items: center; height: 24px; padding: 0 8px; font: 700 12px/1 var(--f-cond); letter-spacing: .16em; text-transform: uppercase; white-space: nowrap; }
.lb-tag--warn { background: var(--warning); color: var(--warn-ink); }
.lb-tag--live { background: var(--lamp); color: var(--lamp-ink); }
.lb-tag--ink { background: var(--lamp-ink); color: var(--lamp); }
.lb-tag--line { border: 1px solid var(--line-strong); color: var(--muted); height: 22px; }
.lb-tag--danger { border: 1px solid var(--danger-rule); color: var(--danger); height: 22px; }
.lb-tag--scene { border: 1px solid var(--muted); color: var(--text); height: 20px; font-size: 11px; padding: 0 6px; }
.lb-kbd { font: 500 11px/1 var(--f-mono); color: var(--muted); border: 1px solid var(--line); padding: 3px 5px; background: var(--surface-muted); }
```

`ui/Tag.tsx`:

```tsx
import type { ReactNode } from "react";

export function Tag({ tone = "line", title, children }: { tone?: "warn" | "live" | "ink" | "line" | "danger" | "scene"; title?: string; children: ReactNode }) {
  return <span className={`lb-tag lb-tag--${tone}`} title={title}>{children}</span>;
}
```

Install and import fonts:

```bash
cd apps/manager && npm install @fontsource/jetbrains-mono@^5
```

`main.tsx` font imports become: `@fontsource/saira/latin-400.css`, `latin-500.css`, `latin-600.css`; `@fontsource/saira-condensed/latin-500.css`, `latin-600.css`, `latin-700.css`, `latin-800.css`; `@fontsource/jetbrains-mono/latin-500.css`. Import `./ui/tokens.css` before `./styles.css`.

- [ ] **Step 6: Run tests, type check, build**

Run: `npx vitest run && npx tsc -p . && npx vite build`
Expected: PASS; the build output includes the new font files.

- [ ] **Step 7: Commit**

```bash
git add apps/manager
git commit -m "feat: add Lamp Black tokens, fonts and family helpers to the manager"
```

---

### Task 7: `TravelScale` and `ChoiceStrip` controls

**Files:**
- Create: `apps/manager/src/ui/TravelScale.tsx`, `apps/manager/src/ui/TravelScale.css`
- Create: `apps/manager/src/ui/ChoiceStrip.tsx`
- Test: `apps/manager/src/ui/TravelScale.test.tsx`, `apps/manager/src/ui/ChoiceStrip.test.tsx`

**Interfaces:**
- Consumes: `NumberControl` (`effects/types.ts`), `displayValue` (Task 6), `splitDisplay`, `Family`, `cx` (`components/ui.tsx`).
- Produces:
  - `TravelScale(props: { control: NumberControl; value: number; family: Family; onChange(value: number, gesture: string): void; onFocusControl?(): void; focused?: boolean; owned?: "scene" | "shared"; onShare?(): void; compact?: boolean })`
  - `snapValue(control, raw): number`, `positionOf(control, value): number` (0..1)
  - `ChoiceStrip<T extends string | boolean>(props: { label: string; options: Array<{ value: T; label: string }>; value: T; onChange(value: T): void; family: Family })`
- Behaviour: pointer down jumps to the pointer and starts a gesture; pointer move continues it; arrows ±`step`, Shift ×0.1, PageUp/PageDown ×10, Home/End ends; a key burst within 900 ms shares one gesture; wheel changes the value only while the scale has focus; double-click resets to `defaultValue`; `display.choices` snaps to the nearest choice.

- [ ] **Step 1: Write the failing tests**

`TravelScale.test.tsx`:

```tsx
import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { NumberControl } from "../effects/types";
import { TravelScale, positionOf, snapValue } from "./TravelScale";

const mix: NumberControl = { kind: "number", key: "mix", label: "Mix", minimum: 0, maximum: 1, step: 0.05, unit: "percent", defaultValue: 0.25 };
const stepped: NumberControl = {
  ...mix, key: "time", label: "Time",
  display: { format: (v) => `${v}`, toInput: (v) => v, fromInput: (v) => v, minimum: 0, maximum: 1, step: 1,
    choices: [{ value: 0.1, label: "1/8" }, { value: 0.5, label: "1/4" }, { value: 0.9, label: "1/2" }] },
};

describe("TravelScale", () => {
  it("shows the value and its unit as separate text", () => {
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={vi.fn()} />);
    expect(screen.getByText("30")).toBeInTheDocument();
    expect(screen.getByText("%")).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Mix" })).toHaveAttribute("aria-valuetext", "30%");
  });

  it("steps with the arrow keys and keeps one gesture for a key burst", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    fireEvent.keyDown(slider, { key: "ArrowRight" });
    fireEvent.keyDown(slider, { key: "ArrowRight" });
    expect(onChange.mock.calls[0][0]).toBeCloseTo(0.35);
    expect(onChange.mock.calls[0][1]).toBe(onChange.mock.calls[1][1]);
  });

  it("goes to the ends with Home and End, and resets on double-click", async () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    fireEvent.keyDown(slider, { key: "End" });
    fireEvent.keyDown(slider, { key: "Home" });
    await userEvent.dblClick(slider);
    expect(onChange.mock.calls.map(([v]) => v)).toEqual([1, 0, 0.25]);
  });

  it("sets the value from the pointer position", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    slider.getBoundingClientRect = () => ({ left: 0, width: 200, top: 0, height: 30, right: 200, bottom: 30, x: 0, y: 0, toJSON: () => ({}) });
    fireEvent.pointerDown(slider, { clientX: 100, button: 0, pointerId: 1 });
    expect(onChange).toHaveBeenCalledWith(0.5, expect.any(String));
  });

  it("changes the value with the wheel only while focused", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    fireEvent.wheel(slider, { deltaY: -100 });
    expect(onChange).not.toHaveBeenCalled();
    slider.focus();
    fireEvent.wheel(slider, { deltaY: -100 });
    expect(onChange.mock.calls[0][0]).toBeCloseTo(0.35);
  });

  it("snaps stepped displays to the nearest choice", () => {
    expect(snapValue(stepped, 0.45)).toBe(0.5);
    expect(positionOf(stepped, 0.9)).toBe(1);
    expect(snapValue(mix, 0.333)).toBeCloseTo(0.35);
  });

  it("marks a scene-owned value and offers Share", async () => {
    const onShare = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={vi.fn()} owned="scene" onShare={onShare} />);
    expect(screen.getByText("SCENE")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Share" }));
    expect(onShare).toHaveBeenCalled();
  });
});
```

`ChoiceStrip.test.tsx`:

```tsx
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";

import { ChoiceStrip } from "./ChoiceStrip";

it("shows the chosen option and reports a new choice", async () => {
  const onChange = vi.fn();
  render(<ChoiceStrip label="Input source" family="amp" value="sum" onChange={onChange}
    options={[{ value: "sum", label: "L+R Avg" }, { value: "left", label: "Left" }]} />);
  expect(screen.getByRole("radio", { name: "L+R Avg" })).toHaveAttribute("aria-checked", "true");
  await userEvent.click(screen.getByRole("radio", { name: "Left" }));
  expect(onChange).toHaveBeenCalledWith("left");
});
```

- [ ] **Step 2: Run them to see them fail**

Run: `npx vitest run src/ui/TravelScale.test.tsx src/ui/ChoiceStrip.test.tsx`
Expected: FAIL, modules not found.

- [ ] **Step 3: Implement `TravelScale.tsx`**

```tsx
import { useEffect, useRef, type KeyboardEvent, type PointerEvent } from "react";

import { cx } from "../components/ui";
import { displayValue } from "../effects/display";
import type { NumberControl } from "../effects/types";
import type { Family } from "./family";
import { splitDisplay } from "./format";
import { Tag } from "./Tag";
import "./TravelScale.css";

const KEY_BURST_MS = 900;
let gestureSeq = 0;
const newGesture = (): string => `gesture-${++gestureSeq}`;

const choicesOf = (control: NumberControl): number[] | undefined => control.display?.choices?.map(({ value }) => value);

export function snapValue(control: NumberControl, raw: number): number {
  const choices = choicesOf(control);
  if (choices) return choices.reduce((best, value) => (Math.abs(value - raw) < Math.abs(best - raw) ? value : best), choices[0]);
  const stepped = Math.round((raw - control.minimum) / control.step) * control.step + control.minimum;
  return Math.min(control.maximum, Math.max(control.minimum, Number(stepped.toFixed(6))));
}

export function positionOf(control: NumberControl, value: number): number {
  const choices = choicesOf(control);
  if (choices) return choices.length > 1 ? choices.indexOf(snapValue(control, value)) / (choices.length - 1) : 0;
  if (control.maximum === control.minimum) return 0;
  return Math.min(1, Math.max(0, (value - control.minimum) / (control.maximum - control.minimum)));
}

function valueAt(control: NumberControl, fraction: number): number {
  const clamped = Math.min(1, Math.max(0, fraction));
  const choices = choicesOf(control);
  if (choices) return choices[Math.round(clamped * (choices.length - 1))];
  return snapValue(control, control.minimum + clamped * (control.maximum - control.minimum));
}

function nudge(control: NumberControl, value: number, direction: 1 | -1, big: boolean, fine: boolean): number {
  const choices = choicesOf(control);
  if (choices) {
    const index = choices.indexOf(snapValue(control, value)) + direction * (big ? 4 : 1);
    return choices[Math.min(choices.length - 1, Math.max(0, index))];
  }
  const step = control.step * (big ? 10 : 1) * (fine ? 0.1 : 1);
  return Math.min(control.maximum, Math.max(control.minimum, Number((value + direction * step).toFixed(6))));
}

export type TravelScaleProps = {
  control: NumberControl;
  value: number;
  family: Family;
  onChange(value: number, gesture: string): void;
  onFocusControl?(): void;
  focused?: boolean;
  owned?: "scene" | "shared";
  onShare?(): void;
  compact?: boolean;
};

export function TravelScale({ control, value, family, onChange, onFocusControl, focused, owned, onShare, compact }: TravelScaleProps) {
  const scaleRef = useRef<HTMLDivElement>(null);
  const gesture = useRef<{ id: string; at: number } | null>(null);
  const latest = useRef({ control, value, onChange });
  latest.current = { control, value, onChange };

  const burstGesture = (): string => {
    const now = Date.now();
    gesture.current = !gesture.current || now - gesture.current.at > KEY_BURST_MS
      ? { id: newGesture(), at: now }
      : { id: gesture.current.id, at: now };
    return gesture.current.id;
  };

  // React wheel listeners are passive, so preventDefault needs a native listener.
  useEffect(() => {
    const element = scaleRef.current;
    if (!element) return undefined;
    const onWheel = (event: WheelEvent) => {
      if (document.activeElement !== element) return;
      event.preventDefault();
      const { control: c, value: v, onChange: change } = latest.current;
      change(nudge(c, v, event.deltaY < 0 ? 1 : -1, false, event.shiftKey), burstGesture());
    };
    element.addEventListener("wheel", onWheel, { passive: false });
    return () => element.removeEventListener("wheel", onWheel);
  }, []);

  const fromPointer = (event: PointerEvent<HTMLDivElement>): number => {
    const rect = event.currentTarget.getBoundingClientRect();
    return valueAt(control, rect.width ? (event.clientX - rect.left) / rect.width : 0);
  };
  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0) return;
    event.currentTarget.setPointerCapture?.(event.pointerId);
    event.currentTarget.focus();
    gesture.current = { id: newGesture(), at: Date.now() };
    onChange(fromPointer(event), gesture.current.id);
  };
  const onPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (!gesture.current || !event.currentTarget.hasPointerCapture?.(event.pointerId)) return;
    onChange(fromPointer(event), gesture.current.id);
  };
  const onPointerUp = (event: PointerEvent<HTMLDivElement>) => {
    event.currentTarget.releasePointerCapture?.(event.pointerId);
    gesture.current = null;
  };
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.key === "Home" || event.key === "End") {
      event.preventDefault();
      onChange(event.key === "Home" ? control.minimum : control.maximum, burstGesture());
      return;
    }
    const moves: Record<string, [1 | -1, boolean]> = {
      ArrowRight: [1, false], ArrowUp: [1, false], ArrowLeft: [-1, false], ArrowDown: [-1, false], PageUp: [1, true], PageDown: [-1, true],
    };
    const move = moves[event.key];
    if (!move) return;
    event.preventDefault();
    onChange(nudge(control, value, move[0], move[1], event.shiftKey), burstGesture());
  };

  const text = displayValue(control, value);
  const { value: number, unit } = splitDisplay(text);
  const percent = `${(positionOf(control, value) * 100).toFixed(2)}%`;
  return (
    <div className={cx("lb-ctl", `fam-${family}`, compact && "lb-ctl--compact", focused && "is-focus")}>
      <div className="lb-ctl__top">
        <span className="lb-ctl__label">{control.label}</span>
        {owned === "scene" && <span className="lb-ctl__own"><Tag tone="scene">SCENE</Tag>
          {onShare && <button type="button" className="lb-ctl__share" onClick={onShare} title="All scenes use one value again">Share</button>}</span>}
      </div>
      <div className="lb-ctl__value"><span>{number}</span>{unit && <small>{unit}</small>}</div>
      <div ref={scaleRef} className="lb-scale" role="slider" tabIndex={0} aria-label={control.label}
        aria-valuemin={control.minimum} aria-valuemax={control.maximum} aria-valuenow={value} aria-valuetext={text}
        onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onPointerCancel={onPointerUp}
        onKeyDown={onKeyDown} onFocus={onFocusControl} onDoubleClick={() => onChange(control.defaultValue, newGesture())}>
        <span className="lb-scale__ticks" />
        <span className="lb-scale__track"><span className="lb-scale__fill" style={{ width: percent }} /><span className="lb-scale__thumb" style={{ left: percent }} /></span>
      </div>
    </div>
  );
}
```

`TravelScale.css` (port of `.ctl` and `.scale` in `mockups/manager-taste/shared.css`):

```css
.lb-ctl { position: relative; display: flex; flex-direction: column; gap: 6px; min-width: 0; padding: 14px 16px 12px; background: var(--surface-muted); border: 1px solid var(--line); }
.lb-ctl__top { display: flex; align-items: center; justify-content: space-between; gap: 8px; min-height: 20px; }
.lb-ctl__label { font: 600 13px/1 var(--f-cond); letter-spacing: .16em; text-transform: uppercase; color: var(--muted); }
.lb-ctl__own { display: flex; align-items: center; gap: 6px; }
.lb-ctl__share { font: 600 11px/1 var(--f-cond); letter-spacing: .14em; text-transform: uppercase; color: var(--muted); background: none; border: 1px solid var(--line-strong); padding: 3px 6px; }
.lb-ctl__share:hover { color: var(--text); border-color: var(--muted); }
.lb-ctl__value { display: flex; align-items: baseline; gap: 5px; font: 700 40px/1 var(--f-cond); font-variant-numeric: tabular-nums; }
.lb-ctl__value small { font: 500 15px/1 var(--f-body); color: var(--muted); }
.lb-ctl.is-focus { border-color: var(--lamp); box-shadow: inset 0 0 0 1px var(--lamp); }
.lb-ctl.is-focus .lb-ctl__label { color: var(--lamp); }
.lb-scale { position: relative; height: 30px; cursor: ew-resize; touch-action: none; outline: none; }
.lb-scale__ticks { position: absolute; left: 0; right: 0; top: 2px; height: 7px; background: repeating-linear-gradient(90deg, var(--disabled) 0 1px, transparent 1px calc(10% - 0.1px)); }
.lb-scale__track { position: absolute; left: 0; right: 0; top: 16px; height: 7px; background: var(--plate-hi); }
.lb-scale__fill { position: absolute; left: 0; top: 0; bottom: 0; background: var(--fam); }
.lb-scale__thumb { position: absolute; top: -8px; width: 8px; height: 23px; margin-left: -4px; background: var(--text); box-shadow: 0 0 0 2px var(--bg); }
.lb-scale:focus-visible .lb-scale__thumb { box-shadow: 0 0 0 2px var(--bg), 0 0 0 4px var(--text); }
.lb-ctl--compact { padding: 8px 10px 6px; gap: 2px; }
.lb-ctl--compact .lb-ctl__value { font-size: 24px; }
.lb-ctl--compact .lb-scale { height: 22px; }
.lb-ctl--compact .lb-scale__ticks { display: none; }
.lb-ctl--compact .lb-scale__track { top: 10px; height: 5px; }
.lb-ctl--compact .lb-scale__thumb { top: -6px; height: 17px; width: 6px; margin-left: -3px; }
.lb-choice { display: flex; flex-wrap: wrap; gap: 4px; }
.lb-choice button { height: 32px; padding: 0 10px; border: 1px solid var(--line); background: var(--surface); font: 600 13px/1 var(--f-cond); letter-spacing: .08em; text-transform: uppercase; color: var(--muted); }
.lb-choice button[aria-checked="true"] { background: var(--text); color: var(--bg); border-color: var(--text); }
@media (max-width: 720px) { .lb-ctl__value { font-size: 32px; } }
```

- [ ] **Step 4: Implement `ChoiceStrip.tsx`**

```tsx
import type { Family } from "./family";
import "./TravelScale.css";

export function ChoiceStrip<T extends string | boolean>({ label, options, value, onChange, family }: {
  label: string;
  options: Array<{ value: T; label: string }>;
  value: T;
  onChange(value: T): void;
  family: Family;
}) {
  return (
    <div className={`lb-ctl fam-${family}`}>
      <div className="lb-ctl__top"><span className="lb-ctl__label">{label}</span></div>
      <div className="lb-choice" role="radiogroup" aria-label={label}>
        {options.map((option) => (
          <button key={String(option.value)} type="button" role="radio" aria-checked={option.value === value} onClick={() => onChange(option.value)}>{option.label}</button>
        ))}
      </div>
    </div>
  );
}
```

- [ ] **Step 5: Run the tests**

Run: `npx vitest run src/ui`
Expected: PASS. If jsdom has no `PointerEvent`, `fireEvent.pointerDown` still dispatches with `clientX` because Testing Library falls back to `MouseEvent` init; if it does not, add `if (!globalThis.PointerEvent) globalThis.PointerEvent = MouseEvent as typeof PointerEvent;` to `src/test/setup.ts`.

- [ ] **Step 6: Commit**

```bash
git add apps/manager/src/ui apps/manager/src/test
git commit -m "feat: add travel scale and choice strip controls"
```

---

### Task 8: Block card, main values and chain codes

**Files:**
- Create: `apps/manager/src/stage/mainValues.ts`, `apps/manager/src/stage/codes.ts`
- Create: `apps/manager/src/stage/BlockCard.tsx`, `apps/manager/src/stage/stage.css`
- Create: `apps/manager/src/test/blocks.ts` (test helper `blockOf`)
- Modify: `apps/manager/src/presets/inspector/EqResponseGraph.tsx` (export `responseAt` and add `eqStateFor`, `responsePath`)
- Test: `apps/manager/src/stage/mainValues.test.ts`, `apps/manager/src/stage/codes.test.ts`, `apps/manager/src/stage/BlockCard.test.tsx`

**Interfaces:**
- Consumes: `findEffectDefinition`, `familyOf`, `capFor`, `fileStem`, `displayValue`, `Tag`, `ValidationIssue`.
- Produces:
  - `mainControls(definition: EffectDefinition): NumberControl[]` (at most two)
  - `stripCode(block: PresetBlock): string`; `MODULE_CODES: Record<string, string>` (definition id → code)
  - `eqStateFor(block: PresetBlock): { bands: EqBand[]; highPass: EqPassFilter; lowPass: EqPassFilter }`; `responsePath(state, width: number, height: number): string`
  - `BlockCard(props: BlockCardProps)` where

```ts
export type BlockCardProps = {
  block: PresetBlock;            // already scene-applied
  selected: boolean;
  issues: ValidationIssue[];
  missingFile: boolean;
  sceneOwnsEnabled: boolean;
  laneTag?: "A" | "B" | "DRY" | "WET";
  onSelect(): void;
  onToggle(): void;
  onNudge(direction: -1 | 1): void; // Alt + Left / Right
  handleProps?: HTMLAttributes<HTMLElement>; // dnd-kit attributes + listeners, spread on the cap
  innerRef?: Ref<HTMLDivElement>;
  style?: CSSProperties;
  dragging?: boolean;
};
```

- [ ] **Step 1: Write the failing tests**

`mainValues.test.ts`:

```ts
import { expect, it } from "vitest";

import { getEffectDefinition } from "../effects/catalog";
import { mainControls } from "./mainValues";

it("picks the device card pairs", () => {
  expect(mainControls(getEffectDefinition("dynamics:compressor")).map(({ key }) => key)).toEqual(["threshold_db", "ratio"]);
  expect(mainControls(getEffectDefinition("delay:tape")).map(({ key }) => key)).toEqual(["time", "repeats"]);
  expect(mainControls(getEffectDefinition("reverb:shimmer")).map(({ key }) => key)).toEqual(["decay", "mix"]);
  expect(mainControls(getEffectDefinition("mod:chorus")).map(({ key }) => key)).toEqual(["speed", "depth"]);
  expect(mainControls(getEffectDefinition("nam"))).toEqual([]);
});
```

`codes.test.ts`:

```ts
import { expect, it } from "vitest";

import { allEffectDefinitions } from "../effects/catalog";
import { MODULE_CODES, stripCode } from "./codes";

it("has a code for every catalog definition", () => {
  for (const definition of allEffectDefinitions()) expect(MODULE_CODES[definition.id], definition.id).toBeTruthy();
});

it("derives codes from the file name for amps and cabs", () => {
  expect(stripCode({ id: "a", type: "nam", enabled: true, asset: "models/Glass Clean.nam", params: {} })).toBe("GLASS");
  expect(stripCode({ id: "c", type: "cab", enabled: true, asset: "irs/Open Back 2x12.wav", params: {} })).toBe("2X12");
  expect(stripCode({ id: "d", type: "delay", enabled: true, asset: "", params: { mode: "tape" } })).toBe("TAPE");
});
```

`BlockCard.test.tsx`:

```tsx
import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { blockOf } from "../test/blocks";
import { BlockCard } from "./BlockCard";

const delay = blockOf("delay:tape", "d1");
const props = { selected: false, issues: [], missingFile: false, sceneOwnsEnabled: false, onSelect: vi.fn(), onToggle: vi.fn(), onNudge: vi.fn() };

describe("BlockCard", () => {
  it("shows the device cap, name and two main values", () => {
    render(<BlockCard block={delay} {...props} />);
    expect(screen.getByText("Delay")).toBeInTheDocument();
    expect(screen.getByText("Tape Delay")).toBeInTheDocument();
    expect(screen.getByText("Time")).toBeInTheDocument();
    expect(screen.getByText("Repeats")).toBeInTheDocument();
  });

  it("shows OFF and hides values when bypassed", () => {
    render(<BlockCard block={{ ...delay, enabled: false }} {...props} />);
    expect(screen.getByText("OFF")).toBeInTheDocument();
    expect(screen.queryByText("Time")).not.toBeInTheDocument();
  });

  it("selects on click and toggles with the power button without selecting", async () => {
    const onSelect = vi.fn();
    const onToggle = vi.fn();
    render(<BlockCard block={delay} {...props} onSelect={onSelect} onToggle={onToggle} />);
    await userEvent.click(screen.getByRole("button", { name: "Bypass Tape Delay" }));
    expect(onToggle).toHaveBeenCalled();
    expect(onSelect).not.toHaveBeenCalled();
    await userEvent.click(screen.getByText("Tape Delay"));
    expect(onSelect).toHaveBeenCalled();
  });

  it("moves with Alt and the arrow keys", () => {
    const onNudge = vi.fn();
    render(<BlockCard block={delay} {...props} onNudge={onNudge} />);
    fireEvent.keyDown(screen.getByRole("group", { name: /Tape Delay/ }), { key: "ArrowRight", altKey: true });
    expect(onNudge).toHaveBeenCalledWith(1);
  });

  it("warns about a missing file and a validation error", () => {
    const nam = { ...blockOf("nam", "n1"), asset: "models/Fuzz Stack.nam" };
    render(<BlockCard block={nam} {...props} missingFile issues={[{ severity: "error", code: "x", message: "Pick a model." }]} />);
    expect(screen.getByText("FILE MISSING")).toBeInTheDocument();
    expect(screen.getByText("FIX")).toHaveAttribute("title", "Pick a model.");
  });
});
```

The helper `src/test/blocks.ts` (created in this task) gives a catalog block a fixed id, because `createBlockFromDefinition(definitionId, existingBlocks, asset?)` makes its own id:

```ts
import type { PresetBlock } from "../api/types";
import { createBlockFromDefinition } from "../effects/catalog";

/** A catalog block with a fixed id, for tests. */
export const blockOf = (definitionId: string, id: string): PresetBlock => ({ ...createBlockFromDefinition(definitionId, []), id });
```

Import it with `import { blockOf } from "../test/blocks";` in every stage test below.

- [ ] **Step 2: Run them to see them fail**

Run: `npx vitest run src/stage`
Expected: FAIL, modules not found.

- [ ] **Step 3: Implement `mainValues.ts` and `codes.ts`**

```ts
// mainValues.ts
import type { EffectDefinition, NumberControl } from "../effects/types";

// The two values a card shows, the same pairs as the pedal and the mockup.
const MAIN: Record<string, [string, string]> = {
  "dynamics:compressor": ["threshold_db", "ratio"], "dynamics:noise_gate": ["threshold_db", "release_ms"],
  "dynamics:transient_shaper": ["attack", "sustain"], "distortion:rat": ["distortion", "filter"],
  "distortion:big_cheese": ["fuzz", "tone"], "distortion:tape": ["drive", "saturation"],
  cab: ["levelDb", "mix"], dualRig: ["leftLevelDb", "rightLevelDb"], "wah:gcb95": ["position", "level"],
  "stereo:widener": ["width", "bassMonoHz"], irreverb: ["mix", "levelDb"],
};
const BY_CATEGORY: Partial<Record<EffectDefinition["category"], [string, string]>> = {
  modulation: ["speed", "depth"], delay: ["time", "repeats"], reverb: ["decay", "mix"],
};

export function mainControls(definition: EffectDefinition): NumberControl[] {
  const numbers = definition.controls.filter((control): control is NumberControl => control.kind === "number");
  const keys = MAIN[definition.id] ?? BY_CATEGORY[definition.category];
  if (!keys) return numbers.slice(0, 2);
  return keys.map((key) => numbers.find((control) => control.key === key)).filter((control): control is NumberControl => Boolean(control));
}
```

```ts
// codes.ts
import type { PresetBlock } from "../api/types";
import { findEffectDefinition } from "../effects/catalog";
import { fileStem } from "../ui/format";

// Module codes, as on the pedal's module drawer and chain strip.
export const MODULE_CODES: Record<string, string> = {
  nam: "NAM", dualAmp: "DAMP", dualRig: "RIG", cab: "CAB", irreverb: "IRV",
  "dynamics:compressor": "CMP", "dynamics:noise_gate": "GATE", "dynamics:transient_shaper": "TRN", "eq:parametric_eq_5": "EQ",
  "stereo:widener": "WIDE", "wah:gcb95": "WAH", "distortion:rat": "RAT", "distortion:big_cheese": "FUZZ", "distortion:tape": "TMC",
  "mod:chorus": "CHO", "mod:flanger": "FLG", "mod:rotary": "ROT", "mod:vibe": "VIBE", "mod:phaser": "PHS", "mod:vintage_trem": "TREM",
  "mod:poly_octave": "OCT", "mod:pattern_trem": "PTRM", "mod:auto_swell": "SWL", "mod:filter": "FLT", "mod:ladder_sweep": "LADR",
  "mod:formant": "FORM", "mod:quadrature": "QUAD", "mod:destroyer": "DSTR", "mod:whammy": "WHAM", "mod:harmonizer": "HARM",
  "delay:digital": "DIG", "delay:tape": "TAPE", "delay:dual": "DUAL", "delay:filter": "FDLY", "delay:lofi": "LOFI", "delay:dbucket": "BBD",
  "delay:duck": "DUCK", "delay:pattern": "PAT", "delay:swell": "SWDL", "delay:trem": "TRDL",
  "reverb:room": "ROOM", "reverb:hall": "HALL", "reverb:plate": "PLATE", "reverb:spring": "SPRG", "reverb:bloom": "BLOOM", "reverb:cloud": "CLOUD",
  "reverb:shimmer": "SHIM", "reverb:chorale": "CHRL", "reverb:nonlinear": "NLIN", "reverb:swell": "SWRV", "reverb:magneto": "MAG", "reverb:reflections": "REFL",
};

export function stripCode(block: PresetBlock): string {
  const name = block.asset ? fileStem(block.asset.split("/").pop() ?? block.asset) : "";
  if (block.type === "cab" && name) return (name.split(" ").find((word) => /\d+x\d+/i.test(word)) ?? name.split(" ")[0]).toUpperCase().slice(0, 6);
  if (block.type === "nam" && name) return name.split(" ")[0].toUpperCase().slice(0, 6);
  const definition = findEffectDefinition(block);
  return (definition && MODULE_CODES[definition.id]) ?? block.type.toUpperCase().slice(0, 4);
}
```

If the codes test names a catalog id that the table misses, add it with a 2 to 5 letter code.

- [ ] **Step 4: Export the EQ helpers**

In `EqResponseGraph.tsx`, change `function responseAt(` to `export function responseAt(` and add:

```ts
export type EqState = { bands: EqBand[]; highPass: EqPassFilter; lowPass: EqPassFilter };

/** The stored EQ params of a block with the defaults the inspector uses. */
export function eqStateFor(block: PresetBlock): EqState {
  const source = Array.isArray(block.params.bands) ? block.params.bands as EqBand[] : [];
  const bands = [0, 1, 2, 3, 4].map((index) => source[index] ?? defaultBand(index));
  const filterFrom = (key: "high_pass" | "low_pass", frequency: number): EqPassFilter => {
    const value = block.params[key];
    const base = { enabled: false, frequency_hz: frequency, q: 0.70710678, slope_db_per_octave: 12 };
    return typeof value === "object" && value !== null && !Array.isArray(value) ? { ...base, ...value } as EqPassFilter : base;
  };
  return { bands, highPass: filterFrom("high_pass", 40), lowPass: filterFrom("low_pass", 16000) };
}

/** An SVG path of the response, for small previews such as the block card. */
export function responsePath({ bands, highPass, lowPass }: EqState, width: number, height: number): string {
  const points: string[] = [];
  for (let i = 0; i <= 64; i += 1) {
    const frequency = MIN_FREQUENCY * Math.pow(MAX_FREQUENCY / MIN_FREQUENCY, i / 64);
    const gain = Math.max(MIN_GAIN, Math.min(MAX_GAIN, responseAt(frequency, bands, highPass, lowPass)));
    points.push(`${i ? "L" : "M"}${((i / 64) * width).toFixed(1)} ${(height / 2 - (gain / MAX_GAIN) * (height / 2)).toFixed(1)}`);
  }
  return points.join(" ");
}
```

Import `PresetBlock` from `../../api/types`. Replace the local band and filter reading in `BlockInspector.tsx` `EqControls` with `eqStateFor(block)` (same defaults).

- [ ] **Step 5: Implement `BlockCard.tsx`**

```tsx
import { GripVertical, Power } from "lucide-react";
import type { CSSProperties, HTMLAttributes, KeyboardEvent, Ref } from "react";

import type { PresetBlock } from "../api/types";
import { cx } from "../components/ui";
import { findEffectDefinition } from "../effects/catalog";
import { displayValue } from "../effects/display";
import type { NumberControl } from "../effects/types";
import { eqStateFor, responsePath } from "../presets/inspector/EqResponseGraph";
import type { ValidationIssue } from "../presets/editor/presetValidation";
import { capFor, familyOf } from "../ui/family";
import { fileStem } from "../ui/format";
import { Tag } from "../ui/Tag";
import { mainControls } from "./mainValues";
import "./stage.css";

export type BlockCardProps = {
  block: PresetBlock;
  selected: boolean;
  issues: ValidationIssue[];
  missingFile: boolean;
  sceneOwnsEnabled: boolean;
  laneTag?: "A" | "B" | "DRY" | "WET";
  onSelect(): void;
  onToggle(): void;
  onNudge(direction: -1 | 1): void;
  handleProps?: HTMLAttributes<HTMLElement>;
  innerRef?: Ref<HTMLDivElement>;
  style?: CSSProperties;
  dragging?: boolean;
};

export function blockTitle(block: PresetBlock): string {
  if (block.asset) return fileStem(block.asset.split("/").pop() ?? block.asset);
  return findEffectDefinition(block)?.name ?? block.type;
}

function Value({ control, value }: { control: NumberControl; value: number }) {
  const fraction = control.maximum === control.minimum ? 0 : (value - control.minimum) / (control.maximum - control.minimum);
  return <>
    <div className="blk__p"><span>{control.label}</span> <b>{displayValue(control, value)}</b></div>
    <div className="blk__bar"><i style={{ width: `${Math.min(100, Math.max(0, fraction * 100)).toFixed(1)}%` }} /></div>
  </>;
}

export function BlockCard({ block, selected, issues, missingFile, sceneOwnsEnabled, laneTag, onSelect, onToggle, onNudge, handleProps, innerRef, style, dragging }: BlockCardProps) {
  const definition = findEffectDefinition(block);
  const family = familyOf(block.type);
  const title = blockTitle(block);
  const error = issues.find(({ severity }) => severity === "error");
  const warning = issues.find(({ severity }) => severity === "warning");
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.altKey && (event.key === "ArrowLeft" || event.key === "ArrowRight")) {
      event.preventDefault();
      onNudge(event.key === "ArrowRight" ? 1 : -1);
    } else if (event.key === "Enter" && event.target === event.currentTarget) onSelect();
  };
  const values = definition ? mainControls(definition).map((control) => {
    const raw = block.params[control.key];
    return <Value key={control.key} control={control} value={typeof raw === "number" ? raw : control.defaultValue} />;
  }) : [];
  return (
    <div ref={innerRef} style={{ ...style, viewTransitionName: `block-${block.id}` }} role="group" tabIndex={0}
      aria-label={`${title}, ${capFor(block.type)}, ${block.enabled ? "on" : "bypassed"}`} aria-current={selected || undefined}
      className={cx("blk", `fam-${family}`, block.enabled ? "is-on" : "is-off", selected && "is-sel", dragging && "is-dragging")}
      onClick={onSelect} onKeyDown={onKeyDown}>
      <div className="blk__cap" {...handleProps}>
        <span>{capFor(block.type)}{sceneOwnsEnabled && <> <Tag tone="scene">SCENE</Tag></>}</span>
        <span className="blk__grip" aria-hidden="true"><GripVertical size={16} /></span>
      </div>
      <button type="button" className="blk__pow" aria-label={`${block.enabled ? "Bypass" : "Turn on"} ${title}`}
        title={block.enabled ? "Bypass (B)" : "Turn on (B)"} onClick={(event) => { event.stopPropagation(); onToggle(); }}><Power size={15} /></button>
      <div className="blk__name">{title}</div>
      <div className="blk__sub">
        {missingFile ? <Tag tone="warn">FILE MISSING</Tag> : block.asset ? definition?.name : null}
        {error ? <Tag tone="danger" title={error.message}>FIX</Tag> : warning ? <Tag tone="line" title={warning.message}>CHECK</Tag> : null}
        {laneTag && <Tag tone="line">{laneTag}</Tag>}
      </div>
      {!block.enabled ? <span className="blk__off">OFF</span>
        : block.type === "eq" ? <svg className="blk__eq" viewBox="0 0 150 64" preserveAspectRatio="none" aria-hidden="true"><path d={responsePath(eqStateFor(block), 150, 64)} /></svg>
          : <div className="blk__params">{values}</div>}
    </div>
  );
}
```

`stage.css` card rules: port the `.blk` block of `mockups/manager-taste/shared.css` (the rules from `/* ---------- Block card` to `.blk.is-off .blk__params { display: none; }`) with these renames: `.blk__pow` stays, `.offtag` → `.blk__off`, `.bar` → `.blk__bar`, `var(--plate)` → `var(--surface)`, `var(--rule)` → `var(--line)`, `var(--rule-strong)` → `var(--line-strong)`, `var(--bone)` → `var(--text)`, `var(--bone-2)` → `var(--muted)`, `var(--bone-3)` → `var(--disabled)`, `var(--ground)` → `var(--bg)`, `var(--recess)` → `var(--surface-muted)`. Add:

```css
.blk__eq { display: block; height: 64px; margin: auto 12px 14px; width: calc(100% - 24px); background: var(--surface-muted); border: 1px solid var(--line); }
.blk__eq path { fill: none; stroke: var(--fam); stroke-width: 2.5; vector-effect: non-scaling-stroke; }
.blk__sub { display: flex; gap: 6px; flex-wrap: wrap; align-items: center; }
.blk__sub .lb-tag { height: 20px; font-size: 11px; }
.blk.is-dragging { opacity: .28; }
```

- [ ] **Step 6: Run the tests**

Run: `npx vitest run src/stage src/presets/inspector && npx tsc -p .`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add apps/manager/src/stage apps/manager/src/presets/inspector
git commit -m "feat: add device-style block card"
```

---

### Task 9: Chain stage with drag to reorder

**Files:**
- Create: `apps/manager/src/stage/dropTarget.ts`
- Create: `apps/manager/src/stage/ChainStage.tsx`
- Modify: `apps/manager/src/stage/stage.css` (jacks, wire, inserts, lanes)
- Test: `apps/manager/src/stage/dropTarget.test.ts`, `apps/manager/src/stage/ChainStage.test.tsx`

**Interfaces:**
- Consumes: `BlockCard`, `AddTarget` (Task 5), dnd-kit.
- Produces:
  - `type ListId = "top" | \`lane:${string}:left\` | \`lane:${string}:right\` | "wdw:dry" | "wdw:wet"`
  - `resolveDrop(from: { listId: ListId; index: number }, to: { listId: ListId; index: number }): EditorAction | undefined` → `move-block`, `move-lane-block` (same rig, either lane) or `move-wdw-block`; `undefined` for a no-op or an unsupported move.
  - `ChainStage(props: { blocks: PresetBlock[]; wdw?: WdwRouting; selectedId?: string; issuesFor(id: string): ValidationIssue[]; missingFile(block: PresetBlock): boolean; sceneOwnsEnabled(id: string): boolean; maxed: boolean; onSelect(id: string): void; onToggle(block: PresetBlock): void; onAdd(target: AddTarget): void; onMove(action: EditorAction): void })`

- [ ] **Step 1: Write the failing `resolveDrop` tests**

```ts
import { describe, expect, it } from "vitest";

import { resolveDrop } from "./dropTarget";

describe("resolveDrop", () => {
  it("moves inside the top chain", () => {
    expect(resolveDrop({ listId: "top", index: 1 }, { listId: "top", index: 4 }, "b2"))
      .toEqual({ type: "move-block", blockId: "b2", index: 4 });
  });

  it("moves inside or across the lanes of one Dual Rig", () => {
    expect(resolveDrop({ listId: "lane:r1:left", index: 0 }, { listId: "lane:r1:right", index: 2 }, "c1"))
      .toEqual({ type: "move-lane-block", rigId: "r1", blockId: "c1", lane: "right", index: 2 });
  });

  it("moves between WDW lanes", () => {
    expect(resolveDrop({ listId: "wdw:dry", index: 0 }, { listId: "wdw:wet", index: 1 }, "n1"))
      .toEqual({ type: "move-wdw-block", lane: "wet", blockId: "n1", index: 1 });
  });

  it("refuses moves the reducer cannot do, and no-op drops", () => {
    expect(resolveDrop({ listId: "top", index: 0 }, { listId: "lane:r1:left", index: 0 }, "b1")).toBeUndefined();
    expect(resolveDrop({ listId: "lane:r1:left", index: 0 }, { listId: "top", index: 0 }, "c1")).toBeUndefined();
    expect(resolveDrop({ listId: "lane:r1:left", index: 0 }, { listId: "lane:r2:left", index: 0 }, "c1")).toBeUndefined();
    expect(resolveDrop({ listId: "top", index: 2 }, { listId: "top", index: 2 }, "b1")).toBeUndefined();
  });
});
```

`resolveDrop` takes the block id as a third argument.

- [ ] **Step 2: Run to see it fail**

Run: `npx vitest run src/stage/dropTarget.test.ts`
Expected: FAIL, module not found.

- [ ] **Step 3: Implement `dropTarget.ts`**

```ts
import type { EditorAction } from "../presets/editor/editorTypes";

export type ListId = "top" | `lane:${string}:left` | `lane:${string}:right` | "wdw:dry" | "wdw:wet";
export type DropPoint = { listId: ListId; index: number };

function laneOf(listId: ListId): { rigId: string; lane: "left" | "right" } | undefined {
  const match = /^lane:(.+):(left|right)$/.exec(listId);
  return match ? { rigId: match[1], lane: match[2] as "left" | "right" } : undefined;
}

/** Maps a drag from one list position to another to the reducer move, or undefined when it is a no-op or not supported. */
export function resolveDrop(from: DropPoint, to: DropPoint, blockId: string): EditorAction | undefined {
  if (from.listId === to.listId && from.index === to.index) return undefined;
  if (from.listId === "top" && to.listId === "top") return { type: "move-block", blockId, index: to.index };
  const fromLane = laneOf(from.listId);
  const toLane = laneOf(to.listId);
  if (fromLane && toLane) {
    return fromLane.rigId === toLane.rigId ? { type: "move-lane-block", rigId: toLane.rigId, blockId, lane: toLane.lane, index: to.index } : undefined;
  }
  if (from.listId.startsWith("wdw:") && to.listId.startsWith("wdw:")) {
    return { type: "move-wdw-block", lane: to.listId === "wdw:dry" ? "dry" : "wet", blockId, index: to.index };
  }
  return undefined;
}
```

- [ ] **Step 4: Run it**

Run: `npx vitest run src/stage/dropTarget.test.ts`
Expected: PASS.

- [ ] **Step 5: Write the failing `ChainStage` test**

```tsx
import { fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { blockOf } from "../test/blocks";
import { ChainStage } from "./ChainStage";

const blocks = [blockOf("dynamics:compressor", "b1"), blockOf("delay:tape", "b2")];
const base = {
  issuesFor: () => [], missingFile: () => false, sceneOwnsEnabled: () => false, maxed: false,
  onSelect: vi.fn(), onToggle: vi.fn(), onAdd: vi.fn(), onMove: vi.fn(),
};

describe("ChainStage", () => {
  it("draws IN, the cards in order, insert points and OUT", () => {
    render(<ChainStage blocks={blocks} {...base} />);
    const stage = screen.getByRole("region", { name: "Signal chain" });
    expect(within(stage).getByText("IN")).toBeInTheDocument();
    expect(within(stage).getByText("OUT")).toBeInTheDocument();
    expect(within(stage).getAllByRole("group").map((el) => el.getAttribute("aria-label")?.split(",")[0])).toEqual(["Compressor", "Tape Delay"]);
    expect(within(stage).getAllByRole("button", { name: /Add a block at position/ })).toHaveLength(3);
  });

  it("inserts at the chosen point", async () => {
    const onAdd = vi.fn();
    render(<ChainStage blocks={blocks} {...base} onAdd={onAdd} />);
    await userEvent.click(screen.getByRole("button", { name: "Add a block at position 2" }));
    expect(onAdd).toHaveBeenCalledWith({ kind: "top", index: 1 });
  });

  it("moves a card with Alt and the arrow keys", () => {
    const onMove = vi.fn();
    render(<ChainStage blocks={blocks} {...base} onMove={onMove} />);
    screen.getByRole("group", { name: /Compressor/ }).focus();
    fireEvent.keyDown(document.activeElement!, { key: "ArrowRight", altKey: true });
    expect(onMove).toHaveBeenCalledWith({ type: "move-block", blockId: "b1", index: 1 });
  });

  it("draws Dual Rig lanes with their own insert points", () => {
    const rig = { ...blockOf("dualRig", "r1"), lanes: { left: { blocks: [blockOf("mod:chorus", "c1")] }, right: { blocks: [] } } };
    render(<ChainStage blocks={[rig]} {...base} />);
    expect(screen.getByText("SPLIT")).toBeInTheDocument();
    expect(screen.getByText("JOIN")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Add a block to lane B" })).toBeInTheDocument();
  });

  it("disables the insert points when the chain is full", () => {
    render(<ChainStage blocks={blocks} {...base} maxed />);
    for (const button of screen.getAllByRole("button", { name: /Add a block at position/ })) expect(button).toBeDisabled();
  });
});
```

- [ ] **Step 6: Run to see it fail**

Run: `npx vitest run src/stage/ChainStage.test.tsx`
Expected: FAIL, module not found.

- [ ] **Step 7: Implement `ChainStage.tsx`**

```tsx
import {
  DndContext, KeyboardSensor, MouseSensor, TouchSensor, closestCenter, useDroppable, useSensor, useSensors, type DragEndEvent,
} from "@dnd-kit/core";
import { SortableContext, horizontalListSortingStrategy, sortableKeyboardCoordinates, useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import { Plus, Split } from "lucide-react";

import type { PresetBlock, WdwRouting } from "../api/types";
import type { AddTarget } from "../presets/editor/usePresetEditor";
import type { EditorAction } from "../presets/editor/editorTypes";
import type { ValidationIssue } from "../presets/editor/presetValidation";
import { BlockCard } from "./BlockCard";
import { resolveDrop, type ListId } from "./dropTarget";

type Props = {
  blocks: PresetBlock[];
  wdw?: WdwRouting;
  selectedId?: string;
  issuesFor(id: string): ValidationIssue[];
  missingFile(block: PresetBlock): boolean;
  sceneOwnsEnabled(id: string): boolean;
  maxed: boolean;
  onSelect(id: string): void;
  onToggle(block: PresetBlock): void;
  onAdd(target: AddTarget): void;
  onMove(action: EditorAction): void;
};

type ItemData = { listId: ListId; index: number };

function targetFor(listId: ListId, index: number): AddTarget {
  if (listId === "top") return { kind: "top", index };
  if (listId === "wdw:dry" || listId === "wdw:wet") return { kind: "wdw", lane: listId === "wdw:dry" ? "dry" : "wet", index };
  const [, rigId, lane] = listId.split(":");
  return { kind: "lane", rigId, lane: lane as "left" | "right", index };
}

function Insert({ listId, index, disabled, label, onAdd }: { listId: ListId; index: number; disabled: boolean; label: string; onAdd(target: AddTarget): void }) {
  return <button type="button" className="ins" disabled={disabled} aria-label={label} title="Add a block" onClick={() => onAdd(targetFor(listId, index))}><Plus size={14} strokeWidth={2.4} /></button>;
}

function SortableCard({ block, listId, index, count, laneTag, props }: { block: PresetBlock; listId: ListId; index: number; count: number; laneTag?: "A" | "B" | "DRY" | "WET"; props: Props }) {
  const sortable = useSortable({ id: block.id, data: { listId, index } satisfies ItemData });
  const nudge = (direction: -1 | 1) => {
    const to = Math.min(count - 1, Math.max(0, index + direction));
    const action = resolveDrop({ listId, index }, { listId, index: to }, block.id);
    if (action) props.onMove(action);
  };
  return <BlockCard block={block} selected={props.selectedId === block.id} issues={props.issuesFor(block.id)}
    missingFile={props.missingFile(block)} sceneOwnsEnabled={props.sceneOwnsEnabled(block.id)} laneTag={laneTag}
    onSelect={() => props.onSelect(block.id)} onToggle={() => props.onToggle(block)} onNudge={nudge}
    innerRef={sortable.setNodeRef} style={{ transform: CSS.Transform.toString(sortable.transform), transition: sortable.transition }}
    handleProps={{ ...sortable.attributes, ...sortable.listeners }} dragging={sortable.isDragging} />;
}

function List({ listId, blocks, props, laneTag, emptyLabel }: { listId: ListId; blocks: PresetBlock[]; props: Props; laneTag?: "A" | "B" | "DRY" | "WET"; emptyLabel?: string }) {
  const { setNodeRef } = useDroppable({ id: `list:${listId}`, data: { listId, index: blocks.length } satisfies ItemData });
  const insertLabel = (index: number) => (listId === "top" ? `Add a block at position ${index + 1}` : `Add a block to lane ${laneTag}${blocks.length ? ` at position ${index + 1}` : ""}`);
  return (
    <div ref={setNodeRef} className="chain__list">
      <SortableContext items={blocks.map(({ id }) => id)} strategy={horizontalListSortingStrategy}>
        {blocks.map((block, index) => <span key={block.id} className="chain__item">
          <Insert listId={listId} index={index} disabled={props.maxed} label={insertLabel(index)} onAdd={props.onAdd} />
          {block.lanes ? <Rig rig={block} props={props} /> : <SortableCard block={block} listId={listId} index={index} count={blocks.length} laneTag={laneTag} props={props} />}
        </span>)}
      </SortableContext>
      <Insert listId={listId} index={blocks.length} disabled={props.maxed} label={blocks.length ? insertLabel(blocks.length) : `Add a block to lane ${laneTag}`} onAdd={props.onAdd} />
      {!blocks.length && emptyLabel && <span className="chain__empty">{emptyLabel}</span>}
    </div>
  );
}

function Rig({ rig, props }: { rig: PresetBlock; props: Props }) {
  const lanes = rig.lanes!;
  return (
    <div className="rig">
      <button type="button" className="rig__node" onClick={() => props.onSelect(rig.id)} aria-label="Dual Rig split. Open its settings."><Split size={18} /><b>SPLIT</b>Dual Rig</button>
      <div className="rig__lanes">
        <div className="rig__lane"><span className="rig__tag rig__tag--a">A</span><List listId={`lane:${rig.id}:left`} blocks={lanes.left.blocks} props={props} laneTag="A" emptyLabel="Empty lane. Drop a block here." /></div>
        <div className="rig__lane"><span className="rig__tag rig__tag--b">B</span><List listId={`lane:${rig.id}:right`} blocks={lanes.right.blocks} props={props} laneTag="B" emptyLabel="Empty lane. Drop a block here." /></div>
      </div>
      <div className="rig__node"><b>JOIN</b>L / R</div>
    </div>
  );
}

export function ChainStage(props: Props) {
  const sensors = useSensors(
    useSensor(MouseSensor, { activationConstraint: { distance: 6 } }),
    useSensor(TouchSensor, { activationConstraint: { delay: 250, tolerance: 8 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates }),
  );
  const onDragEnd = ({ active, over }: DragEndEvent) => {
    const from = active.data.current as ItemData | undefined;
    const to = over?.data.current as ItemData | undefined;
    if (!from || !to) return;
    const action = resolveDrop(from, to, String(active.id));
    if (action) props.onMove(action);
  };
  return (
    <section className="chain-stage" aria-label="Signal chain">
      <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={onDragEnd}>
        <div className="chain">
          <div className="jack">IN<small>MONO</small></div><span className="chain__wire" />
          {props.wdw ? (
            <div className="rig">
              <div className="rig__node"><b>SPLIT</b>WDW</div>
              <div className="rig__lanes">
                <div className="rig__lane"><span className="rig__tag rig__tag--a">DRY</span><List listId="wdw:dry" blocks={props.wdw.dry.blocks} props={props} laneTag="DRY" emptyLabel="Empty lane." /></div>
                <div className="rig__lane"><span className="rig__tag rig__tag--b">WET</span><List listId="wdw:wet" blocks={props.wdw.wet.blocks} props={props} laneTag="WET" emptyLabel="Empty lane." /></div>
              </div>
              <div className="rig__node"><b>JOIN</b>L / R</div>
            </div>
          ) : <List listId="top" blocks={props.blocks} props={props} />}
          <span className="chain__wire" /><div className="jack">OUT<small>STEREO</small></div>
        </div>
      </DndContext>
      {!props.wdw && props.blocks.length === 0 && <p className="chain__hint">Start with an amp. Press + to add a block.</p>}
    </section>
  );
}
```

Add the chain rules to `stage.css`: port `.chain`, `.chain__list`, `.chain__wire`, `.jack`, `.ins`, `.rig`, `.rig__node`, `.rig__lanes`, `.rig__lane`, `.rig__lane-tag` (renamed `.rig__tag`), `.rig__empty` (renamed `.chain__empty`) from `mockups/manager-taste/shared.css` with the token renames of Task 8. Add `.chain__item { display: flex; align-items: center; }`, `.rig__tag--a { background: var(--utility); }`, `.rig__tag--b { background: var(--amp); }`, and `.chain-stage { overflow-x: auto; overflow-y: hidden; }`.

- [ ] **Step 8: Run the tests**

Run: `npx vitest run src/stage && npx tsc -p .`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add apps/manager/src/stage
git commit -m "feat: add reorderable chain stage with Dual Rig and WDW lanes"
```

---

### Task 10: Bank bar, preset tiles and chain strips

**Files:**
- Create: `apps/manager/src/stage/useBankPresets.ts`
- Create: `apps/manager/src/stage/PresetTile.tsx`, `apps/manager/src/stage/BankBar.tsx`, `apps/manager/src/stage/bank.css`
- Create: `apps/manager/src/assets/assetRefs.ts` (shared with Task 15)
- Test: `apps/manager/src/stage/useBankPresets.test.tsx`, `apps/manager/src/stage/BankBar.test.tsx`, `apps/manager/src/assets/assetRefs.test.ts`

**Interfaces:**
- Consumes: `useDeviceSession()` (`presets`, `client.getPreset`, `device.active`), `stripCode`, `familyOf`, `bankLabel`, `slotLabel`.
- Produces:
  - `assetRefs(preset: Preset): Array<{ blockId: string; path: string; kind: AssetKind }>` (walks top blocks, Dual Rig lanes, WDW lanes, Dual Amp params); `missingPaths(preset: Preset, inventory: { models: Asset[]; irs: Asset[]; reverbIrs: Asset[] }): Set<string>`
  - `useBankPresets(bank: number, draft?: { location: PresetLocation; preset: Preset }): Map<number, Preset>` (slot → preset; the draft replaces its own slot)
  - `PresetTile(props: { slot: number; preset?: Preset; live: boolean; editing: boolean; dirty: boolean; missing: boolean; onOpen(): void })`
  - `BankBar(props: { bank: number; editing: PresetLocation; live?: PresetLocation; dirty: boolean; draft: Preset; disabled: boolean; onBank(bank: number): void; onOpen(location: PresetLocation): void })`

- [ ] **Step 1: Write the failing tests**

`assetRefs.test.ts`:

```ts
import { expect, it } from "vitest";

import type { Preset } from "../api/types";
import { assetRefs, missingPaths } from "./assetRefs";

const preset: Preset = {
  version: 2, name: "Wide", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [
    { id: "n1", type: "nam", enabled: true, asset: "models/Clean.nam", params: {} },
    { id: "r1", type: "dualRig", enabled: true, asset: "", params: {}, lanes: {
      left: { blocks: [{ id: "c1", type: "cab", enabled: true, asset: "irs/2x12.wav", params: {} }] }, right: { blocks: [] } } },
    { id: "v1", type: "irreverb", enabled: true, asset: "reverb-irs/Chapel.wav", params: {} },
  ],
};

it("lists every file reference with its kind", () => {
  expect(assetRefs(preset)).toEqual([
    { blockId: "n1", path: "models/Clean.nam", kind: "models" },
    { blockId: "c1", path: "irs/2x12.wav", kind: "irs" },
    { blockId: "v1", path: "reverb-irs/Chapel.wav", kind: "reverb-irs" },
  ]);
});

it("finds paths with no file on the pedal", () => {
  const file = (path: string) => ({ id: path, kind: "model" as const, filename: path.split("/")[1], path, sizeBytes: 1 });
  expect([...missingPaths(preset, { models: [file("models/Clean.nam")], irs: [], reverbIrs: [] })]).toEqual(["irs/2x12.wav", "reverb-irs/Chapel.wav"]);
});
```

`useBankPresets.test.tsx`:

```tsx
import { renderHook, waitFor } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { useBankPresets } from "./useBankPresets";

const make = (name: string): Preset => ({ version: 1, name, routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [] });
const getPreset = vi.fn(async (bank: number, slot: number) => ({ bank, slot, preset: make(`B${bank}S${slot}`) }));
const session = { client: { getPreset }, presets: [{ bank: 1, slot: 0, exists: true }, { bank: 1, slot: 2, exists: true }, { bank: 1, slot: 1, exists: false }] };
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

it("loads the existing slots of the bank and uses the draft for its own slot", async () => {
  const { result } = renderHook(() => useBankPresets(1, { location: { bank: 1, slot: 2 }, preset: make("Draft") }));
  await waitFor(() => expect(result.current.get(0)?.name).toBe("B1S0"));
  expect(result.current.get(2)?.name).toBe("Draft");
  expect(result.current.has(1)).toBe(false);
  expect(getPreset).toHaveBeenCalledTimes(1);
});
```

`BankBar.test.tsx`:

```tsx
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";

import { PresetTile } from "./PresetTile";

it("floods the live tile, outlines the open one and names the footswitch", async () => {
  const onOpen = vi.fn();
  const preset = { version: 1 as const, name: "Glass Cathedral", routing: "serial" as const, global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
    blocks: [{ id: "d", type: "delay", enabled: true, asset: "", params: { mode: "tape" } }] };
  render(<PresetTile slot={0} preset={preset} live editing dirty missing={false} onOpen={onOpen} />);
  const tile = screen.getByRole("button", { name: /Glass Cathedral/ });
  expect(tile).toHaveClass("is-live", "is-edit");
  expect(screen.getByText("FS 1")).toBeInTheDocument();
  expect(screen.getByText("LIVE")).toBeInTheDocument();
  expect(screen.getByText("EDITED")).toBeInTheDocument();
  expect(screen.getByText("TAPE")).toBeInTheDocument();
  await userEvent.click(tile);
  expect(onOpen).toHaveBeenCalled();
});

it("shows an empty slot", () => {
  render(<PresetTile slot={3} live={false} editing={false} dirty={false} missing={false} onOpen={vi.fn()} />);
  expect(screen.getByRole("button", { name: "FS 4, empty slot" })).toBeInTheDocument();
});
```

- [ ] **Step 2: Run to see them fail**

Run: `npx vitest run src/stage/BankBar.test.tsx src/stage/useBankPresets.test.tsx src/assets/assetRefs.test.ts`
Expected: FAIL, modules not found.

- [ ] **Step 3: Implement `assetRefs.ts`**

```ts
import type { Asset, AssetKind, Preset, PresetBlock } from "../api/types";

export type AssetRef = { blockId: string; path: string; kind: AssetKind };

const KIND_BY_TYPE: Record<string, AssetKind> = { nam: "models", cab: "irs", irreverb: "reverb-irs" };
const DUAL_AMP_KEYS: Array<[string, AssetKind]> = [["leftNamAsset", "models"], ["leftIrAsset", "irs"], ["rightNamAsset", "models"], ["rightIrAsset", "irs"]];

function walk(blocks: PresetBlock[], out: AssetRef[]): AssetRef[] {
  for (const block of blocks) {
    const kind = KIND_BY_TYPE[block.type];
    if (kind && block.asset) out.push({ blockId: block.id, path: block.asset, kind });
    if (block.type === "dualAmp") {
      for (const [key, dualKind] of DUAL_AMP_KEYS) {
        const value = block.params[key];
        if (typeof value === "string" && value) out.push({ blockId: block.id, path: value, kind: dualKind });
      }
    }
    if (block.lanes) { walk(block.lanes.left.blocks, out); walk(block.lanes.right.blocks, out); }
  }
  return out;
}

/** Every file a preset references, in chain order. Mirrors collectAssetPaths in managerd. */
export function assetRefs(preset: Preset): AssetRef[] {
  const refs = walk(preset.blocks, []);
  if (preset.wdw) { walk(preset.wdw.dry.blocks, refs); walk(preset.wdw.wet.blocks, refs); }
  return refs;
}

export function missingPaths(preset: Preset, inventory: { models: Asset[]; irs: Asset[]; reverbIrs: Asset[] }): Set<string> {
  const known = new Set([...inventory.models, ...inventory.irs, ...inventory.reverbIrs].map(({ path }) => path));
  return new Set(assetRefs(preset).map(({ path }) => path).filter((path) => !known.has(path)));
}
```

- [ ] **Step 4: Implement `useBankPresets.ts`**

```ts
import { useEffect, useMemo, useState } from "react";

import type { Preset } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import type { PresetLocation } from "../presets/editor/editorTypes";

/** The presets of one bank for the tiles. The draft replaces its own saved slot. */
export function useBankPresets(bank: number, draft?: { location: PresetLocation; preset: Preset }): Map<number, Preset> {
  const session = useDeviceSession();
  const [loaded, setLoaded] = useState<Map<number, Preset>>(new Map());
  const existing = useMemo(() => session.presets.filter((summary) => summary.bank === bank && summary.exists).map(({ slot }) => slot).join(","), [session.presets, bank]);

  useEffect(() => {
    let cancelled = false;
    const client = session.client;
    const slots = existing ? existing.split(",").map(Number) : [];
    const skip = draft && draft.location.bank === bank ? draft.location.slot : -1;
    if (!client) return undefined;
    void Promise.all(slots.filter((slot) => slot !== skip).map(async (slot) => [slot, (await client.getPreset(bank, slot)).preset] as const))
      .then((entries) => { if (!cancelled) setLoaded(new Map(entries)); })
      .catch(() => { if (!cancelled) setLoaded(new Map()); });
    return () => { cancelled = true; };
  }, [session.client, bank, existing, draft?.location.bank, draft?.location.slot]);

  return useMemo(() => {
    const merged = new Map(loaded);
    if (draft && draft.location.bank === bank) merged.set(draft.location.slot, draft.preset);
    return merged;
  }, [loaded, draft, bank]);
}
```

A failed load shows the slot without a strip; it does not block the editor.

- [ ] **Step 5: Implement `PresetTile.tsx` and `BankBar.tsx`**

```tsx
// PresetTile.tsx
import type { Preset, PresetBlock } from "../api/types";
import { cx } from "../components/ui";
import { familyOf } from "../ui/family";
import { slotLabel } from "../ui/format";
import { Tag } from "../ui/Tag";
import { stripCode } from "./codes";
import "./bank.css";

function Strip({ blocks }: { blocks: PresetBlock[] }) {
  return <div className="strip" aria-hidden="true">{blocks.map((block) => block.lanes
    ? <span key={block.id} className="fam-amp w2">RIG {block.lanes.left.blocks.length}+{block.lanes.right.blocks.length}</span>
    : <span key={block.id} className={cx(`fam-${familyOf(block.type)}`, !block.enabled && "off", ["amp", "cab"].includes(familyOf(block.type)) && "w2")}>{stripCode(block)}</span>)}</div>;
}

export function PresetTile({ slot, preset, live, editing, dirty, missing, onOpen }: {
  slot: number; preset?: Preset; live: boolean; editing: boolean; dirty: boolean; missing: boolean; onOpen(): void;
}) {
  if (!preset) {
    return <button type="button" className={cx("tile", "is-empty", editing && "is-edit")} onClick={onOpen} aria-label={`${slotLabel(slot)}, empty slot`}>
      <div className="tile__top"><span className="tile__fs">{slotLabel(slot)}</span></div><div className="tile__name">Empty slot</div></button>;
  }
  const blocks = preset.routing === "wdw" && preset.wdw ? [...preset.wdw.dry.blocks, ...preset.wdw.wet.blocks] : preset.blocks;
  return (
    <button type="button" className={cx("tile", live && "is-live", editing && "is-edit")} onClick={onOpen} aria-pressed={editing}
      aria-label={`${preset.name}, ${slotLabel(slot)}${live ? ", live on the pedal" : ""}${editing ? ", open in the editor" : ""}`}>
      <div className="tile__top"><span className="tile__fs">{slotLabel(slot)}</span>
        <span className="tile__tags">{live && <Tag tone="ink">LIVE</Tag>}{dirty && <Tag tone="warn">EDITED</Tag>}{missing && <Tag tone="warn" title="Uses a file that is not on the pedal">MISSING FILE</Tag>}</span></div>
      <div className="tile__name">{preset.name}</div>
      <Strip blocks={blocks} />
    </button>
  );
}
```

```tsx
// BankBar.tsx
import { ChevronLeft, ChevronRight } from "lucide-react";

import type { Preset } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { IconButton } from "../components/ui";
import type { PresetLocation } from "../presets/editor/editorTypes";
import { missingPaths } from "../assets/assetRefs";
import { bankLabel } from "../ui/format";
import { PresetTile } from "./PresetTile";
import { useBankPresets } from "./useBankPresets";

export function BankBar({ bank, editing, live, dirty, draft, disabled, onBank, onOpen }: {
  bank: number; editing: PresetLocation; live?: PresetLocation; dirty: boolean; draft: Preset; disabled: boolean;
  onBank(bank: number): void; onOpen(location: PresetLocation): void;
}) {
  const session = useDeviceSession();
  const presets = useBankPresets(bank, { location: editing, preset: draft });
  const inventory = { models: session.models, irs: session.irs, reverbIrs: session.reverbIrs };
  return (
    <section className="bankbar" aria-label="Bank and presets">
      <div className="bank-step">
        <IconButton label="Previous bank" disabled={disabled || bank === 0} onClick={() => onBank(bank - 1)}><ChevronLeft size={18} /></IconButton>
        <div className="bank-step__mid"><b>{bankLabel(bank)}</b></div>
        <IconButton label="Next bank" disabled={disabled || bank === 99} onClick={() => onBank(bank + 1)}><ChevronRight size={18} /></IconButton>
      </div>
      <div className="bank-tiles">
        {[0, 1, 2, 3].map((slot) => {
          const preset = presets.get(slot);
          const isEditing = editing.bank === bank && editing.slot === slot;
          return <PresetTile key={slot} slot={slot} preset={preset}
            live={live?.bank === bank && live.slot === slot} editing={isEditing} dirty={isEditing && dirty}
            missing={preset ? missingPaths(preset, inventory).size > 0 : false} onOpen={() => onOpen({ bank, slot })} />;
        })}
      </div>
    </section>
  );
}
```

`bank.css`: port `.tile*`, `.strip*` from `mockups/manager-taste/shared.css` and `.bankbar`, `.bank-step`, `.bank-tiles` from `1-stage-drawer.html` (the `<style>` block), with the token renames of Task 8 and `.bank-step .mid` renamed `.bank-step__mid`. Keep the live-tile glow (`.tile.is-live::after` with `radial-gradient(closest-side, color-mix(in srgb, var(--lamp) 30%, transparent), transparent)`), because it has a visible source.

- [ ] **Step 6: Run the tests**

Run: `npx vitest run src/stage src/assets/assetRefs.test.ts && npx tsc -p .`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add apps/manager/src/stage apps/manager/src/assets/assetRefs.ts apps/manager/src/assets/assetRefs.test.ts
git commit -m "feat: add bank bar with device-style preset tiles"
```

---

### Task 11: Block drawer and chip strip

**Files:**
- Create: `apps/manager/src/stage/ChipStrip.tsx`, `apps/manager/src/stage/BlockDrawer.tsx`, `apps/manager/src/stage/FilePicker.tsx`, `apps/manager/src/stage/EqEditor.tsx`, `apps/manager/src/stage/drawer.css`
- Create: `apps/manager/src/stage/viewTransition.ts`
- Modify: `apps/manager/src/presets/editor/usePresetEditor.ts` (`editParameter`, `editBlockEnabled` scene rule)
- Test: `apps/manager/src/stage/BlockDrawer.test.tsx`, `apps/manager/src/stage/ChipStrip.test.tsx`, `apps/manager/src/presets/editor/usePresetEditor.test.tsx`

**Interfaces:**
- Consumes: `TravelScale`, `ChoiceStrip`, `familyOf`, `capFor`, `blockTitle`, `eqStateFor`, `EqResponseGraph`, `resolveDrop`, dnd-kit, `PresetEditor` (Task 5).
- Produces:
  - `withViewTransition(update: () => void): void` (uses `document.startViewTransition` with `flushSync` when present and motion is not reduced)
  - `ChipStrip(props: { blocks: PresetBlock[]; selectedId?: string; onSelect(id: string): void; onMove(action: EditorAction): void; onAdd(): void })`
  - `BlockDrawer(props: { block: PresetBlock; issues: ValidationIssue[]; focusedKey?: string; onFocusKey(key: string): void; onClose(): void; onManageFiles(kind: AssetKind): void })` reading the editor from `usePresetEditorContext()`
  - `FilePicker(props: { label: string; kind: AssetKind; value: string; onChange(path: string): void; onManage(): void })`
- Scene rule in `usePresetEditor`: when `editingScene` is set and `editingScene` does not own the address, `editParameter` dispatches `set-scene-scope` (scope "scene", current shared value) and then `set-scene-parameter` with the new value. Same for `editBlockEnabled` with `blockEnabled`. With no scene set, it dispatches `set-block-param` / `toggle-block`.

- [ ] **Step 1: Write the failing hook test for the scene rule**

`usePresetEditor.test.tsx` (same session mock as `EditorContext.test.tsx`, with a v4 preset that has a four-scene `sceneSet` and a delay `d1` with `mix: 0.25`):

```tsx
it("makes all scenes own a value on the first scene edit and changes only the open scene", () => {
  const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
  act(() => result.current.dispatch({ type: "select-scene", sceneId: "solo" }));
  act(() => result.current.editParameter("d1", "mix", 0.5, "g1"));
  const scenes = result.current.present.sceneSet!.scenes;
  const mixOf = (id: string) => scenes.find((s) => s.id === id)!.targets.find((t) => t.target === "parameter" && t.parameter === "mix");
  expect(mixOf("solo")).toMatchObject({ value: 0.5 });
  expect(mixOf("verse")).toMatchObject({ value: 0.25 });
  expect(result.current.present.blocks[0].params.mix).toBe(0.25);
  act(() => result.current.dispatch({ type: "undo" }));
  expect(result.current.present.sceneSet!.scenes.every((s) => s.targets.length === 0)).toBe(true);
});
```

- [ ] **Step 2: Run to see it fail**

Run: `npx vitest run src/presets/editor/usePresetEditor.test.tsx`
Expected: FAIL: the solo target is missing (the old rule edits the shared value).

- [ ] **Step 3: Change the scene rule**

In `usePresetEditor.ts`:

```ts
  const editParameter = (blockId: string, parameter: string, value: unknown, gesture?: string) => {
    if (editingScene && typeof value === "number") {
      if (!sceneOwns(editingScene, blockId, parameter)) {
        const current = findPresetBlockInPreset(present, blockId)?.params[parameter];
        dispatch({ type: "set-scene-scope", sceneId: editingScene.id, blockId, parameter, scope: "scene", value: typeof current === "number" ? current : value, gesture });
      }
      dispatch({ type: "set-scene-parameter", sceneId: editingScene.id, blockId, parameter, value, gesture });
      return;
    }
    dispatch({ type: "set-block-param", blockId, key: parameter, value, gesture });
  };
  const editBlockEnabled = (blockId: string, enabled: boolean) => {
    if (editingScene) {
      if (!sceneOwns(editingScene, blockId)) {
        const current = findPresetBlockInPreset(present, blockId)?.enabled ?? enabled;
        dispatch({ type: "set-scene-scope", sceneId: editingScene.id, blockId, scope: "scene", value: current });
      }
      dispatch({ type: "set-scene-block-enabled", sceneId: editingScene.id, blockId, value: enabled });
      return;
    }
    dispatch({ type: "toggle-block", blockId, enabled });
  };
```

The `select-scene` action stays the way scene scope opens; the drawer scope switch dispatches `select-scene` or clears it (see Step 7). Check the existing `PresetWorkspace` tests after this change; a test that expects a scene edit to change the shared value must now expect the scene value.

Also make sure `editingScene` is `undefined` when the person picks "Preset" scope. Add an action to clear it if the reducer has none: `{ type: "select-scene"; sceneId: string }` needs a sibling `{ type: "clear-scene" }` in `editorTypes.ts`, handled in `editorReducer.ts` as `return { ...state, editingSceneId: undefined };`, with a reducer test:

```ts
it("clears the edited scene, so edits go to the preset", () => {
  const state = editorReducer(createEditorState({ bank: 0, slot: 0 }, preset()), { type: "clear-scene" });
  expect(state.editingSceneId).toBeUndefined();
});
```

- [ ] **Step 4: Run the hook and reducer tests**

Run: `npx vitest run src/presets`
Expected: PASS.

- [ ] **Step 5: Write the failing drawer and chip tests**

`BlockDrawer.test.tsx` renders `<EditorProvider>` with the session mock (a v1 preset with `delay:tape` `d1`, a `nam` `n1` with `asset: "models/Clean.nam"`, and session `models: [{ id: "m1", kind: "model", filename: "Clean.nam", path: "models/Clean.nam", sizeBytes: 1 }, { id: "m2", kind: "model", filename: "Lead.nam", path: "models/Lead.nam", sizeBytes: 1 }]`) and a small harness that selects the block:

```tsx
function Harness({ id }: { id: string }) {
  const editor = usePresetEditorContext();
  const block = editor.present.blocks.find((b) => b.id === id)!;
  return <BlockDrawer block={block} issues={[]} onFocusKey={vi.fn()} onClose={onClose} onManageFiles={onManage} />;
}

it("shows every control of the block at drawer scale", () => {
  render(<EditorProvider><Harness id="d1" /></EditorProvider>);
  expect(screen.getByRole("heading", { name: "Tape Delay" })).toBeInTheDocument();
  for (const name of ["Time", "Repeats", "Mix", "Filter"]) expect(screen.getByRole("slider", { name })).toBeInTheDocument();
});

it("turns the block off with BLOCK ON", async () => {
  render(<EditorProvider><Harness id="d1" /></EditorProvider>);
  await userEvent.click(screen.getByRole("button", { name: "Block on" }));
  expect(screen.getByRole("button", { name: "Block off" })).toBeInTheDocument();
});

it("picks a NAM file and links to Assets", async () => {
  render(<EditorProvider><Harness id="n1" /></EditorProvider>);
  await userEvent.click(screen.getByRole("radio", { name: "Lead" }));
  expect(screen.getByRole("radio", { name: "Lead" })).toHaveAttribute("aria-checked", "true");
  await userEvent.click(screen.getByRole("button", { name: "Manage files" }));
  expect(onManage).toHaveBeenCalledWith("models");
});

it("says when the file is not on the pedal", () => {
  session.current.preset.blocks[1].asset = "models/Gone.nam";
  render(<EditorProvider><Harness id="n1" /></EditorProvider>);
  expect(screen.getByText(/Gone.nam is not on the pedal/)).toBeInTheDocument();
});
```

Reset `session.current.preset` in `beforeEach` with `structuredClone` so the last test does not leak.

`ChipStrip.test.tsx`:

```tsx
it("lists chips in chain order, selects one, and moves it with Alt and an arrow", async () => {
  const onSelect = vi.fn();
  const onMove = vi.fn();
  const blocks = [blockOf("dynamics:compressor", "b1"), blockOf("delay:tape", "b2")];
  render(<ChipStrip blocks={blocks} selectedId="b2" onSelect={onSelect} onMove={onMove} onAdd={vi.fn()} />);
  expect(screen.getByRole("button", { name: /Tape Delay/ })).toHaveAttribute("aria-current", "true");
  await userEvent.click(screen.getByRole("button", { name: /Compressor/ }));
  expect(onSelect).toHaveBeenCalledWith("b1");
  fireEvent.keyDown(screen.getByRole("button", { name: /Compressor/ }), { key: "ArrowRight", altKey: true });
  expect(onMove).toHaveBeenCalledWith({ type: "move-block", blockId: "b1", index: 1 });
});
```

- [ ] **Step 6: Run to see them fail**

Run: `npx vitest run src/stage/BlockDrawer.test.tsx src/stage/ChipStrip.test.tsx`
Expected: FAIL, modules not found.

- [ ] **Step 7: Implement**

`viewTransition.ts`:

```ts
import { flushSync } from "react-dom";

type TransitionDocument = Document & { startViewTransition?: (update: () => void) => unknown };

/** Runs a state update inside a view transition, so the chain folds into the chip strip like the pedal. */
export function withViewTransition(update: () => void): void {
  const doc = document as TransitionDocument;
  if (!doc.startViewTransition || matchMedia("(prefers-reduced-motion: reduce)").matches) {
    update();
    return;
  }
  doc.startViewTransition(() => flushSync(update));
}
```

`FilePicker.tsx`:

```tsx
import { FolderOpen } from "lucide-react";

import type { AssetKind } from "../api/types";
import { useDeviceSession } from "../connection/deviceSession";
import { fileStem } from "../ui/format";

const LABELS: Record<AssetKind, string> = { models: "NAM model", irs: "Cabinet IR", "reverb-irs": "Reverb IR" };

export function FilePicker({ label, kind, value, onChange, onManage }: { label?: string; kind: AssetKind; value: string; onChange(path: string): void; onManage(): void }) {
  const session = useDeviceSession();
  const files = kind === "models" ? session.models : kind === "irs" ? session.irs : session.reverbIrs;
  const missing = value !== "" && !files.some(({ path }) => path === value);
  return (
    <div className="lb-ctl lb-ctl--wide">
      <div className="lb-ctl__top"><span className="lb-ctl__label">{label ?? LABELS[kind]}</span>
        <button type="button" className="lb-ctl__share" onClick={onManage}><FolderOpen size={12} /> Manage files</button></div>
      {missing && <p className="lb-missing">{value.split("/").pop()} is not on the pedal. Pick another file, or upload it in Assets.</p>}
      <div className="lb-choice" role="radiogroup" aria-label={label ?? LABELS[kind]}>
        {files.map((file) => <button key={file.id} type="button" role="radio" aria-checked={file.path === value} onClick={() => onChange(file.path)}>{fileStem(file.filename)}</button>)}
        {files.length === 0 && <span className="lb-note">No files yet. Use Manage files to upload one.</span>}
      </div>
    </div>
  );
}
```

The button's accessible name is `Manage files` (lucide icons render `aria-hidden`); the test depends on it.

`EqEditor.tsx`: wraps `EqResponseGraph` with the stage state that `BlockInspector` `EqControls` uses today (`activeStage`, band or pass-filter controls), but renders the band values with `TravelScale` controls built from `NumberControl` objects for `frequency_hz` (20 to 20000, step 1, unit "hz"), `gain_db` (-18 to 18, step 0.1, unit "db") and `q` (0.1 to 18, step 0.1, unit "plain"), dispatching `set-eq-band` with the gesture. Pass filters keep their `onParam(high_pass | low_pass)` path. Move the EQ-specific JSX from `BlockInspector.tsx` lines 163-221 into this file and adapt the calls; keep the props `block`, `onEqBand(blockId, index, patch, gesture?)`, `onParam(blockId, key, value, gesture?)`.

`BlockDrawer.tsx`:

```tsx
import { Copy, Power, RotateCcw, Trash2, X } from "lucide-react";

import type { AssetKind, PresetBlock } from "../api/types";
import { Button, IconButton } from "../components/ui";
import { allEffectDefinitions, findEffectDefinition } from "../effects/catalog";
import type { EffectControl } from "../effects/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import type { ValidationIssue } from "../presets/editor/presetValidation";
import { sceneOwns } from "../presets/scenes/sceneView";
import { ChoiceStrip } from "../ui/ChoiceStrip";
import { capFor, familyOf } from "../ui/family";
import { Tag } from "../ui/Tag";
import { TravelScale } from "../ui/TravelScale";
import { blockTitle } from "./BlockCard";
import { EqEditor } from "./EqEditor";
import { FilePicker } from "./FilePicker";
import { SceneScope } from "./SceneScope";
import "./drawer.css";

export function BlockDrawer({ block, issues, focusedKey, onFocusKey, onClose, onManageFiles }: {
  block: PresetBlock; issues: ValidationIssue[]; focusedKey?: string; onFocusKey(key: string): void; onClose(): void; onManageFiles(kind: AssetKind): void;
}) {
  const editor = usePresetEditorContext();
  const definition = findEffectDefinition(block);
  const family = familyOf(block.type);
  const scene = editor.editingScene;
  const modes = definition?.mode ? allEffectDefinitions().filter((d) => d.blockType === definition.blockType && d.mode) : [];
  const valueOf = <T,>(key: string, fallback: T): T => (typeof block.params[key] === typeof fallback ? block.params[key] as T : fallback);

  const control = (c: EffectControl) => {
    if (c.kind === "asset") {
      const value = c.key ? String(block.params[c.key] ?? "") : block.asset;
      return <FilePicker key={c.key ?? c.label} label={c.label} kind={c.assetKind} value={value} onManage={() => onManageFiles(c.assetKind)}
        onChange={(path) => (c.key ? editor.editParameter(block.id, c.key, path) : editor.dispatch({ type: "set-block-asset", blockId: block.id, asset: path }))} />;
    }
    if (c.kind === "number") {
      return <TravelScale key={c.key} control={c} value={valueOf(c.key, c.defaultValue)} family={family}
        focused={focusedKey === c.key} onFocusControl={() => onFocusKey(c.key)}
        owned={scene ? (sceneOwns(scene, block.id, c.key) ? "scene" : "shared") : undefined}
        onShare={scene ? () => editor.dispatch({ type: "set-scene-scope", sceneId: scene.id, blockId: block.id, parameter: c.key, scope: "shared", value: valueOf(c.key, c.defaultValue) }) : undefined}
        onChange={(value, gesture) => editor.editParameter(block.id, c.key, value, gesture)} />;
    }
    if (c.kind === "choice") return <ChoiceStrip key={c.key} label={c.label} family={family} value={valueOf(c.key, c.defaultValue)} options={c.choices} onChange={(value) => editor.editParameter(block.id, c.key, value)} />;
    if (c.kind === "toggle") return <ChoiceStrip key={c.key} label={c.label} family={family} value={valueOf(c.key, c.defaultValue)} options={[{ value: false, label: "Off" }, { value: true, label: "On" }]} onChange={(value) => editor.editParameter(block.id, c.key, value)} />;
    return <EqEditor key="eq" block={block}
      onEqBand={(id, band, patch, gesture) => editor.dispatch({ type: "set-eq-band", blockId: id, band, patch, gesture })}
      onParam={(id, key, value, gesture) => editor.editParameter(id, key, value, gesture)} />;
  };

  return (
    <section className={`drawer fam-${family}`} aria-label={`${blockTitle(block)} parameters`}>
      <div className="drawer__head">
        <Tag tone="line">{capFor(block.type)}</Tag>
        <h2>{blockTitle(block)}</h2>
        <span className="drawer__sub">{block.asset ? definition?.name : definition?.description}</span>
        <span className="drawer__actions">
          <SceneScope />
          <Button className="pow-big" aria-pressed={block.enabled} onClick={() => editor.editBlockEnabled(block.id, !block.enabled)}><Power size={16} />{block.enabled ? "Block on" : "Block off"}</Button>
          <IconButton label="Duplicate" onClick={() => editor.dispatch({ type: "duplicate-block", blockId: block.id })}><Copy size={16} /></IconButton>
          <IconButton label="Reset to defaults" onClick={() => editor.dispatch({ type: "reset-block", blockId: block.id })}><RotateCcw size={16} /></IconButton>
          <Button variant="danger" onClick={() => { editor.dispatch({ type: "remove-block", blockId: block.id }); onClose(); }}><Trash2 size={16} />Delete</Button>
          <IconButton label="Close" onClick={onClose}><X size={16} /></IconButton>
        </span>
      </div>
      {issues.length > 0 && <ul className="drawer__issues">{issues.map((issue, i) => <li key={`${issue.code}-${i}`} className={`is-${issue.severity}`}>{issue.message}</li>)}</ul>}
      {modes.length > 1 && <div className="drawer__modes"><ChoiceStrip label="Type" family={family} value={definition!.id}
        options={modes.map((m) => ({ value: m.id, label: m.name }))} onChange={(id) => editor.dispatch({ type: "change-definition", blockId: block.id, definitionId: id })} /></div>}
      {editor.present.version === 4 && ["delay", "reverb", "irreverb"].includes(block.type) && <div className="drawer__modes">
        <ChoiceStrip label="On scene bypass" family={family} value={block.sceneBypass ?? "letRing"}
          options={[{ value: "cut", label: "Cut" }, { value: "letRing", label: "Let ring" }]}
          onChange={(policy) => editor.dispatch({ type: "set-scene-bypass", blockId: block.id, policy })} /></div>}
      <div className="ctlgrid">{definition ? definition.controls.map(control) : <p className="lb-note">This block type is unknown to this manager. You can turn it off or delete it.</p>}</div>
    </section>
  );
}
```

Dual Amp and Dual Rig: split `definition.controls` into general, `left*` and `right*` groups like `BlockInspector` does today, and render the lane groups under `<h3>Lane A</h3>` and `<h3>Lane B</h3>` headings inside `.ctlgrid`.

Create `SceneScope.tsx` (used by the drawer here, and the stage head and Global drawer in Task 12):

```tsx
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { useDeviceSession } from "../connection/deviceSession";

/** Preset or one scene as the edit scope, like the pedal's scene screen. */
export function SceneScope() {
  const editor = usePresetEditorContext();
  const session = useDeviceSession();
  const scenes = editor.present.sceneSet?.scenes;
  if (!scenes) return null;
  const liveId = session.device?.active?.liveSceneId;
  return (
    <div className="seg" role="group" aria-label="Edit scope">
      <button type="button" className="btn btn--sm" aria-pressed={!editor.editingScene} onClick={() => editor.dispatch({ type: "clear-scene" })}>Preset</button>
      {scenes.map((scene, index) => (
        <button key={scene.id} type="button" className="btn btn--sm" aria-pressed={editor.editingScene?.id === scene.id}
          onClick={() => editor.dispatch({ type: "select-scene", sceneId: scene.id })}>
          {index + 1} {scene.name}{liveId === scene.id && <i className="lampdot" title="Live scene" />}
        </button>
      ))}
    </div>
  );
}
```

`ChipStrip.tsx`: a `DndContext` with `MouseSensor`, `TouchSensor`, `KeyboardSensor` (same settings as `ChainStage`), a `SortableContext` over the top-level blocks, and one `<button className="chip fam-…">` per block with `aria-current`, `style={{ viewTransitionName: \`block-${block.id}\` }}`, `<small>{capFor(type)}{enabled ? "" : " · off"}</small><b>{blockTitle(block)}</b>`, and an `onKeyDown` that maps Alt+Left/Right through `resolveDrop({ listId: "top", index }, { listId: "top", index: index ± 1 }, block.id)`. Dual Rig renders as a `chip-rig` group with two small sortable lane lists (`lane:<id>:left`, `lane:<id>:right`) using the same `resolveDrop`. End with a `chip-add` button (`aria-label="Add a block at the end"`, calls `onAdd`).

`drawer.css`: port `.drawer`, `.drawer__head`, `.ctlgrid`, `.pow-big`, `.scene-note` from the `1-stage-drawer.html` `<style>` block, `.chip*` rules and `.lampdot` from `shared.css`, `.asset-missing` (renamed `.lb-missing`) and `.note` (renamed `.lb-note`) from `shared.css`, all with the Task 8 token renames. Add `.lb-ctl--wide { grid-column: 1 / -1; }` and `.drawer__issues li.is-error { color: var(--danger); }`.

- [ ] **Step 8: Run the tests**

Run: `npx vitest run && npx tsc -p .`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add apps/manager/src
git commit -m "feat: add block drawer, chip strip and scene-first edits"
```

---

### Task 12: Global drawer, Scenes drawer, context rail and edit rail

**Files:**
- Create: `apps/manager/src/stage/GlobalDrawer.tsx`, `apps/manager/src/stage/ScenesDrawer.tsx`, `apps/manager/src/stage/EditRail.tsx`, `apps/manager/src/stage/LiveState.tsx`
- Modify: `apps/manager/src/presets/scenes/SceneWorkspaceBar.tsx` (class names only, for the new CSS)
- Test: `apps/manager/src/stage/GlobalDrawer.test.tsx`, `apps/manager/src/stage/EditRail.test.tsx`, `apps/manager/src/stage/LiveState.test.tsx`

**Interfaces:**
- Consumes: `usePresetEditorContext()`, `useDeviceSession()`, `TravelScale`, `ChoiceStrip`, `SceneScope`, `SceneWorkspaceBar`.
- Produces:
  - `GlobalDrawer(props: { onClose(): void })`: input gain (scene-ownable through `set-scene-input-scope` / `set-scene-input-gain`), output level, the fixed safety limiter, topology (`set-routing` serial / wdw), WDW lane mix controls (`editWdwMix`) when `routing === "wdw"`, expression (enable, target block, parameter, heel/toe range with `TravelScale`, invert).
  - `ScenesDrawer(props: { onClose(): void })`: `SceneWorkspaceBar` with the props `PresetWorkspace` passes today, or a **Create four scenes** button (`dispatch({ type: "enable-scenes" })`) when there is no scene set.
  - `LiveState()`: one of `LIVE ON PEDAL` tag, **Load on pedal** (`apply()`), **Save and load** (`saveAndApply()`), or a busy label; reads `editor.dirty`, `editor.runtimeMatchesDraft`, `session.busy`.
  - `EditRail(props: { drawer: "none" | "block" | "global" | "scenes"; focused?: { blockId: string; control: NumberControl }; onOpen(drawer: "global" | "scenes"): void; onAdd(): void; onDone(): void })`: Save (primary when dirty; disabled with a reason when `!validation.canSave`), Undo, Redo, Add block, Global, Scenes, the focused control's context (label, value, fine −/+ as Shift-steps through `editParameter`, **Assign EXP** when the block is in `expressionTargets`, Reset to `defaultValue`), `LiveState`, and **Done** when a drawer is open.

- [ ] **Step 1: Write the failing tests**

`LiveState.test.tsx` (session mock as before; vary `device.active` and the draft):

```tsx
it("says LIVE ON PEDAL when the saved slot is live", () => {
  session.device.active = { bank: 0, slot: 0, generation: 3, storedRevisionMatches: true };
  renderWithEditor(<LiveState />);
  expect(screen.getByText("LIVE ON PEDAL")).toBeInTheDocument();
});

it("offers Load on pedal for a clean preset that is not live", async () => {
  session.device.active = { bank: 5, slot: 0, generation: 3, storedRevisionMatches: true };
  renderWithEditor(<LiveState />);
  await userEvent.click(screen.getByRole("button", { name: "Load on pedal" }));
  expect(session.applyCurrent).toHaveBeenCalled();
});

it("offers Save and load when the draft has changes", async () => {
  const { editor } = renderWithEditor(<LiveState />);
  act(() => editor().editParameter("d1", "mix", 0.5));
  await userEvent.click(screen.getByRole("button", { name: "Save and load" }));
  expect(session.saveCurrent).toHaveBeenCalled();
});
```

`renderWithEditor` renders the node inside `EditorProvider` together with a probe that stores `usePresetEditorContext()` in a ref, and returns `{ editor: () => ref.current }`.

`EditRail.test.tsx` (same session mock as `BlockDrawer.test.tsx`, with the tape delay `d1` as the first block and `mix: 0.25`):

```tsx
it("keeps Save quiet until there is a change, then makes it primary", () => {
  const { editor } = renderWithEditor(<EditRail drawer="none" onOpen={vi.fn()} onAdd={vi.fn()} onDone={vi.fn()} />);
  expect(screen.getByRole("button", { name: "Save" })).toBeDisabled();
  act(() => editor().editParameter("d1", "mix", 0.5));
  expect(screen.getByRole("button", { name: "Save" })).toHaveClass("button--primary");
});

it("shows the focused control with fine steps and Assign EXP", async () => {
  const control = getEffectDefinition("delay:tape").controls.find((c) => c.kind === "number" && c.key === "mix") as NumberControl;
  const { editor } = renderWithEditor(<EditRail drawer="block" focused={{ blockId: "d1", control }} onOpen={vi.fn()} onAdd={vi.fn()} onDone={vi.fn()} />);
  expect(screen.getByText("Mix")).toBeInTheDocument();
  await userEvent.click(screen.getByRole("button", { name: "Fine increase" }));
  expect(editor().present.blocks[0].params.mix).toBeCloseTo(0.255);
  await userEvent.click(screen.getByRole("button", { name: "Assign EXP" }));
  expect(editor().present.expression).toMatchObject({ blockId: "d1", parameter: "mix" });
});
```

`GlobalDrawer.test.tsx`:

```tsx
it("shows input, output, the fixed limiter and the topology", () => {
  renderWithEditor(<GlobalDrawer onClose={vi.fn()} />);
  expect(screen.getByRole("slider", { name: "Input gain" })).toBeInTheDocument();
  expect(screen.getByRole("slider", { name: "Output level" })).toBeInTheDocument();
  expect(screen.getByText("Protection, not a tone control. It cannot be changed.")).toBeInTheDocument();
  expect(screen.queryByRole("slider", { name: /limit/i })).not.toBeInTheDocument();
  expect(screen.getByRole("radio", { name: "Serial" })).toHaveAttribute("aria-checked", "true");
});
```

- [ ] **Step 2: Run to see them fail**

Run: `npx vitest run src/stage/LiveState.test.tsx src/stage/EditRail.test.tsx src/stage/GlobalDrawer.test.tsx`
Expected: FAIL, modules not found.

- [ ] **Step 3: Implement `LiveState.tsx`**

```tsx
import { Send } from "lucide-react";

import { Button } from "../components/ui";
import { useDeviceSession } from "../connection/deviceSession";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { Tag } from "../ui/Tag";

/** The pedal plays saved slots only, so a changed draft offers Save and load. */
export function LiveState() {
  const editor = usePresetEditorContext();
  const session = useDeviceSession();
  if (session.busy.apply || editor.saving) return <Tag tone="line">Sending to the pedal</Tag>;
  if (editor.dirty) {
    return <Button variant="secondary" disabled={!editor.validation.canApply && !editor.validation.canSave} onClick={() => void editor.saveAndApply()}
      title="Saves the slot, then loads it on the pedal"><Send size={15} />Save and load</Button>;
  }
  if (editor.runtimeMatchesDraft) return <Tag tone="live" title="The pedal plays this saved preset">LIVE ON PEDAL</Tag>;
  return <Button variant="secondary" disabled={!editor.validation.canApply} onClick={() => void editor.apply()}><Send size={15} />Load on pedal</Button>;
}
```

- [ ] **Step 4: Implement `EditRail.tsx`**

```tsx
import { ChevronLeft, ChevronRight, Footprints, Grid2x2, Plus, Redo2, RotateCcw, Save, SlidersHorizontal, Undo2 } from "lucide-react";

import { Button, IconButton } from "../components/ui";
import { displayValue } from "../effects/display";
import type { NumberControl } from "../effects/types";
import { findPresetBlockInPreset } from "../presets/editor/editorReducer";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { applySceneToBlock } from "../presets/scenes/sceneView";
import { LiveState } from "./LiveState";

export function EditRail({ drawer, focused, onOpen, onAdd, onDone }: {
  drawer: "none" | "block" | "global" | "scenes";
  focused?: { blockId: string; control: NumberControl };
  onOpen(drawer: "global" | "scenes"): void; onAdd(): void; onDone(): void;
}) {
  const editor = usePresetEditorContext();
  const block = focused ? findPresetBlockInPreset(editor.present, focused.blockId) : undefined;
  const shown = block ? applySceneToBlock(block, editor.editingScene) : undefined;
  const value = focused && shown ? (typeof shown.params[focused.control.key] === "number" ? shown.params[focused.control.key] as number : focused.control.defaultValue) : 0;
  const fine = (direction: 1 | -1) => focused && editor.editParameter(focused.blockId, focused.control.key,
    Math.min(focused.control.maximum, Math.max(focused.control.minimum, value + direction * focused.control.step * 0.1)), `fine-${focused.blockId}-${focused.control.key}`);
  const canExpress = focused && editor.expressionTargets.some((t) => t.block.id === focused.blockId && t.parameters.some((p) => p.key === focused.control.key));
  const saveBlocked = !editor.validation.canSave;
  return (
    <nav className="rail" aria-label="Edit actions">
      <IconButton label="Undo" disabled={editor.editor.history.past.length === 0} onClick={() => editor.dispatch({ type: "undo" })}><Undo2 size={17} /></IconButton>
      <IconButton label="Redo" disabled={editor.editor.history.future.length === 0} onClick={() => editor.dispatch({ type: "redo" })}><Redo2 size={17} /></IconButton>
      <Button variant={editor.dirty ? "primary" : "secondary"} disabled={!editor.dirty || saveBlocked || editor.saving}
        title={saveBlocked ? editor.validation.issues.find((i) => i.severity === "error")?.message : "Save to the slot (⌘S)"} onClick={() => void editor.save()}><Save size={16} /><span className="lbl">Save</span></Button>
      <Button onClick={onAdd}><Plus size={16} /><span className="lbl">Add block</span></Button>
      <Button aria-pressed={drawer === "global"} onClick={() => onOpen("global")}><SlidersHorizontal size={16} /><span className="lbl">Global</span></Button>
      <Button aria-pressed={drawer === "scenes"} onClick={() => onOpen("scenes")}><Grid2x2 size={16} /><span className="lbl">{editor.present.sceneSet ? "Scenes" : "Create scenes"}</span></Button>
      {focused && <div className="ctx">
        <span><span className="ctx__label">{focused.control.label}</span><span className="ctx__value">{displayValue(focused.control, value)}</span></span>
        <IconButton label="Fine decrease" onClick={() => fine(-1)}><ChevronLeft size={16} /></IconButton>
        <IconButton label="Fine increase" onClick={() => fine(1)}><ChevronRight size={16} /></IconButton>
        {canExpress && <Button className="hide-sm" onClick={() => editor.dispatch({ type: "set-expression", expression: { blockId: focused.blockId, parameter: focused.control.key, minimum: focused.control.minimum, maximum: focused.control.maximum, inverted: false } })}><Footprints size={15} />Assign EXP</Button>}
        <Button className="hide-sm" onClick={() => editor.editParameter(focused.blockId, focused.control.key, focused.control.defaultValue)}><RotateCcw size={15} />Reset</Button>
      </div>}
      <span className="rail__push" />
      <LiveState />
      {drawer !== "none" && <Button variant="primary" onClick={onDone}>Done</Button>}
    </nav>
  );
}
```

If lucide-react has no `Grid2x2` or `Footprints` export in the installed version, use `LayoutGrid` and `Gauge`.

- [ ] **Step 5: Implement `GlobalDrawer.tsx` and `ScenesDrawer.tsx`**

`GlobalDrawer.tsx` renders `<section className="drawer fam-util">` with a head (`<h2>Global</h2>`, `SceneScope`, Close) and a `.ctlgrid` with:
- `TravelScale` for input gain: `control = { kind: "number", key: "inputGainDb", label: "Input gain", minimum: -60, maximum: 24, step: 0.5, unit: "db", defaultValue: 0 }`, `value = editor.displayedInputGain`, `onChange` → `editor.sceneInputOwned && editor.editingScene ? dispatch({ type: "set-scene-input-gain", sceneId, value, gesture }) : dispatch({ type: "set-global", key: "inputGainDb", value, gesture })`; `owned` from `editor.sceneInputOwned`; `onShare` → `dispatch({ type: "set-scene-input-scope", sceneId, scope: "shared" })`. The first edit in scene scope when not owned dispatches `set-scene-input-scope` with `scope: "scene"` first.
- `TravelScale` for output level (`outputGainDb`, -60 to 12).
- The fixed limiter card: label "Safety limiter", `<Tag>FIXED</Tag>`, value `-1 dBFS`, note "Protection, not a tone control. It cannot be changed."
- `ChoiceStrip` "Topology": Serial / Wet dry wet → `dispatch({ type: "set-routing", routing })`.
- When WDW: lane level, pan (dry) and width (wet) `TravelScale`s and an on/off `ChoiceStrip` per lane, through `editor.editWdwMix`.
- Expression: port the `expression-strip` JSX of today's `PresetWorkspace` (lines 400-470): enable toggle (`editor.enableExpression()` / `dispatch({ type: "set-expression" })`), target block select, parameter select, heel and toe `TravelScale`s bound to `minimum`/`maximum` of the chosen `NumberControl`, invert toggle, all through `editor.patchExpression`.

`ScenesDrawer.tsx` renders `<section className="drawer fam-mod">` with a head (`<h2>Scenes</h2>`, Close) and either `<SceneWorkspaceBar ...>` with the same props `PresetWorkspace` passes today (lines 375-394, taken from the editor context), or, with no scene set, the text "Scenes change blocks and values inside one preset. FS 1 to 4 pick them on the pedal." and a `Create four scenes` button that dispatches `enable-scenes`.

Validation issue `scene-set-required` must offer the same fix: in `BlockDrawer` and `StageHead` (Task 14) issue lists, render a `Create four scenes` button next to that issue code.

- [ ] **Step 6: Run the tests**

Run: `npx vitest run && npx tsc -p .`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add apps/manager/src
git commit -m "feat: add global and scenes drawers and the edit rail"
```

---

### Task 13: Module drawer

**Files:**
- Create: `apps/manager/src/stage/ModuleDrawer.tsx`, `apps/manager/src/stage/modules.css`
- Test: `apps/manager/src/stage/ModuleDrawer.test.tsx`

**Interfaces:**
- Consumes: `allEffectDefinitions`, `MODULE_CODES`, `familyOf`, Radix Dialog, `PortalSurface`, `disabledDefinitions` (from the editor).
- Produces: `ModuleDrawer(props: { open: boolean; where: string; disabledIds: Map<string, string>; onOpenChange(open: boolean): void; onChoose(definition: EffectDefinition): void })`. Families in this order: Amp and drive, Cab, Dynamics and tone, Modulation, Delay, Reverb. Search matches name, description, aliases and code. Enter picks the first enabled row.

- [ ] **Step 1: Write the failing test**

```tsx
import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { ModuleDrawer } from "./ModuleDrawer";

describe("ModuleDrawer", () => {
  it("groups blocks by family with code squares and says where they go", () => {
    render(<ModuleDrawer open where="Insert after Tape Delay, position 9" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    expect(screen.getByText("Insert after Tape Delay, position 9")).toBeInTheDocument();
    const delay = screen.getByRole("group", { name: "Delay" });
    expect(within(delay).getByRole("button", { name: /Tape Delay/ })).toBeInTheDocument();
    expect(within(delay).getByText("TAPE")).toBeInTheDocument();
  });

  it("filters by search and picks the first match with Enter", async () => {
    const onChoose = vi.fn();
    render(<ModuleDrawer open where="" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={onChoose} />);
    await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "shimmer{Enter}");
    expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: "reverb:shimmer" }));
  });

  it("disables a block with the reason", () => {
    render(<ModuleDrawer open where="" disabledIds={new Map([["delay:digital", "Disable Tape Delay first"]])} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    const row = screen.getByRole("button", { name: /Digital Delay/ });
    expect(row).toBeDisabled();
    expect(row).toHaveTextContent("Disable Tape Delay first");
  });
});
```

- [ ] **Step 2: Run to see it fail**

Run: `npx vitest run src/stage/ModuleDrawer.test.tsx`
Expected: FAIL, module not found.

- [ ] **Step 3: Implement**

```tsx
import * as Dialog from "@radix-ui/react-dialog";
import { Plus, Search, X } from "lucide-react";
import { useMemo, useState } from "react";

import { IconButton } from "../components/ui";
import { allEffectDefinitions } from "../effects/catalog";
import type { EffectDefinition } from "../effects/types";
import { PortalSurface } from "../theme/surface";
import { familyOf, type Family } from "../ui/family";
import { MODULE_CODES } from "./codes";
import "./modules.css";

const GROUPS: Array<{ family: Family; label: string }> = [
  { family: "amp", label: "Amp and drive" }, { family: "cab", label: "Cab" }, { family: "util", label: "Dynamics and tone" },
  { family: "mod", label: "Modulation" }, { family: "dly", label: "Delay" }, { family: "rev", label: "Reverb" },
];

const matches = (definition: EffectDefinition, query: string) =>
  [definition.name, definition.description, ...(definition.aliases ?? []), MODULE_CODES[definition.id] ?? ""].join(" ").toLowerCase().includes(query.toLowerCase());

export function ModuleDrawer({ open, where, disabledIds, onOpenChange, onChoose }: {
  open: boolean; where: string; disabledIds: Map<string, string>; onOpenChange(open: boolean): void; onChoose(definition: EffectDefinition): void;
}) {
  const [query, setQuery] = useState("");
  const [family, setFamily] = useState<Family | "all">("all");
  const definitions = useMemo(() => allEffectDefinitions().filter((d) => (family === "all" || familyOf(d.blockType) === family) && matches(d, query)), [family, query]);
  const choose = (definition: EffectDefinition) => { onChoose(definition); setQuery(""); };
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal><PortalSurface>
        <Dialog.Overlay className="mods-scrim" />
        <Dialog.Content className="mods" aria-describedby={undefined}>
          <div className="mods__head">
            <div className="mods__title"><div><Dialog.Title>Add block</Dialog.Title><p>{where}</p></div>
              <Dialog.Close asChild><IconButton label="Close"><X size={18} /></IconButton></Dialog.Close></div>
            <label className="search"><Search size={17} /><input type="search" aria-label="Search blocks" placeholder={`Search ${allEffectDefinitions().length} blocks`} value={query} autoFocus
              onChange={(e) => setQuery(e.target.value)} onKeyDown={(e) => { if (e.key !== "Enter") return; const first = definitions.find((d) => !disabledIds.has(d.id)); if (first) choose(first); }} /></label>
            <div className="fams" role="group" aria-label="Families">
              <button type="button" aria-pressed={family === "all"} onClick={() => setFamily("all")}>All</button>
              {GROUPS.map((g) => <button key={g.family} type="button" className={`fam-${g.family}`} aria-pressed={family === g.family} onClick={() => setFamily(g.family)}>{g.label}</button>)}
            </div>
          </div>
          <div className="mods__list">
            {GROUPS.map((group) => {
              const rows = definitions.filter((d) => familyOf(d.blockType) === group.family);
              if (!rows.length) return null;
              return <div key={group.family} role="group" aria-label={group.label}>
                <div className="mods__grp"><span>{group.label}</span><span>{rows.length}</span></div>
                {rows.map((d) => {
                  const reason = disabledIds.get(d.id);
                  return <button key={d.id} type="button" className={`mod-row fam-${group.family}`} disabled={Boolean(reason)} onClick={() => choose(d)}>
                    <span className="code">{MODULE_CODES[d.id]}</span><span><b>{d.name}</b><small>{reason ?? d.description}</small></span><span className="plus"><Plus size={16} /></span>
                  </button>;
                })}
              </div>;
            })}
            {definitions.length === 0 && <p className="lb-note">No block matches. Try a family name, for example delay.</p>}
          </div>
        </Dialog.Content>
      </PortalSurface></Dialog.Portal>
    </Dialog.Root>
  );
}
```

The families group label for the first row is "Amp and drive" (the mockup's "Amp & drive" uses an ampersand; STE copy uses "and"). The mockup's "Turns off X" hint is replaced by the real `disabledDefinitions` reason.

`modules.css`: port `.mods-scrim`, `.mods*`, `.search`, `.fams`, `.mod-row*` from `mockups/manager-taste/shared.css` with the Task 8 token renames. Radix controls open state, so drop the `.is-open` transforms and use `data-state="open"` animations: `.mods[data-state="open"] { animation: mods-in .26s var(--ease); } @keyframes mods-in { from { transform: translateX(100%); } }`.

- [ ] **Step 4: Run the test**

Run: `npx vitest run src/stage/ModuleDrawer.test.tsx`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add apps/manager/src/stage
git commit -m "feat: add module drawer grouped by family"
```

---

### Task 14: Stage workspace and app shell

**Files:**
- Create: `apps/manager/src/stage/StageWorkspace.tsx`, `apps/manager/src/stage/StageWorkspace.test.tsx`
- Create: `apps/manager/src/app/AppBar.tsx`, `apps/manager/src/app/ConnectionPill.tsx`, `apps/manager/src/app/useAppView.ts`, `apps/manager/src/app/app.css`
- Modify: `apps/manager/src/app/AppShell.tsx`
- Delete: `apps/manager/src/presets/workspace/PresetWorkspace.tsx` and its test after porting the assertions
- Test: `apps/manager/src/app/useAppView.test.ts`, `apps/manager/src/app/ConnectionPill.test.tsx`

**Interfaces:**
- Consumes: everything from Tasks 5-13, `UnsavedChangesDialog`, `ConnectionDialog`, `SettingsDialog`, `isHostedCloudRuntime`.
- Produces:
  - `useAppView(): { view: "edit" | "assets"; assetKind?: AssetKind; goto(view: "edit" | "assets", kind?: AssetKind): void }` (hash `#assets` and `#assets/irs`; `hashchange` aware)
  - `StageWorkspace({ onManageFiles(kind: AssetKind): void; onConnection(): void })`: bank bar, stage head (EDIT, preset name input, block count, `SceneScope`, issues with fixes), `ChainStage` or `ChipStrip` + drawer, `EditRail`, `ModuleDrawer`, `UnsavedChangesDialog`, keyboard shortcuts (⌘Z, ⇧⌘Z, ⌘S, B, Delete, Esc, A)
  - `ConnectionPill({ onOpen(): void })`: dot, device name, address, `HTTP` tag unless hosted
  - `AppBar({ view, onView, onConnection, onSettings, onCloudDevices? })`

- [ ] **Step 1: Write the failing tests**

`useAppView.test.ts`:

```ts
import { act, renderHook } from "@testing-library/react";
import { expect, it } from "vitest";

import { useAppView } from "./useAppView";

it("keeps the view in the hash", () => {
  window.history.replaceState(null, "", "/#assets/irs");
  const { result } = renderHook(() => useAppView());
  expect(result.current).toMatchObject({ view: "assets", assetKind: "irs" });
  act(() => result.current.goto("edit"));
  expect(window.location.hash).toBe("");
  expect(result.current.view).toBe("edit");
});
```

`ConnectionPill.test.tsx`:

```tsx
it("says HTTP on the LAN build", () => {
  render(<ConnectionPill onOpen={vi.fn()} />);
  expect(screen.getByText("HTTP")).toHaveAttribute("title", expect.stringMatching(/trusted networks/));
});
```

(with the session mock `{ status: "connected", baseUrl: "http://192.168.88.12:8080", device: { deviceName: "Ardor Pedal" } }` and `vi.mock("../runtime/platform", () => ({ isHostedCloudRuntime: () => false }))`; a second test with `true` expects no `HTTP` text.)

`StageWorkspace.test.tsx`: port every assertion of `PresetWorkspace.test.tsx` with the new names: "Save & Apply" → "Save and load"; the Compare flow opens through the Scenes rail button, then `Compare` and `Shared` inside `SceneWorkspaceBar` as today. Add:

```tsx
it("keeps an unsaved edit when the Assets view opens and closes", async () => {
  const { rerender } = render(<AppProviders><StageWorkspace onManageFiles={vi.fn()} onConnection={vi.fn()} /></AppProviders>);
  await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
  await userEvent.type(screen.getByRole("slider", { name: "Mix" }), "{ArrowRight}");
  rerender(<AppProviders><p>Assets</p></AppProviders>);
  rerender(<AppProviders><StageWorkspace onManageFiles={vi.fn()} onConnection={vi.fn()} /></AppProviders>);
  expect(screen.getByText("MODIFIED")).toBeInTheDocument();
});

it("opens the drawer on a card and folds the chain into chips", async () => {
  render(<AppProviders><StageWorkspace onManageFiles={vi.fn()} onConnection={vi.fn()} /></AppProviders>);
  await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
  expect(screen.getByRole("region", { name: "Tape Delay parameters" })).toBeInTheDocument();
  expect(screen.queryByRole("region", { name: "Signal chain" })).not.toBeInTheDocument();
  await userEvent.keyboard("{Escape}");
  expect(screen.getByRole("region", { name: "Signal chain" })).toBeInTheDocument();
});
```

`AppProviders` is a test helper: `({ children }) => <EditorProvider>{children}</EditorProvider>` with the session mock (the provider stays mounted across `rerender`, like `AppShell`).

- [ ] **Step 2: Run to see them fail**

Run: `npx vitest run src/app src/stage/StageWorkspace.test.tsx`
Expected: FAIL, modules not found.

- [ ] **Step 3: Implement `useAppView.ts`**

```ts
import { useCallback, useEffect, useState } from "react";

import type { AssetKind } from "../api/types";

type View = { view: "edit" | "assets"; assetKind?: AssetKind };
const KINDS: AssetKind[] = ["models", "irs", "reverb-irs"];

function fromHash(hash: string): View {
  const [name, kind] = hash.replace(/^#/, "").split("/");
  if (name !== "assets") return { view: "edit" };
  return { view: "assets", assetKind: KINDS.includes(kind as AssetKind) ? kind as AssetKind : undefined };
}

export function useAppView() {
  const [state, setState] = useState<View>(() => fromHash(window.location.hash));
  useEffect(() => {
    const onHash = () => setState(fromHash(window.location.hash));
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);
  const goto = useCallback((view: "edit" | "assets", kind?: AssetKind) => {
    const hash = view === "assets" ? `#assets${kind ? `/${kind}` : ""}` : "";
    window.history.replaceState(null, "", `${window.location.pathname}${window.location.search}${hash}`);
    setState({ view, assetKind: kind });
  }, []);
  return { ...state, goto };
}
```

- [ ] **Step 4: Implement `ConnectionPill.tsx` and `AppBar.tsx`**

```tsx
// ConnectionPill.tsx
import { useDeviceSession } from "../connection/deviceSession";
import { isHostedCloudRuntime } from "../runtime/platform";

export function ConnectionPill({ onOpen }: { onOpen(): void }) {
  const session = useDeviceSession();
  const hosted = isHostedCloudRuntime();
  const connected = session.status === "connected";
  const address = session.baseUrl.replace(/^https?:\/\//, "");
  return (
    <button type="button" className={`conn conn--${session.status}`} onClick={onOpen} aria-label={`Device: ${connected ? session.device?.deviceName ?? "Connected" : session.status === "error" ? "Connection error" : "Disconnected"}`}>
      <span className="conn__dot" aria-hidden="true" />
      <b>{connected ? session.device?.deviceName ?? "Ardor Pedal" : session.status === "error" ? "Connection error" : "Not connected"}</b>
      {connected && <span className="conn__addr">{address}</span>}
      {!hosted && <span className="conn__http" title="LAN access uses plain HTTP. Use it on trusted networks only.">HTTP</span>}
    </button>
  );
}
```

`AppBar.tsx` renders `<header className="appbar">`: `<span className="mark"><i />Ardor</span>`, a `seg` with **Edit** and **Assets** buttons (`aria-pressed`), the preset title from the editor context (`present.name`, `bankLabel(location.bank) · slotLabel(location.slot)`), `<Tag tone="warn">MODIFIED</Tag>` when `dirty`, a push spacer, `<LiveState />` (hidden in the Assets view), `<ConnectionPill />`, a **Devices** back button when hosted with `onCloudDevices`, and a Settings `IconButton` when not hosted. `app.css`: port `.appbar`, `.mark`, `.conn*`, `.seg`, `.btn*` (as `.button*` overrides) from `mockups/manager-taste/shared.css` with the Task 8 token renames; the `.button` classes of `components/ui.tsx` get the Lamp Black look (square, Saira Condensed 600 15px uppercase, `.button--primary` bone fill with ground text, `.button--danger` danger text on danger rule, `:active { transform: translateY(1px) }`).

- [ ] **Step 5: Implement `StageWorkspace.tsx`**

The component:
- reads `editor = usePresetEditorContext()` and `session = useDeviceSession()`;
- keeps `bank` state (starts at `editor.editor.location.bank`, follows it on load), `drawer: "none" | "block" | "global" | "scenes"`, and `focusedKey?: string`;
- offline: returns the connect card (port the offline JSX of `PresetWorkspace`, restyled, with a **Connect to device** button calling `onConnection`);
- renders `<BankBar>` with `onOpen={editor.selectLocation}` and `onBank={setBank}`;
- stage head: `<h1>Edit</h1>`, a preset-name `<input aria-label="Preset name">` (`set-name`), `{n} blocks`, `<SceneScope />`, `editor.actionError` as an alert, and the preset-level issues (issues without `blockId`) with the `scene-set-required` fix button;
- overview (`drawer === "none"`): `<ChainStage blocks={editor.displayedBlocks} wdw={editor.present.routing === "wdw" ? editor.displayedWdw : undefined} selectedId={editor.editor.selectedBlockId} issuesFor={(id) => issuesForBlock(editor.validation, id)} missingFile={(b) => Boolean(b.asset) && missing.has(b.asset)} sceneOwnsEnabled={(id) => sceneOwns(editor.editingScene, id)} maxed={...} onSelect={(id) => withViewTransition(() => { editor.dispatch({ type: "select-block", blockId: id }); setDrawer("block"); })} onToggle={(b) => editor.editBlockEnabled(b.id, !b.enabled)} onAdd={editor.setAddTarget} onMove={editor.dispatch} />` where `missing = missingPaths(editor.present, inventory)` and `maxed` is the block-limit flag `PresetWorkspace` computes today;
- drawer: `<ChipStrip>` over the same blocks, then `BlockDrawer` (for `drawer === "block"` with `editor.inspectorBlock`), `GlobalDrawer` or `ScenesDrawer`;
- `<EditRail drawer={drawer} focused={...} onOpen={(d) => withViewTransition(() => setDrawer(d))} onAdd={() => editor.setAddTarget({ kind: "top", index: editor.present.blocks.length })} onDone={close} />` where `close` runs `withViewTransition(() => { setDrawer("none"); editor.dispatch({ type: "select-block" }); })`;
- `<ModuleDrawer open={Boolean(editor.addTarget)} where={whereText(editor.addTarget, editor.present)} disabledIds={editor.disabledDefinitions} onOpenChange={(open) => !open && editor.setAddTarget()} onChoose={add} />` where `add` dispatches `add-block`, `add-lane-block` or `add-wdw-block` exactly as `PresetWorkspace` does today (keep its `initialAsset` choice for NAM, cab and IR reverb);
- `<UnsavedChangesDialog open={Boolean(editor.pendingLocation)} busy={editor.saving} onChoice={(c) => void editor.resolveNavigation(c)} />`;
- a `keydown` listener on `window` (skipped while focus is in an input, select, textarea or `[role=slider]`): ⌘Z / Ctrl+Z undo, ⇧⌘Z redo, ⌘S save (`preventDefault`), `B` toggles the selected block, `Delete` / `Backspace` removes it, `A` opens the module drawer after it, `Escape` closes the drawer. ⌘Z and ⌘S also work while a slider has focus.

`whereText(target, preset)`: `"Insert at the start"`, or `Insert after <blockTitle(previous)>, position <index + 1>`, with ` in lane A` / ` in lane B` / ` on the dry lane` / ` on the wet lane` for lane targets.

Keep each helper in its own small module if `StageWorkspace.tsx` grows past 300 lines (`stage/shortcuts.ts` for the key map, `stage/whereText.ts`).

- [ ] **Step 6: Update `AppShell.tsx`**

Wrap everything in `EditorProvider`. Use `useAppView()`. Render `<AppBar>`; for `view === "edit"` render `<StageWorkspace onManageFiles={(kind) => goto("assets", kind)} onConnection={...} />`, for `"assets"` render `<AssetLibrary tone3000DeviceId={tone3000DeviceId} />` (Task 16 replaces it with `AssetsView`). Remove the palette cycle button from the bar (the palette stays in Settings). Keep `ConnectionDialog` and the lazy `SettingsDialog` as they are.

- [ ] **Step 7: Delete the old workspace**

Delete `presets/workspace/PresetWorkspace.tsx` and `PresetWorkspace.test.tsx` once every assertion lives in `StageWorkspace.test.tsx`. Keep `UnsavedChangesDialog.tsx`.

- [ ] **Step 8: Run everything**

Run: `npx vitest run && npx tsc -p . && npx vite build`
Expected: PASS.

- [ ] **Step 9: Look at it**

Run `npm run dev` with the mocked-API Playwright helper from Task 18 not yet in place: connect to a real pedal if one is on the LAN (`http://<pedal>:8080`), else skip to Task 18 and check there. Compare with `mockups/manager-taste/1-stage-drawer.html` at 1440 × 900 and 390 × 844.

- [ ] **Step 10: Commit**

```bash
git add apps/manager/src
git commit -m "feat: replace the preset workspace with the Stage and Drawer editor"
```

---

### Task 15: Asset library model

**Files:**
- Create: `apps/manager/src/assets/uploadQueue.ts`
- Create: `apps/manager/src/assets/assetUsage.ts`
- Create: `apps/manager/src/assets/useAssetLibrary.ts`
- Create: `apps/manager/src/tone3000/useTone3000.ts` (move the TONE3000 flow out of `AssetLibrary.tsx` lines 164-252)
- Test: `apps/manager/src/assets/uploadQueue.test.ts`, `apps/manager/src/assets/assetUsage.test.ts`, `apps/manager/src/assets/useAssetLibrary.test.tsx`

**Interfaces:**
- Produces:
  - Upload queue:

```ts
export type QueueItem = { id: number; file: File; kind: AssetKind; state: "waiting" | "uploading" | "conflict" | "rejected"; replace: boolean };
export type QueueAction =
  | { type: "enqueue"; files: File[]; openKind: AssetKind; existing: Record<AssetKind, string[]> }
  | { type: "start"; id: number }
  | { type: "resolve"; id: number; choice: "replace" | "skip" }
  | { type: "done"; id: number }
  | { type: "failed"; id: number }
  | { type: "dismiss"; id: number };
export function routeKind(filename: string, openKind: AssetKind): AssetKind | undefined;
export function uploadQueue(state: QueueItem[], action: QueueAction): QueueItem[];
export function nextUpload(state: QueueItem[]): QueueItem | undefined; // first "waiting"
```

  - Usage:

```ts
export function usedBy(usage: AssetUsageEntry[] | undefined, path: string): AssetUse[] | undefined; // undefined when usage is unknown
export function missingFiles(usage: AssetUsageEntry[] | undefined, inventory: { models: Asset[]; irs: Asset[]; reverbIrs: Asset[] }): Array<{ path: string; kind: AssetKind; presets: AssetUse[] }>;
```

  - `useAssetLibrary(initialKind?: AssetKind)` returning `{ kind, setKind, query, setQuery, sort, setSort, visible: Asset[], files: Asset[], checked: Set<string>, toggleChecked(id), toggleAll(), openId?, setOpenId(id?), queue: QueueItem[], enqueue(files: File[]), resolve(id, choice), dismiss(id), replaceFile(asset: Asset, file: File), rename(asset: Asset, filename: string): Promise<string | undefined> /* error text */, confirmDelete: boolean, askDelete(ids?: string[]), cancelDelete(), deleteChecked(): Promise<void>, notice?: string, error?: string, usage: AssetUsageEntry[] | undefined, missing }`.

- [ ] **Step 1: Write the failing queue and usage tests**

```ts
// uploadQueue.test.ts
import { describe, expect, it } from "vitest";

import { nextUpload, routeKind, uploadQueue } from "./uploadQueue";

const file = (name: string) => new File(["x"], name);
const existing = { models: ["Brown Sound.nam"], irs: [], "reverb-irs": [] };

describe("uploadQueue", () => {
  it("routes by extension and the open tab", () => {
    expect(routeKind("A.NAM", "irs")).toBe("models");
    expect(routeKind("room.wav", "models")).toBe("irs");
    expect(routeKind("room.wav", "reverb-irs")).toBe("reverb-irs");
    expect(routeKind("notes.txt", "models")).toBeUndefined();
  });

  it("holds a name conflict and rejects other file types", () => {
    const state = uploadQueue([], { type: "enqueue", files: [file("Brown Sound.nam"), file("New.nam"), file("notes.txt")], openKind: "models", existing });
    expect(state.map(({ file: f, state: s }) => [f.name, s])).toEqual([["Brown Sound.nam", "conflict"], ["New.nam", "waiting"], ["notes.txt", "rejected"]]);
    expect(nextUpload(state)?.file.name).toBe("New.nam");
  });

  it("uploads a conflict only after Replace, and drops it on Skip", () => {
    const [conflict] = uploadQueue([], { type: "enqueue", files: [file("Brown Sound.nam")], openKind: "models", existing });
    const replaced = uploadQueue([conflict], { type: "resolve", id: conflict.id, choice: "replace" });
    expect(replaced[0]).toMatchObject({ state: "waiting", replace: true });
    expect(uploadQueue([conflict], { type: "resolve", id: conflict.id, choice: "skip" })).toEqual([]);
  });

  it("removes an item when it is done", () => {
    const state = uploadQueue([], { type: "enqueue", files: [file("New.nam")], openKind: "models", existing });
    const started = uploadQueue(state, { type: "start", id: state[0].id });
    expect(started[0].state).toBe("uploading");
    expect(uploadQueue(started, { type: "done", id: state[0].id })).toEqual([]);
  });
});
```

```ts
// assetUsage.test.ts
import { expect, it } from "vitest";

import { missingFiles, usedBy } from "./assetUsage";

const usage = [
  { path: "models/Clean.nam", presets: [{ bank: 0, slot: 0, name: "Clean" }] },
  { path: "models/Gone.nam", presets: [{ bank: 1, slot: 1, name: "Doom" }] },
];
const inventory = { models: [{ id: "c", kind: "model" as const, filename: "Clean.nam", path: "models/Clean.nam", sizeBytes: 1 }], irs: [], reverbIrs: [] };

it("finds the presets that use a file, and says when usage is unknown", () => {
  expect(usedBy(usage, "models/Clean.nam")).toEqual([{ bank: 0, slot: 0, name: "Clean" }]);
  expect(usedBy(usage, "models/Other.nam")).toEqual([]);
  expect(usedBy(undefined, "models/Clean.nam")).toBeUndefined();
});

it("lists referenced paths with no file on the pedal", () => {
  expect(missingFiles(usage, inventory)).toEqual([{ path: "models/Gone.nam", kind: "models", presets: [{ bank: 1, slot: 1, name: "Doom" }] }]);
});
```

- [ ] **Step 2: Run to see them fail**

Run: `npx vitest run src/assets/uploadQueue.test.ts src/assets/assetUsage.test.ts`
Expected: FAIL, modules not found.

- [ ] **Step 3: Implement**

```ts
// uploadQueue.ts
import type { AssetKind } from "../api/types";

export type QueueItem = { id: number; file: File; kind: AssetKind; state: "waiting" | "uploading" | "conflict" | "rejected"; replace: boolean };
export type QueueAction =
  | { type: "enqueue"; files: File[]; openKind: AssetKind; existing: Record<AssetKind, string[]> }
  | { type: "start"; id: number }
  | { type: "resolve"; id: number; choice: "replace" | "skip" }
  | { type: "done"; id: number }
  | { type: "failed"; id: number }
  | { type: "dismiss"; id: number };

let nextId = 0;

/** .nam goes to models; .wav goes to the open IR tab, else cabinet IRs. */
export function routeKind(filename: string, openKind: AssetKind): AssetKind | undefined {
  const name = filename.toLowerCase();
  if (name.endsWith(".nam")) return "models";
  if (name.endsWith(".wav")) return openKind === "reverb-irs" ? "reverb-irs" : "irs";
  return undefined;
}

export function uploadQueue(state: QueueItem[], action: QueueAction): QueueItem[] {
  switch (action.type) {
    case "enqueue":
      return [...state, ...action.files.map((file): QueueItem => {
        const kind = routeKind(file.name, action.openKind);
        const exists = kind !== undefined && action.existing[kind].some((name) => name.toLowerCase() === file.name.toLowerCase());
        return { id: ++nextId, file, kind: kind ?? action.openKind, state: !kind ? "rejected" : exists ? "conflict" : "waiting", replace: false };
      })];
    case "start":
      return state.map((item) => (item.id === action.id ? { ...item, state: "uploading" } : item));
    case "resolve":
      return action.choice === "skip"
        ? state.filter((item) => item.id !== action.id)
        : state.map((item) => (item.id === action.id ? { ...item, state: "waiting", replace: true } : item));
    case "done":
    case "dismiss":
      return state.filter((item) => item.id !== action.id);
    case "failed":
      return state.map((item) => (item.id === action.id ? { ...item, state: "rejected" } : item));
  }
}

export const nextUpload = (state: QueueItem[]): QueueItem | undefined => state.find((item) => item.state === "waiting");
```

A `failed` item shows as a rejection row with the server message kept in the hook's `error` state.

```ts
// assetUsage.ts
import type { Asset, AssetKind, AssetUse, AssetUsageEntry } from "../api/types";

const kindOfPath = (path: string): AssetKind | undefined =>
  path.startsWith("models/") ? "models" : path.startsWith("irs/") ? "irs" : path.startsWith("reverb-irs/") ? "reverb-irs" : undefined;

export function usedBy(usage: AssetUsageEntry[] | undefined, path: string): AssetUse[] | undefined {
  if (!usage) return undefined;
  return usage.find((entry) => entry.path === path)?.presets ?? [];
}

export function missingFiles(usage: AssetUsageEntry[] | undefined, inventory: { models: Asset[]; irs: Asset[]; reverbIrs: Asset[] }) {
  if (!usage) return [];
  const known = new Set([...inventory.models, ...inventory.irs, ...inventory.reverbIrs].map(({ path }) => path));
  return usage.flatMap((entry) => {
    const kind = kindOfPath(entry.path);
    return kind && !known.has(entry.path) ? [{ path: entry.path, kind, presets: entry.presets }] : [];
  });
}
```

- [ ] **Step 4: Run the model tests**

Run: `npx vitest run src/assets/uploadQueue.test.ts src/assets/assetUsage.test.ts`
Expected: PASS.

- [ ] **Step 5: Write the failing hook test**

`useAssetLibrary.test.tsx` mocks `useDeviceSession()` with `models: [Brown Sound.nam, Clean.nam]`, `uploadAsset: vi.fn(async () => ({}))`, `refreshAssets: vi.fn()`, `client: { renameAsset: vi.fn(async () => ({ asset: { filename: "Clean v2.nam" }, updatedPresetCount: 2 })), deleteAsset: vi.fn() }`, `current` and `selectLocation`:

```tsx
it("uploads new files one by one and waits on a conflict", async () => {
  const { result } = renderHook(() => useAssetLibrary("models"));
  act(() => result.current.enqueue([new File(["x"], "Brown Sound.nam"), new File(["x"], "New.nam")]));
  await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "New.nam" }), false));
  expect(session.uploadAsset).toHaveBeenCalledTimes(1);
  act(() => result.current.resolve(result.current.queue[0].id, "replace"));
  await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Brown Sound.nam" }), true));
});

it("renames and reports the updated presets", async () => {
  const { result } = renderHook(() => useAssetLibrary("models"));
  await act(async () => { await result.current.rename(session.models[1], "Clean v2.nam"); });
  expect(result.current.notice).toBe("Renamed to Clean v2.nam. 2 saved presets use the new name.");
});

it("refuses a name without the right ending", async () => {
  const { result } = renderHook(() => useAssetLibrary("models"));
  let error: string | undefined;
  await act(async () => { error = await result.current.rename(session.models[1], "Clean v2"); });
  expect(error).toBe("The name must end in .nam.");
  expect(session.client.renameAsset).not.toHaveBeenCalled();
});

it("deletes the checked files after the confirmation", async () => {
  const { result } = renderHook(() => useAssetLibrary("models"));
  act(() => result.current.askDelete([session.models[0].id]));
  expect(result.current.confirmDelete).toBe(true);
  await act(() => result.current.deleteChecked());
  expect(session.client.deleteAsset).toHaveBeenCalledWith("models", session.models[0].id);
  expect(result.current.confirmDelete).toBe(false);
});
```

- [ ] **Step 6: Implement `useAssetLibrary.ts` and `useTone3000.ts`**

`useAssetLibrary`:
- `const [queue, dispatchQueue] = useReducer(uploadQueue, [])`;
- `enqueue(files)` dispatches `enqueue` with `openKind: kind` and `existing` from `session.models / irs / reverbIrs` file names, then closes the open file (`setOpenId(undefined)`) and switches `kind` to the first routed kind;
- an effect uploads `nextUpload(queue)` when no item is `uploading`: `start`, then `await session.uploadAsset(item.kind, item.file, item.replace)`, `await session.refreshAssets(item.kind)`, `done` and `setNotice(\`${item.file.name} uploaded to ${KIND_LABELS[item.kind]}.\`)`; on error `failed` and `setError(message)`;
- `replaceFile(asset, file)` enqueues `new File([file], asset.filename)` as a pre-resolved replace (`enqueue` then `resolve(id, "replace")`);
- `rename(asset, filename)`: trims; returns `"Type a file name."`, `` `The name must end in ${ext}.` `` or, on `ArdorApiError` code `asset_exists`, `"A file with that name is already on the pedal."`; on success calls `session.refreshAssets(kind)`, reloads the open preset (`session.selectLocation(session.current.location)`) like today, sets the notice `Renamed to ${response.asset.filename}.${count ? \` ${count} saved preset${count === 1 ? "" : "s"} use the new name.\` : ""}` and returns `undefined`;
- `askDelete(ids?)` sets `checked` to `ids` when given and `confirmDelete` true; `deleteChecked()` deletes each checked id with `session.client.deleteAsset(kind, id)`, collects failures into `error` (`Could not delete ${names}.`), refreshes, clears `checked`, and sets the notice `${n === 1 ? name : \`${n} files\`} deleted from the pedal.`;
- `visible` filters by `query` on the file name and sorts by `sort` ("name" by filename, "size" descending, "used" by `usedBy(...).length` descending, then name);
- `usage = session.assetUsage`, `missing = missingFiles(session.assetUsage, inventory)`.

`useTone3000(deviceId?: string)`: move `launchTone3000`, `browseTone3000`, `continueToTone3000`, `cancelTone3000Flow`, `installTone3000Model` and their state from `AssetLibrary.tsx` unchanged; return `{ phase, selection, selectedModelId, setSelectedModelId, available, browse, continueFlow, cancel, install, error }`, where `available` is the same `tone3000Available` expression for `kind === "models"`.

- [ ] **Step 7: Run the tests**

Run: `npx vitest run src/assets && npx tsc -p .`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add apps/manager/src/assets apps/manager/src/tone3000
git commit -m "feat: add asset library model with upload queue and usage"
```

---

### Task 16: Assets view

**Files:**
- Create: `apps/manager/src/assets/AssetsView.tsx`, `apps/manager/src/assets/KindBar.tsx`, `apps/manager/src/assets/FileList.tsx`, `apps/manager/src/assets/FileDrawer.tsx`, `apps/manager/src/assets/AssetsRail.tsx`, `apps/manager/src/assets/assets.css`
- Create: `apps/manager/src/app/useFileDrop.ts`
- Modify: `apps/manager/src/app/AppShell.tsx` (render `AssetsView`; window file drop)
- Delete: `apps/manager/src/assets/AssetLibrary.tsx` after porting `AssetLibrary.test.tsx` to `AssetsView.test.tsx`
- Test: `apps/manager/src/assets/AssetsView.test.tsx`, `apps/manager/src/app/useFileDrop.test.ts`

**Interfaces:**
- Consumes: `useAssetLibrary`, `useTone3000`, `Tone3000Dialog`, `usePresetEditorContext()` (for Try in preset), `PresetTile`, `fileSize`, `fileStem`, `usedBy`, `missingFiles`, `useAppView().goto`.
- Produces:
  - `AssetsView({ initialKind?: AssetKind; tone3000DeviceId?: string; pendingFiles?: File[]; onFilesTaken(): void; onOpenPreset(location: PresetLocation, blockPath?: string): void })`
  - `useFileDrop(onFiles: (files: File[]) => void): boolean` (returns `dragging`; listens on `window` for `dragenter`/`dragover`/`dragleave`/`drop` with files only)
- Layout and copy follow `mockups/manager-taste/assets-view.js` and `assets-view.css`: kind tiles (code square, label, `.nam · for NAM Model`, count, total size), summary (`N files on the pedal`, size, `N missing`), toolbar (search `Find a file in NAM models`, sort Name / Size / Most used, hint `Drop .nam files anywhere to upload`), one compact warn row per missing reference (`<b>Doom Fuzz</b> needs <b>Fuzz Stack.nam</b>, which is not on the pedal.` with **Upload** and **Pick another**), queue rows (indeterminate bar while uploading; Replace / Skip for a conflict; `notes.txt is not a .nam or .wav file. The pedal takes NAM models and WAV impulse responses.` with **Dismiss**), the list (check, name + `.nam`, size, used-in chips or `Not used`; the Used in column is hidden when `usage` is undefined), the file drawer (tag, name, `models/Glass Clean.nam · 403 KB`, **Try in <preset>**, **Rename**, **Replace file**, **Delete**, Close; inline rename form; `Used in N presets` tiles or `Not used in a preset`), and the rail (**Upload .nam**, **Browse TONE3000** for models, `N selected` with **Delete N** and **Clear**, **Back to edit**; while `confirmDelete`, the rail holds the confirmation: `Delete <file> from the pedal? <presets> use it. Those presets stay saved but cannot load until you pick another file. You cannot undo this.` with **Cancel** and **Delete file**).

- [ ] **Step 1: Write the failing tests**

`useFileDrop.test.ts`:

```ts
import { act, renderHook } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { useFileDrop } from "./useFileDrop";

function dragEvent(type: string, files: File[] = []) {
  const event = new Event(type, { bubbles: true, cancelable: true }) as DragEvent;
  Object.defineProperty(event, "dataTransfer", { value: { types: ["Files"], files } });
  return event;
}

it("reports dragging and hands over dropped files", () => {
  const onFiles = vi.fn();
  const { result } = renderHook(() => useFileDrop(onFiles));
  act(() => { window.dispatchEvent(dragEvent("dragenter")); });
  expect(result.current).toBe(true);
  const file = new File(["x"], "A.nam");
  act(() => { window.dispatchEvent(dragEvent("drop", [file])); });
  expect(onFiles).toHaveBeenCalledWith([file]);
  expect(result.current).toBe(false);
});
```

`AssetsView.test.tsx` (session mock with models `Clean.nam` used by preset 0/0 "Glass Cathedral" and `Plexi.nam` unused, `assetUsage` accordingly plus `models/Gone.nam` used by 1/1 "Doom", and an `EditorProvider` with a preset whose NAM block uses `models/Clean.nam`):

```tsx
it("shows kind tiles, files with their presets, and a missing file row", () => {
  renderAssets();
  expect(screen.getByRole("button", { name: /NAM models/ })).toHaveAttribute("aria-pressed", "true");
  expect(screen.getByRole("button", { name: "Clean" })).toBeInTheDocument();
  expect(screen.getByText("Glass Cathedral")).toBeInTheDocument();
  expect(screen.getByText("Not used")).toBeInTheDocument();
  expect(screen.getByText(/needs/)).toHaveTextContent("Doom needs Gone.nam, which is not on the pedal.");
});

it("tries a file in the open preset as an undoable edit", async () => {
  const { editor } = renderAssets();
  await userEvent.click(screen.getByRole("button", { name: "Plexi" }));
  await userEvent.click(screen.getByRole("button", { name: /Try in/ }));
  expect(editor().present.blocks.find((b) => b.type === "nam")!.asset).toBe("models/Plexi.nam");
  act(() => editor().dispatch({ type: "undo" }));
  expect(editor().present.blocks.find((b) => b.type === "nam")!.asset).toBe("models/Clean.nam");
});

it("asks in the rail before it deletes", async () => {
  renderAssets();
  await userEvent.click(screen.getByRole("button", { name: "Delete Clean.nam" }));
  const rail = screen.getByRole("alertdialog", { name: "Confirm delete" });
  expect(rail).toHaveTextContent("Glass Cathedral use it");
  expect(rail).toHaveTextContent("You cannot undo this.");
  await userEvent.click(within(rail).getByRole("button", { name: "Delete file" }));
  expect(session.client.deleteAsset).toHaveBeenCalled();
});

it("hides the Used in column when the pedal cannot report usage", () => {
  session.assetUsage = undefined;
  renderAssets();
  expect(screen.queryByText("Used in")).not.toBeInTheDocument();
});
```

Port the remaining assertions of `AssetLibrary.test.tsx` (upload by file picker, extension check, conflict Replace/Skip, rename, bulk delete, TONE3000 availability) with the new names.

- [ ] **Step 2: Run to see them fail**

Run: `npx vitest run src/assets/AssetsView.test.tsx src/app/useFileDrop.test.ts`
Expected: FAIL, modules not found.

- [ ] **Step 3: Implement `useFileDrop.ts`**

```ts
import { useEffect, useRef, useState } from "react";

const hasFiles = (event: DragEvent) => Array.from(event.dataTransfer?.types ?? []).includes("Files");

/** Files dropped anywhere in the window. Returns true while files are over the window. */
export function useFileDrop(onFiles: (files: File[]) => void): boolean {
  const [dragging, setDragging] = useState(false);
  const depth = useRef(0);
  const handler = useRef(onFiles);
  handler.current = onFiles;
  useEffect(() => {
    const enter = (event: DragEvent) => { if (!hasFiles(event)) return; depth.current += 1; setDragging(true); };
    const leave = () => { depth.current = Math.max(0, depth.current - 1); if (depth.current === 0) setDragging(false); };
    const over = (event: DragEvent) => { if (hasFiles(event)) event.preventDefault(); };
    const drop = (event: DragEvent) => {
      if (!hasFiles(event)) return;
      event.preventDefault();
      depth.current = 0;
      setDragging(false);
      handler.current(Array.from(event.dataTransfer?.files ?? []));
    };
    window.addEventListener("dragenter", enter);
    window.addEventListener("dragleave", leave);
    window.addEventListener("dragover", over);
    window.addEventListener("drop", drop);
    return () => {
      window.removeEventListener("dragenter", enter);
      window.removeEventListener("dragleave", leave);
      window.removeEventListener("dragover", over);
      window.removeEventListener("drop", drop);
    };
  }, []);
  return dragging;
}
```

- [ ] **Step 4: Implement the view**

Split by responsibility, each file under 250 lines:
- `KindBar.tsx`: the three kind buttons and the summary (hide Reverb IRs when `!session.supportsReverbIrs`, like today).
- `FileList.tsx`: toolbar, missing rows, queue rows, the list; props from `useAssetLibrary`.
- `FileDrawer.tsx`: the open file; Try in preset finds `editor.allBlocks.find((b) => ({ nam: "models", cab: "irs", irreverb: "reverb-irs" })[b.type] === kind)` and dispatches `{ type: "set-block-asset", blockId, asset: file.path }`; the button is disabled with the title `<preset> has no <block name> block` when none exists, and reads `In <preset>` when that block already uses the file. Used-in tiles use `PresetTile` with `onOpen={() => onOpenPreset({ bank, slot })}`.
- `AssetsRail.tsx`: the rail, including the `role="alertdialog"` `aria-label="Confirm delete"` confirmation state.
- `AssetsView.tsx`: composes them; a hidden `<input type="file" multiple>` created once with `useRef` and kept outside the parts that re-render per upload tick (set `accept` to `.nam` or `.wav` per kind when opened); calls `enqueue(pendingFiles)` once when `pendingFiles` is given, then `onFilesTaken()`; renders `Tone3000Dialog` from `useTone3000` as today; shows `notice` as a status line and `error` as an alert.
- `assets.css`: port `mockups/manager-taste/assets-view.css` with the Task 8 token renames, and the indeterminate bar: `.aq__bar i { width: 30%; animation: aq-run 1.1s var(--ease) infinite; } @keyframes aq-run { from { transform: translateX(-100%); } to { transform: translateX(340%); } } @media (prefers-reduced-motion: reduce) { .aq__bar i { animation: none; width: 100%; opacity: .5; } }`.

`AppShell`: `const [pendingFiles, setPendingFiles] = useState<File[]>();` and `const dragging = useFileDrop((files) => { setPendingFiles(files); goto("assets"); });`. Render the drop overlay when `dragging` (`Drop to upload`, `.nam files go to NAM models. .wav files go to Cabinet IRs.`). For `view === "assets"` render `<AssetsView initialKind={assetKind} tone3000DeviceId={tone3000DeviceId} pendingFiles={pendingFiles} onFilesTaken={() => setPendingFiles(undefined)} onOpenPreset={(location) => { editor.selectLocation(location); goto("edit"); }} />`. `AppShell` needs the editor context for `onOpenPreset`, so move the body of `AppShell` into an inner `Shell` component rendered inside `EditorProvider`.

- [ ] **Step 5: Run everything**

Run: `npx vitest run && npx tsc -p . && npx vite build`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add apps/manager/src
git commit -m "feat: add Assets view with usage, missing files and drop upload"
```

---

### Task 17: Dialogs, clean-up and docs

**Files:**
- Modify: `apps/manager/src/styles.css` (remove rules for deleted components; restyle dialogs)
- Modify: `apps/manager/src/connection/ConnectionDialog.tsx`, `apps/manager/src/settings/SettingsDialog.tsx`, `apps/manager/src/presets/workspace/UnsavedChangesDialog.tsx`, `apps/manager/src/tone3000/Tone3000Dialog.tsx`, `apps/manager/src/localAuth/LocalDeviceManager.tsx`, `apps/manager/src/cloud/HostedManager.tsx` (class names and copy only)
- Delete: `apps/manager/src/presets/chain/ChainCanvas.tsx`, `WdwRoutingCanvas.tsx`, `ChainCanvas.test.tsx`, `apps/manager/src/presets/inspector/BlockInspector.tsx`, `BlockInspector.test.tsx`, `apps/manager/src/presets/browser/PresetSidebar.tsx`, `apps/manager/src/presets/block-browser/BlockBrowser.tsx`, `BlockBrowser.test.tsx`, `apps/manager/src/components/ParameterSlider.tsx` (after moving any remaining import to `effects/display.ts` or `TravelScale`)
- Modify: `DESIGN.md` (add a "Manager" section)
- Test: existing dialog tests (`ConnectionDialog.test.tsx`, `SettingsDialog.test.tsx`, `LocalDeviceManager.test.tsx`, `HostedManager.test.tsx`)

- [ ] **Step 1: Port the remaining inspector and browser assertions**

Before deleting, check each deleted test file for behaviour not yet covered (asset picker listing reverb IRs plus the cab IR in use, unknown block inspector, Dual Amp lane controls, mode change, EQ band edit, disabled reasons). Add the missing ones to `BlockDrawer.test.tsx` or `ModuleDrawer.test.tsx`. Example for the reverb IR rule `BlockInspector` has today:

```tsx
it("lists reverb IRs, plus the cab IR the block already uses", () => {
  session.reverbIrs = [{ id: "r", kind: "ir", filename: "Chapel.wav", path: "reverb-irs/Chapel.wav", sizeBytes: 1 }];
  session.irs = [{ id: "c", kind: "ir", filename: "Room.wav", path: "irs/Room.wav", sizeBytes: 1 }];
  session.current.preset.blocks = [{ id: "v1", type: "irreverb", enabled: true, asset: "irs/Room.wav", params: { mix: 0.3 } }];
  render(<EditorProvider><Harness id="v1" /></EditorProvider>);
  expect(screen.getByRole("radio", { name: "Chapel" })).toBeInTheDocument();
  expect(screen.getByRole("radio", { name: "Room" })).toHaveAttribute("aria-checked", "true");
});
```

If `FilePicker` does not do this yet, extend it: for `kind === "reverb-irs"`, list `[...session.reverbIrs, ...session.irs.filter(({ path }) => path === value)]`.

- [ ] **Step 2: Delete the old components and run the suite**

Run: `npx vitest run && npx tsc -p .`
Expected: PASS, with no import of a deleted file left.

- [ ] **Step 3: Restyle the dialogs**

Keep structure and behaviour. Remove `eyebrow` labels (a banned pattern in the Lamp Black rules). Headings become Saira Condensed 700 uppercase. Buttons use the `Button` component variants. The Settings "Panel palette" section keeps the four palettes and now says: `Slate uses the pedal's Lamp Black values.` Copy follows STE (for example, the connection dialog error for 401 stays specific: `The pedal refused the login. Check the user name and password.`).

- [ ] **Step 4: Remove dead CSS**

Delete every rule in `styles.css` whose selectors no component uses any more. Check with:

```bash
cd apps/manager/src
for sel in $(grep -o '^\.[a-z][a-z0-9_-]*' styles.css | sort -u | sed 's/^\.//'); do grep -rq --include=*.tsx "$sel" . || echo "unused: .$sel"; done
```

Expected after clean-up: no `unused:` lines (or only selectors that are built dynamically; check those by hand).

- [ ] **Step 5: Document the manager design**

Add a `## Manager` section to `DESIGN.md` after "Components":
- surfaces: app bar, bank bar, stage (overview and drawer), rail, Assets view;
- rules: one lamp, family colours, device caps (`labelForBlockType`), card value pairs, chip strip, view transition, rail context, HTTP pill, fixed limiter;
- tokens: the `paletteVariables` names and the new ones (`--plate-hi`, `--lamp-ink`, `--danger-rule`, `--lift`, `--warn-ink`);
- phone rules;
- source: `mockups/manager-taste/1-stage-drawer.html`.

- [ ] **Step 6: Commit**

```bash
git add -A apps/manager DESIGN.md
git commit -m "refactor: remove the old manager components and restyle dialogs"
```

---

### Task 18: End-to-end tests and release checks

**Files:**
- Create: `apps/manager/playwright.config.ts`
- Create: `apps/manager/e2e/mockApi.ts`, `apps/manager/e2e/stage.spec.ts`, `apps/manager/e2e/assets.spec.ts`
- Modify: `.github/workflows/*` only if the manager CI job runs `npm test` but not `test:e2e` and the team wants E2E in CI (ask the user; do not add it silently)

**Interfaces:**
- Consumes: the whole app, run with `VITE_DEVICE_HOSTED=true` so `LocalDeviceManager` connects to the page origin.
- Produces: `mockApi(page, state)` that answers `GET /api/auth/status` (`{ state: "disabled", insecureTransport: true }`), `/api/device`, `/api/assets/{kind}`, `POST /api/assets/{kind}`, `PATCH|DELETE /api/assets/{kind}/{id}`, `/api/assets/usage`, `/api/presets`, `GET|PUT /api/presets/banks/{b}/slots/{s}`, `POST .../apply` and `/api/runtime/apply/{id}`, from an in-memory state object so saves and uploads show up in later reads.

- [ ] **Step 1: Add the Playwright config**

```ts
import { defineConfig, devices } from "@playwright/test";

export default defineConfig({
  testDir: "./e2e",
  use: { baseURL: "http://localhost:4173", trace: "retain-on-failure" },
  projects: [
    { name: "desktop", use: { ...devices["Desktop Chrome"], viewport: { width: 1440, height: 900 } } },
    { name: "phone", use: { ...devices["Pixel 7"] } },
  ],
  webServer: { command: "VITE_DEVICE_HOSTED=true npx vite --port 4173 --strictPort", url: "http://localhost:4173", reuseExistingServer: !process.env.CI },
});
```

- [ ] **Step 2: Write the mock API**

`e2e/mockApi.ts` exports `type MockState = { presets: Record<string, Preset>; assets: Record<AssetKind, Asset[]>; active: { bank: number; slot: number } }`, `defaultState()` (the "Glass Cathedral" nine-block preset from `mockups/manager-taste/data.js` translated to the real `Preset` schema with ids `gate-1` … `verb-1`, NAM `models/Glass Clean.nam`, cab `irs/Open Back 2x12.wav`; a second preset "Doom Fuzz" in bank 0 slot 1 using `models/Fuzz Stack.nam`; models `Glass Clean.nam`, `Brown Sound.nam`, `Plexi Lead.nam`; one IR), and `mockApi(page, state)` which calls `page.route("**/api/**", handler)` and implements the routes listed above against `state`. Asset usage is computed from `state.presets` with the same walk as `assetRefs`.

- [ ] **Step 3: Write the stage flows**

```ts
import { expect, test } from "@playwright/test";

import { defaultState, mockApi } from "./mockApi";

test.beforeEach(async ({ page }) => { await mockApi(page, defaultState()); await page.goto("/"); });

test("reorders a block by dragging its cap and saves", async ({ page, isMobile }) => {
  test.skip(isMobile, "Mouse drag; touch drag is covered by long-press below");
  const rat = page.getByRole("group", { name: /RAT Distortion/ });
  const cab = page.getByRole("group", { name: /Open Back 2x12/ });
  const from = await rat.locator(".blk__cap").boundingBox();
  const to = await cab.boundingBox();
  await page.mouse.move(from!.x + 20, from!.y + 10);
  await page.mouse.down();
  await page.mouse.move(to!.x + to!.width * 0.8, to!.y + 40, { steps: 12 });
  await page.mouse.up();
  const names = await page.getByRole("region", { name: "Signal chain" }).getByRole("group").evaluateAll((els) => els.map((el) => el.getAttribute("aria-label")!.split(",")[0]));
  expect(names.indexOf("RAT Distortion")).toBeGreaterThan(names.indexOf("Open Back 2x12"));
  await expect(page.getByText("MODIFIED")).toBeVisible();
  await page.getByRole("button", { name: "Save" }).click();
  await expect(page.getByText("MODIFIED")).toBeHidden();
});

test("edits a value in the drawer and undoes the whole drag at once", async ({ page }) => {
  await page.getByRole("group", { name: /Tape Delay/ }).click();
  const mix = page.getByRole("slider", { name: "Mix" });
  const before = await mix.getAttribute("aria-valuenow");
  const box = await mix.boundingBox();
  await page.mouse.move(box!.x + 5, box!.y + box!.height / 2);
  await page.mouse.down();
  await page.mouse.move(box!.x + box!.width - 5, box!.y + box!.height / 2, { steps: 10 });
  await page.mouse.up();
  await page.keyboard.press("ControlOrMeta+z");
  await expect(mix).toHaveAttribute("aria-valuenow", before!);
});

test("moves a block with the keyboard", async ({ page }) => {
  const gate = page.getByRole("group", { name: /Noise Gate/ });
  await gate.focus();
  await page.keyboard.press("Alt+ArrowRight");
  const names = await page.getByRole("region", { name: "Signal chain" }).getByRole("group").evaluateAll((els) => els.map((el) => el.getAttribute("aria-label")!.split(",")[0]));
  expect(names.slice(0, 2)).toEqual(["Compressor", "Noise Gate"]);
});

test("keeps the draft when the Assets view opens and closes", async ({ page }) => {
  await page.getByRole("group", { name: /Tape Delay/ }).click();
  await page.getByRole("slider", { name: "Mix" }).press("ArrowRight");
  await page.getByRole("button", { name: "Assets" }).click();
  await page.getByRole("button", { name: "Edit" }).click();
  await expect(page.getByText("MODIFIED")).toBeVisible();
});
```

- [ ] **Step 4: Write the asset flows**

```ts
import { expect, test } from "@playwright/test";

import { defaultState, mockApi } from "./mockApi";

test.beforeEach(async ({ page }) => { await mockApi(page, defaultState()); await page.goto("/#assets"); });

test("uploads with a name conflict and rejects a wrong file type", async ({ page }) => {
  await page.locator('input[type="file"]').setInputFiles([
    { name: "Brown Sound.nam", mimeType: "application/octet-stream", buffer: Buffer.from("nam") },
    { name: "Liquid Lead.nam", mimeType: "application/octet-stream", buffer: Buffer.from("nam") },
    { name: "notes.txt", mimeType: "text/plain", buffer: Buffer.from("hi") },
  ]);
  await expect(page.getByText("Brown Sound.nam is already on the pedal.", { exact: false })).toBeVisible();
  await expect(page.getByText("notes.txt is not a .nam or .wav file.", { exact: false })).toBeVisible();
  await expect(page.getByRole("button", { name: "Liquid Lead" })).toBeVisible();
  await page.getByRole("button", { name: "Replace" }).click();
  await expect(page.getByText("Brown Sound.nam uploaded to NAM models.")).toBeVisible();
});

test("renames a file and reports the presets", async ({ page }) => {
  await page.getByRole("button", { name: "Glass Clean" }).click();
  await page.getByRole("button", { name: "Rename" }).click();
  await page.getByRole("textbox", { name: "New file name" }).fill("Glass Clean v2.nam");
  await page.keyboard.press("Enter");
  await expect(page.getByText("Renamed to Glass Clean v2.nam. 1 saved preset uses the new name.")).toBeVisible();
});

test("asks before it deletes, in the rail", async ({ page }) => {
  await page.getByRole("button", { name: "Delete Plexi Lead.nam" }).click();
  const confirm = page.getByRole("alertdialog", { name: "Confirm delete" });
  await expect(confirm).toContainText("You cannot undo this.");
  await confirm.getByRole("button", { name: "Delete file" }).click();
  await expect(page.getByRole("button", { name: "Plexi Lead" })).toBeHidden();
});

test("shows a missing file and opens the preset from it", async ({ page }) => {
  await expect(page.getByText("Doom Fuzz needs Fuzz Stack.nam, which is not on the pedal.")).toBeVisible();
  await page.getByRole("button", { name: "Pick another" }).click();
  await expect(page.getByRole("textbox", { name: "Preset name" })).toHaveValue("Doom Fuzz");
});
```

Make the rename notice agree with the hook: singular `1 saved preset uses the new name.`, plural `2 saved presets use the new name.` (update Task 15's hook and test if they differ).

- [ ] **Step 5: Run the E2E suite**

```bash
cd apps/manager
npx playwright install chromium
npx playwright test
```

Expected: all tests PASS in both projects (the mouse-drag test is skipped on the phone project).

- [ ] **Step 6: Compare with the mockup**

Take screenshots with the mock API at 1440 × 900 and 390 × 844 for: Edit overview, block drawer (Tape Delay), Global drawer, module drawer, Assets list, file drawer, delete confirmation. Put them next to the same states of `mockups/manager-taste/1-stage-drawer.html` (served with `python3 -m http.server 8765` from `mockups/manager-taste/`). Fix layout gaps in one batch, then check once more. Differences listed in the spec's "Decisions" table are expected.

- [ ] **Step 7: Release checks**

```bash
cd apps/manager && npx vitest run && npx tsc -p . && npm run build && npm run build:hosted && npm run build:device
cd ../../services/managerd && go test ./...
```

Expected: all PASS. `build:device` writes into `services/managerd/internal/webui/dist`; check `git status` shows the rebuilt bundle, and commit it only if the repository normally commits that folder (check `git log --oneline -3 -- services/managerd/internal/webui/dist`).

- [ ] **Step 8: Commit**

```bash
git add apps/manager/playwright.config.ts apps/manager/e2e
git commit -m "test: add manager end-to-end tests with a mocked pedal API"
```

---

## Self-review notes

- **Spec coverage:** layout (Tasks 10, 11, 12, 14, 16), decisions table (LiveState in Task 12, no MIDI learn, `BANK 00` in Task 6, disabled module reasons in Task 13, indeterminate upload in Task 16, usage endpoint in Tasks 1, 2 and 15), behaviour rules (one lamp and tokens in Task 6, caps in Task 6, value pairs in Task 8, reorder in Tasks 9 and 11, undo in Tasks 3 and 7, scene rule in Task 11, draft across views in Tasks 5 and 14, assets rules in Tasks 15 and 16, HTTP in Task 14, limiter in Task 12), acceptance (Tasks 17 and 18).
- **Type names used across tasks:** `AddTarget` and `PresetEditor` (Task 5); `Family`, `familyOf`, `capFor`, `bankLabel`, `slotLabel`, `splitDisplay`, `fileSize`, `fileStem`, `displayValue` (Task 6); `TravelScale`, `ChoiceStrip` (Task 7); `BlockCard`, `blockTitle`, `mainControls`, `stripCode`, `MODULE_CODES`, `eqStateFor`, `responsePath` (Task 8); `ListId`, `resolveDrop`, `ChainStage` (Task 9); `assetRefs`, `missingPaths`, `useBankPresets`, `PresetTile`, `BankBar` (Task 10); `withViewTransition`, `SceneScope`, `FilePicker`, `ChipStrip`, `BlockDrawer`, `clear-scene` (Task 11); `LiveState`, `EditRail`, `GlobalDrawer`, `ScenesDrawer` (Task 12); `ModuleDrawer` (Task 13); `useAppView`, `ConnectionPill`, `AppBar`, `StageWorkspace` (Task 14); `uploadQueue`, `routeKind`, `nextUpload`, `usedBy`, `missingFiles`, `useAssetLibrary`, `useTone3000` (Task 15); `useFileDrop`, `AssetsView` (Task 16).
- **Known judgement calls for the implementer:** helper names in existing test files (Task 2 Step 5) must be checked against the code; the plan says what to keep fixed (the assertions).
