package presets

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"os"
	"path"
	"path/filepath"
	"strings"
	"unicode"
	"unicode/utf8"
)

type Preset map[string]any

type Slot struct {
	Bank   int    `json:"bank"`
	Slot   int    `json:"slot"`
	Preset Preset `json:"preset"`
}

type Summary struct {
	Bank   int    `json:"bank"`
	Slot   int    `json:"slot"`
	Exists bool   `json:"exists"`
	Name   string `json:"name,omitempty"`
}

type Store struct {
	root string
}

func NewStore(root string) Store {
	return Store{root: root}
}

func Validate(preset Preset) error {
	version, ok := preset["version"].(float64)
	if !ok || (version != 1 && version != 2 && version != 3 && version != 4) {
		return errors.New("preset version must be 1, 2, 3, or 4")
	}
	routing, ok := preset["routing"].(string)
	if !ok || (routing != "serial" && routing != "wdw") {
		return errors.New("preset routing must be serial or wdw")
	}
	if routing == "serial" && version == 3 {
		return errors.New("wet/dry/wet routing requires preset version 3")
	}
	if routing == "wdw" && version != 3 && version != 4 {
		return errors.New("wet/dry/wet routing requires preset version 3 or 4")
	}
	if _, ok := preset["global"].(map[string]any); !ok {
		return errors.New("preset global must be an object")
	}
	blocks, ok := preset["blocks"].([]any)
	if !ok {
		return errors.New("preset blocks must be an array")
	}
	if err := validateBlocks(blocks, version, false); err != nil {
		return err
	}
	if routing != "wdw" {
		return validateScenesAndMidi(preset, version)
	}
	if len(blocks) != 0 {
		return errors.New("wet/dry/wet presets must keep top-level blocks empty")
	}
	wdw, ok := preset["wdw"].(map[string]any)
	if !ok {
		return errors.New("wet/dry/wet preset requires a wdw object")
	}
	for _, laneName := range []string{"dry", "wet"} {
		lane, ok := wdw[laneName].(map[string]any)
		if !ok {
			return fmt.Errorf("WDW %s lane must be an object", laneName)
		}
		children, ok := lane["blocks"].([]any)
		if !ok {
			return fmt.Errorf("WDW %s lane must contain a blocks array", laneName)
		}
		if err := validateBlocks(children, version, true); err != nil {
			return err
		}
		if level, ok := lane["levelDb"]; ok {
			value, ok := level.(float64)
			if !ok || value < -60 || value > 12 {
				return fmt.Errorf("WDW %s lane level must be between -60 and 12 dB", laneName)
			}
		}
		if pan, ok := lane["pan"]; ok && laneName == "dry" {
			value, ok := pan.(float64)
			if !ok || value < -1 || value > 1 {
				return fmt.Errorf("WDW %s lane pan must be between -1 and 1", laneName)
			}
		} else if pan, ok := lane["pan"]; ok {
			value, ok := pan.(float64)
			if !ok || value != 0 {
				return fmt.Errorf("WDW %s lane does not support pan", laneName)
			}
		}
		if width, ok := lane["width"]; ok {
			value, ok := width.(float64)
			if !ok {
				return fmt.Errorf("WDW %s lane width must be between 0 and 1", laneName)
			}
			if laneName == "dry" && value != 1 {
				return fmt.Errorf("WDW %s lane does not support width", laneName)
			}
			if laneName == "wet" && (value < 0 || value > 1) {
				return fmt.Errorf("WDW %s lane width must be between 0 and 1", laneName)
			}
		}
	}
	return validateScenesAndMidi(preset, version)
}

func validateScenesAndMidi(preset Preset, version float64) error {
	if err := validateSceneSet(preset, version); err != nil {
		return err
	}
	return validateSceneMidiMappings(preset)
}

type midiAddress struct {
	channel int
	cc      int
}

