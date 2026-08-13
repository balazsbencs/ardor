package update

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"golang.org/x/sys/unix"
)

const (
	maxBundleBytes     = 128 << 20
	freeSpaceMargin    = 64 << 20
	healthCheckTimeout = 30 * time.Second
)

type InstallRequest struct {
	SchemaVersion    int       `json:"schemaVersion"`
	Selection        Selection `json:"selection"`
	PublicKeyPath    string    `json:"publicKeyPath"`
	SystemRoot       string    `json:"systemRoot"`
	InstalledVersion string    `json:"installedVersion"`
	BaseVersion      string    `json:"baseVersion"`
}

type Transaction struct {
	SchemaVersion int    `json:"schemaVersion"`
	Phase         string `json:"phase"`
	Version       string `json:"version"`
	Previous      string `json:"previous"`
	Factory       bool   `json:"factory"`
}

type Installer struct {
	HTTPClient      *http.Client
	PedalService    string
	ManagerService  string
	HealthURL       string
	TelemetryPath   string
	ValidationDelay time.Duration
}

func (installer Installer) Apply(ctx context.Context, requestPath string) (err error) {
	request, manifest, publicKey, err := loadInstallRequest(requestPath)
	if err != nil {
		return err
	}
	systemRoot := filepath.Clean(request.SystemRoot)
	if systemRoot == "." || systemRoot == "/" {
		return errors.New("unsafe OTA system root")
	}
	rolledBack := false
	defer func() {
		if err != nil && !rolledBack {
			setOperationFailure(systemRoot, err)
		}
	}()
	lockPath := filepath.Join(systemRoot, "update", "lock")
	if err := os.MkdirAll(filepath.Dir(lockPath), 0o700); err != nil {
		return err
	}
	lock, err := os.OpenFile(lockPath, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		if errors.Is(err, os.ErrExist) {
			return errors.New("another update operation is already running")
		}
		return err
	}
	_ = lock.Close()
	defer os.Remove(lockPath)
	setOperationState(systemRoot, "downloading", "", "")

	downloadDirectory := filepath.Join(systemRoot, "update", "downloads")
	if err := os.MkdirAll(downloadDirectory, 0o700); err != nil {
		return err
	}
	bundlePath := filepath.Join(downloadDirectory, manifest.Bundle.Name+".part")
	defer os.Remove(bundlePath)
	if err := ensureFreeSpace(systemRoot, manifest); err != nil {
		setOperationState(systemRoot, "failed", "insufficient_space", err.Error())
		return err
	}
	if err := installer.downloadBundle(ctx, request.Selection.BundleURL, bundlePath, manifest.Bundle); err != nil {
		setOperationState(systemRoot, "failed", "download_failed", err.Error())
		return err
	}
	setOperationState(systemRoot, "verifying", "", "")
	if err := VerifyManifestSignature(publicKey, request.Selection.Manifest, []byte(request.Selection.Signature)); err != nil {
		setOperationState(systemRoot, "failed", "signature_failed", err.Error())
		return err
	}

	releasesDirectory := filepath.Join(systemRoot, "releases")
	stagingPath := filepath.Join(releasesDirectory, "."+manifest.Version+".staging")
	releasePath := filepath.Join(releasesDirectory, manifest.Version)
	if err := os.MkdirAll(releasesDirectory, 0o755); err != nil {
		return err
	}
	if _, err := os.Lstat(releasePath); err == nil {
		referenced, referenceErr := releaseIsReferenced(systemRoot, "releases/"+manifest.Version)
		if referenceErr != nil {
			return referenceErr
		}
		if referenced {
			return errors.New("candidate release directory is already active or retained for rollback")
		}
		// A power loss before the transaction was created can leave a fully
		// staged but unreferenced candidate behind. It is safe to replace only
		// this signed, canonical version path while holding the updater lock.
		if err := os.RemoveAll(releasePath); err != nil {
			return err
		}
	} else if !errors.Is(err, os.ErrNotExist) {
		return err
	}
	if err := os.RemoveAll(stagingPath); err != nil {
		return err
	}
	defer os.RemoveAll(stagingPath)
	if err := extractBundle(bundlePath, stagingPath, manifest); err != nil {
		setOperationState(systemRoot, "failed", "bundle_invalid", err.Error())
		return err
	}
	if err := os.WriteFile(filepath.Join(stagingPath, "manifest.json"), request.Selection.Manifest, 0o644); err != nil {
		return err
	}
	if err := syncTree(stagingPath); err != nil {
		return err
	}
	if err := os.Rename(stagingPath, releasePath); err != nil {
		return err
	}
	if err := syncDirectory(releasesDirectory); err != nil {
		return err
	}
	setOperationState(systemRoot, "staged", "", "")

	transaction, err := prepareTransaction(systemRoot, manifest.Version)
	if err != nil {
		return err
	}
	if err := activateRelease(systemRoot, transaction); err != nil {
		return err
	}
	transaction.Phase = "switched"
	if err := writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600); err != nil {
		_ = restorePrevious(systemRoot, transaction)
		return err
	}
	setOperationState(systemRoot, "restarting", "", "")
	switched := true
	defer func() {
		if err != nil && switched {
			if rollbackErr := installer.rollback(context.Background(), systemRoot, transaction, err); rollbackErr == nil {
				rolledBack = true
			}
		}
	}()
	if err = installer.restartServices(ctx); err != nil {
		return err
	}
	setOperationState(systemRoot, "validating", "", "")
	if err = installer.validate(ctx, manifest.Version); err != nil {
		return err
	}
	transaction.Phase = "confirmed"
	if err = writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600); err != nil {
		return err
	}
	switched = false
	if !transaction.Factory {
		_ = replaceSymlink(filepath.Join(systemRoot, "previous"), transaction.Previous)
	}
	_ = pruneOldReleases(systemRoot)
	setOperationState(systemRoot, "succeeded", "", "")
	_ = os.Remove(filepath.Join(systemRoot, "update", "transaction.json"))
	return nil
}

