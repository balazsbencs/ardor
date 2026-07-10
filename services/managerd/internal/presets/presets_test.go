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
