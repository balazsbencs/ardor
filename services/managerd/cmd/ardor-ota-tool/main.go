package main

import (
	"archive/tar"
	"compress/gzip"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"

	"ardor.local/managerd/internal/update"
)

func main() {
	if len(os.Args) < 2 {
		fatal(errors.New("usage: ardor-ota-tool {bundle|verify} [flags]"))
	}
	if os.Args[1] == "verify" {
		verifyFlags := flag.NewFlagSet("verify", flag.ExitOnError)
		manifestPath := verifyFlags.String("manifest", "", "manifest path")
		signaturePath := verifyFlags.String("signature", "", "signature path")
		bundlePath := verifyFlags.String("bundle", "", "bundle path")
		_ = verifyFlags.Parse(os.Args[2:])
		if *manifestPath == "" || *signaturePath == "" || *bundlePath == "" {
			fatal(errors.New("all verify flags are required"))
		}
		if err := verify(*manifestPath, *signaturePath, *bundlePath); err != nil {
			fatal(err)
		}
		return
	}
	if os.Args[1] != "bundle" {
		fatal(errors.New("usage: ardor-ota-tool {bundle|verify} [flags]"))
	}
	flags := flag.NewFlagSet("bundle", flag.ExitOnError)
	version := flags.String("version", "", "release version without v")
	commit := flags.String("commit", "", "full Git commit")
	minimumBase := flags.String("minimum-base", "", "minimum bootstrap image version")
	pedal := flags.String("pedal", "", "path to ardor-pedal")
	managerd := flags.String("managerd", "", "path to ardor-managerd")
	output := flags.String("output", "", "output directory")
	_ = flags.Parse(os.Args[2:])
	if *version == "" || *commit == "" || *minimumBase == "" || *pedal == "" || *managerd == "" || *output == "" {
		fatal(errors.New("all bundle flags are required"))
	}
	privateKey, err := privateKeyFromEnvironment()
	if err != nil {
		fatal(err)
	}
	if err := build(*version, *commit, *minimumBase, *pedal, *managerd, *output, privateKey); err != nil {
		fatal(err)
	}
}

func verify(manifestPath, signaturePath, bundlePath string) error {
	manifestBytes, err := os.ReadFile(manifestPath)
	if err != nil {
		return err
	}
	signatureBytes, err := os.ReadFile(signaturePath)
	if err != nil {
		return err
	}
	publicKey, err := publicKeyFromEnvironment()
	if err != nil {
		return err
	}
	if err := update.VerifyManifestSignature(publicKey, manifestBytes, signatureBytes); err != nil {
		return err
	}
	manifest, err := update.ParseManifest(manifestBytes)
	if err != nil {
		return err
	}
	info, err := os.Stat(bundlePath)
	if err != nil {
		return err
	}
	digest, err := fileSHA256(bundlePath)
	if err != nil {
		return err
	}
	if filepath.Base(bundlePath) != manifest.Bundle.Name || info.Size() != manifest.Bundle.Size || digest != manifest.Bundle.SHA256 {
		return errors.New("bundle does not match signed manifest")
	}
	return nil
}

