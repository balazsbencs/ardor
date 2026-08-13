package update

import (
	"bytes"
	"crypto/ed25519"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"path"
	"regexp"
	"strconv"
	"strings"
)

const (
	ManifestSchemaVersion = 1
	TargetRaspberryPi4    = "raspberry-pi-4"
	ArchitectureAArch64   = "aarch64"
	UpdaterVersion        = "1.0.0"
	maxManifestBytes      = 64 << 10
)

var sha256Pattern = regexp.MustCompile(`^[a-f0-9]{64}$`)

var ErrNoNewerVersion = errors.New("candidate version is not newer than the installed version")

type Manifest struct {
	SchemaVersion         int            `json:"schemaVersion"`
	Version               string         `json:"version"`
	Tag                   string         `json:"tag"`
	Commit                string         `json:"commit"`
	Target                string         `json:"target"`
	Arch                  string         `json:"arch"`
	MinimumUpdaterVersion string         `json:"minimumUpdaterVersion"`
	MinimumBaseVersion    string         `json:"minimumBaseVersion"`
	Bundle                Bundle         `json:"bundle"`
	Files                 []ManifestFile `json:"files"`
}

type Bundle struct {
	Name   string `json:"name"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
}

type ManifestFile struct {
	Path   string `json:"path"`
	Size   int64  `json:"size"`
	Mode   uint32 `json:"mode"`
	SHA256 string `json:"sha256"`
}

func ParseManifest(data []byte) (Manifest, error) {
	if len(data) == 0 || len(data) > maxManifestBytes {
		return Manifest{}, errors.New("update manifest is empty or exceeds 64 KiB")
	}
	var manifest Manifest
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&manifest); err != nil {
		return Manifest{}, fmt.Errorf("decode update manifest: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return Manifest{}, errors.New("update manifest must contain exactly one JSON value")
	}
	if err := manifest.Validate(); err != nil {
		return Manifest{}, err
	}
	return manifest, nil
}

func (manifest Manifest) Validate() error {
	if manifest.SchemaVersion != ManifestSchemaVersion {
		return fmt.Errorf("unsupported update manifest schema %d", manifest.SchemaVersion)
	}
	version, err := ParseVersion(manifest.Version)
	if err != nil || version.String() != manifest.Version {
		return fmt.Errorf("manifest version is not canonical semantic version: %q", manifest.Version)
	}
	if manifest.Tag != "v"+manifest.Version {
		return errors.New("manifest tag does not match version")
	}
	if len(manifest.Commit) != 40 {
		return errors.New("manifest commit must be a full hexadecimal Git commit")
	}
	if _, err := hex.DecodeString(manifest.Commit); err != nil {
		return errors.New("manifest commit must be a full hexadecimal Git commit")
	}
	if manifest.Target != TargetRaspberryPi4 || manifest.Arch != ArchitectureAArch64 {
		return fmt.Errorf("unsupported update target %q/%q", manifest.Target, manifest.Arch)
	}
	if _, err := ParseVersion(manifest.MinimumUpdaterVersion); err != nil {
		return fmt.Errorf("invalid minimum updater version: %w", err)
	}
	if _, err := ParseVersion(manifest.MinimumBaseVersion); err != nil {
		return fmt.Errorf("invalid minimum base version: %w", err)
	}
	expectedBundle := "ardor-device-" + manifest.Version + "-linux-aarch64.tar.gz"
	if manifest.Bundle.Name != expectedBundle {
		return fmt.Errorf("bundle name must be %q", expectedBundle)
	}
	if manifest.Bundle.Size <= 0 || !sha256Pattern.MatchString(manifest.Bundle.SHA256) {
		return errors.New("bundle size or SHA-256 is invalid")
	}
	expectedFiles := map[string]bool{
		"bin/ardor-pedal":    false,
		"bin/ardor-managerd": false,
	}
	for _, file := range manifest.Files {
		if _, ok := expectedFiles[file.Path]; !ok {
			return fmt.Errorf("unexpected manifest file %q", file.Path)
		}
		if expectedFiles[file.Path] {
			return fmt.Errorf("duplicate manifest file %q", file.Path)
		}
		if path.Clean(file.Path) != file.Path || strings.HasPrefix(file.Path, "/") {
			return fmt.Errorf("unsafe manifest file path %q", file.Path)
		}
		if file.Size <= 0 || file.Mode != 0o755 || !sha256Pattern.MatchString(file.SHA256) {
			return fmt.Errorf("manifest metadata for %q is invalid", file.Path)
		}
		expectedFiles[file.Path] = true
	}
	for file, present := range expectedFiles {
		if !present {
			return fmt.Errorf("manifest is missing %q", file)
		}
	}
	return nil
}

func VerifyManifestSignature(publicKey ed25519.PublicKey, manifest, encodedSignature []byte) error {
	if len(publicKey) != ed25519.PublicKeySize {
		return errors.New("update public key is invalid")
	}
	signature, err := base64.StdEncoding.Strict().DecodeString(strings.TrimSpace(string(encodedSignature)))
	if err != nil || len(signature) != ed25519.SignatureSize {
		return errors.New("update manifest signature is invalid")
	}
	if !ed25519.Verify(publicKey, manifest, signature) {
		return errors.New("update manifest signature verification failed")
	}
	return nil
}

type Version struct {
	major int
	minor int
	patch int
}

func ParseVersion(raw string) (Version, error) {
	parts := strings.Split(raw, ".")
	if len(parts) != 3 {
		return Version{}, errors.New("version must have exactly three numeric components")
	}
	values := [3]int{}
	for index, part := range parts {
		if part == "" || (len(part) > 1 && part[0] == '0') {
			return Version{}, errors.New("version components must be canonical non-negative integers")
		}
		value, err := strconv.Atoi(part)
		if err != nil || value < 0 {
			return Version{}, errors.New("version components must be canonical non-negative integers")
		}
		values[index] = value
	}
	return Version{major: values[0], minor: values[1], patch: values[2]}, nil
}

func (version Version) String() string {
	return fmt.Sprintf("%d.%d.%d", version.major, version.minor, version.patch)
}

func (version Version) Compare(other Version) int {
	left := [3]int{version.major, version.minor, version.patch}
	right := [3]int{other.major, other.minor, other.patch}
	for index := range left {
		if left[index] < right[index] {
			return -1
		}
		if left[index] > right[index] {
			return 1
		}
	}
	return 0
}

func Compatible(manifest Manifest, installedVersion, baseVersion string) error {
	candidate, err := ParseVersion(manifest.Version)
	if err != nil {
		return err
	}
	installed, err := ParseVersion(installedVersion)
	if err != nil {
		return fmt.Errorf("installed version is invalid: %w", err)
	}
	if candidate.Compare(installed) <= 0 {
		return ErrNoNewerVersion
	}
	updater, _ := ParseVersion(UpdaterVersion)
	minimumUpdater, _ := ParseVersion(manifest.MinimumUpdaterVersion)
	if updater.Compare(minimumUpdater) < 0 {
		return errors.New("candidate requires a newer updater")
	}
	base, err := ParseVersion(baseVersion)
	if err != nil {
		return fmt.Errorf("base version is invalid: %w", err)
	}
	minimumBase, _ := ParseVersion(manifest.MinimumBaseVersion)
	if base.Compare(minimumBase) < 0 {
		return errors.New("candidate requires a newer base image")
	}
	return nil
}