func midiAddressesOverlap(left, right midiAddress) bool {
	return left.cc == right.cc && (left.channel == -1 || right.channel == -1 || left.channel == right.channel)
}

func midiAddressFromMapping(mapping map[string]any) (midiAddress, error) {
	channelValue, channelOK := mapping["channel"].(float64)
	ccValue, ccOK := mapping["controlChange"].(float64)
	if !channelOK || channelValue != math.Trunc(channelValue) || channelValue < -1 || channelValue > 15 ||
		!ccOK || ccValue != math.Trunc(ccValue) || ccValue < 0 || ccValue > 127 {
		return midiAddress{}, errors.New("MIDI binding channel or controller is invalid")
	}
	return midiAddress{int(channelValue), int(ccValue)}, nil
}

func validateSceneMidiMappings(preset Preset) error {
	occupied := []midiAddress{}
	actionCount := 0
	if raw, present := preset["midiMappings"]; present && raw != nil {
		mappings, ok := raw.([]any)
		if !ok {
			return errors.New("MIDI mappings must be an array")
		}
		for _, rawMapping := range mappings {
			mapping, ok := rawMapping.(map[string]any)
			if !ok {
				return errors.New("MIDI mapping must be an object")
			}
			address, err := midiAddressFromMapping(mapping)
			if err != nil {
				return err
			}
			occupied = append(occupied, address)
			if actions, ok := mapping["actions"].([]any); ok {
				actionCount += len(actions)
			}
		}
	}
	raw, present := preset["sceneMidiMappings"]
	if !present || raw == nil {
		if actionCount > 256 {
			return errors.New("a preset can contain at most 256 MIDI action targets")
		}
		return nil
	}
	mappings, ok := raw.([]any)
	if !ok {
		return errors.New("scene MIDI mappings must be an array")
	}
	if len(mappings) > 0 {
		if _, ok := preset["sceneSet"].(map[string]any); !ok {
			return errors.New("scene MIDI actions require a scene set")
		}
	}
	sceneIDs := map[string]bool{}
	if sceneSet, ok := preset["sceneSet"].(map[string]any); ok {
		if scenes, ok := sceneSet["scenes"].([]any); ok {
			for _, rawScene := range scenes {
				if scene, ok := rawScene.(map[string]any); ok {
					if id, ok := scene["id"].(string); ok {
						sceneIDs[id] = true
					}
				}
			}
		}
	}
	for _, rawMapping := range mappings {
		mapping, ok := rawMapping.(map[string]any)
		if !ok {
			return errors.New("scene MIDI mapping must be an object")
		}
		address, err := midiAddressFromMapping(mapping)
		if err != nil {
			return err
		}
		for _, other := range occupied {
			if midiAddressesOverlap(address, other) {
				return errors.New("scene and parameter MIDI bindings must not overlap")
			}
		}
		occupied = append(occupied, address)
		action, ok := mapping["action"].(string)
		if !ok || (action != "selectScene" && action != "sceneNumber" &&
			action != "showPresets" && action != "showScenes") {
			return errors.New("unknown scene MIDI action")
		}
		sceneID, hasSceneID := mapping["sceneId"].(string)
		if action == "selectScene" {
			if !hasSceneID || !sceneIDs[sceneID] {
				return errors.New("scene MIDI action must reference an existing scene ID")
			}
		} else if _, present := mapping["sceneId"]; present {
			return errors.New("only direct scene MIDI actions may contain a scene ID")
		}
		actionCount++
	}
	if actionCount > 256 {
		return errors.New("a preset can contain at most 256 MIDI action targets")
	}
	return nil
}

func validSceneID(value string) bool {
	if len(value) == 0 || len(value) > 64 {
		return false
	}
	for _, char := range []byte(value) {
		if !((char >= 'a' && char <= 'z') || (char >= 'A' && char <= 'Z') ||
			(char >= '0' && char <= '9') || char == '-' || char == '_') {
			return false
		}
	}
	return true
}

