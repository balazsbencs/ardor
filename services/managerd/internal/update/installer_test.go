package update

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"testing"
)

func TestExtractBundleRequiresExactSignedFiles(t *testing.T) {
	files := map[string][]byte{
		"bin/ardor-pedal":    []byte("pedal-binary"),
		"bin/ardor-managerd": []byte("manager-binary"),
	}
	manifest := validManifest()
	for index := range manifest.Files {
		contents := files[manifest.Files[index].Path]
		digest := sha256.Sum256(contents)
		manifest.Files[index].Size = int64(len(contents))
		manifest.Files[index].SHA256 = hex.EncodeToString(digest[:])
	}
	bundle := filepath.Join(t.TempDir(), "bundle.tar.gz")
	writeTestBundle(t, bundle, files)
	destination := filepath.Join(t.TempDir(), "release")
	if err := extractBundle(bundle, destination, manifest); err != nil {
		t.Fatalf("valid bundle rejected: %v", err)
	}
	for name, expected := range files {
		actual, err := os.ReadFile(filepath.Join(destination, filepath.FromSlash(name)))
		if err != nil || string(actual) != string(expected) {
			t.Fatalf("extracted %s=%q err=%v", name, actual, err)
		}
	}
}

func TestExtractBundleRejectsUnexpectedEntry(t *testing.T) {
	manifest := validManifest()
	bundle := filepath.Join(t.TempDir(), "bundle.tar.gz")
	writeTestBundle(t, bundle, map[string][]byte{"../escape": []byte("bad")})
	if err := extractBundle(bundle, filepath.Join(t.TempDir(), "release"), manifest); err == nil {
		t.Fatal("unexpected traversal entry was accepted")
	}
}

