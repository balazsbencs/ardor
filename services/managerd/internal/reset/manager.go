package reset

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"
	"time"

	"ardor.local/managerd/internal/localauth"
)

const (
	stateVersion    = 1
	factoryResetTTL = 2 * time.Minute
)

var ErrResetPending = errors.New("a reset operation is already pending")

type Status struct {
	ResetID   string    `json:"resetId"`
	Kind      string    `json:"kind"`
	State     string    `json:"state"`
	ExpiresAt time.Time `json:"expiresAt,omitempty"`
}

type operation struct {
	Version   int       `json:"version"`
	ResetID   string    `json:"resetId"`
	Kind      string    `json:"kind"`
	State     string    `json:"state"`
	CreatedAt time.Time `json:"createdAt"`
	ExpiresAt time.Time `json:"expiresAt,omitempty"`
}

type decision struct {
	Version  int    `json:"version"`
	ResetID  string `json:"resetId"`
	Approved bool   `json:"approved"`
}

type Manager struct {
	mu           sync.Mutex
	dataRoot     string
	auth         *localauth.Store
	markerPath   string
	pendingPath  string
	decisionPath string
	current      *operation
}

func New(dataRoot string, auth *localauth.Store) (*Manager, error) {
	if auth == nil {
		return nil, errors.New("reset manager requires local auth")
	}
	manager := &Manager{
		dataRoot: dataRoot, auth: auth,
		markerPath:   filepath.Join(dataRoot, "identity", "reset-operation.json"),
		pendingPath:  filepath.Join(dataRoot, "runtime", "local-access", "factory-reset.json"),
		decisionPath: filepath.Join(dataRoot, "runtime", "local-access", "factory-reset-decision.json"),
	}
	if err := manager.recover(time.Now().UTC()); err != nil {
		return nil, err
	}
	return manager, nil
}

func (manager *Manager) Run(ctx context.Context) {
	ticker := time.NewTicker(250 * time.Millisecond)
	defer ticker.Stop()
	for {
		manager.poll(time.Now().UTC())
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

func (manager *Manager) ResetLocalAccess(now time.Time) error {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	if manager.current != nil {
		return ErrResetPending
	}
	resetID, err := randomID()
	if err != nil {
		return err
	}
	current := &operation{Version: stateVersion, ResetID: resetID, Kind: "local_access", State: "applying", CreatedAt: now}
	if err := writeAtomicJSON(manager.markerPath, current); err != nil {
		return err
	}
	manager.current = current
	if err := manager.auth.ResetLocalAccess(now); err != nil {
		return err
	}
	return manager.finishLocked(now)
}

func (manager *Manager) BeginFactoryReset(now time.Time) (Status, error) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	if manager.current != nil {
		return statusOf(manager.current), ErrResetPending
	}
	resetID, err := randomID()
	if err != nil {
		return Status{}, err
	}
	current := &operation{
		Version: stateVersion, ResetID: resetID, Kind: "factory", State: "awaiting_physical_confirmation",
		CreatedAt: now, ExpiresAt: now.Add(factoryResetTTL),
	}
	if err := writeAtomicJSON(manager.markerPath, current); err != nil {
		return Status{}, err
	}
	if err := manager.publishPending(current); err != nil {
		_ = os.Remove(manager.markerPath)
		return Status{}, err
	}
	manager.current = current
	return statusOf(current), nil
}

func (manager *Manager) Status(resetID string, now time.Time) (Status, bool) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	if manager.current == nil || manager.current.ResetID != resetID {
		return Status{}, false
	}
	if manager.current.State == "awaiting_physical_confirmation" && !manager.current.ExpiresAt.After(now) {
		manager.cancelLocked("expired", now)
	}
	if manager.current == nil {
		return Status{ResetID: resetID, Kind: "factory", State: "expired"}, true
	}
	return statusOf(manager.current), true
}

func (manager *Manager) recover(now time.Time) error {
	var current operation
	if err := readStrictJSON(manager.markerPath, &current); errors.Is(err, os.ErrNotExist) {
		return nil
	} else if err != nil {
		return fmt.Errorf("read reset marker: %w", err)
	}
	if current.Version != stateVersion || current.ResetID == "" || (current.Kind != "local_access" && current.Kind != "factory") {
		return errors.New("reset marker is invalid")
	}
	manager.current = &current
	if current.State == "applying" {
		if current.Kind == "factory" {
			return manager.applyFactoryLocked(now)
		}
		if err := manager.auth.ResetLocalAccess(now); err != nil {
			return err
		}
		return manager.finishLocked(now)
	}
	if current.Kind == "factory" && current.State == "awaiting_physical_confirmation" {
		if !current.ExpiresAt.After(now) {
			manager.cancelLocked("expired", now)
			return nil
		}
		return manager.publishPending(&current)
	}
	return errors.New("reset marker has an unsupported state")
}