func collectBlockTypes(raw []any, blocks map[string]string) {
	for _, value := range raw {
		block, ok := value.(map[string]any)
		if !ok {
			continue
		}
		if id, ok := block["id"].(string); ok && id != "" {
			typeName, _ := block["type"].(string)
			blocks[id] = typeName
		}
		lanes, _ := block["lanes"].(map[string]any)
		for _, laneName := range []string{"left", "right"} {
			lane, _ := lanes[laneName].(map[string]any)
			children, _ := lane["blocks"].([]any)
			collectBlockTypes(children, blocks)
		}
	}
}

func finiteSceneNumber(value any) bool {
	number, ok := value.(float64)
	return ok && !math.IsNaN(number) && !math.IsInf(number, 0)
}

func validateSceneTarget(target map[string]any, blockTypes map[string]string, hasWDW bool) (string, error) {
	targetType, ok := target["target"].(string)
	if !ok {
		return "", errors.New("scene target requires a target type")
	}
	value, hasValue := target["value"]
	if !hasValue {
		return "", errors.New("scene target requires a value")
	}
	blockID, _ := target["blockId"].(string)
	parameter, _ := target["parameter"].(string)
	lane, _ := target["lane"].(string)
	switch targetType {
	case "inputGainDb":
		if blockID != "" || parameter != "" || lane != "" || !finiteSceneNumber(value) {
			return "", errors.New("scene input gain target is invalid")
		}
		return "inputGainDb", nil
	case "parameter":
		if _, exists := blockTypes[blockID]; !exists || parameter == "" || lane != "" || !finiteSceneNumber(value) {
			return "", errors.New("scene parameter target is invalid")
		}
		return "parameter\x1f" + blockID + "\x1f" + parameter, nil
	case "blockEnabled":
		blockType, exists := blockTypes[blockID]
		if !exists || parameter != "" || lane != "" {
			return "", errors.New("scene block-enabled target is invalid")
		}
		if _, ok := value.(bool); !ok {
			return "", errors.New("scene block-enabled target requires a boolean value")
		}
		sceneBypassType := blockType == "mod" || blockType == "delay" || blockType == "reverb" ||
			blockType == "irreverb" || blockType == "stereo" || blockType == "dynamics" ||
			blockType == "distortion" || blockType == "wah" || blockType == "eq"
		if !sceneBypassType {
			return "", errors.New("structural block enable is shared by all scenes")
		}
		return "blockEnabled\x1f" + blockID, nil
	case "wdwLane":
		laneValid := lane == "dry" || lane == "wet"
		parameterValid := parameter == "levelDb" || parameter == "enabled" ||
			(lane == "dry" && parameter == "pan") || (lane == "wet" && parameter == "width")
		valueValid := finiteSceneNumber(value)
		if parameter == "enabled" {
			_, valueValid = value.(bool)
		}
		if !hasWDW || blockID != "" || !laneValid || !parameterValid || !valueValid {
			return "", errors.New("scene wet/dry/wet lane target is invalid")
		}
		return "wdwLane\x1f" + lane + "\x1f" + parameter, nil
	default:
		return "", errors.New("unknown scene target type")
	}
}

func sameAddressSet(left, right map[string]struct{}) bool {
	if len(left) != len(right) {
		return false
	}
	for address := range left {
		if _, ok := right[address]; !ok {
			return false
		}
	}
	return true
}

