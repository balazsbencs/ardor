package reset

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"

	"ardor.local/managerd/internal/localauth"
)

func TestFactoryResetRequiresPhysicalDecisionAndPreservesIdentity(t *testing.T) {
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "identity"), 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "identity", "device.json"), []byte("identity"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(root, "presets"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "presets", "user.json"), []byte("preset"), 0o644); err != nil {
		t.Fatal(err)
	}
	auth, err := localauth.New(root)
	if err != nil {
		t.Fatal(err)
	}
	manager, err := New(root, auth)
	if err != nil {
		t.Fatal(err)
	}
	status, err := manager.BeginFactoryReset(time.Now().UTC())
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, "presets", "user.json")); err != nil {
		t.Fatal("content was removed before physical approval")
	}
	decisionPath := filepath.Join(root, "runtime", "local-access", "factory-reset-decision.json")
	if err := writeAtomicJSON(decisionPath, decision{Version: 1, ResetID: status.ResetID, Approved: true}); err != nil {
		t.Fatal(err)
	}
	manager.poll(time.Now().UTC())
	if _, err := os.Stat(filepath.Join(root, "presets")); !os.IsNotExist(err) {
		t.Fatal("factory reset did not clear presets")
	}
	if data, err := os.ReadFile(filepath.Join(root, "identity", "device.json")); err != nil || string(data) != "identity" {
		t.Fatalf("identity was not preserved: %q err=%v", data, err)
	}
	if !auth.SetupRequired() {
		t.Fatal("factory reset did not restore setup mode")
	}
}

func TestApplyingFactoryResetRecoversAfterRestart(t *testing.T) {
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "identity"), 0o700); err != nil {
		t.Fatal(err)
	}
	auth, _ := localauth.New(root)
	marker := operation{Version: 1, ResetID: "interrupted", Kind: "factory", State: "applying", CreatedAt: time.Now().UTC()}
	if err := writeAtomicJSON(filepath.Join(root, "identity", "reset-operation.json"), marker); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(root, "models"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "models", "old.nam"), []byte("old"), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := New(root, auth); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, "models")); !os.IsNotExist(err) {
		t.Fatal("restart did not complete the applying reset")
	}
	audit, err := os.ReadFile(filepath.Join(root, "identity", "reset-audit.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	var event map[string]any
	if json.Unmarshal(audit, &event) != nil || event["resetId"] != "interrupted" {
		t.Fatalf("audit=%s", audit)
	}
}
