package buildinfo

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadUsesBaseAndActiveMetadata(t *testing.T) {
	directory := t.TempDir()
	base := filepath.Join(directory, "base.json")
	active := filepath.Join(directory, "active.json")
	if err := os.WriteFile(base, []byte(`{"version":"0.1.24","commit":"base-commit"}`), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(active, []byte(`{"version":"0.1.25","commit":"active-commit","ignored":"allowed-for-manifest"}`), 0o600); err != nil {
		t.Fatal(err)
	}
	info, err := Load(active, base)
	if err != nil {
		t.Fatal(err)
	}
	if info.SoftwareVersion != "0.1.25" || info.BuildCommit != "active-commit" || info.BaseVersion != "0.1.24" {
		t.Fatalf("unexpected build info: %+v", info)
	}
}

func TestLoadRejectsInvalidVersion(t *testing.T) {
	path := filepath.Join(t.TempDir(), "release.json")
	if err := os.WriteFile(path, []byte(`{"version":"latest","commit":"commit"}`), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := Load("", path); err == nil {
		t.Fatal("invalid release version was accepted")
	}
}