func (installer Installer) Recover(_ context.Context, systemRoot string) error {
	systemRoot = filepath.Clean(systemRoot)
	if systemRoot == "." || systemRoot == "/" {
		return errors.New("unsafe OTA system root")
	}
	// Recovery runs before either application service starts, so no updater
	// can legitimately own this lock. Removing it makes pre-transaction power
	// loss retryable instead of permanently blocking future updates.
	if err := os.Remove(filepath.Join(systemRoot, "update", "lock")); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	transactionPath := filepath.Join(systemRoot, "update", "transaction.json")
	var transaction Transaction
	if err := readJSONFile(transactionPath, 64<<10, &transaction); errors.Is(err, os.ErrNotExist) {
		markInterruptedOperation(systemRoot)
		return nil
	} else if err != nil {
		return err
	}
	if transaction.SchemaVersion != 1 || transaction.Version == "" {
		return errors.New("OTA transaction is invalid")
	}
	if version, err := ParseVersion(transaction.Version); err != nil || version.String() != transaction.Version {
		return errors.New("OTA transaction version is invalid")
	}
	switch transaction.Phase {
	case "prepared":
		activated, err := transactionIsActivated(systemRoot, transaction)
		if err != nil {
			return err
		}
		if activated {
			if err := restorePrevious(systemRoot, transaction); err != nil {
				return err
			}
			setOperationState(systemRoot, "rolled_back", "power_loss_recovery", "An unconfirmed update was rolled back during boot")
		} else {
			markInterruptedOperation(systemRoot)
		}
		return os.Remove(transactionPath)
	case "switched":
		activated, err := transactionIsActivated(systemRoot, transaction)
		if err != nil {
			return err
		}
		if activated {
			if err := restorePrevious(systemRoot, transaction); err != nil {
				return err
			}
		}
		transaction.Phase = "rolled_back"
		_ = os.Remove(transactionPath)
		setOperationState(systemRoot, "rolled_back", "power_loss_recovery", "An unconfirmed update was rolled back during boot")
		return nil
	case "confirmed":
		activated, err := transactionIsActivated(systemRoot, transaction)
		if err != nil {
			return err
		}
		if !activated {
			return errors.New("confirmed OTA transaction is not active")
		}
		if !transaction.Factory {
			_ = replaceSymlink(filepath.Join(systemRoot, "previous"), transaction.Previous)
		}
		_ = pruneOldReleases(systemRoot)
		setOperationState(systemRoot, "succeeded", "", "")
		return os.Remove(transactionPath)
	case "rolled_back":
		return os.Remove(transactionPath)
	default:
		return errors.New("OTA transaction phase is invalid")
	}
}