func validateSceneSet(preset Preset, version float64) error {
	raw, present := preset["sceneSet"]
	if version == 4 && (!present || raw == nil) {
		return errors.New("preset version 4 requires a scene set")
	}
	if version != 4 && present && raw != nil {
		return errors.New("scenes require preset version 4")
	}
	if version != 4 {
		return nil
	}
	sceneSet, ok := raw.(map[string]any)
	if !ok {
		return errors.New("scene set must be an object")
	}
	openIn, ok := sceneSet["openIn"].(string)
	if !ok || (openIn != "presets" && openIn != "scenes") {
		return errors.New("scene set openIn must be presets or scenes")
	}
	scenes, ok := sceneSet["scenes"].([]any)
	if !ok || len(scenes) != 4 {
		return errors.New("scene set requires exactly four scenes")
	}
	blockTypes := make(map[string]string)
	blocks, _ := preset["blocks"].([]any)
	collectBlockTypes(blocks, blockTypes)
	wdw, hasWDW := preset["wdw"].(map[string]any)
	if hasWDW {
		for _, laneName := range []string{"dry", "wet"} {
			lane, _ := wdw[laneName].(map[string]any)
			children, _ := lane["blocks"].([]any)
			collectBlockTypes(children, blockTypes)
		}
	}
	ids := make(map[string]struct{})
	var expected map[string]struct{}
	for index, rawScene := range scenes {
		scene, ok := rawScene.(map[string]any)
		if !ok {
			return errors.New("scene must be an object")
		}
		id, ok := scene["id"].(string)
		if !ok || !validSceneID(id) {
			return errors.New("scene IDs must be safe identifiers")
		}
		if _, exists := ids[id]; exists {
			return errors.New("scene IDs must be unique")
		}
		ids[id] = struct{}{}
		name, ok := scene["name"].(string)
		if !ok || !utf8.ValidString(name) || strings.TrimSpace(name) != name || utf8.RuneCountInString(name) < 1 || utf8.RuneCountInString(name) > 24 {
			return errors.New("scene name must contain 1 to 24 characters without surrounding whitespace")
		}
		for _, char := range name {
			if unicode.IsControl(char) {
				return errors.New("scene name cannot contain control characters")
			}
		}
		enterTime, ok := scene["enterTimeMs"].(float64)
		if !ok || enterTime != math.Trunc(enterTime) || (enterTime != 0 && (enterTime < 100 || enterTime > 10000 || int(enterTime)%100 != 0)) {
			return errors.New("scene enter time must be instant or 100 to 10000 ms in 100 ms steps")
		}
		trim, ok := scene["outputTrimDb"].(float64)
		if !ok || math.IsNaN(trim) || math.IsInf(trim, 0) || trim < -12 || trim > 6 {
			return errors.New("scene output trim must be between -12 and 6 dB")
		}
		targets, ok := scene["targets"].([]any)
		if !ok || len(targets) > 512 {
			return errors.New("scene targets must be an array with at most 512 entries")
		}
		addresses := make(map[string]struct{}, len(targets))
		for _, rawTarget := range targets {
			target, ok := rawTarget.(map[string]any)
			if !ok {
				return errors.New("scene target must be an object")
			}
			address, err := validateSceneTarget(target, blockTypes, hasWDW)
			if err != nil {
				return err
			}
			if _, duplicate := addresses[address]; duplicate {
				return errors.New("scene target addresses must be unique")
			}
			addresses[address] = struct{}{}
		}
		if index == 0 {
			expected = addresses
		} else if !sameAddressSet(expected, addresses) {
			return errors.New("all scenes must define the same target addresses")
		}
	}
	defaultID, ok := sceneSet["defaultSceneId"].(string)
	if !ok {
		return errors.New("scene set requires a default scene ID")
	}
	if _, exists := ids[defaultID]; !exists {
		return errors.New("default scene ID must reference one of the four scenes")
	}
	return nil
}

