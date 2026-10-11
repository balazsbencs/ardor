package assets

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"reflect"
)

// NAMParameter follows the self-describing parametric fork's config.params.
type NAMParameter struct {
	Name      string   `json:"name"`
	Min       float64  `json:"min"`
	Max       float64  `json:"max"`
	Default   float64  `json:"default"`
	Type      string   `json:"type"`
	EnumNames []string `json:"enum_names,omitempty"`
}
type NAMInfo struct {
	Architecture string         `json:"architecture"`
	Version      string         `json:"version"`
	GearType     string         `json:"gearType"`
	Parameters   []NAMParameter `json:"parameters,omitempty"`
	Slimmable    bool           `json:"slimmable,omitempty"`
	Supported    bool           `json:"supported"`
	Error        string         `json:"error,omitempty"`
}

type namDocument struct {
	Architecture string       `json:"architecture"`
	Version      string       `json:"version"`
	SampleRate   float64      `json:"sample_rate"`
	Embedded     *namDocument `json:"embedded_parametric"`
	Config       struct {
		Params     json.RawMessage `json:"params"`
		Parametric *struct {
			Type          string          `json:"type"`
			Schema        int             `json:"schema_version"`
			Head          string          `json:"head_mode"`
			ConditionSize int             `json:"condition_size"`
			Parameters    json.RawMessage `json:"parameters"`
			FilmLayers    []string        `json:"film_layers"`
			Lora          json.RawMessage `json:"lora"`
		} `json:"parametric"`
		Layers    json.RawMessage `json:"layers"`
		HeadScale *float64        `json:"head_scale"`
		Submodels []struct {
			Max   float64      `json:"max_value"`
			Model *namDocument `json:"model"`
		} `json:"submodels"`
	} `json:"config"`
	Metadata struct {
		GearType   string   `json:"gear_type"`
		InputLevel *float64 `json:"input_level_dbu"`
	} `json:"metadata"`
}

func readNAMInfo(path string) *NAMInfo {
	file, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer file.Close()
	var document namDocument
	if json.NewDecoder(file).Decode(&document) != nil || document.Architecture == "" {
		return nil
	}
	return describeNAM(&document, 0)
}

