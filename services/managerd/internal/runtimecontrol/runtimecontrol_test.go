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
	if commands[TypeApplyPreset].ID == "" {
		t.Fatal("apply command has no correlation id")
	}
	status, err := ReadApplyStatus(root, commands[TypeApplyPreset].ID)
	if err != nil {
		t.Fatal(err)
	}
	if status.State != "pending" || status.Bank != 2 || status.Slot != 3 {
		t.Fatalf("pending status=%+v", status)
	}
}

func TestReadApplyStatusRejectsUnsafeIDs(t *testing.T) {
	if _, err := ReadApplyStatus(t.TempDir(), "../active-preset"); !os.IsNotExist(err) {
		t.Fatalf("unsafe id error=%v", err)
	}
}

func TestQueueSceneRecallUsesEphemeralCommandChannel(t *testing.T) {
	root := t.TempDir()
	if err := QueueSceneRecall(root, 42, "scene-solo", "request-1"); err != nil {
		t.Fatal(err)
	}
	body, err := os.ReadFile(filepath.Join(root, "runtime", "live-commands", mustOnlyEntry(t, filepath.Join(root, "runtime", "live-commands"))))
	if err != nil {
		t.Fatal(err)
	}
	var command Command
	if err := json.Unmarshal(body, &command); err != nil {
		t.Fatal(err)
	}
	if command.Type != TypeRecallScene || command.Generation != 42 || command.SceneID != "scene-solo" || command.RequestID != "request-1" {
		t.Fatalf("scene command=%+v", command)
	}
	if _, err := os.Stat(filepath.Join(root, "runtime", "commands")); !os.IsNotExist(err) {
		t.Fatal("scene recall leaked into durable command queue")
	}
}

func mustOnlyEntry(t *testing.T, directory string) string {
	t.Helper()
	entries, err := os.ReadDir(directory)
	if err != nil || len(entries) != 1 {
		t.Fatalf("entries=%v error=%v", entries, err)
	}
	return entries[0].Name()
}