// ValidateRunnable applies the runtime topology contract in addition to the
// storage/schema contract. Draft WDW presets may be saved while being edited,
// but they must not be submitted to the pedal as an apply request until both
// lanes can construct a real engine.
func ValidateRunnable(preset Preset) error {
	if err := Validate(preset); err != nil {
		return err
	}
	if preset["routing"] != "wdw" {
		return nil
	}
	wdw := preset["wdw"].(map[string]any)
	blockIDs := make(map[string]struct{})
	for _, laneName := range []string{"dry", "wet"} {
		lane := wdw[laneName].(map[string]any)
		blocks := lane["blocks"].([]any)
		namCount := 0
		cabCount := 0
		namIndex := -1
		cabIndex := -1
		timeSeen := false
		for index, raw := range blocks {
			block := raw.(map[string]any)
			id, ok := block["id"].(string)
			if !ok || id == "" {
				return fmt.Errorf("WDW %s lane contains a block with no ID", laneName)
			}
			if _, exists := blockIDs[id]; exists {
				return fmt.Errorf("WDW routing requires globally unique block IDs: %s", id)
			}
			blockIDs[id] = struct{}{}
			typeName, ok := block["type"].(string)
			if !ok || typeName == "" {
				return fmt.Errorf("WDW %s lane block %s has no type", laneName, id)
			}
			if !wdwBlockAllowed(laneName, typeName) {
				return fmt.Errorf("WDW %s lane does not admit %s block: %s", laneName, typeName, id)
			}
			enabled, ok := block["enabled"].(bool)
			if !ok {
				return fmt.Errorf("WDW %s lane block %s must declare enabled", laneName, id)
			}
			if enabled && !wdwBlockModeSupported(typeName, block) {
				return fmt.Errorf("WDW %s lane block is unsupported: %s", laneName, id)
			}
			if typeName == "nam" {
				if !enabled {
					return fmt.Errorf("WDW %s lane cannot disable its required NAM block", laneName)
				}
				namCount++
				namIndex = index
			}
			if typeName == "cab" && enabled {
				if timeSeen {
					return fmt.Errorf("WDW %s lane requires cabinet before time-based effects", laneName)
				}
				cabCount++
				cabIndex = index
			}
			if enabled && (typeName == "mod" || typeName == "delay" || typeName == "reverb" ||
				typeName == "irreverb" || typeName == "stereo") {
				if namIndex < 0 {
					return fmt.Errorf("WDW %s lane requires NAM before time-based effects", laneName)
				}
				if cabIndex >= 0 && index < cabIndex {
					return fmt.Errorf("WDW %s lane requires cabinet before time-based effects", laneName)
				}
				timeSeen = true
			}
		}
		if namCount != 1 {
			return fmt.Errorf("WDW %s lane requires exactly one enabled NAM block", laneName)
		}
		if cabCount > 1 {
			return fmt.Errorf("WDW %s lane supports at most one enabled cabinet block", laneName)
		}
		if cabIndex >= 0 && namIndex > cabIndex {
			return fmt.Errorf("WDW %s lane requires NAM before cabinet", laneName)
		}
	}
	return nil
}

// ValidateRunnableAt adds the asset-readiness checks that managerd can perform
// before handing an otherwise valid WDW topology to the pedal runtime.
func ValidateRunnableAt(preset Preset, dataRoot string) error {
	if err := ValidateRunnable(preset); err != nil {
		return err
	}
	if preset["routing"] != "wdw" {
		return nil
	}
	wdw := preset["wdw"].(map[string]any)
	for _, laneName := range []string{"dry", "wet"} {
		lane := wdw[laneName].(map[string]any)
		for _, raw := range lane["blocks"].([]any) {
			block := raw.(map[string]any)
			enabled, _ := block["enabled"].(bool)
			if !enabled {
				continue
			}
			typeName, _ := block["type"].(string)
			id, _ := block["id"].(string)
			asset := ""
			switch typeName {
			case "nam", "cab", "irreverb":
				asset, _ = block["asset"].(string)
			case "wah":
				asset = "assets/wah/gcb95.wahtable"
			default:
				continue
			}
			if asset == "" || !validRelativeAsset(asset) {
				return fmt.Errorf("WDW %s lane block is missing its asset: %s", laneName, id)
			}
			info, err := os.Stat(filepath.Join(dataRoot, filepath.FromSlash(asset)))
			if err != nil || !info.Mode().IsRegular() {
				return fmt.Errorf("WDW %s lane block asset is not ready: %s", laneName, id)
			}
		}
	}
	return nil
}

