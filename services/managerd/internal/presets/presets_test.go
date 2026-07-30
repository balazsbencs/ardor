package presets

import (
	"os"
	"path/filepath"
	"testing"
)

func validPreset() Preset {
	return Preset{
		"version": float64(1),
		"name":    "Clean",
		"routing": "serial",
		"global": map[string]any{
			"inputGainDb":   float64(0),
			"outputGainDb":  float64(0),
			"safetyLimitDb": float64(-1),
		},
		"blocks": []any{
			map[string]any{
				"id":      "block-1",
				"type":    "nam",
				"enabled": true,
				"asset":   "models/clean.nam",
				"params":  map[string]any{},
				"future":  "preserve",
			},
		},
		"futureRoot": true,
	}
}

func TestValidatePreset(t *testing.T) {
	if err := Validate(validPreset()); err != nil {
		t.Fatal(err)
	}

	badRouting := validPreset()
	badRouting["routing"] = "parallel"
	if err := Validate(badRouting); err == nil {
		t.Fatal("parallel routing should fail")
	}

	badAsset := validPreset()
	badAsset["blocks"].([]any)[0].(map[string]any)["asset"] = "../escape.nam"
	if err := Validate(badAsset); err == nil {
		t.Fatal("traversal asset should fail")
	}

	dual := validPreset()
	dual["blocks"] = []any{map[string]any{
		"id": "dual", "type": "dualAmp", "enabled": true, "asset": "",
		"params": map[string]any{
			"leftNamAsset": "models/left.nam", "leftIrAsset": "irs/left.wav",
			"rightNamAsset": "models/right.nam", "rightIrAsset": "irs/right.wav",
		},
	}}
	if err := Validate(dual); err != nil {
		t.Fatalf("valid dual amp rejected: %v", err)
	}
	dual["blocks"].([]any)[0].(map[string]any)["params"].(map[string]any)["rightIrAsset"] = "../escape.wav"
	if err := Validate(dual); err == nil {
		t.Fatal("dual amp traversal asset should fail")
	}
}

func TestReplaceDualAmpAssetReferences(t *testing.T) {
	root := t.TempDir()
	store := NewStore(root)
	preset := validPreset()
	preset["blocks"] = []any{map[string]any{
		"id": "dual", "type": "dualAmp", "enabled": true, "asset": "",
		"params": map[string]any{
			"leftNamAsset": "models/old.nam", "leftIrAsset": "irs/left.wav",
			"rightNamAsset": "models/right.nam", "rightIrAsset": "irs/right.wav",
		},
	}}
	if _, err := store.Save(0, 0, preset); err != nil {
		t.Fatal(err)
	}
	changed, err := store.ReplaceAssetReferences("models/old.nam", "models/new.nam")
	if err != nil || changed != 1 {
		t.Fatalf("replace result changed=%d err=%v", changed, err)
	}
	loaded, err := store.Load(0, 0)
	if err != nil {
		t.Fatal(err)
	}
	params := loaded.Preset["blocks"].([]any)[0].(map[string]any)["params"].(map[string]any)
	if params["leftNamAsset"] != "models/new.nam" {
		t.Fatalf("dual amp reference was not updated: %#v", params)
	}
}

func TestValidateAndReplaceDualRigAssetReferences(t *testing.T) {
	root := t.TempDir()
	store := NewStore(root)
	preset := validPreset()
	preset["version"] = float64(2)
	preset["blocks"] = []any{map[string]any{
		"id": "rig", "type": "dualRig", "enabled": true, "asset": "",
		"params": map[string]any{"inputMode": "sum"},
		"lanes": map[string]any{
			"left": map[string]any{"blocks": []any{
				map[string]any{"id": "left-nam", "type": "nam", "enabled": true, "asset": "models/old.nam", "params": map[string]any{}},
				map[string]any{"id": "left-cab", "type": "cab", "enabled": true, "asset": "irs/left.wav", "params": map[string]any{}},
			}},
			"right": map[string]any{"blocks": []any{
				map[string]any{"id": "right-nam", "type": "nam", "enabled": true, "asset": "models/right.nam", "params": map[string]any{}},
				map[string]any{"id": "right-cab", "type": "cab", "enabled": true, "asset": "irs/right.wav", "params": map[string]any{}},
			}},
		},
	}}
	if err := Validate(preset); err != nil {
		t.Fatalf("valid dual rig rejected: %v", err)
	}
	if _, err := store.Save(0, 0, preset); err != nil {
		t.Fatal(err)
	}
	changed, err := store.ReplaceAssetReferences("models/old.nam", "models/new.nam")
	if err != nil || changed != 1 {
		t.Fatalf("replace result changed=%d err=%v", changed, err)
	}
	loaded, err := store.Load(0, 0)
	if err != nil {
		t.Fatal(err)
	}
	rig := loaded.Preset["blocks"].([]any)[0].(map[string]any)
	left := rig["lanes"].(map[string]any)["left"].(map[string]any)["blocks"].([]any)
	if left[0].(map[string]any)["asset"] != "models/new.nam" {
		t.Fatalf("dual rig child reference was not updated: %#v", left[0])
	}

	nested := left[0].(map[string]any)
	nested["type"] = "dualAmp"
	nested["params"] = map[string]any{
		"leftNamAsset": "models/a.nam", "leftIrAsset": "irs/a.wav",
		"rightNamAsset": "models/b.nam", "rightIrAsset": "irs/b.wav",
	}
	if err := Validate(loaded.Preset); err == nil {
		t.Fatal("nested split block should fail")
	}
}