func transactionIsActivated(systemRoot string, transaction Transaction) (bool, error) {
	current, err := os.Readlink(filepath.Join(systemRoot, "current"))
	if errors.Is(err, os.ErrNotExist) && transaction.Factory {
		return false, nil
	}
	if err != nil {
		return false, err
	}
	candidate := "releases/" + transaction.Version
	if current == candidate {
		return true, nil
	}
	if !transaction.Factory && current == transaction.Previous && validReleaseLink(current) {
		return false, nil
	}
	return false, errors.New("current OTA release does not match the transaction")
}

func releaseIsReferenced(systemRoot, target string) (bool, error) {
	for _, name := range []string{"current", "previous"} {
		link, err := os.Readlink(filepath.Join(systemRoot, name))
		if errors.Is(err, os.ErrNotExist) {
			continue
		}
		if err != nil {
			return false, err
		}
		if link == target {
			return true, nil
		}
	}
	return false, nil
}

func pruneOldReleases(systemRoot string) error {
	retained := map[string]bool{}
	for _, name := range []string{"current", "previous"} {
		link, err := os.Readlink(filepath.Join(systemRoot, name))
		if errors.Is(err, os.ErrNotExist) {
			continue
		}
		if err != nil {
			return err
		}
		if !validReleaseLink(link) {
			return errors.New("refusing to prune with an invalid OTA release link")
		}
		retained[strings.TrimPrefix(link, "releases/")] = true
	}
	releasesRoot := filepath.Join(systemRoot, "releases")
	entries, err := os.ReadDir(releasesRoot)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	if err != nil {
		return err
	}
	for _, entry := range entries {
		version, versionErr := ParseVersion(entry.Name())
		if versionErr != nil || version.String() != entry.Name() || retained[entry.Name()] {
			continue
		}
		if err := os.RemoveAll(filepath.Join(releasesRoot, entry.Name())); err != nil {
			return err
		}
	}
	return syncDirectory(releasesRoot)
}

func markInterruptedOperation(systemRoot string) {
	path := filepath.Join(systemRoot, "update", "operation.json")
	var status Status
	if readJSONFile(path, 256<<10, &status) != nil {
		return
	}
	switch status.State {
	case "downloading", "verifying", "staged", "restarting", "validating":
		status.State = "failed"
		status.ErrorCode = "power_loss_interrupted"
		status.ErrorMessage = "The update was interrupted by a restart before activation"
		_ = writeJSONAtomic(path, status, 0o600)
	}
}

func loadInstallRequest(path string) (InstallRequest, Manifest, ed25519.PublicKey, error) {
	var request InstallRequest
	if err := readJSONFile(path, 512<<10, &request); err != nil {
		return request, Manifest{}, nil, err
	}
	if request.SchemaVersion != 1 || request.SystemRoot == "" || request.PublicKeyPath == "" {
		return request, Manifest{}, nil, errors.New("install request is invalid")
	}
	publicKey, err := LoadPublicKey(request.PublicKeyPath)
	if err != nil {
		return request, Manifest{}, nil, err
	}
	if err := VerifyManifestSignature(publicKey, request.Selection.Manifest, []byte(request.Selection.Signature)); err != nil {
		return request, Manifest{}, nil, err
	}
	manifest, err := ParseManifest(request.Selection.Manifest)
	if err != nil {
		return request, Manifest{}, nil, err
	}
	if manifest.Version != request.Selection.Version || manifest.Bundle.SHA256 != request.Selection.BundleSHA256 || manifest.Bundle.Size != request.Selection.BundleSize {
		return request, Manifest{}, nil, errors.New("install request does not match signed manifest")
	}
	if err := validateReleaseAssetURL(request.Selection.BundleURL, manifest.Tag, manifest.Bundle.Name); err != nil {
		return request, Manifest{}, nil, err
	}
	if err := Compatible(manifest, request.InstalledVersion, request.BaseVersion); err != nil {
		return request, Manifest{}, nil, err
	}
	return request, manifest, publicKey, nil
}