func wdwBlockAllowed(laneName, typeName string) bool {
	if laneName == "dry" {
		switch typeName {
		case "nam", "cab", "dynamics", "eq", "distortion", "wah":
			return true
		}
		return false
	}
	switch typeName {
	case "nam", "cab", "mod", "delay", "reverb", "irreverb", "stereo":
		return true
	}
	return false
}

func wdwBlockModeSupported(typeName string, block map[string]any) bool {
	params, _ := block["params"].(map[string]any)
	mode, _ := params["mode"].(string)
	switch typeName {
	case "dynamics":
		return mode == "compressor" || mode == "noise_gate" || mode == "transient_shaper"
	case "eq":
		return mode == "parametric_eq_5"
	case "distortion":
		return mode == "" || mode == "rat" || mode == "big_cheese" || mode == "tape"
	case "wah":
		return mode == "" || mode == "gcb95"
	case "mod":
		return stringIn(mode, "chorus", "flanger", "rotary", "vibe", "phaser", "vintage_trem",
			"poly_octave", "pattern_trem", "auto_swell", "filter", "ladder_sweep", "formant",
			"quadrature", "destroyer", "whammy", "harmonizer")
	case "delay":
		return stringIn(mode, "digital", "tape", "dual", "filter", "lofi", "dbucket", "duck",
			"pattern", "swell", "trem")
	case "reverb":
		return stringIn(mode, "room", "hall", "plate", "spring", "bloom", "cloud", "shimmer",
			"chorale", "nonlinear", "swell", "magneto", "reflections")
	default:
		return true
	}
}

func stringIn(value string, choices ...string) bool {
	for _, choice := range choices {
		if value == choice {
			return true
		}
	}
	return false
}

func validateBlocks(blocks []any, version float64, insideLane bool) error {
	for _, item := range blocks {
		block, ok := item.(map[string]any)
		if !ok {
			return errors.New("preset block must be an object")
		}
		asset, _ := block["asset"].(string)
		if asset != "" && !validRelativeAsset(asset) {
			return errors.New("preset asset must stay under data root")
		}
		if policy, present := block["sceneBypass"]; present {
			policyName, ok := policy.(string)
			if !ok || (policyName != "cut" && policyName != "letRing") {
				return errors.New("scene bypass must be cut or letRing")
			}
			typeName, _ := block["type"].(string)
			if policyName == "letRing" && (version != 4 ||
				(typeName != "delay" && typeName != "reverb" && typeName != "irreverb")) {
				return errors.New("let-ring scene bypass requires a version 4 delay or reverb block")
			}
		}
		if block["type"] == "dualAmp" {
			if insideLane {
				return errors.New("dual rig lanes cannot contain split blocks")
			}
			params, ok := block["params"].(map[string]any)
			if !ok {
				return errors.New("dual amp params must be an object")
			}
			for _, key := range []string{"leftNamAsset", "leftIrAsset", "rightNamAsset", "rightIrAsset"} {
				asset, ok := params[key].(string)
				if !ok {
					return fmt.Errorf("dual amp %s must be an asset path", key)
				}
				if asset != "" && !validRelativeAsset(asset) {
					return fmt.Errorf("dual amp %s must stay under data root", key)
				}
			}
		}
		if block["type"] == "dualRig" {
			if version != 2 && version != 4 {
				return errors.New("dual rig requires preset version 2 or 4")
			}
			if insideLane {
				return errors.New("nested dual rig blocks are not supported")
			}
			lanes, ok := block["lanes"].(map[string]any)
			if !ok {
				return errors.New("dual rig lanes must be an object")
			}
			for _, laneName := range []string{"left", "right"} {
				lane, ok := lanes[laneName].(map[string]any)
				if !ok {
					return fmt.Errorf("dual rig %s lane must be an object", laneName)
				}
				children, ok := lane["blocks"].([]any)
				if !ok || len(children) == 0 {
					return fmt.Errorf("dual rig %s lane must contain blocks", laneName)
				}
				if err := validateBlocks(children, version, true); err != nil {
					return err
				}
			}
		}
	}
	return nil
}

