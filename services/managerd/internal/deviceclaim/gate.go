package deviceclaim

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"
)

const fileVersion = 1

type Code struct {
	Version     int       `json:"version"`
	ClaimFlowID string    `json:"claimFlowId"`
	ManualCode  string    `json:"manualCode"`
	ExpiresAt   time.Time `json:"expiresAt"`
}

type Pending struct {
	Version            int       `json:"version"`
	ClaimFlowID        string    `json:"claimFlowId"`
	CorrelationID      string    `json:"correlationId"`
	AccountID          string    `json:"accountId"`
	AccountDisplayName string    `json:"accountDisplayName"`
	Nonce              string    `json:"nonce"`
	NextClaimEpoch     uint64    `json:"nextClaimEpoch"`
	ExpiresAt          time.Time `json:"expiresAt"`
}

type localDecision struct {
	Version     int    `json:"version"`
	ClaimFlowID string `json:"claimFlowId"`
	Approved    bool   `json:"approved"`
	DecidedAt   any    `json:"decidedAt"`
}

type Decision struct {
	Pending  Pending
	Approved bool
}

type Gate interface {
	DisplayCode(Code) error
	Begin(Pending) error
	Decisions() <-chan Decision
	Complete(string) error
}

type FileGate struct {
	directory    string
	codePath     string
	pendingPath  string
	decisionPath string
	decisions    chan Decision
}

func NewFileGate(dataRoot string) (*FileGate, error) {
	directory := filepath.Join(dataRoot, "runtime", "cloud-claim")
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return nil, fmt.Errorf("create cloud claim directory: %w", err)
	}
	if err := os.Chmod(directory, 0o700); err != nil {
		return nil, fmt.Errorf("secure cloud claim directory: %w", err)
	}
	return &FileGate{
		directory: directory, codePath: filepath.Join(directory, "code.json"),
		pendingPath: filepath.Join(directory, "pending.json"), decisionPath: filepath.Join(directory, "decision.json"),
		decisions: make(chan Decision, 1),
	}, nil
}

func (gate *FileGate) Run(ctx context.Context) {
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()
	for {
		gate.poll(time.Now().UTC())
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

func (gate *FileGate) DisplayCode(code Code) error {
	code.Version = fileVersion
	if code.ClaimFlowID == "" || code.ManualCode == "" || !code.ExpiresAt.After(time.Now().UTC()) {
		return errors.New("claim code is invalid")
	}
	return atomicJSON(gate.codePath, code)
}

func (gate *FileGate) Begin(pending Pending) error {
	pending.Version = fileVersion
	if pending.ClaimFlowID == "" || pending.CorrelationID == "" || pending.AccountID == "" || pending.Nonce == "" || pending.NextClaimEpoch == 0 || !pending.ExpiresAt.After(time.Now().UTC()) {
		return errors.New("pending claim is invalid")
	}
	if existing, err := gate.Pending(); err == nil && existing.ClaimFlowID != pending.ClaimFlowID && existing.ExpiresAt.After(time.Now().UTC()) {
		return errors.New("another physical claim confirmation is pending")
	}
	if err := atomicJSON(gate.pendingPath, pending); err != nil {
		return err
	}
	_ = os.Remove(gate.codePath)
	_ = os.Remove(gate.decisionPath)
	return nil
}

func (gate *FileGate) Decisions() <-chan Decision {
	return gate.decisions
}

func (gate *FileGate) Complete(claimFlowID string) error {
	pending, err := gate.Pending()
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	if err == nil && pending.ClaimFlowID != claimFlowID {
		return errors.New("claim completion does not match pending claim")
	}
	for _, path := range []string{gate.pendingPath, gate.decisionPath, gate.codePath} {
		if err := os.Remove(path); err != nil && !errors.Is(err, os.ErrNotExist) {
			return err
		}
	}
	return nil
}

// RecordDecision is called only by the physical pedal UI process. It has no
// HTTP route and is deliberately not exposed through the manager API.
func (gate *FileGate) RecordDecision(approved bool) error {
	pending, err := gate.Pending()
	if err != nil {
		return err
	}
	if !pending.ExpiresAt.After(time.Now().UTC()) {
		return errors.New("pending claim has expired")
	}
	return atomicJSON(gate.decisionPath, localDecision{Version: fileVersion, ClaimFlowID: pending.ClaimFlowID, Approved: approved, DecidedAt: time.Now().UTC()})
}

func (gate *FileGate) Code() (Code, error) {
	var value Code
	err := readStrictJSON(gate.codePath, &value)
	return value, err
}

func (gate *FileGate) Pending() (Pending, error) {
	var value Pending
	err := readStrictJSON(gate.pendingPath, &value)
	return value, err
}

func (gate *FileGate) poll(now time.Time) {
	pending, err := gate.Pending()
	if err != nil {
		if code, codeErr := gate.Code(); codeErr == nil && !code.ExpiresAt.After(now) {
			_ = os.Remove(gate.codePath)
		}
		return
	}
	if !pending.ExpiresAt.After(now) {
		_ = gate.Complete(pending.ClaimFlowID)
		return
	}
	var decision localDecision
	if err := readStrictJSON(gate.decisionPath, &decision); err != nil || decision.Version != fileVersion || decision.ClaimFlowID != pending.ClaimFlowID {
		return
	}
	select {
	case gate.decisions <- Decision{Pending: pending, Approved: decision.Approved}:
	default:
	}
}

func atomicJSON(path string, value any) error {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')
	temp, err := os.CreateTemp(filepath.Dir(path), ".claim-*.json")
	if err != nil {
		return err
	}
	tempPath := temp.Name()
	defer func() {
		_ = temp.Close()
		_ = os.Remove(tempPath)
	}()
	if err := temp.Chmod(0o600); err != nil {
		return err
	}
	if _, err := temp.Write(data); err != nil {
		return err
	}
	if err := temp.Sync(); err != nil {
		return err
	}
	if err := temp.Close(); err != nil {
		return err
	}
	if err := os.Rename(tempPath, path); err != nil {
		return err
	}
	return nil
}

func readStrictJSON(path string, value any) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if len(data) > 16*1024 {
		return errors.New("claim state exceeds size limit")
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("claim state contains multiple JSON values")
	}
	return nil
}