func (installer Installer) downloadBundle(ctx context.Context, rawURL, destination string, bundle Bundle) error {
	if bundle.Size > maxBundleBytes {
		return errors.New("bundle exceeds maximum supported size")
	}
	client := installer.HTTPClient
	if client == nil {
		client = &http.Client{Timeout: 15 * time.Minute, CheckRedirect: safeReleaseRedirect}
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return err
	}
	request.Header.Set("User-Agent", "ardor-device-updater/"+UpdaterVersion)
	response, err := client.Do(request)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("unexpected HTTP status %d", response.StatusCode)
	}
	file, err := os.OpenFile(destination, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0o600)
	if err != nil {
		return err
	}
	hash := sha256.New()
	written, copyErr := io.Copy(io.MultiWriter(file, hash), io.LimitReader(response.Body, bundle.Size+1))
	if copyErr == nil && written != bundle.Size {
		copyErr = fmt.Errorf("downloaded bundle size %d does not match expected %d", written, bundle.Size)
	}
	if copyErr == nil && hex.EncodeToString(hash.Sum(nil)) != bundle.SHA256 {
		copyErr = errors.New("downloaded bundle SHA-256 does not match signed manifest")
	}
	if copyErr == nil {
		copyErr = file.Sync()
	}
	if closeErr := file.Close(); copyErr == nil {
		copyErr = closeErr
	}
	if copyErr != nil {
		_ = os.Remove(destination)
	}
	return copyErr
}

func safeReleaseRedirect(request *http.Request, via []*http.Request) error {
	if len(via) >= 10 || request.URL.Scheme != "https" {
		return errors.New("unsafe release asset redirect")
	}
	host := strings.ToLower(request.URL.Hostname())
	if host == "github.com" || host == "release-assets.githubusercontent.com" || strings.HasSuffix(host, ".githubusercontent.com") {
		return nil
	}
	return errors.New("release asset redirected to an untrusted host")
}

func ensureFreeSpace(systemRoot string, manifest Manifest) error {
	var stats unix.Statfs_t
	if err := unix.Statfs(systemRoot, &stats); err != nil {
		return err
	}
	available := int64(stats.Bavail) * int64(stats.Bsize)
	required := manifest.Bundle.Size + freeSpaceMargin
	for _, file := range manifest.Files {
		required += file.Size
	}
	if available < required {
		return fmt.Errorf("update requires %d free bytes but only %d are available", required, available)
	}
	return nil
}

func extractBundle(bundlePath, destination string, manifest Manifest) error {
	if err := os.MkdirAll(destination, 0o755); err != nil {
		return err
	}
	file, err := os.Open(bundlePath)
	if err != nil {
		return err
	}
	defer file.Close()
	gzipReader, err := gzip.NewReader(file)
	if err != nil {
		return err
	}
	defer gzipReader.Close()
	expected := make(map[string]ManifestFile, len(manifest.Files))
	for _, entry := range manifest.Files {
		expected[entry.Path] = entry
	}
	seen := make(map[string]bool, len(expected))
	reader := tar.NewReader(gzipReader)
	for {
		header, err := reader.Next()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			return err
		}
		if header.Typeflag == tar.TypeDir && header.Name == "bin/" {
			continue
		}
		entry, ok := expected[header.Name]
		if !ok || seen[header.Name] || header.Typeflag != tar.TypeReg || header.Size != entry.Size {
			return fmt.Errorf("unexpected or invalid archive entry %q", header.Name)
		}
		seen[header.Name] = true
		destinationPath := filepath.Join(destination, filepath.FromSlash(header.Name))
		if err := os.MkdirAll(filepath.Dir(destinationPath), 0o755); err != nil {
			return err
		}
		output, err := os.OpenFile(destinationPath, os.O_CREATE|os.O_EXCL|os.O_WRONLY, os.FileMode(entry.Mode))
		if err != nil {
			return err
		}
		hash := sha256.New()
		written, copyErr := io.Copy(io.MultiWriter(output, hash), io.LimitReader(reader, entry.Size+1))
		if copyErr == nil && written != entry.Size {
			copyErr = errors.New("archive entry size mismatch")
		}
		if copyErr == nil && hex.EncodeToString(hash.Sum(nil)) != entry.SHA256 {
			copyErr = errors.New("archive entry SHA-256 mismatch")
		}
		if copyErr == nil {
			copyErr = output.Sync()
		}
		if closeErr := output.Close(); copyErr == nil {
			copyErr = closeErr
		}
		if copyErr != nil {
			return fmt.Errorf("extract %q: %w", header.Name, copyErr)
		}
	}
	for name := range expected {
		if !seen[name] {
			return fmt.Errorf("archive is missing %q", name)
		}
	}
	return nil
}