func (manager *Manager) poll(now time.Time) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	current := manager.current
	if current == nil {
		return
	}
	if current.State == "applying" {
		if current.Kind == "factory" {
			_ = manager.applyFactoryLocked(now)
		} else if err := manager.auth.ResetLocalAccess(now); err == nil {
			_ = manager.finishLocked(now)
		}
		return
	}
	if current.Kind != "factory" {
		return
	}
	if current.State != "awaiting_physical_confirmation" {
		return
	}
	if !current.ExpiresAt.After(now) {
		manager.cancelLocked("expired", now)
		return
	}
	var recorded decision
	if err := readStrictJSON(manager.decisionPath, &recorded); err != nil || recorded.Version != stateVersion || recorded.ResetID != current.ResetID {
		return
	}
	if !recorded.Approved {
		manager.cancelLocked("rejected", now)
		return
	}
	current.State = "applying"
	if err := writeAtomicJSON(manager.markerPath, current); err != nil {
		return
	}
	_ = manager.applyFactoryLocked(now)
}

func (manager *Manager) applyFactoryLocked(now time.Time) error {
	entries, err := os.ReadDir(manager.dataRoot)
	if err != nil {
		return err
	}
	for _, entry := range entries {
		if entry.Name() == "identity" {
			continue
		}
		if err := os.RemoveAll(filepath.Join(manager.dataRoot, entry.Name())); err != nil {
			return err
		}
	}
	if err := syncDirectory(manager.dataRoot); err != nil {
		return err
	}
	if err := manager.auth.ResetLocalAccess(now); err != nil {
		return err
	}
	if err := appendAudit(manager.dataRoot, manager.current.ResetID, now); err != nil {
		return err
	}
	return manager.finishLocked(now)
}

func (manager *Manager) finishLocked(_ time.Time) error {
	directories := map[string]struct{}{}
	for _, path := range []string{manager.pendingPath, manager.decisionPath, manager.markerPath} {
		if err := os.Remove(path); err != nil && !errors.Is(err, os.ErrNotExist) {
			return err
		}
		directories[filepath.Dir(path)] = struct{}{}
	}
	for directory := range directories {
		if err := syncDirectory(directory); err != nil && !errors.Is(err, os.ErrNotExist) {
			return err
		}
	}
	manager.current = nil
	return nil
}

func (manager *Manager) cancelLocked(_ string, _ time.Time) {
	_ = os.Remove(manager.pendingPath)
	_ = os.Remove(manager.decisionPath)
	_ = os.Remove(manager.markerPath)
	manager.current = nil
}

func (manager *Manager) publishPending(current *operation) error {
	return writeAtomicJSON(manager.pendingPath, map[string]any{
		"version": stateVersion, "resetId": current.ResetID, "expiresAt": current.ExpiresAt,
	})
}

func statusOf(current *operation) Status {
	return Status{ResetID: current.ResetID, Kind: current.Kind, State: current.State, ExpiresAt: current.ExpiresAt}
}

func randomID() (string, error) {
	var value [16]byte
	if _, err := rand.Read(value[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(value[:]), nil
}

func appendAudit(dataRoot, resetID string, now time.Time) error {
	path := filepath.Join(dataRoot, "identity", "reset-audit.jsonl")
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	file, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o600)
	if err != nil {
		return err
	}
	defer file.Close()
	if err := json.NewEncoder(file).Encode(map[string]any{"version": 1, "resetId": resetID, "completedAt": now}); err != nil {
		return err
	}
	return file.Sync()
}

func writeAtomicJSON(path string, value any) error {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(path), ".reset-*.tmp")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer func() { _ = temporary.Close(); _ = os.Remove(temporaryPath) }()
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
	if err := os.Rename(temporaryPath, path); err != nil {
		return err
	}
	return syncDirectory(filepath.Dir(path))
}

func syncDirectory(path string) error {
	directory, err := os.Open(path)
	if err != nil {
		return err
	}
	defer directory.Close()
	return directory.Sync()
}

func readStrictJSON(path string, value any) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if len(data) > 16*1024 {
		return errors.New("reset state exceeds size limit")
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("reset state contains multiple values")
	}
	return nil
}
