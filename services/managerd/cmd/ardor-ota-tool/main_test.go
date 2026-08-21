package main

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/x509"
	"encoding/base64"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestBuildIsDeterministicAndVerifiable(t *testing.T) {
	publicKey, privateKey, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	directory := t.TempDir()
	pedal := filepath.Join(directory, "ardor-pedal")
	managerd := filepath.Join(directory, "ardor-managerd")
	if err := os.WriteFile(pedal, []byte("pedal"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(managerd, []byte("manager"), 0o755); err != nil {
		t.Fatal(err)
	}
	first := filepath.Join(directory, "first")
	second := filepath.Join(directory, "second")
	commit := strings.Repeat("a", 40)
	if err := build("0.1.24", commit, "0.1.24", pedal, managerd, first, privateKey); err != nil {
		t.Fatal(err)
	}
	if err := build("0.1.24", commit, "0.1.24", pedal, managerd, second, privateKey); err != nil {
		t.Fatal(err)
	}
	prefix := "ardor-device-0.1.24-linux-aarch64"
	firstBundle, _ := os.ReadFile(filepath.Join(first, prefix+".tar.gz"))
	secondBundle, _ := os.ReadFile(filepath.Join(second, prefix+".tar.gz"))
	if !bytes.Equal(firstBundle, secondBundle) {
		t.Fatal("identical inputs did not produce a deterministic bundle")
	}
	t.Setenv("ARDOR_UPDATE_PUBLIC_KEY_BASE64", base64.StdEncoding.EncodeToString(publicKey))
	if err := verify(
		filepath.Join(first, prefix+".manifest.json"),
		filepath.Join(first, prefix+".manifest.sig"),
		filepath.Join(first, prefix+".tar.gz"),
	); err != nil {
		t.Fatalf("generated release did not verify: %v", err)
	}
	if err := os.WriteFile(filepath.Join(first, prefix+".tar.gz"), append(firstBundle, 'x'), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := verify(
		filepath.Join(first, prefix+".manifest.json"),
		filepath.Join(first, prefix+".manifest.sig"),
		filepath.Join(first, prefix+".tar.gz"),
	); err == nil {
		t.Fatal("altered bundle passed verification")
	}
}

func TestStandardPKIXKeyEncodingsAreAccepted(t *testing.T) {
	publicKey, privateKey, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	encodedPrivateKey, err := x509.MarshalPKCS8PrivateKey(privateKey)
	if err != nil {
		t.Fatal(err)
	}
	encodedPublicKey, err := x509.MarshalPKIXPublicKey(publicKey)
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv("ARDOR_OTA_PRIVATE_KEY_BASE64", base64.StdEncoding.EncodeToString(encodedPrivateKey))
	t.Setenv("ARDOR_UPDATE_PUBLIC_KEY_BASE64", base64.StdEncoding.EncodeToString(encodedPublicKey))
	loadedPrivateKey, err := privateKeyFromEnvironment()
	if err != nil || !bytes.Equal(loadedPrivateKey, privateKey) {
		t.Fatalf("PKCS#8 private key was not accepted: %v", err)
	}
	loadedPublicKey, err := publicKeyFromEnvironment()
	if err != nil || !bytes.Equal(loadedPublicKey, publicKey) {
		t.Fatalf("PKIX public key was not accepted: %v", err)
	}
}
