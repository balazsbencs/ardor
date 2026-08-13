package update

import (
	"context"
	"crypto/ed25519"
	"encoding/json"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"time"
)

type Status struct {
	State            string     `json:"state"`
	Enabled          bool       `json:"enabled"`
	InstalledVersion string     `json:"installedVersion"`
	BaseVersion      string     `json:"baseVersion"`
	UpdaterVersion   string     `json:"updaterVersion"`
	CheckedAt        *time.Time `json:"checkedAt,omitempty"`
	Available        *Selection `json:"available,omitempty"`
	ErrorCode        string     `json:"errorCode,omitempty"`
	ErrorMessage     string     `json:"errorMessage,omitempty"`
}

type StatusResponse struct {
	State            string             `json:"state"`
	Enabled          bool               `json:"enabled"`
	InstalledVersion string             `json:"installedVersion"`
	BaseVersion      string             `json:"baseVersion"`
	UpdaterVersion   string             `json:"updaterVersion"`
	CheckedAt        *time.Time         `json:"checkedAt,omitempty"`
	Available        *SelectionResponse `json:"available,omitempty"`
	ErrorCode        string             `json:"errorCode,omitempty"`
	ErrorMessage     string             `json:"errorMessage,omitempty"`
}

type SelectionResponse struct {
	Version         string `json:"version"`
	Tag             string `json:"tag"`
	ReleaseURL      string `json:"releaseUrl"`
	ReleaseNotes    string `json:"releaseNotes,omitempty"`
	BundleSize      int64  `json:"bundleSize"`
	ReflashRequired bool   `json:"reflashRequired"`
	Incompatibility string `json:"incompatibility,omitempty"`
}

func PublicStatus(status Status) StatusResponse {
	response := StatusResponse{
		State: status.State, Enabled: status.Enabled, InstalledVersion: status.InstalledVersion,
		BaseVersion: status.BaseVersion, UpdaterVersion: status.UpdaterVersion,
		CheckedAt: status.CheckedAt, ErrorCode: status.ErrorCode, ErrorMessage: status.ErrorMessage,
	}
	if status.Available != nil {
		response.Available = &SelectionResponse{
			Version: status.Available.Version, Tag: status.Available.Tag,
			ReleaseURL: status.Available.ReleaseURL, ReleaseNotes: status.Available.ReleaseNotes,
			BundleSize: status.Available.BundleSize, ReflashRequired: status.Available.ReflashRequired,
			Incompatibility: status.Available.Incompatibility,
		}
	}
	return response
}

type ManagerConfig struct {
	DataRoot          string
	PublicKeyPath     string
	InstalledVersion  string
	BaseVersion       string
	ReleaseClient     ReleaseClient
	SystemRoot        string
	UpdaterExecutable string
}

type Manager struct {
	mu                sync.Mutex
	status            Status
	publicKey         ed25519.PublicKey
	statePath         string
	client            ReleaseClient
	checking          bool
	systemRoot        string
	publicKeyPath     string
	updaterExecutable string
}

func NewManager(config ManagerConfig) *Manager {
	systemRoot := stringDefault(config.SystemRoot, filepath.Join(config.DataRoot, "system"))
	manager := &Manager{
		statePath:         filepath.Join(systemRoot, "update", "operation.json"),
		client:            config.ReleaseClient,
		systemRoot:        systemRoot,
		publicKeyPath:     config.PublicKeyPath,
		updaterExecutable: stringDefault(config.UpdaterExecutable, "/usr/bin/ardor-updater"),
		status: Status{
			State: "idle", InstalledVersion: config.InstalledVersion,
			BaseVersion: config.BaseVersion, UpdaterVersion: UpdaterVersion,
		},
	}
	publicKey, err := LoadPublicKey(config.PublicKeyPath)
	if err != nil {
		manager.status.ErrorCode = "updates_not_configured"
		manager.status.ErrorMessage = "This base image does not contain an OTA signing public key"
		return manager
	}
	manager.publicKey = publicKey
	manager.status.Enabled = true
	manager.restoreStatus()
	return manager
}