func prepareTransaction(systemRoot, version string) (Transaction, error) {
	transaction := Transaction{SchemaVersion: 1, Phase: "prepared", Version: version}
	currentPath := filepath.Join(systemRoot, "current")
	previous, err := os.Readlink(currentPath)
	if errors.Is(err, os.ErrNotExist) {
		transaction.Factory = true
	} else if err != nil {
		return transaction, err
	} else {
		if !validReleaseLink(previous) {
			return transaction, errors.New("current OTA release link is invalid")
		}
		transaction.Previous = previous
	}
	err = writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600)
	return transaction, err
}

func activateRelease(systemRoot string, transaction Transaction) error {
	return replaceSymlink(filepath.Join(systemRoot, "current"), "releases/"+transaction.Version)
}

func restorePrevious(systemRoot string, transaction Transaction) error {
	currentPath := filepath.Join(systemRoot, "current")
	current, err := os.Readlink(currentPath)
	if err != nil {
		return err
	}
	if current != "releases/"+transaction.Version {
		return errors.New("refusing to roll back a current link not owned by the transaction")
	}
	if transaction.Factory {
		if err := os.Remove(currentPath); err != nil {
			return err
		}
		return syncDirectory(systemRoot)
	}
	if !validReleaseLink(transaction.Previous) {
		return errors.New("transaction previous release link is invalid")
	}
	return replaceSymlink(currentPath, transaction.Previous)
}

func validReleaseLink(target string) bool {
	return strings.HasPrefix(target, "releases/") && filepath.Clean(target) == target && !strings.Contains(strings.TrimPrefix(target, "releases/"), "/")
}

func replaceSymlink(linkPath, target string) error {
	temporary := linkPath + ".new"
	_ = os.Remove(temporary)
	if err := os.Symlink(target, temporary); err != nil {
		return err
	}
	if err := os.Rename(temporary, linkPath); err != nil {
		_ = os.Remove(temporary)
		return err
	}
	return syncDirectory(filepath.Dir(linkPath))
}

func (installer Installer) restartServices(ctx context.Context) error {
	pedal := stringDefault(installer.PedalService, "/etc/init.d/S99ardor-pedal")
	manager := stringDefault(installer.ManagerService, "/etc/init.d/S98ardor-managerd")
	for _, command := range [][]string{{pedal, "stop"}, {manager, "stop"}, {manager, "start"}, {pedal, "start"}} {
		if output, err := exec.CommandContext(ctx, command[0], command[1:]...).CombinedOutput(); err != nil {
			return fmt.Errorf("%s %s failed: %w: %s", command[0], command[1], err, strings.TrimSpace(string(output)))
		}
	}
	return nil
}

