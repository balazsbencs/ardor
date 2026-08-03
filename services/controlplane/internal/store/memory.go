package store

import (
	"bytes"
	"context"
	"crypto/subtle"
	"sync"
	"time"

	"ardor.local/controlplane/internal/securevalue"
)

type Memory struct {
	mu            sync.Mutex
	accounts      map[string]Account
	accountNames  map[string]string
	recoveryCodes map[[32]byte]string
	recoveryUsed  map[[32]byte]bool
	sessions      map[[32]byte]Session
	revoked       map[[32]byte]bool
	devices       map[string]Device
	challenges    map[string]DeviceChallenge
	tokens        map[[32]byte]ConnectionToken
	claims        map[string]ClaimFlow
	claimCodes    map[[32]byte]string
	memberships   map[string]DeviceMembership
	audit         []AuditEvent
}

func NewMemory() *Memory {
	return &Memory{
		accounts:      map[string]Account{},
		accountNames:  map[string]string{},
		recoveryCodes: map[[32]byte]string{},
		recoveryUsed:  map[[32]byte]bool{},
		sessions:      map[[32]byte]Session{},
		revoked:       map[[32]byte]bool{},
		devices:       map[string]Device{},
		challenges:    map[string]DeviceChallenge{},
		tokens:        map[[32]byte]ConnectionToken{},
		claims:        map[string]ClaimFlow{},
		claimCodes:    map[[32]byte]string{},
		memberships:   map[string]DeviceMembership{},
	}
}

func (memory *Memory) CreateAccount(_ context.Context, account Account, recovery [][32]byte) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	if _, exists := memory.accountNames[account.UsernameNormalized]; exists {
		return ErrConflict
	}
	memory.accounts[account.ID] = account
	memory.accountNames[account.UsernameNormalized] = account.ID
	for _, hash := range recovery {
		memory.recoveryCodes[hash] = account.ID
	}
	return nil
}

func (memory *Memory) AccountByUsername(_ context.Context, username string) (Account, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	id, ok := memory.accountNames[username]
	if !ok {
		return Account{}, ErrNotFound
	}
	return memory.accounts[id], nil
}

func (memory *Memory) CreateSession(_ context.Context, session Session) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	if _, exists := memory.accounts[session.AccountID]; !exists {
		return ErrNotFound
	}
	memory.sessions[session.TokenHash] = session
	return nil
}

func (memory *Memory) AccountBySession(_ context.Context, hash [32]byte, now time.Time) (Account, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	session, ok := memory.sessions[hash]
	if !ok || memory.revoked[hash] || !session.ExpiresAt.After(now) {
		return Account{}, ErrUnauthorized
	}
	account, ok := memory.accounts[session.AccountID]
	if !ok || account.State != "active" {
		return Account{}, ErrUnauthorized
	}
	return account, nil
}

func (memory *Memory) RevokeSession(_ context.Context, hash [32]byte, _ time.Time) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	if _, ok := memory.sessions[hash]; !ok {
		return ErrNotFound
	}
	memory.revoked[hash] = true
	return nil
}

func (memory *Memory) RevokeAllSessions(_ context.Context, accountID string, _ time.Time) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	for hash, session := range memory.sessions {
		if session.AccountID == accountID {
			memory.revoked[hash] = true
		}
	}
	return nil
}

func (memory *Memory) RecoverAccount(_ context.Context, username string, codeHash [32]byte, passwordHash string, now time.Time) (Account, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	id, ok := memory.accountNames[username]
	if !ok || memory.recoveryCodes[codeHash] != id || memory.recoveryUsed[codeHash] {
		return Account{}, ErrUnauthorized
	}
	memory.recoveryUsed[codeHash] = true
	account := memory.accounts[id]
	account.PasswordHash = passwordHash
	account.UpdatedAt = now
	memory.accounts[id] = account
	for hash, session := range memory.sessions {
		if session.AccountID == id {
			memory.revoked[hash] = true
		}
	}
	return account, nil
}

