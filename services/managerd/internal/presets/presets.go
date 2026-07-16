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
	if version, ok := preset["version"].(float64); !ok || version != 1 {
		return errors.New("preset version must be 1")
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
	for _, item := range blocks {
		block, ok := item.(map[string]any)
		if !ok {
			return errors.New("preset block must be an object")
		}
		asset, _ := block["asset"].(string)
		if asset != "" && !validRelativeAsset(asset) {
			return errors.New("preset asset must stay under data root")
		}
	}
	return nil
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
	if err := Validate(preset); err != nil {
		return Slot{}, err
	}
	return Slot{Bank: bank, Slot: slot, Preset: preset}, nil
}

func (s Store) Save(bank int, slot int, preset Preset) (Slot, error) {
	if err := validateSlot(bank, slot); err != nil {
		return Slot{}, err
	}
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
			for _, item := range loaded.Preset["blocks"].([]any) {
				block := item.(map[string]any)
				if asset, _ := block["asset"].(string); asset == oldPath {
					block["asset"] = newPath
					dirty = true
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
