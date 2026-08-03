package deviceidentity

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestLoadOrCreatePersistsStableIdentity(t *testing.T) {
	root := t.TempDir()
	first, err := LoadOrCreate(root)
	if err != nil {
		t.Fatal(err)
	}
	second, err := LoadOrCreate(root)
	if err != nil {
		t.Fatal(err)
	}
	if first.DeviceID != second.DeviceID || !bytes.Equal(first.PrivateKey, second.PrivateKey) {
		t.Fatal("identity changed after reload")
	}
	info, err := os.Stat(filepath.Join(root, identityDir, identityFile))
	if err != nil {
		t.Fatal(err)
	}
	if got := info.Mode().Perm(); got != 0o600 {
		t.Fatalf("identity mode = %04o, want 0600", got)
	}
	message := []byte("ardor identity test")
	if !ed25519.Verify(first.PublicKey, message, first.Sign(message)) {
		t.Fatal("signature did not verify")
	}
}

func TestLoadOrCreateDoesNotReplaceCorruptIdentity(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, identityDir, identityFile)
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		t.Fatal(err)
	}
	corrupt := []byte("not-json")
	if err := os.WriteFile(path, corrupt, 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadOrCreate(root); err == nil {
		t.Fatal("expected corrupt identity error")
	}
	after, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(after, corrupt) {
		t.Fatal("corrupt identity was silently replaced")
	}
}

func TestLoadRejectsExposedPrivateKeyPermissions(t *testing.T) {
	root := t.TempDir()
	if _, err := LoadOrCreate(root); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(root, identityDir, identityFile)
	if err := os.Chmod(path, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadOrCreate(root); err == nil {
		t.Fatal("expected insecure permission error")
	}
}

func TestLoadRejectsMismatchedKeyPair(t *testing.T) {
	root := t.TempDir()
	if _, err := LoadOrCreate(root); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(root, identityDir, identityFile)
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var stored map[string]any
	if err := json.Unmarshal(data, &stored); err != nil {
		t.Fatal(err)
	}
	otherPublic, _, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	stored["publicKey"] = base64.StdEncoding.EncodeToString(otherPublic)
	data, err = json.Marshal(stored)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, data, 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadOrCreate(root); err == nil {
		t.Fatal("expected mismatched key error")
	}
}

func TestLoadRejectsSymlink(t *testing.T) {
	root := t.TempDir()
	target := filepath.Join(root, "target")
	if err := os.WriteFile(target, []byte("{}"), 0o600); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(root, identityDir, identityFile)
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(target, path); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadOrCreate(root); err == nil {
		t.Fatal("expected symlink error")
	}
}