// normalizeLegacyEffectBlocks upgrades the generic effect placeholders emitted
// by early pedal UI builds to the concrete effects they represented. Without
// this mapping they appear as unsupported blocks and expose no controls.
func normalizeLegacyEffectBlocks(preset Preset) {
	blocks, ok := preset["blocks"].([]any)
	if ok {
		normalizeLegacyBlocks(blocks)
	}
	if wdw, ok := preset["wdw"].(map[string]any); ok {
		for _, laneName := range []string{"dry", "wet"} {
			lane, _ := wdw[laneName].(map[string]any)
			children, _ := lane["blocks"].([]any)
			normalizeLegacyBlocks(children)
		}
	}
}

func normalizeLegacyBlocks(blocks []any) {
	for _, item := range blocks {
		block, ok := item.(map[string]any)
		if !ok {
			continue
		}
		params, ok := block["params"].(map[string]any)
		if !ok {
			continue
		}
		mode, hasMode := params["mode"].(string)
		switch block["type"] {
		case "time":
			block["type"] = "delay"
			if !hasMode || mode == "" {
				params["mode"] = "tape"
			}
		case "modulation":
			block["type"] = "mod"
			if !hasMode || mode == "" {
				params["mode"] = "chorus"
			}
		case "dynamics":
			if !hasMode || mode == "" {
				params["mode"] = "compressor"
			}
		}
		if lanes, ok := block["lanes"].(map[string]any); ok {
			for _, laneName := range []string{"left", "right"} {
				lane, _ := lanes[laneName].(map[string]any)
				children, _ := lane["blocks"].([]any)
				normalizeLegacyBlocks(children)
			}
		}
	}
}

func (s Store) List() ([]Summary, error) {
	out := make([]Summary, 0, 400)
	for bank := 0; bank < 100; bank++ {
		for slot := 0; slot < 4; slot++ {
			summary := Summary{Bank: bank, Slot: slot}
			presetSlot, err := s.Load(bank, slot)
			if err == nil {
				summary.Exists = true
				if name, ok := presetSlot.Preset["name"].(string); ok {
					summary.Name = name
				}
			} else if !errors.Is(err, os.ErrNotExist) {
				return nil, fmt.Errorf("load bank %d slot %d: %w", bank, slot, err)
			}
			out = append(out, summary)
		}
	}
	return out, nil
}

func (s Store) Load(bank int, slot int) (Slot, error) {
	if err := validateSlot(bank, slot); err != nil {
		return Slot{}, err
	}
	bytes, err := os.ReadFile(s.pathFor(bank, slot))
	if err != nil {
		return Slot{}, err
	}
	var preset Preset
	if err := json.Unmarshal(bytes, &preset); err != nil {
		return Slot{}, err
	}
	normalizeLegacyEffectBlocks(preset)
	if err := Validate(preset); err != nil {
		return Slot{}, err
	}
	return Slot{Bank: bank, Slot: slot, Preset: preset}, nil
}

