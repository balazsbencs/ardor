package presets

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"
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
	if !ok || (version != 1 && version != 2) {
		return errors.New("preset version must be 1 or 2")
	}
	if routing, ok := preset["routing"].(string); !ok || routing != "serial" {
		return errors.New("preset routing must be serial")
	}
	if _, ok := preset["global"].(map[string]any); !ok {
		return errors.New("preset global must be an object")
	}
	blocks, ok := preset["blocks"].([]any)
	if !ok {
		return errors.New("preset blocks must be an array")
	}
	return validateBlocks(blocks, version, false)
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
			if version != 2 {
				return errors.New("dual rig requires preset version 2")
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
	if !ok {
		return
	}
	normalizeLegacyBlocks(blocks)
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
			dirty := replaceAssetInBlocks(loaded.Preset["blocks"].([]any), oldPath, newPath)
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