func (installer Installer) validate(ctx context.Context, version string) error {
	delay := installer.ValidationDelay
	if delay == 0 {
		delay = healthCheckTimeout
	}
	deadline := time.Now().Add(delay)
	healthURL := stringDefault(installer.HealthURL, "http://127.0.0.1:8080/api/device")
	telemetry := stringDefault(installer.TelemetryPath, "/run/ardor-pedal.telemetry")
	client := installer.HTTPClient
	if client == nil {
		client = &http.Client{Timeout: 2 * time.Second}
	}
	startedAt := time.Now()
	var healthySince time.Time
	for time.Now().Before(deadline) {
		request, _ := http.NewRequestWithContext(ctx, http.MethodGet, healthURL, nil)
		response, requestErr := client.Do(request)
		managerHealthy := false
		if requestErr == nil {
			var device struct {
				SoftwareVersion string `json:"softwareVersion"`
			}
			managerHealthy = response.StatusCode == http.StatusOK && json.NewDecoder(io.LimitReader(response.Body, 64<<10)).Decode(&device) == nil && device.SoftwareVersion == version
			response.Body.Close()
		}
		_, processErr := exec.CommandContext(ctx, "pidof", "ardor-pedal").Output()
		telemetryInfo, telemetryErr := os.Stat(telemetry)
		telemetryHealthy := telemetryErr == nil && telemetryInfo.ModTime().After(startedAt) && time.Since(telemetryInfo.ModTime()) < 5*time.Second
		if managerHealthy && processErr == nil && telemetryHealthy {
			if healthySince.IsZero() {
				healthySince = time.Now()
			} else if time.Since(healthySince) >= 10*time.Second {
				return nil
			}
		} else {
			healthySince = time.Time{}
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(time.Second):
		}
	}
	return errors.New("candidate did not pass manager and audio health checks")
}

func (installer Installer) rollback(ctx context.Context, systemRoot string, transaction Transaction, cause error) error {
	pedal := stringDefault(installer.PedalService, "/etc/init.d/S99ardor-pedal")
	manager := stringDefault(installer.ManagerService, "/etc/init.d/S98ardor-managerd")
	_, _ = exec.CommandContext(ctx, pedal, "stop").CombinedOutput()
	_, _ = exec.CommandContext(ctx, manager, "stop").CombinedOutput()
	if err := restorePrevious(systemRoot, transaction); err != nil {
		return err
	}
	_, _ = exec.CommandContext(ctx, manager, "start").CombinedOutput()
	_, _ = exec.CommandContext(ctx, pedal, "start").CombinedOutput()
	transaction.Phase = "rolled_back"
	_ = writeJSONAtomic(filepath.Join(systemRoot, "update", "transaction.json"), transaction, 0o600)
	setOperationState(systemRoot, "rolled_back", "candidate_unhealthy", cause.Error())
	return nil
}

func setOperationState(systemRoot, state, code, message string) {
	path := filepath.Join(systemRoot, "update", "operation.json")
	var status Status
	_ = readJSONFile(path, 256<<10, &status)
	status.State = state
	status.ErrorCode = code
	status.ErrorMessage = message
	_ = writeJSONAtomic(path, status, 0o600)
}

func setOperationFailure(systemRoot string, cause error) {
	path := filepath.Join(systemRoot, "update", "operation.json")
	var status Status
	_ = readJSONFile(path, 256<<10, &status)
	if status.State == "failed" || status.State == "rolled_back" {
		return
	}
	status.State = "failed"
	status.ErrorCode = "update_failed"
	status.ErrorMessage = cause.Error()
	_ = writeJSONAtomic(path, status, 0o600)
}

func readJSONFile(path string, limit int64, target any) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	decoder := json.NewDecoder(io.LimitReader(file, limit))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(target); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("JSON file must contain exactly one value")
	}
	return nil
}

func writeJSONAtomic(path string, value any, mode os.FileMode) error {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(path), ".ota-*.tmp")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer func() {
		_ = temporary.Close()
		_ = os.Remove(temporaryPath)
	}()
	if err := temporary.Chmod(mode); err != nil {
		return err
	}
	if _, err := temporary.Write(data); err != nil {
		return err
	}
	if err := temporary.Sync(); err != nil {
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	if err := os.Rename(temporaryPath, path); err != nil {
		return err
	}
	return syncDirectory(filepath.Dir(path))
}

func syncTree(root string) error {
	directories := []string{}
	if err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			directories = append(directories, path)
		}
		return nil
	}); err != nil {
		return err
	}
	for index := len(directories) - 1; index >= 0; index-- {
		if err := syncDirectory(directories[index]); err != nil {
			return err
		}
	}
	return nil
}

func syncDirectory(path string) error {
	directory, err := os.Open(path)
	if err != nil {
		return err
	}
	defer directory.Close()
	return directory.Sync()
}

func stringDefault(value, fallback string) string {
	if value == "" {
		return fallback
	}
	return value
}