func (memory *Memory) EnsureDevice(_ context.Context, device Device) (Device, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	if existing, ok := memory.devices[device.ID]; ok {
		if subtle.ConstantTimeCompare(existing.PublicKey, device.PublicKey) != 1 {
			return Device{}, ErrConflict
		}
		return existing, nil
	}
	device.PublicKey = bytes.Clone(device.PublicKey)
	memory.devices[device.ID] = device
	return device, nil
}

func (memory *Memory) Device(_ context.Context, id string) (Device, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	device, ok := memory.devices[id]
	if !ok {
		return Device{}, ErrNotFound
	}
	device.PublicKey = bytes.Clone(device.PublicKey)
	return device, nil
}

func (memory *Memory) DeviceOwner(_ context.Context, deviceID string) (string, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	membership, ok := memory.memberships[deviceID]
	if !ok {
		return "", ErrNotFound
	}
	return membership.AccountID, nil
}

func (memory *Memory) CreateDeviceChallenge(_ context.Context, challenge DeviceChallenge) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	memory.challenges[challenge.ID] = challenge
	return nil
}

func (memory *Memory) ConsumeDeviceChallenge(_ context.Context, id, deviceID string, now time.Time) (DeviceChallenge, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	challenge, ok := memory.challenges[id]
	if !ok || challenge.DeviceID != deviceID {
		return DeviceChallenge{}, ErrNotFound
	}
	if challenge.ConsumedAt != nil {
		return DeviceChallenge{}, ErrConsumed
	}
	if !challenge.ExpiresAt.After(now) {
		return DeviceChallenge{}, ErrExpired
	}
	consumed := now
	challenge.ConsumedAt = &consumed
	memory.challenges[id] = challenge
	return challenge, nil
}

func (memory *Memory) CreateConnectionToken(_ context.Context, token ConnectionToken) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	memory.tokens[token.TokenHash] = token
	return nil
}

func (memory *Memory) ConsumeConnectionToken(_ context.Context, hash [32]byte, now time.Time) (ConnectionToken, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	token, ok := memory.tokens[hash]
	if !ok {
		return ConnectionToken{}, ErrUnauthorized
	}
	if token.ConsumedAt != nil {
		return ConnectionToken{}, ErrConsumed
	}
	if !token.ExpiresAt.After(now) {
		return ConnectionToken{}, ErrExpired
	}
	consumed := now
	token.ConsumedAt = &consumed
	memory.tokens[hash] = token
	return token, nil
}

func (memory *Memory) SetDevicePresence(_ context.Context, deviceID string, now time.Time) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	device, ok := memory.devices[deviceID]
	if !ok {
		return ErrNotFound
	}
	seen := now
	device.LastSeenAt = &seen
	device.UpdatedAt = now
	memory.devices[deviceID] = device
	return nil
}

func (memory *Memory) CreateClaimFlow(_ context.Context, flow ClaimFlow, now time.Time) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	if _, claimed := memory.memberships[flow.DeviceID]; claimed {
		return ErrConflict
	}
	for id, existing := range memory.claims {
		if existing.DeviceID == flow.DeviceID && existing.ConsumedAt == nil {
			consumed := now
			existing.Status = "cancelled"
			existing.ConsumedAt = &consumed
			memory.claims[id] = existing
			delete(memory.claimCodes, existing.ManualCodeHash)
		}
	}
	memory.claims[flow.ID] = flow
	memory.claimCodes[flow.ManualCodeHash] = flow.ID
	return nil
}

