package assets

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestTrainerA2Exports(t *testing.T) {
	bytes, err := os.ReadFile("../../../../tests/fixtures/parametric-trainer-a2.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Models []struct {
			Model json.RawMessage `json:"model"`
		} `json:"models"`
	}
	if err := json.Unmarshal(bytes, &fixture); err != nil {
		t.Fatal(err)
	}
	store := NewStore(t.TempDir())
	has24, hasSwitch, hasPedal, hasAmp := false, false, false, false
	for index, entry := range fixture.Models {
		info, err := store.Save(KindModel, fmt.Sprintf("trainer-%d.nam", index), strings.NewReader(string(entry.Model)), false)
		if err != nil || info.NAM == nil || !info.NAM.Supported || len(info.NAM.Parameters) == 0 {
			t.Fatalf("trainer export metadata: %+v %v", info, err)
		}
		has24 = has24 || len(info.NAM.Parameters) == 24
		if len(info.NAM.Parameters) == 24 {
			var oversized namDocument
			if err := json.Unmarshal(entry.Model, &oversized); err != nil {
				t.Fatal(err)
			}
			var parameters []map[string]interface{}
			if err := json.Unmarshal(oversized.Config.Params, &parameters); err != nil {
				t.Fatal(err)
			}
			parameters = append(parameters, map[string]interface{}{"name": "Extra", "min": 0, "max": 10, "default": 5})
			oversized.Config.Params, err = json.Marshal(parameters)
			if err != nil {
				t.Fatal(err)
			}
			bad := describeNAM(&oversized, 0)
			if bad.Supported || len(bad.Parameters) != 0 || !strings.Contains(bad.Error, "1 to 24") {
				t.Fatalf("oversized trainer export advertised: %+v", bad)
			}
		}
		hasPedal = hasPedal || info.NAM.GearType == "pedal"
		hasAmp = hasAmp || info.NAM.GearType == "amp"
		for _, parameter := range info.NAM.Parameters {
			hasSwitch = hasSwitch || parameter.Type == "switch"
		}
	}
	if !has24 || !hasSwitch || !hasPedal || !hasAmp {
		t.Fatalf("missing trainer contract coverage: 24=%v switch=%v pedal=%v amp=%v", has24, hasSwitch, hasPedal, hasAmp)
	}
}

func TestCombinedTrainerA2Export(t *testing.T) {
	bytes, err := os.ReadFile("../../../../tests/fixtures/parametric-trainer-a2-dual.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Models []struct {
			Model json.RawMessage `json:"model"`
		} `json:"models"`
	}
	if err := json.Unmarshal(bytes, &fixture); err != nil {
		t.Fatal(err)
	}
	if len(fixture.Models) != 1 {
		t.Fatal("combined export should represent one model asset")
	}
	store := NewStore(t.TempDir())
	info, err := store.Save(KindModel, "Combined.nam", strings.NewReader(string(fixture.Models[0].Model)), false)
	if err != nil || info.NAM == nil || !info.NAM.Supported || !info.NAM.Slimmable || info.NAM.GearType != "pedal" || len(info.NAM.Parameters) != 2 {
		t.Fatalf("combined upload: %+v %v", info, err)
	}
	listed, err := store.List(KindModel)
	if err != nil || len(listed) != 1 || !listed[0].NAM.Slimmable {
		t.Fatalf("combined model list: %+v %v", listed, err)
	}
	renamed, err := store.Rename(KindModel, info.ID, "Renamed combined.nam")
	if err != nil || renamed.NAM == nil || !renamed.NAM.Slimmable {
		t.Fatalf("combined rename: %+v %v", renamed, err)
	}
	for fault, modify := range map[string]func(*namDocument){
		"controls": func(m *namDocument) {
			m.Config.Submodels[0].Model.Config.Params = json.RawMessage(`[{"name":"Other","min":0,"max":10,"default":5}]`)
		},
		"version":     func(m *namDocument) { m.Config.Submodels[0].Model.Version = "0.7.0" },
		"rate":        func(m *namDocument) { m.Config.Submodels[0].Model.SampleRate = 44100 },
		"family":      func(m *namDocument) { m.Config.Submodels[0].Model.Architecture = "ParametricWaveNet" },
		"boundary":    func(m *namDocument) { m.Config.Submodels[0].Max = 1 },
		"gear":        func(m *namDocument) { m.Config.Submodels[0].Model.Metadata.GearType = "amp" },
		"calibration": func(m *namDocument) { value := 6.4; m.Config.Submodels[0].Model.Metadata.InputLevel = &value },
	} {
		t.Run(fault, func(t *testing.T) {
			var model namDocument
			if err := json.Unmarshal(fixture.Models[0].Model, &model); err != nil {
				t.Fatal(err)
			}
			modify(&model)
			result := describeNAM(&model, 0)
			if result.Supported || len(result.Parameters) != 0 || result.Error == "" {
				t.Fatalf("bad combined export advertised: %+v", result)
			}
		})
	}
}