func (s Store) Save(bank int, slot int, preset Preset) (Slot, error) {
	if err := validateSlot(bank, slot); err != nil {
		return Slot{}, err
	}
	normalizeLegacyEffectBlocks(preset)
	if err := Validate(preset); err != nil {
		return Slot{}, err
	}
	finalPath := s.pathFor(bank, slot)
	if err := os.MkdirAll(filepath.Dir(finalPath), 0o755); err != nil {
		return Slot{}, err
	}
	tmpPath := finalPath + ".tmp"
	bytes, err := json.MarshalIndent(preset, "", "  ")
	if err != nil {
		return Slot{}, err
	}
	tmp, err := os.OpenFile(tmpPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return Slot{}, err
	}
	if _, err := tmp.Write(append(bytes, '\n')); err != nil {
		_ = tmp.Close()
		_ = os.Remove(tmpPath)
		return Slot{}, err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		_ = os.Remove(tmpPath)
		return Slot{}, err
	}
	if err := tmp.Close(); err != nil {
		_ = os.Remove(tmpPath)
		return Slot{}, err
	}
	if err := os.Rename(tmpPath, finalPath); err != nil {
		_ = os.Remove(tmpPath)
		return Slot{}, err
	}
	return Slot{Bank: bank, Slot: slot, Preset: preset}, nil
}

// ReplaceAssetReferences updates every saved slot that uses oldPath and
// returns the number of changed presets. It intentionally uses Save so every
// rewritten JSON document keeps the same validation and atomic-write rules as
// a normal manager save.
func (s Store) ReplaceAssetReferences(oldPath, newPath string) (int, error) {
	changed := 0
	for bank := 0; bank < 100; bank++ {
		for slot := 0; slot < 4; slot++ {
			loaded, err := s.Load(bank, slot)
			if errors.Is(err, os.ErrNotExist) {
				continue
			}
			if err != nil {
				return changed, fmt.Errorf("load bank %d slot %d: %w", bank, slot, err)
			}
			dirty := false
			if blocks, ok := loaded.Preset["blocks"].([]any); ok {
				dirty = replaceAssetInBlocks(blocks, oldPath, newPath)
			}
			if wdw, ok := loaded.Preset["wdw"].(map[string]any); ok {
				for _, laneName := range []string{"dry", "wet"} {
					lane, _ := wdw[laneName].(map[string]any)
					children, _ := lane["blocks"].([]any)
					dirty = replaceAssetInBlocks(children, oldPath, newPath) || dirty
				}
			}
			if !dirty {
				continue
			}
			if _, err := s.Save(bank, slot, loaded.Preset); err != nil {
				return changed, fmt.Errorf("save bank %d slot %d: %w", bank, slot, err)
			}
			changed++
		}
	}
	return changed, nil
}

func replaceAssetInBlocks(blocks []any, oldPath, newPath string) bool {
	dirty := false
	for _, item := range blocks {
		block := item.(map[string]any)
		if asset, _ := block["asset"].(string); asset == oldPath {
			block["asset"] = newPath
			dirty = true
		}
		if block["type"] == "dualAmp" {
			params, _ := block["params"].(map[string]any)
			for _, key := range []string{"leftNamAsset", "leftIrAsset", "rightNamAsset", "rightIrAsset"} {
				if asset, _ := params[key].(string); asset == oldPath {
					params[key] = newPath
					dirty = true
				}
			}
		}
		if lanes, ok := block["lanes"].(map[string]any); ok {
			for _, laneName := range []string{"left", "right"} {
				lane, _ := lanes[laneName].(map[string]any)
				children, _ := lane["blocks"].([]any)
				dirty = replaceAssetInBlocks(children, oldPath, newPath) || dirty
			}
		}
	}
	return dirty
}

func (s Store) pathFor(bank int, slot int) string {
	return filepath.Join(s.root, "presets", fmt.Sprintf("bank-%03d", bank), fmt.Sprintf("preset-%d.json", slot))
}

func validateSlot(bank int, slot int) error {
	if bank < 0 || bank > 99 || slot < 0 || slot > 3 {
		return errors.New("preset slot out of range")
	}
	return nil
}

func validRelativeAsset(asset string) bool {
	if filepath.IsAbs(asset) || strings.Contains(asset, "\\") {
		return false
	}
	clean := path.Clean(asset)
	if clean == "." || strings.HasPrefix(clean, "../") || clean == ".." {
		return false
	}
	return clean == asset
}
