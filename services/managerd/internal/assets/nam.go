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
	Architecture string          `json:"architecture"`
	Version      string          `json:"version"`
	SampleRate   float64         `json:"sample_rate"`
	Embedded     *namDocument    `json:"embedded_parametric"`
	Config       namConfig       `json:"config"`
	Weights      json.RawMessage `json:"weights"`
	Extensions   json.RawMessage `json:"extensions"`
	Metadata     struct {
		GearType   string   `json:"gear_type"`
		InputLevel *float64 `json:"input_level_dbu"`
	} `json:"metadata"`
}

type namConfig struct {
	Params       json.RawMessage `json:"params"`
	Hypernet     json.RawMessage `json:"hypernet"`
	ConditionDSP json.RawMessage `json:"condition_dsp"`
	Parametric   *struct {
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
		Max          float64      `json:"max_value"`
		Model        *namDocument `json:"model"`
		Architecture string       `json:"architecture"`
		Config       *namConfig   `json:"config"`
		WeightOffset *int         `json:"weight_offset"`
		WeightCount  int          `json:"weight_count"`
	} `json:"submodels"`
}

func readNAMInfo(directory, filename string) *NAMInfo {
	unreadable := &NAMInfo{GearType: "amp", Error: "Cannot read NAM document."}
	root, err := os.OpenRoot(directory)
	if err != nil {
		return unreadable
	}
	defer root.Close()
	file, err := root.Open(filename)
	if err != nil {
		return unreadable
	}
	defer file.Close()
	var document namDocument
	if json.NewDecoder(file).Decode(&document) != nil || document.Architecture == "" {
		return &NAMInfo{GearType: "amp", Error: "Invalid NAM document."}
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
	if document.Architecture == "ParametricSlimmableContainer" {
		if depth > 0 {
			return fail("Nested parametric NAM containers are not supported.")
		}
		parameters, err := describeFlatNAM(document)
		if err != nil {
			return fail(err.Error())
		}
		result.Parameters = parameters
		result.Slimmable = len(document.Config.Submodels) > 1
		return result
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
	hyper := document.Architecture == "HyperWaveNet"
	if !film && !concat && !hyper {
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
	if hyper {
		var network map[string]json.RawMessage
		var layers []struct {
			Input     int             `json:"input_size"`
			Condition int             `json:"condition_size"`
			Packing   json.RawMessage `json:"packing"`
			Slimmable json.RawMessage `json:"slimmable"`
		}
		present := func(raw json.RawMessage) bool { return len(raw) > 0 && string(raw) != "null" }
		if json.Unmarshal(document.Config.Hypernet, &network) != nil || network == nil || present(document.Config.ConditionDSP) ||
			json.Unmarshal(document.Config.Layers, &layers) != nil || len(layers) < 1 || len(layers) > 8 || layers[0].Input != 1 {
			return fail("HyperWaveNet requires hypernet, mono layers and no nested condition DSP.")
		}
		for _, layer := range layers {
			if layer.Condition != 1 || present(layer.Packing) || present(layer.Slimmable) {
				return fail("HyperWaveNet does not support packed, slimmable or multichannel conditioning.")
			}
		}
	}
	parameters, err := parseNAMParameters(definitions, film)
	if err != nil {
		return fail(err.Error())
	}
	if film && document.Config.Parametric.ConditionSize != len(parameters) {
		return fail("FiLM condition_size must match parameter definitions.")
	}
	result.Parameters = parameters
	return result
}

func parseNAMParameters(definitions json.RawMessage, film bool) ([]NAMParameter, error) {
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
		return nil, fmt.Errorf("Parametric NAM requires 1 to %d parameter definitions.", maxParameters)
	}
	parameters := make([]NAMParameter, 0, len(specs))
	names := map[string]bool{}
	for _, spec := range specs {
		if spec.Name == "" || names[spec.Name] || spec.Min == nil || spec.Max == nil || spec.Default == nil {
			return nil, fmt.Errorf("NAM parameters require unique names and min, max, default values.")
		}
		names[spec.Name] = true
		p := NAMParameter{Name: spec.Name, Min: *spec.Min, Max: *spec.Max, Default: *spec.Default, Type: spec.Type, EnumNames: spec.EnumNames}
		if p.Type == "" {
			p.Type = "continuous"
		}
		if p.Min >= p.Max || p.Default < p.Min || p.Default > p.Max {
			return nil, fmt.Errorf("Invalid range or default for NAM parameter %s.", p.Name)
		}
		if film && (p.Type != "continuous" || spec.Steps > 1) {
			return nil, fmt.Errorf("FiLM NAM currently supports continuous parameters.")
		}
		if p.Type == "switch" {
			enums := map[string]bool{}
			if len(p.EnumNames) < 2 || p.Min != 0 || p.Max != float64(len(p.EnumNames)-1) || math.Trunc(p.Default) != p.Default {
				return nil, fmt.Errorf("NAM switches require an integer index and a complete enum_names list.")
			}
			for _, label := range p.EnumNames {
				if label == "" || enums[label] {
					return nil, fmt.Errorf("NAM switch choices must be unique and nonempty.")
				}
				enums[label] = true
			}
		} else if p.Type != "continuous" || len(p.EnumNames) > 0 {
			return nil, fmt.Errorf("Unsupported NAM parameter type.")
		}
		parameters = append(parameters, p)
	}
	return parameters, nil
}

func describeFlatNAM(document *namDocument) ([]NAMParameter, error) {
	tiers := document.Config.Submodels
	var weights []*float32
	if document.Version != "1.0.0" || document.SampleRate != 48000 || len(tiers) == 0 || len(tiers) > 8 ||
		json.Unmarshal(document.Weights, &weights) != nil {
		return nil, fmt.Errorf("invalid flat parametric NAM container")
	}
	for _, weight := range weights {
		if weight == nil {
			return nil, fmt.Errorf("invalid parametric NAM weight")
		}
	}
	parameters, err := parseNAMParameters(document.Config.Params, false)
	if err != nil {
		return nil, err
	}
	nextWeight, previousBoundary := 0, 0.0
	for _, tier := range tiers {
		if tier.Architecture != "HyperWaveNet" || tier.Config == nil || tier.Max <= previousBoundary || tier.Max > 1 ||
			tier.WeightOffset == nil || *tier.WeightOffset != nextWeight || tier.WeightCount <= 0 || tier.WeightCount > len(weights)-nextWeight {
			return nil, fmt.Errorf("invalid flat HyperWaveNet NAM tier")
		}
		child := describeNAM(&namDocument{Architecture: tier.Architecture, Version: document.Version,
			SampleRate: document.SampleRate, Config: *tier.Config}, 1)
		if !child.Supported {
			return nil, fmt.Errorf("invalid HyperWaveNet tier: %s", child.Error)
		}
		if !reflect.DeepEqual(parameters, child.Parameters) {
			return nil, fmt.Errorf("NAM tiers must share the container's parameter definitions")
		}
		nextWeight += tier.WeightCount
		previousBoundary = tier.Max
	}
	if previousBoundary != 1 || nextWeight != len(weights) {
		return nil, fmt.Errorf("NAM tiers must cover 1.0 and the complete weight payload")
	}
	var extensions struct {
		LinearVolume json.RawMessage `json:"linear_volume"`
	}
	if len(document.Extensions) != 0 && json.Unmarshal(document.Extensions, &extensions) != nil {
		return nil, fmt.Errorf("invalid NAM extensions")
	}
	if len(extensions.LinearVolume) != 0 {
		return insertNAMVolume(parameters, extensions.LinearVolume)
	}
	return parameters, nil
}

func insertNAMVolume(parameters []NAMParameter, raw json.RawMessage) ([]NAMParameter, error) {
	var volume struct {
		Schema        int      `json:"schema_version"`
		Interpolation string   `json:"interpolation"`
		Name          string   `json:"name"`
		Index         *int     `json:"index"`
		Min           *float64 `json:"min"`
		Max           *float64 `json:"max"`
		Default       *float64 `json:"default"`
		Reference     *float64 `json:"reference"`
		SmoothingMs   *float64 `json:"smoothing_ms"`
		Segments      []struct {
			Left         *float64   `json:"left"`
			Right        *float64   `json:"right"`
			Coefficients []*float64 `json:"coefficients"`
		} `json:"segments"`
	}
	if json.Unmarshal(raw, &volume) != nil || volume.Schema != 1 || volume.Interpolation != "pchip" ||
		volume.Index == nil || *volume.Index < 0 || *volume.Index > len(parameters) || len(parameters) >= 24 ||
		volume.Name == "" || volume.Min == nil || volume.Max == nil || volume.Default == nil ||
		math.Abs(*volume.Min) > math.MaxFloat32 || math.Abs(*volume.Max) > math.MaxFloat32 ||
		float32(*volume.Min) >= float32(*volume.Max) || *volume.Default < *volume.Min || *volume.Default > *volume.Max ||
		volume.Reference == nil || *volume.Reference < *volume.Min || *volume.Reference > *volume.Max ||
		volume.SmoothingMs == nil || *volume.SmoothingMs < 0 ||
		math.Round(float64(float32(*volume.SmoothingMs/1000))*48000) >= math.MaxInt32 || len(volume.Segments) == 0 {
		return nil, fmt.Errorf("invalid NAM linear volume extension")
	}
	for _, parameter := range parameters {
		if volume.Name == parameter.Name {
			return nil, fmt.Errorf("NAM linear volume duplicates a learned parameter")
		}
	}
	end := *volume.Min
	for _, segment := range volume.Segments {
		if segment.Left == nil || segment.Right == nil || *segment.Left != end ||
			*segment.Right <= end || *segment.Right > *volume.Max || len(segment.Coefficients) != 4 {
			return nil, fmt.Errorf("invalid NAM volume segment")
		}
		bound := 0.0
		for _, coefficient := range segment.Coefficients {
			if coefficient == nil {
				return nil, fmt.Errorf("invalid NAM volume coefficient")
			}
			bound = bound*(*segment.Right-end) + math.Abs(*coefficient)
		}
		if bound > math.MaxFloat32 {
			return nil, fmt.Errorf("invalid NAM volume coefficients")
		}
		end = *segment.Right
	}
	if end != *volume.Max {
		return nil, fmt.Errorf("NAM volume segments must cover the control range")
	}
	index := *volume.Index
	parameters = append(parameters, NAMParameter{})
	copy(parameters[index+1:], parameters[index:])
	parameters[index] = NAMParameter{Name: volume.Name, Min: *volume.Min, Max: *volume.Max,
		Default: *volume.Default, Type: "continuous"}
	return parameters, nil
}