func TestRecoverRollsBackUnconfirmedFactoryUpdate(t *testing.T) {
	systemRoot := t.TempDir()
	if err := os.MkdirAll(filepath.Join(systemRoot, "releases", "0.1.24"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("releases/0.1.24", filepath.Join(systemRoot, "current")); err != nil {
		t.Fatal(err)
	}
	transaction := Transaction{SchemaVersion: 1, Phase: "switched", Version: "0.1.24", Factory: true}
	if err := writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := (Installer{}).Recover(context.Background(), systemRoot); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Lstat(filepath.Join(systemRoot, "current")); !os.IsNotExist(err) {
		t.Fatalf("factory current link was not removed: %v", err)
	}
	var status Status
	if err := readJSONFile(filepath.Join(systemRoot, "update", "operation.json"), 256<<10, &status); err != nil {
		t.Fatal(err)
	}
	if status.State != "rolled_back" || status.ErrorCode != "power_loss_recovery" {
		t.Fatalf("unexpected recovery status: %+v", status)
	}
}

func TestRecoverRollsBackActivationBeforePhaseWasRecorded(t *testing.T) {
	systemRoot := t.TempDir()
	for _, version := range []string{"0.1.23", "0.1.24"} {
		if err := os.MkdirAll(filepath.Join(systemRoot, "releases", version), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.Symlink("releases/0.1.24", filepath.Join(systemRoot, "current")); err != nil {
		t.Fatal(err)
	}
	transaction := Transaction{
		SchemaVersion: 1, Phase: "prepared", Version: "0.1.24", Previous: "releases/0.1.23",
	}
	if err := writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := (Installer{}).Recover(context.Background(), systemRoot); err != nil {
		t.Fatal(err)
	}
	current, err := os.Readlink(filepath.Join(systemRoot, "current"))
	if err != nil || current != "releases/0.1.23" {
		t.Fatalf("prepared transaction was not rolled back: current=%q err=%v", current, err)
	}
	var status Status
	if err := readJSONFile(filepath.Join(systemRoot, "update", "operation.json"), 256<<10, &status); err != nil {
		t.Fatal(err)
	}
	if status.State != "rolled_back" || status.ErrorCode != "power_loss_recovery" {
		t.Fatalf("unexpected recovery status: %+v", status)
	}
}

func TestRecoverClearsStaleLockAndMarksPreTransactionInterruption(t *testing.T) {
	systemRoot := t.TempDir()
	updateRoot := filepath.Join(systemRoot, "update")
	if err := os.MkdirAll(updateRoot, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(updateRoot, "lock"), nil, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := writeJSONAtomic(filepath.Join(updateRoot, "operation.json"), Status{State: "downloading"}, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := (Installer{}).Recover(context.Background(), systemRoot); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(updateRoot, "lock")); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("stale updater lock remains: %v", err)
	}
	var status Status
	if err := readJSONFile(filepath.Join(updateRoot, "operation.json"), 256<<10, &status); err != nil {
		t.Fatal(err)
	}
	if status.State != "failed" || status.ErrorCode != "power_loss_interrupted" {
		t.Fatalf("unexpected interrupted status: %+v", status)
	}
}

func TestRecoverMarksPreparedButNotActivatedUpdateInterrupted(t *testing.T) {
	systemRoot := t.TempDir()
	for _, version := range []string{"0.1.23", "0.1.24"} {
		if err := os.MkdirAll(filepath.Join(systemRoot, "releases", version), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.Symlink("releases/0.1.23", filepath.Join(systemRoot, "current")); err != nil {
		t.Fatal(err)
	}
	transaction := Transaction{
		SchemaVersion: 1, Phase: "prepared", Version: "0.1.24", Previous: "releases/0.1.23",
	}
	if err := writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := writeJSONAtomic(filepath.Join(systemRoot, "update", "operation.json"), Status{State: "staged"}, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := (Installer{}).Recover(context.Background(), systemRoot); err != nil {
		t.Fatal(err)
	}
	var status Status
	if err := readJSONFile(filepath.Join(systemRoot, "update", "operation.json"), 256<<10, &status); err != nil {
		t.Fatal(err)
	}
	if status.State != "failed" || status.ErrorCode != "power_loss_interrupted" {
		t.Fatalf("unexpected interrupted status: %+v", status)
	}
}

func TestRecoverCompletesRollbackWhosePhaseWriteWasInterrupted(t *testing.T) {
	systemRoot := t.TempDir()
	for _, version := range []string{"0.1.23", "0.1.24"} {
		if err := os.MkdirAll(filepath.Join(systemRoot, "releases", version), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.Symlink("releases/0.1.23", filepath.Join(systemRoot, "current")); err != nil {
		t.Fatal(err)
	}
	transaction := Transaction{
		SchemaVersion: 1, Phase: "switched", Version: "0.1.24", Previous: "releases/0.1.23",
	}
	if err := writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := (Installer{}).Recover(context.Background(), systemRoot); err != nil {
		t.Fatal(err)
	}
	current, err := os.Readlink(filepath.Join(systemRoot, "current"))
	if err != nil || current != "releases/0.1.23" {
		t.Fatalf("rollback target changed: current=%q err=%v", current, err)
	}
}

func TestRecoverFinalizesConfirmedUpdate(t *testing.T) {
	systemRoot := t.TempDir()
	for _, version := range []string{"0.1.23", "0.1.24"} {
		if err := os.MkdirAll(filepath.Join(systemRoot, "releases", version), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.Symlink("releases/0.1.24", filepath.Join(systemRoot, "current")); err != nil {
		t.Fatal(err)
	}
	transaction := Transaction{
		SchemaVersion: 1, Phase: "confirmed", Version: "0.1.24", Previous: "releases/0.1.23",
	}
	if err := writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := (Installer{}).Recover(context.Background(), systemRoot); err != nil {
		t.Fatal(err)
	}
	previous, err := os.Readlink(filepath.Join(systemRoot, "previous"))
	if err != nil || previous != "releases/0.1.23" {
		t.Fatalf("rollback release was not retained: previous=%q err=%v", previous, err)
	}
	var status Status
	if err := readJSONFile(filepath.Join(systemRoot, "update", "operation.json"), 256<<10, &status); err != nil {
		t.Fatal(err)
	}
	if status.State != "succeeded" {
		t.Fatalf("unexpected confirmed status: %+v", status)
	}
}

func TestPruneOldReleasesKeepsOnlyActiveAndRollbackVersions(t *testing.T) {
	systemRoot := t.TempDir()
	for _, version := range []string{"0.1.22", "0.1.23", "0.1.24"} {
		if err := os.MkdirAll(filepath.Join(systemRoot, "releases", version), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(systemRoot, "releases", "operator-note"), []byte("keep"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("releases/0.1.24", filepath.Join(systemRoot, "current")); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("releases/0.1.23", filepath.Join(systemRoot, "previous")); err != nil {
		t.Fatal(err)
	}
	if err := pruneOldReleases(systemRoot); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(systemRoot, "releases", "0.1.22")); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("obsolete release remains: %v", err)
	}
	for _, name := range []string{"0.1.23", "0.1.24", "operator-note"} {
		if _, err := os.Stat(filepath.Join(systemRoot, "releases", name)); err != nil {
			t.Fatalf("retained entry %q missing: %v", name, err)
		}
	}
}

func TestRestorePreviousRefusesForeignCurrentLink(t *testing.T) {
	systemRoot := t.TempDir()
	if err := os.Symlink("releases/other", filepath.Join(systemRoot, "current")); err != nil {
		t.Fatal(err)
	}
	transaction := Transaction{SchemaVersion: 1, Phase: "switched", Version: "0.1.24", Previous: "releases/0.1.23"}
	if err := restorePrevious(systemRoot, transaction); err == nil {
		t.Fatal("rollback replaced a current link it did not own")
	}
}

func writeTestBundle(t *testing.T, destination string, files map[string][]byte) {
	t.Helper()
	file, err := os.Create(destination)
	if err != nil {
		t.Fatal(err)
	}
	gzipWriter := gzip.NewWriter(file)
	tarWriter := tar.NewWriter(gzipWriter)
	for name, contents := range files {
		header := &tar.Header{Name: name, Mode: 0o755, Size: int64(len(contents)), Typeflag: tar.TypeReg}
		if err := tarWriter.WriteHeader(header); err != nil {
			t.Fatal(err)
		}
		if _, err := tarWriter.Write(contents); err != nil {
			t.Fatal(err)
		}
	}
	if err := tarWriter.Close(); err != nil {
		t.Fatal(err)
	}
	if err := gzipWriter.Close(); err != nil {
		t.Fatal(err)
	}
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}
}