func (manager *Manager) PrepareInstall(version string) (Status, func() error, error) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	if manager.status.State != "available" || manager.status.Available == nil {
		return cloneStatus(manager.status), nil, errors.New("no checked update is available")
	}
	if version != manager.status.Available.Version {
		return cloneStatus(manager.status), nil, errors.New("requested version does not match the checked update")
	}
	if manager.status.Available.ReflashRequired {
		return cloneStatus(manager.status), nil, errors.New("this release requires a base-image reflash")
	}
	requestPath := filepath.Join(manager.systemRoot, "update", "install-request.json")
	request := InstallRequest{
		SchemaVersion: 1, Selection: *manager.status.Available,
		PublicKeyPath: manager.publicKeyPath, SystemRoot: manager.systemRoot,
		InstalledVersion: manager.status.InstalledVersion, BaseVersion: manager.status.BaseVersion,
	}
	if err := writeJSONAtomic(requestPath, request, 0o600); err != nil {
		return cloneStatus(manager.status), nil, err
	}
	manager.status.State = "downloading"
	manager.status.ErrorCode = ""
	manager.status.ErrorMessage = ""
	if err := manager.persistLocked(); err != nil {
		return cloneStatus(manager.status), nil, err
	}
	executable := manager.updaterExecutable
	launch := func() error {
		command := exec.Command(executable, "apply", requestPath)
		if err := command.Start(); err != nil {
			setOperationState(manager.systemRoot, "failed", "updater_start_failed", err.Error())
			return err
		}
		return command.Process.Release()
	}
	return cloneStatus(manager.status), launch, nil
}

func (manager *Manager) Status() Status {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	if !manager.checking {
		manager.restoreStatus()
	}
	return cloneStatus(manager.status)
}

func (manager *Manager) Check(ctx context.Context) (Status, error) {
	manager.mu.Lock()
	if !manager.status.Enabled {
		status := cloneStatus(manager.status)
		manager.mu.Unlock()
		return status, errors.New(status.ErrorMessage)
	}
	if manager.checking {
		status := cloneStatus(manager.status)
		manager.mu.Unlock()
		return status, errors.New("an update check is already running")
	}
	manager.checking = true
	manager.status.State = "checking"
	manager.status.ErrorCode = ""
	manager.status.ErrorMessage = ""
	manager.status.Available = nil
	_ = manager.persistLocked()
	installedVersion := manager.status.InstalledVersion
	baseVersion := manager.status.BaseVersion
	manager.mu.Unlock()

	selection, err := manager.client.Discover(ctx, manager.publicKey, installedVersion, baseVersion)
	now := time.Now().UTC()
	manager.mu.Lock()
	defer manager.mu.Unlock()
	manager.checking = false
	manager.status.CheckedAt = &now
	manager.status.State = "idle"
	if errors.Is(err, ErrNoNewerVersion) {
		err = nil
	} else if err != nil {
		manager.status.ErrorCode = "update_check_failed"
		manager.status.ErrorMessage = err.Error()
	} else {
		manager.status.State = "available"
		manager.status.Available = &selection
	}
	_ = manager.persistLocked()
	return cloneStatus(manager.status), err
}

func (manager *Manager) persistLocked() error {
	data, err := json.MarshalIndent(manager.status, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')
	directory := filepath.Dir(manager.statePath)
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(directory, ".operation-*.tmp")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer func() {
		_ = temporary.Close()
		_ = os.Remove(temporaryPath)
	}()
	if err := temporary.Chmod(0o600); err != nil {
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
	return os.Rename(temporaryPath, manager.statePath)
}

func (manager *Manager) restoreStatus() {
	data, err := os.ReadFile(manager.statePath)
	if err != nil || len(data) > 256<<10 {
		return
	}
	var stored Status
	if json.Unmarshal(data, &stored) != nil {
		return
	}
	// Installed/base/updater versions and enabled state come from this boot,
	// never from writable state.
	stored.InstalledVersion = manager.status.InstalledVersion
	stored.BaseVersion = manager.status.BaseVersion
	stored.UpdaterVersion = manager.status.UpdaterVersion
	stored.Enabled = true
	if stored.State == "checking" {
		stored.State = "idle"
		stored.ErrorCode = "update_check_interrupted"
		stored.ErrorMessage = "The previous update check was interrupted"
	}
	manager.status = stored
}

func cloneStatus(status Status) Status {
	copy := status
	if status.Available != nil {
		selection := *status.Available
		selection.Manifest = append(json.RawMessage(nil), status.Available.Manifest...)
		copy.Available = &selection
	}
	return copy
}