func TestTrainerFiLMExports(t *testing.T) {
	bytes, err := os.ReadFile("../../../../tests/fixtures/parametric-trainer-film.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Models []struct {
			Model json.RawMessage `json:"model"`
		} `json:"models"`
	}
	if err := json.Unmarshal(bytes, &fixture); err != nil {
		t.Fatal(err)
	}
	if len(fixture.Models) != 2 {
		t.Fatal("expected combined pedal and individual amp exports")
	}
	store := NewStore(t.TempDir())
	for index, entry := range fixture.Models {
		info, err := store.Save(KindModel, fmt.Sprintf("FiLM-%d.nam", index), strings.NewReader(string(entry.Model)), false)
		if err != nil || info.NAM == nil || !info.NAM.Supported || len(info.NAM.Parameters) != 2 {
			t.Fatalf("FiLM upload metadata: %+v %v", info, err)
		}
		if info.NAM.Slimmable != (index == 0) || info.NAM.Parameters[0].Name != "Drive" || info.NAM.Parameters[1].Name != "Tone" || info.NAM.Parameters[1].Min != -5 {
			t.Fatalf("FiLM controls and tiers: %+v", info.NAM)
		}
		expectedGear := "amp"
		if index == 0 {
			expectedGear = "pedal"
		}
		if info.NAM.GearType != expectedGear {
			t.Fatalf("FiLM gear: %+v", info.NAM)
		}
		renamed, err := store.Rename(KindModel, info.ID, fmt.Sprintf("Renamed FiLM-%d.nam", index))
		if err != nil || renamed.NAM == nil || !renamed.NAM.Supported || renamed.NAM.Slimmable != info.NAM.Slimmable {
			t.Fatalf("FiLM rename: %+v %v", renamed, err)
		}
	}
	listed, err := store.List(KindModel)
	if err != nil || len(listed) != 2 {
		t.Fatalf("FiLM asset list: %+v %v", listed, err)
	}
}

func TestParametricNAMMetadata(t *testing.T) {
	model := `{"version":"1.0.0","architecture":"ConcatLSTM","sample_rate":48000,"metadata":{"gear_type":"pedal"},"config":{"params":[{"name":"Drive","min":0,"max":10,"default":5},{"name":"Mode","min":0,"max":1,"default":0,"type":"switch","enum_names":["Soft","Hard"]}]},"weights":[0,1]}`
	store := NewStore(t.TempDir())
	info, err := store.Save(KindModel, "Open OD.nam", strings.NewReader(model), false)
	if err != nil {
		t.Fatal(err)
	}
	if info.NAM == nil || !info.NAM.Supported || info.NAM.GearType != "pedal" || len(info.NAM.Parameters) != 2 {
		t.Fatalf("wrong metadata: %+v", info.NAM)
	}
	if info.NAM.Parameters[0].Type != "continuous" {
		t.Fatal("missing type should default to continuous")
	}
	renamed, err := store.Rename(KindModel, info.ID, "Renamed.nam")
	if err != nil || renamed.NAM == nil || len(renamed.NAM.Parameters) != 2 {
		t.Fatalf("rename lost metadata: %+v %v", renamed, err)
	}
	listed, err := store.List(KindModel)
	if err != nil || len(listed) != 1 || listed[0].NAM == nil {
		t.Fatalf("list lost metadata: %+v %v", listed, err)
	}
	for name, doc := range map[string]string{
		"unsupported":    strings.ReplaceAll(model, "ConcatLSTM", "HyperWaveNet"),
		"version":        strings.ReplaceAll(model, "1.0.0", "0.7.0"),
		"sampleRate":     strings.ReplaceAll(model, "48000", "44100"),
		"missingRate":    strings.ReplaceAll(model, `,"sample_rate":48000`, ""),
		"duplicate":      strings.ReplaceAll(model, "\"Mode\"", "\"Drive\""),
		"missingDefault": strings.ReplaceAll(model, `,"default":5`, ""),
		"switchDefault":  strings.ReplaceAll(model, `"default":0`, `"default":0.5`),
		"switchRange":    strings.ReplaceAll(model, `"max":1,`, `"max":2,`),
	} {
		t.Run(name, func(t *testing.T) {
			path := filepath.Join(store.root, name+".nam")
			if err := os.WriteFile(path, []byte(doc), 0600); err != nil {
				t.Fatal(err)
			}
			metadata := readNAMInfo(path)
			if metadata == nil || metadata.Supported || metadata.Error == "" || len(metadata.Parameters) != 0 {
				t.Fatalf("invalid model advertised controls: %+v", metadata)
			}
		})
	}
}