func describeNAM(document *namDocument, depth int) *NAMInfo {
	result := &NAMInfo{Architecture: document.Architecture, Version: document.Version, GearType: "amp", Supported: true}
	if document.Metadata.GearType == "pedal" {
		result.GearType = "pedal"
	}
	fail := func(message string) *NAMInfo {
		result.Supported = false
		result.Error = message
		result.Parameters = nil
		return result
	}
	if depth > 1 {
		return fail("Nested parametric NAM containers are not supported.")
	}
	if document.Embedded != nil {
		if depth != 0 || document.Embedded.Embedded != nil {
			return fail("Invalid embedded parametric NAM.")
		}
		return describeNAM(document.Embedded, 0)
	}
	if document.Architecture == "SlimmableContainer" {
		tiers := document.Config.Submodels
		parametric := false
		for _, tier := range tiers {
			if tier.Model != nil && (tier.Model.Architecture == "ParametricWaveNet" || tier.Model.Architecture == "ConcatWaveNet" ||
				tier.Model.Architecture == "ConcatLSTM" || tier.Model.Architecture == "HyperWaveNet" || len(tier.Model.Config.Params) > 0 || tier.Model.Config.Parametric != nil) {
				parametric = true
			}
		}
		if !parametric {
			return result
		}
		if depth > 0 || len(tiers) == 0 || len(tiers) > 8 || tiers[0].Model == nil {
			return fail("Parametric NAM containers require 1 to 8 direct tiers.")
		}
		family := tiers[0].Model.Architecture
		version := "0.7.0"
		if family == "ConcatWaveNet" {
			version = "1.0.0"
		} else if family != "ParametricWaveNet" {
			return fail("Unsupported parametric NAM container family.")
		}
		if document.Version != version || document.SampleRate != 48000 {
			return fail("Unsupported parametric NAM container version or sample rate.")
		}
		previous := 0.0
		for _, tier := range tiers {
			if tier.Model == nil || tier.Model.Architecture != family || tier.Model.Embedded != nil ||
				tier.Model.Version != document.Version || tier.Model.SampleRate != document.SampleRate || tier.Max <= previous || tier.Max > 1 {
				return fail("Invalid parametric NAM tier or boundary.")
			}
			child := describeNAM(tier.Model, depth+1)
			if !child.Supported {
				return fail(child.Error)
			}
			if family == "ConcatWaveNet" && (child.GearType != result.GearType || !reflect.DeepEqual(document.Metadata.InputLevel, tier.Model.Metadata.InputLevel)) {
				return fail("NAM tiers must share gear type and input calibration.")
			}
			if len(result.Parameters) == 0 {
				result.Parameters = child.Parameters
			} else if !reflect.DeepEqual(result.Parameters, child.Parameters) {
				return fail("NAM tiers must share parameter definitions.")
			}
			previous = tier.Max
		}
		if previous != 1 {
			return fail("NAM final tier must cover 1.0.")
		}
		result.Slimmable = len(tiers) > 1
		return result
	}
	film := document.Architecture == "ParametricWaveNet"
	concat := document.Architecture == "ConcatWaveNet" || document.Architecture == "ConcatLSTM"
	if !film && !concat {
		if len(document.Config.Params) > 0 || document.Config.Parametric != nil || document.Architecture == "HyperWaveNet" {
			return fail("This parametric NAM architecture is not supported.")
		}
		return result
	}
	expectedVersion := "1.0.0"
	definitions := document.Config.Params
	if film {
		expectedVersion = "0.7.0"
		parametric := document.Config.Parametric
		var channels int
		if parametric == nil || parametric.Schema != 1 || parametric.Head != "skip" || (parametric.Type != "" && parametric.Type != "film") || len(parametric.Lora) > 0 || json.Unmarshal(document.Config.Layers, &channels) != nil || channels < 3 || channels > 9 || document.Config.HeadScale == nil {
			return fail("FiLM NAM requires schema 1, a skip head and A2 width 3 to 9.")
		}
		if parametric.FilmLayers != nil {
			if len(parametric.FilmLayers) != 23 {
				return fail("FiLM requires all 23 A2 layers.")
			}
			for i, layer := range parametric.FilmLayers {
				if layer != fmt.Sprintf("layer_%d", i) {
					return fail("Invalid FiLM layer order.")
				}
			}
		}
		definitions = parametric.Parameters
	}
	if document.Version != expectedVersion {
		return fail("Unsupported parametric NAM version.")
	}
	if document.SampleRate != 48000 {
		return fail("Parametric NAM files must specify a 48000 Hz sample_rate.")
	}
	// Pointer fields distinguish an omitted default/minimum from a valid zero.
	var specs []struct {
		Name      string   `json:"name"`
		Min       *float64 `json:"min"`
		Max       *float64 `json:"max"`
		Default   *float64 `json:"default"`
		Type      string   `json:"type"`
		EnumNames []string `json:"enum_names"`
		Steps     int      `json:"steps"`
	}
	maxParameters := 24
	if film {
		maxParameters = 16
	}
	if json.Unmarshal(definitions, &specs) != nil || len(specs) == 0 || len(specs) > maxParameters {
		return fail(fmt.Sprintf("Parametric NAM requires 1 to %d parameter definitions.", maxParameters))
	}
	if film && document.Config.Parametric.ConditionSize != len(specs) {
		return fail("FiLM condition_size must match parameter definitions.")
	}
	names := map[string]bool{}
	for _, spec := range specs {
		if spec.Name == "" || names[spec.Name] || spec.Min == nil || spec.Max == nil || spec.Default == nil {
			return fail("NAM parameters require unique names and min, max, default values.")
		}
		names[spec.Name] = true
		p := NAMParameter{Name: spec.Name, Min: *spec.Min, Max: *spec.Max, Default: *spec.Default, Type: spec.Type, EnumNames: spec.EnumNames}
		if p.Type == "" {
			p.Type = "continuous"
		}
		if p.Min >= p.Max || p.Default < p.Min || p.Default > p.Max {
			return fail(fmt.Sprintf("Invalid range or default for NAM parameter %s.", p.Name))
		}
		if film && (p.Type != "continuous" || spec.Steps > 1) {
			return fail("FiLM NAM currently supports continuous parameters.")
		}
		if p.Type == "switch" {
			enums := map[string]bool{}
			if len(p.EnumNames) < 2 || p.Min != 0 || p.Max != float64(len(p.EnumNames)-1) || math.Trunc(p.Default) != p.Default {
				return fail("NAM switches require an integer index and a complete enum_names list.")
			}
			for _, label := range p.EnumNames {
				if label == "" || enums[label] {
					return fail("NAM switch choices must be unique and nonempty.")
				}
				enums[label] = true
			}
		} else if p.Type != "continuous" || len(p.EnumNames) > 0 {
			return fail("Unsupported NAM parameter type.")
		}
		result.Parameters = append(result.Parameters, p)
	}
	return result
}
