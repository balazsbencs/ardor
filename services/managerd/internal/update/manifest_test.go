package update

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"strings"
	"testing"
)

func validManifest() Manifest {
	return Manifest{
		SchemaVersion: ManifestSchemaVersion,
		Version:       "0.1.24", Tag: "v0.1.24",
		Commit:                strings.Repeat("a", 40),
		Target:                TargetRaspberryPi4,
		Arch:                  ArchitectureAArch64,
		MinimumUpdaterVersion: "1.0.0",
		MinimumBaseVersion:    "0.1.23",
		Bundle:                Bundle{Name: "ardor-device-0.1.24-linux-aarch64.tar.gz", Size: 30, SHA256: strings.Repeat("b", 64)},
		Files: []ManifestFile{
			{Path: "bin/ardor-pedal", Size: 10, Mode: 0o755, SHA256: strings.Repeat("c", 64)},
			{Path: "bin/ardor-managerd", Size: 20, Mode: 0o755, SHA256: strings.Repeat("d", 64)},
		},
	}
}

func TestManifestStrictValidation(t *testing.T) {
	manifest := validManifest()
	data, err := json.Marshal(manifest)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := ParseManifest(data); err != nil {
		t.Fatalf("valid manifest rejected: %v", err)
	}

	unknown := append(data[:len(data)-1], []byte(`,"surprise":true}`)...)
	if _, err := ParseManifest(unknown); err == nil {
		t.Fatal("unknown manifest field was accepted")
	}

	manifest.Files[1].Path = "../ardor-managerd"
	data, _ = json.Marshal(manifest)
	if _, err := ParseManifest(data); err == nil {
		t.Fatal("unsafe or unexpected file path was accepted")
	}
}

func TestVersionComparisonAndCompatibility(t *testing.T) {
	for _, invalid := range []string{"v1.2.3", "1.2", "01.2.3", "1.2.-1", "1.2.3-beta"} {
		if _, err := ParseVersion(invalid); err == nil {
			t.Fatalf("invalid version %q was accepted", invalid)
		}
	}
	one, _ := ParseVersion("1.2.3")
	two, _ := ParseVersion("1.3.0")
	if one.Compare(two) >= 0 || two.Compare(one) <= 0 || one.Compare(one) != 0 {
		t.Fatal("semantic version comparison is incorrect")
	}
	manifest := validManifest()
	if err := Compatible(manifest, "0.1.23", "0.1.23"); err != nil {
		t.Fatalf("compatible update rejected: %v", err)
	}
	if err := Compatible(manifest, "0.1.24", "0.1.23"); err == nil {
		t.Fatal("same-version update was accepted")
	}
	manifest.MinimumBaseVersion = "0.2.0"
	if err := Compatible(manifest, "0.1.23", "0.1.23"); err == nil {
		t.Fatal("incompatible base version was accepted")
	}
}

func TestManifestSignature(t *testing.T) {
	publicKey, privateKey, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	data := []byte(`{"schemaVersion":1}`)
	signature := base64.StdEncoding.EncodeToString(ed25519.Sign(privateKey, data))
	if err := VerifyManifestSignature(publicKey, data, []byte(signature+"\n")); err != nil {
		t.Fatalf("valid signature rejected: %v", err)
	}
	if err := VerifyManifestSignature(publicKey, append(data, ' '), []byte(signature)); err == nil {
		t.Fatal("altered manifest signature was accepted")
	}
}
