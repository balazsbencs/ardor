package assets

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSanitizeFilename(t *testing.T) {
	got, err := SanitizeFilename("../../Clean Amp!.nam", KindModel)
	if err != nil {
		t.Fatal(err)
	}
	if got != "Clean_Amp_.nam" {
		t.Fatalf("got %q", got)
	}

	got, err = SanitizeFilename("Room 1.WAV", KindIR)
	if err != nil {
		t.Fatal(err)
	}
	if got != "Room_1.wav" {
		t.Fatalf("got %q", got)
	}

	if _, err := SanitizeFilename("cab.wav", KindModel); err == nil {
		t.Fatal("expected wrong extension to fail")
	}
}

func TestStoreSaveListDelete(t *testing.T) {
	root := t.TempDir()
	store := NewStore(root)

	info, err := store.Save(KindModel, "Clean Amp!.nam", strings.NewReader("nam-bytes"), false)
	if err != nil {
		t.Fatal(err)
	}
	if info.ID != "Clean_Amp_.nam" || info.Path != "models/Clean_Amp_.nam" || info.Kind != "model" {
		t.Fatalf("bad info: %+v", info)
	}
	if _, err := os.Stat(filepath.Join(root, "models", "Clean_Amp_.nam")); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, "models", "Clean_Amp_.nam.tmp")); !os.IsNotExist(err) {
		t.Fatalf("tmp file should be gone, err=%v", err)
	}

	if _, err := store.Save(KindModel, "Clean Amp!.nam", strings.NewReader("new"), false); err == nil {
		t.Fatal("duplicate without overwrite should fail")
	}
	if _, err := store.Save(KindModel, "Clean Amp!.nam", strings.NewReader("new"), true); err != nil {
		t.Fatal(err)
	}

	if _, err := store.Save(KindIR, "Open Back.wav", strings.NewReader("wav"), false); err != nil {
		t.Fatal(err)
	}
	models, err := store.List(KindModel)
	if err != nil {
		t.Fatal(err)
	}
	if len(models) != 1 {
		t.Fatalf("models len=%d", len(models))
	}
	irs, err := store.List(KindIR)
	if err != nil {
		t.Fatal(err)
	}
	if len(irs) != 1 || irs[0].Path != "irs/Open_Back.wav" {
		t.Fatalf("bad irs: %+v", irs)
	}

	if err := store.Delete(KindModel, "Clean_Amp_.nam"); err != nil {
		t.Fatal(err)
	}
	models, err = store.List(KindModel)
	if err != nil {
		t.Fatal(err)
	}
	if len(models) != 0 {
		t.Fatalf("models len after delete=%d", len(models))
	}
}