func (memory *Memory) BeginClaim(_ context.Context, codeHash [32]byte, account Account, nonce []byte, now time.Time) (ClaimFlow, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	id, ok := memory.claimCodes[codeHash]
	if !ok {
		return ClaimFlow{}, ErrNotFound
	}
	flow := memory.claims[id]
	if flow.ConsumedAt != nil || flow.Status != "code_visible" {
		return ClaimFlow{}, ErrConsumed
	}
	if !flow.ExpiresAt.After(now) {
		return ClaimFlow{}, ErrExpired
	}
	if _, claimed := memory.memberships[flow.DeviceID]; claimed {
		return ClaimFlow{}, ErrConflict
	}
	device := memory.devices[flow.DeviceID]
	flow.Status = "confirm_on_device"
	flow.AccountID = account.ID
	flow.AccountDisplayName = account.UsernameDisplay
	flow.ClaimNonce = bytes.Clone(nonce)
	flow.NextClaimEpoch = device.ClaimEpoch + 1
	memory.claims[id] = flow
	delete(memory.claimCodes, codeHash)
	return flow, nil
}

func (memory *Memory) ClaimForAccount(_ context.Context, flowID, accountID string) (ClaimFlow, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	flow, ok := memory.claims[flowID]
	if !ok || flow.AccountID != accountID {
		return ClaimFlow{}, ErrNotFound
	}
	return flow, nil
}

func (memory *Memory) PendingClaimForDevice(_ context.Context, deviceID string, now time.Time) (ClaimFlow, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	for _, flow := range memory.claims {
		if flow.DeviceID == deviceID && flow.Status == "confirm_on_device" && flow.ExpiresAt.After(now) {
			return flow, nil
		}
	}
	return ClaimFlow{}, ErrNotFound
}

func (memory *Memory) CompleteClaim(_ context.Context, flowID string, approved bool, now time.Time) (ClaimFlow, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	flow, ok := memory.claims[flowID]
	if !ok {
		return ClaimFlow{}, ErrNotFound
	}
	if flow.Status != "confirm_on_device" || flow.ConsumedAt != nil {
		return ClaimFlow{}, ErrConsumed
	}
	if !flow.ExpiresAt.After(now) {
		return ClaimFlow{}, ErrExpired
	}
	consumed := now
	flow.ConsumedAt = &consumed
	if !approved {
		flow.Status = "rejected"
		memory.claims[flowID] = flow
		return flow, nil
	}
	if _, exists := memory.memberships[flow.DeviceID]; exists {
		return ClaimFlow{}, ErrConflict
	}
	membershipID, err := securevalue.UUID()
	if err != nil {
		return ClaimFlow{}, err
	}
	flow.Status = "claimed"
	memory.claims[flowID] = flow
	memory.memberships[flow.DeviceID] = DeviceMembership{
		ID: membershipID, AccountID: flow.AccountID, DeviceID: flow.DeviceID, Role: "owner", CreatedAt: now,
	}
	device := memory.devices[flow.DeviceID]
	device.ClaimEpoch = flow.NextClaimEpoch
	device.UpdatedAt = now
	memory.devices[flow.DeviceID] = device
	return flow, nil
}

func (memory *Memory) ListAccountDevices(_ context.Context, accountID string) ([]AccountDevice, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	result := []AccountDevice{}
	for deviceID, membership := range memory.memberships {
		if membership.AccountID != accountID {
			continue
		}
		device := memory.devices[deviceID]
		result = append(result, AccountDevice{DeviceID: deviceID, Role: membership.Role, ClaimEpoch: device.ClaimEpoch, LastSeenAt: device.LastSeenAt})
	}
	return result, nil
}

func (memory *Memory) UnclaimDevice(_ context.Context, accountID, deviceID string, now time.Time) (uint64, error) {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	membership, ok := memory.memberships[deviceID]
	if !ok || membership.AccountID != accountID {
		return 0, ErrNotFound
	}
	delete(memory.memberships, deviceID)
	device := memory.devices[deviceID]
	device.ClaimEpoch++
	device.UpdatedAt = now
	memory.devices[deviceID] = device
	return device.ClaimEpoch, nil
}

func (memory *Memory) AppendAudit(_ context.Context, event AuditEvent) error {
	memory.mu.Lock()
	defer memory.mu.Unlock()
	memory.audit = append(memory.audit, event)
	return nil
}
