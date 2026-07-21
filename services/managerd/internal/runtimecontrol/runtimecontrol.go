package runtimecontrol

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

const (
	TypeReloadAssets = "reload_assets"
	TypeApplyPreset  = "apply_preset"
)

type Command struct {
	Type string `json:"type"`
	Bank int    `json:"bank,omitempty"`
	Slot int    `json:"slot,omitempty"`
}

func QueueAssetReload(dataRoot string) error {
	return queue(dataRoot, Command{Type: TypeReloadAssets})
}

func QueueApplyPreset(dataRoot string, bank, slot int) error {
	return queue(dataRoot, Command{Type: TypeApplyPreset, Bank: bank, Slot: slot})
}

func queue(dataRoot string, command Command) error {
	directory := filepath.Join(dataRoot, "runtime", "commands")
	if err := os.MkdirAll(directory, 0o755); err != nil {
		return err
	}

	temporary, err := os.CreateTemp(directory, ".command-*.tmp")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer os.Remove(temporaryPath)

	if err := json.NewEncoder(temporary).Encode(command); err != nil {
		temporary.Close()
		return err
	}
	if err := temporary.Sync(); err != nil {
		temporary.Close()
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}

	finalPath := filepath.Join(directory, fmt.Sprintf("command-%020d-%s.json", time.Now().UnixNano(), filepath.Base(temporaryPath)))
	return os.Rename(temporaryPath, finalPath)
}
