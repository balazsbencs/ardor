package backup

import (
	"archive/zip"
	"bytes"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestRoundTripReplacesAssetsAndPresets(t *testing.T) {
	source := t.TempDir()
	write(t, source, "models/clean.nam", "model")
	write(t, source, "irs/cab.wav", "cab")
	write(t, source, "reverb-irs/hall.wav", "hall")
	write(t, source, "presets/bank-002/preset-3.json", `{"version":2,"name":"Clean","routing":"serial","global":{},"blocks":[]}`)
	var archive bytes.Buffer
	manifest, err := Export(source, &archive, time.Unix(100, 0))
	if err != nil {
		t.Fatal(err)
	}
	if manifest.FileCount != 4 {
		t.Fatalf("file count=%d", manifest.FileCount)
	}

	target := t.TempDir()
	write(t, target, "models/old.nam", "old")
	reader := bytes.NewReader(archive.Bytes())
	result, err := Import(target, reader, reader.Size())
	if err != nil {
		t.Fatal(err)
	}
	if result.AssetCount != 3 || result.PresetCount != 1 {
		t.Fatalf("result=%+v", result)
	}
	if _, err := os.Stat(filepath.Join(target, "models", "old.nam")); !os.IsNotExist(err) {
		t.Fatalf("old asset still exists: %v", err)
	}
	if body, err := os.ReadFile(filepath.Join(target, "models", "clean.nam")); err != nil || string(body) != "model" {
		t.Fatalf("restored asset=%q err=%v", body, err)
	}
}

func TestInvalidArchiveDoesNotChangeCurrentData(t *testing.T) {
	target := t.TempDir()
	write(t, target, "models/current.nam", "keep")
	var archive bytes.Buffer
	zw := zip.NewWriter(&archive)
	w, _ := zw.Create("../escape.nam")
	_, _ = w.Write([]byte("bad"))
	_ = zw.Close()
	reader := bytes.NewReader(archive.Bytes())
	if _, err := Import(target, reader, reader.Size()); err == nil {
		t.Fatal("unsafe archive was accepted")
	}
	if body, err := os.ReadFile(filepath.Join(target, "models", "current.nam")); err != nil || string(body) != "keep" {
		t.Fatalf("current data changed: %q err=%v", body, err)
	}
}

func write(t *testing.T, root, name, body string) {
	t.Helper()
	filename := filepath.Join(root, filepath.FromSlash(name))
	if err := os.MkdirAll(filepath.Dir(filename), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filename, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
}
