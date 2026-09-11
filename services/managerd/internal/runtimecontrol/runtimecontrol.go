package runtimecontrol

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	TypeReloadAssets = "reload_assets"
	TypeApplyPreset  = "apply_preset"
)

type Command struct {
	ID   string `json:"id,omitempty"`
	Type string `json:"type"`
	Bank int    `json:"bank,omitempty"`
	Slot int    `json:"slot,omitempty"`
}

type ApplyStatus struct {
	ID        string `json:"id"`
	State     string `json:"state"`
	Bank      int    `json:"bank"`
	Slot      int    `json:"slot"`
	Message   string `json:"message,omitempty"`
	UpdatedAt string `json:"updatedAt,omitempty"`
}

type ActivePreset struct {
	Bank      int    `json:"bank"`
	Slot      int    `json:"slot"`
	Name      string `json:"name,omitempty"`
	UpdatedAt string `json:"updatedAt,omitempty"`
}

func QueueAssetReload(dataRoot string) error {
	return queue(dataRoot, Command{Type: TypeReloadAssets})
}

func QueueApplyPreset(dataRoot string, bank, slot int) error {
	_, err := QueueApplyPresetWithID(dataRoot, bank, slot)
	return err
}

func QueueApplyPresetWithID(dataRoot string, bank, slot int) (string, error) {
	id := fmt.Sprintf("apply-%d", time.Now().UnixNano())
	status := ApplyStatus{
		ID:        id,
		State:     "pending",
		Bank:      bank,
		Slot:      slot,
		UpdatedAt: time.Now().UTC().Format(time.RFC3339Nano),
	}
	if err := writeApplyStatus(dataRoot, status); err != nil {
		return "", err
	}
	if err := queue(dataRoot, Command{ID: id, Type: TypeApplyPreset, Bank: bank, Slot: slot}); err != nil {
		status.State = "rejected"
		status.Message = err.Error()
		status.UpdatedAt = time.Now().UTC().Format(time.RFC3339Nano)
		_ = writeApplyStatus(dataRoot, status)
		return "", err
	}
	return id, nil
}

func ReadApplyStatus(dataRoot, id string) (ApplyStatus, error) {
	if !validApplyID(id) {
		return ApplyStatus{}, os.ErrNotExist
	}
	body, err := os.ReadFile(filepath.Join(dataRoot, "runtime", "apply-results", id+".json"))
	if err != nil {
		return ApplyStatus{}, err
	}
	var status ApplyStatus
	if err := json.Unmarshal(body, &status); err != nil {
		return ApplyStatus{}, err
	}
	if status.ID != id {
		return ApplyStatus{}, fmt.Errorf("apply status id mismatch")
	}
	return status, nil
}

func ReadActivePreset(dataRoot string) (ActivePreset, error) {
	body, err := os.ReadFile(filepath.Join(dataRoot, "runtime", "active-preset.json"))
	if err != nil {
		return ActivePreset{}, err
	}
	var active ActivePreset
	if err := json.Unmarshal(body, &active); err != nil {
		return ActivePreset{}, err
	}
	return active, nil
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

func writeApplyStatus(dataRoot string, status ApplyStatus) error {
	directory := filepath.Join(dataRoot, "runtime", "apply-results")
	if err := os.MkdirAll(directory, 0o755); err != nil {
		return err
	}
	return writeJSONAtomic(filepath.Join(directory, status.ID+".json"), status)
}

func writeJSONAtomic(path string, value any) error {
	directory := filepath.Dir(path)
	if err := os.MkdirAll(directory, 0o755); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(directory, ".status-*.tmp")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer os.Remove(temporaryPath)
	if err := json.NewEncoder(temporary).Encode(value); err != nil {
		_ = temporary.Close()
		return err
	}
	if err := temporary.Sync(); err != nil {
		_ = temporary.Close()
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	return os.Rename(temporaryPath, path)
}

func validApplyID(id string) bool {
	if !strings.HasPrefix(id, "apply-") || len(id) <= len("apply-") {
		return false
	}
	for _, character := range id[len("apply-"):] {
		if character < '0' || character > '9' {
			return false
		}
	}
	return true
}