func build(version, commit, minimumBase, pedalPath, managerdPath, outputDirectory string, privateKey ed25519.PrivateKey) error {
	if _, err := update.ParseVersion(version); err != nil {
		return err
	}
	if _, err := update.ParseVersion(minimumBase); err != nil {
		return err
	}
	if len(commit) != 40 {
		return errors.New("commit must be a full 40-character Git commit")
	}
	if _, err := hex.DecodeString(commit); err != nil {
		return errors.New("commit must be hexadecimal")
	}
	if err := os.MkdirAll(outputDirectory, 0o755); err != nil {
		return err
	}
	prefix := "ardor-device-" + version + "-linux-aarch64"
	bundlePath := filepath.Join(outputDirectory, prefix+".tar.gz")
	inputs := []struct {
		archivePath string
		sourcePath  string
	}{
		{archivePath: "bin/ardor-managerd", sourcePath: managerdPath},
		{archivePath: "bin/ardor-pedal", sourcePath: pedalPath},
	}
	files := make([]update.ManifestFile, 0, len(inputs))
	for _, input := range inputs {
		info, err := os.Stat(input.sourcePath)
		if err != nil {
			return err
		}
		digest, err := fileSHA256(input.sourcePath)
		if err != nil {
			return err
		}
		files = append(files, update.ManifestFile{Path: input.archivePath, Size: info.Size(), Mode: 0o755, SHA256: digest})
	}
	if err := writeBundle(bundlePath, inputs); err != nil {
		return err
	}
	bundleInfo, err := os.Stat(bundlePath)
	if err != nil {
		return err
	}
	bundleDigest, err := fileSHA256(bundlePath)
	if err != nil {
		return err
	}
	manifest := update.Manifest{
		SchemaVersion: update.ManifestSchemaVersion, Version: version, Tag: "v" + version,
		Commit: commit, Target: update.TargetRaspberryPi4, Arch: update.ArchitectureAArch64,
		MinimumUpdaterVersion: update.UpdaterVersion, MinimumBaseVersion: minimumBase,
		Bundle: update.Bundle{Name: filepath.Base(bundlePath), Size: bundleInfo.Size(), SHA256: bundleDigest},
		Files:  files,
	}
	if err := manifest.Validate(); err != nil {
		return err
	}
	manifestBytes, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return err
	}
	manifestBytes = append(manifestBytes, '\n')
	manifestPath := filepath.Join(outputDirectory, prefix+".manifest.json")
	if err := os.WriteFile(manifestPath, manifestBytes, 0o644); err != nil {
		return err
	}
	signature := base64.StdEncoding.EncodeToString(ed25519.Sign(privateKey, manifestBytes)) + "\n"
	return os.WriteFile(filepath.Join(outputDirectory, prefix+".manifest.sig"), []byte(signature), 0o644)
}

func writeBundle(destination string, inputs []struct {
	archivePath string
	sourcePath  string
}) error {
	output, err := os.OpenFile(destination, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	gzipWriter, err := gzip.NewWriterLevel(output, gzip.BestCompression)
	if err != nil {
		output.Close()
		return err
	}
	gzipWriter.Header.ModTime = time.Unix(0, 0).UTC()
	gzipWriter.Header.OS = 255
	tarWriter := tar.NewWriter(gzipWriter)
	for _, input := range inputs {
		info, err := os.Stat(input.sourcePath)
		if err != nil {
			return err
		}
		header := &tar.Header{
			Name: input.archivePath, Mode: 0o755, Size: info.Size(), Typeflag: tar.TypeReg,
			ModTime: time.Unix(0, 0).UTC(), AccessTime: time.Time{}, ChangeTime: time.Time{},
			Uid: 0, Gid: 0, Uname: "", Gname: "", Format: tar.FormatUSTAR,
		}
		if err := tarWriter.WriteHeader(header); err != nil {
			return err
		}
		file, err := os.Open(input.sourcePath)
		if err != nil {
			return err
		}
		_, copyErr := io.Copy(tarWriter, file)
		closeErr := file.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}
	if err := tarWriter.Close(); err != nil {
		return err
	}
	if err := gzipWriter.Close(); err != nil {
		return err
	}
	if err := output.Sync(); err != nil {
		return err
	}
	return output.Close()
}

func fileSHA256(path string) (string, error) {
	file, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer file.Close()
	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return "", err
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

func privateKeyFromEnvironment() (ed25519.PrivateKey, error) {
	raw := strings.TrimSpace(os.Getenv("ARDOR_OTA_PRIVATE_KEY_BASE64"))
	decoded, err := base64.StdEncoding.Strict().DecodeString(raw)
	if err != nil || len(decoded) != ed25519.PrivateKeySize {
		return nil, errors.New("ARDOR_OTA_PRIVATE_KEY_BASE64 must contain one base64-encoded Ed25519 private key")
	}
	return ed25519.PrivateKey(decoded), nil
}

func publicKeyFromEnvironment() (ed25519.PublicKey, error) {
	raw := strings.TrimSpace(os.Getenv("ARDOR_UPDATE_PUBLIC_KEY_BASE64"))
	decoded, err := base64.StdEncoding.Strict().DecodeString(raw)
	if err != nil || len(decoded) != ed25519.PublicKeySize {
		return nil, errors.New("ARDOR_UPDATE_PUBLIC_KEY_BASE64 must contain one base64-encoded Ed25519 public key")
	}
	return ed25519.PublicKey(decoded), nil
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, "ardor-ota-tool:", err)
	os.Exit(1)
}
