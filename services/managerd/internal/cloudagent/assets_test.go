package cloudagent

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestAssetTransferPublishesOnlyValidatedContent(t *testing.T) {
	root := t.TempDir()
	registry := newAssetTransferRegistry(root)
	content := []byte(`{"version":"0.5.0","weights":[1,2,3]}`)
	transferID := "018f7f1a-8b25-7e31-a951-5c43272e2001"

	if _, failure := registry.begin(assetPayload(t, map[string]any{
		"transferId": transferID, "kind": "models", "filename": "T3K clean.nam",
		"overwrite": false, "size": len(content), "source": map[string]any{"provider": "tone3000", "modelId": 9},
	})); failure != nil {
		t.Fatal(failure)
	}
	if _, err := os.Stat(filepath.Join(root, "models", "T3K_clean.nam")); !os.IsNotExist(err) {
		t.Fatal("staged model became visible before commit")
	}
	if _, failure := registry.chunk(assetPayload(t, map[string]any{
		"transferId": transferID, "offset": 0, "data": base64.StdEncoding.EncodeToString(content),
	})); failure != nil {
		t.Fatal(failure)
	}
	digest := sha256.Sum256(content)
	result, failure := registry.commit(assetPayload(t, map[string]any{"transferId": transferID, "sha256": hex.EncodeToString(digest[:])}))
	if failure != nil {
		t.Fatal(failure)
	}
	if result == nil {
		t.Fatal("commit returned no asset")
	}
	installed, err := os.ReadFile(filepath.Join(root, "models", "T3K_clean.nam"))
	if err != nil || string(installed) != string(content) {
		t.Fatalf("installed model=%q err=%v", installed, err)
	}
	metadata, err := os.ReadFile(filepath.Join(root, "assets", "metadata", "model-T3K_clean.nam.json"))
	if err != nil || !json.Valid(metadata) {
		t.Fatalf("metadata=%q err=%v", metadata, err)
	}
}

func TestAssetTransferRejectsInvalidModelAndCleansStage(t *testing.T) {
	root := t.TempDir()
	registry := newAssetTransferRegistry(root)
	content := []byte("not a NAM model")
	transferID := "018f7f1a-8b25-7e31-a951-5c43272e2002"
	if _, failure := registry.begin(assetPayload(t, map[string]any{
		"transferId": transferID, "kind": "models", "filename": "broken.nam", "overwrite": false, "size": len(content),
	})); failure != nil {
		t.Fatal(failure)
	}
	if _, failure := registry.chunk(assetPayload(t, map[string]any{
		"transferId": transferID, "offset": 0, "data": base64.StdEncoding.EncodeToString(content),
	})); failure != nil {
		t.Fatal(failure)
	}
	digest := sha256.Sum256(content)
	if _, failure := registry.commit(assetPayload(t, map[string]any{"transferId": transferID, "sha256": hex.EncodeToString(digest[:])})); failure == nil || failure.Code != "invalid_asset" {
		t.Fatalf("failure=%+v", failure)
	}
	if _, err := os.Stat(filepath.Join(root, "models", "broken.nam")); !os.IsNotExist(err) {
		t.Fatal("invalid model was published")
	}
	if entries, err := os.ReadDir(filepath.Join(root, "cloud-transfers")); err != nil || len(entries) != 0 {
		t.Fatalf("staged files=%v err=%v", entries, err)
	}
}

func assetPayload(t *testing.T, value any) json.RawMessage {
	t.Helper()
	encoded, err := json.Marshal(value)
	if err != nil {
		t.Fatal(err)
	}
	return encoded
}