func TestPublishedFiLMNAMMetadata(t *testing.T) {
	bytes, err := os.ReadFile("../../../../assets/nam/clone-overdrive.nam")
	if err != nil {
		t.Fatal(err)
	}
	var model namDocument
	if err := json.Unmarshal(bytes, &model); err != nil {
		t.Fatal(err)
	}
	metadata := describeNAM(&model, 0)
	if !metadata.Supported || !metadata.Slimmable || metadata.GearType != "pedal" || len(metadata.Parameters) != 2 || metadata.Parameters[0].Name != "Gain" || metadata.Parameters[1].Name != "Tone" || metadata.Parameters[0].Default != 0.5 {
		t.Fatalf("wrong published model metadata: %+v", metadata)
	}
	store := NewStore(t.TempDir())
	saved, err := store.Save(KindModel, "clone-overdrive.nam", strings.NewReader(string(bytes)), false)
	if err != nil || saved.NAM == nil || !saved.NAM.Slimmable {
		t.Fatalf("upload lost FiLM metadata: %+v %v", saved, err)
	}
	for name, modify := range map[string]func(*namDocument){
		"schema":     func(m *namDocument) { m.Config.Submodels[0].Model.Config.Parametric.Schema = 2 },
		"head":       func(m *namDocument) { m.Config.Submodels[0].Model.Config.Parametric.Head = "residual" },
		"lora":       func(m *namDocument) { m.Config.Submodels[0].Model.Config.Parametric.Lora = json.RawMessage(`{}`) },
		"condition":  func(m *namDocument) { m.Config.Submodels[0].Model.Config.Parametric.ConditionSize = 3 },
		"width":      func(m *namDocument) { m.Config.Submodels[0].Model.Config.Layers = json.RawMessage(`4096`) },
		"version":    func(m *namDocument) { m.Version = "1.0.0" },
		"sampleRate": func(m *namDocument) { m.SampleRate = 44100 },
		"boundary":   func(m *namDocument) { m.Config.Submodels[0].Max = 1 },
		"coverage":   func(m *namDocument) { m.Config.Submodels[1].Max = 0.9 },
		"tierControls": func(m *namDocument) {
			m.Config.Submodels[1].Model.Config.Parametric.Parameters = json.RawMessage(`[{"name":"Other","min":0,"max":1,"default":0.5},{"name":"Tone","min":0,"max":1,"default":0.5}]`)
		},
		"partialLayers": func(m *namDocument) { m.Config.Submodels[0].Model.Config.Parametric.FilmLayers = []string{"layer_0"} },
		"tooManyControls": func(m *namDocument) {
			parameters := make([]map[string]interface{}, 17)
			for i := range parameters {
				parameters[i] = map[string]interface{}{"name": fmt.Sprintf("Control %d", i), "min": 0, "max": 1, "default": 0.5}
			}
			m.Config.Submodels[0].Model.Config.Parametric.Parameters, _ = json.Marshal(parameters)
			m.Config.Submodels[0].Model.Config.Parametric.ConditionSize = 17
		},
		"mixedTier": func(m *namDocument) { m.Config.Submodels[0].Model.Architecture = "WaveNet" },
	} {
		t.Run(name, func(t *testing.T) {
			var changed namDocument
			if err := json.Unmarshal(bytes, &changed); err != nil {
				t.Fatal(err)
			}
			modify(&changed)
			info := describeNAM(&changed, 0)
			if info.Supported || info.Error == "" || len(info.Parameters) != 0 {
				t.Fatalf("invalid FiLM model advertised: %+v", info)
			}
		})
	}
	embedded := namDocument{Architecture: "WaveNet", Version: "0.7.0", Embedded: &model}
	info := describeNAM(&embedded, 0)
	if !info.Supported || !info.Slimmable || info.GearType != "pedal" || len(info.Parameters) != 2 {
		t.Fatalf("embedded parametric metadata lost: %+v", info)
	}
}
