package runtimecontrol

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestQueueCommandsAtomically(t *testing.T) {
	root := t.TempDir()
	if err := QueueAssetReload(root); err != nil {
		t.Fatal(err)
	}
	if err := QueueApplyPreset(root, 2, 3); err != nil {
		t.Fatal(err)
	}

	directory := filepath.Join(root, "runtime", "commands")
	entries, err := os.ReadDir(directory)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 2 {
		t.Fatalf("commands=%d", len(entries))
	}

	commands := make(map[string]Command)
	for _, entry := range entries {
		if filepath.Ext(entry.Name()) != ".json" {
			t.Fatalf("unexpected command file %q", entry.Name())
		}
		body, err := os.ReadFile(filepath.Join(directory, entry.Name()))
		if err != nil {
			t.Fatal(err)
		}
		var command Command
		if err := json.Unmarshal(body, &command); err != nil {
			t.Fatal(err)
		}
		commands[command.Type] = command
	}
	if commands[TypeReloadAssets].Type != TypeReloadAssets {
		t.Fatalf("reload command=%+v", commands[TypeReloadAssets])
	}
	if apply := commands[TypeApplyPreset]; apply.Bank != 2 || apply.Slot != 3 {
		t.Fatalf("apply command=%+v", apply)
	}
}