func TestStoreSaveLoadList(t *testing.T) {
	root := t.TempDir()
	store := NewStore(root)
	preset := validPreset()

	slot, err := store.Save(2, 3, preset)
	if err != nil {
		t.Fatal(err)
	}
	if slot.Bank != 2 || slot.Slot != 3 {
		t.Fatalf("bad slot: %+v", slot)
	}
	path := filepath.Join(root, "presets", "bank-002", "preset-3.json")
	if _, err := os.Stat(path); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(path + ".tmp"); !os.IsNotExist(err) {
		t.Fatalf("tmp should be gone, err=%v", err)
	}

	loaded, err := store.Load(2, 3)
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Preset["futureRoot"] != true {
		t.Fatalf("future root field not preserved: %+v", loaded.Preset)
	}

	summaries, err := store.List()
	if err != nil {
		t.Fatal(err)
	}
	if len(summaries) != 400 {
		t.Fatalf("summary len=%d", len(summaries))
	}
	found := summaries[2*4+3]
	if !found.Exists || found.Name != "Clean" {
		t.Fatalf("bad summary: %+v", found)
	}
}

func TestStoreNormalizesLegacyEffectPlaceholders(t *testing.T) {
	root := t.TempDir()
	store := NewStore(root)
	preset := validPreset()
	preset["blocks"] = []any{
		map[string]any{"id": "old-delay", "type": "time", "enabled": true, "asset": "", "params": map[string]any{}},
		map[string]any{"id": "old-chorus", "type": "modulation", "enabled": true, "asset": "", "params": map[string]any{}},
		map[string]any{"id": "old-compressor", "type": "dynamics", "enabled": true, "asset": "", "params": map[string]any{}},
	}

	saved, err := store.Save(0, 0, preset)
	if err != nil {
		t.Fatal(err)
	}
	blocks := saved.Preset["blocks"].([]any)
	want := []struct{ effectType, mode string }{
		{"delay", "tape"}, {"mod", "chorus"}, {"dynamics", "compressor"},
	}
	for index, expected := range want {
		block := blocks[index].(map[string]any)
		params := block["params"].(map[string]any)
		if block["type"] != expected.effectType || params["mode"] != expected.mode {
			t.Fatalf("block %d was not normalized: %#v", index, block)
		}
	}

	loaded, err := store.Load(0, 0)
	if err != nil {
		t.Fatal(err)
	}
	loadedBlocks := loaded.Preset["blocks"].([]any)
	if loadedBlocks[0].(map[string]any)["type"] != "delay" {
		t.Fatalf("normalized block did not persist: %#v", loadedBlocks[0])
	}

	rawLegacy := `{
  "version": 1,
  "name": "Raw legacy preset",
  "routing": "serial",
  "global": {},
  "blocks": [
    {"id":"old-delay", "type":"time", "enabled":true, "asset":"", "params":{}}
  ]
}`
	if err := os.WriteFile(store.pathFor(0, 0), []byte(rawLegacy), 0o644); err != nil {
		t.Fatal(err)
	}
	loaded, err = store.Load(0, 0)
	if err != nil {
		t.Fatal(err)
	}
	rawBlock := loaded.Preset["blocks"].([]any)[0].(map[string]any)
	if rawBlock["type"] != "delay" || rawBlock["params"].(map[string]any)["mode"] != "tape" {
		t.Fatalf("raw legacy block was not normalized on load: %#v", rawBlock)
	}
}
