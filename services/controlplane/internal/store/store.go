package store

import (
	"context"
	"errors"
	"time"
)

var (
	ErrNotFound       = errors.New("not found")
	ErrConflict       = errors.New("conflict")
	ErrExpired        = errors.New("expired")
	ErrConsumed       = errors.New("already consumed")
	ErrUnauthorized   = errors.New("unauthorized")
	ErrPhysicalNeeded = errors.New("physical confirmation required")
)

type Account struct {
	ID                 string
	UsernameNormalized string
	UsernameDisplay    string
	PasswordHash       string
	State              string
	CreatedAt          time.Time
	UpdatedAt          time.Time
}

type Session struct {
	ID        string
	AccountID string
	TokenHash [32]byte
	CreatedAt time.Time
	ExpiresAt time.Time
}

type Device struct {
	ID         string
	PublicKey  []byte
	ClaimEpoch uint64
	LastSeenAt *time.Time
	CreatedAt  time.Time
	UpdatedAt  time.Time
}

type DeviceChallenge struct {
	ID         string
	DeviceID   string
	Nonce      []byte
	Purpose    string
	CreatedAt  time.Time
	ExpiresAt  time.Time
	ConsumedAt *time.Time
}

type ConnectionToken struct {
	ID         string
	DeviceID   string
	TokenHash  [32]byte
	CreatedAt  time.Time
	ExpiresAt  time.Time
	ConsumedAt *time.Time
}

type ClaimFlow struct {
	ID                 string
	DeviceID           string
	ManualCodeHash     [32]byte
	Status             string
	AccountID          string
	AccountDisplayName string
	ClaimNonce         []byte
	NextClaimEpoch     uint64
	CreatedAt          time.Time
	ExpiresAt          time.Time
	ConsumedAt         *time.Time
}

type DeviceMembership struct {
	ID        string
	AccountID string
	DeviceID  string
	Role      string
	CreatedAt time.Time
}

type AccountDevice struct {
	DeviceID   string
	Role       string
	ClaimEpoch uint64
	LastSeenAt *time.Time
}

type AuditEvent struct {
	ActorType   string
	ActorID     string
	EventType   string
	SubjectType string
	SubjectID   string
	Metadata    map[string]any
	CreatedAt   time.Time
}

type Repository interface {
	CreateAccount(context.Context, Account, [][32]byte) error
	AccountByUsername(context.Context, string) (Account, error)
	CreateSession(context.Context, Session) error
	AccountBySession(context.Context, [32]byte, time.Time) (Account, error)
	RevokeSession(context.Context, [32]byte, time.Time) error
	RevokeAllSessions(context.Context, string, time.Time) error
	RecoverAccount(context.Context, string, [32]byte, string, time.Time) (Account, error)

	EnsureDevice(context.Context, Device) (Device, error)
	Device(context.Context, string) (Device, error)
	DeviceOwner(context.Context, string) (string, error)
	CreateDeviceChallenge(context.Context, DeviceChallenge) error
	ConsumeDeviceChallenge(context.Context, string, string, time.Time) (DeviceChallenge, error)
	CreateConnectionToken(context.Context, ConnectionToken) error
	ConsumeConnectionToken(context.Context, [32]byte, time.Time) (ConnectionToken, error)
	SetDevicePresence(context.Context, string, time.Time) error

	CreateClaimFlow(context.Context, ClaimFlow, time.Time) error
	BeginClaim(context.Context, [32]byte, Account, []byte, time.Time) (ClaimFlow, error)
	ClaimForAccount(context.Context, string, string) (ClaimFlow, error)
	PendingClaimForDevice(context.Context, string, time.Time) (ClaimFlow, error)
	CompleteClaim(context.Context, string, bool, time.Time) (ClaimFlow, error)
	ListAccountDevices(context.Context, string) ([]AccountDevice, error)
	UnclaimDevice(context.Context, string, string, time.Time) (uint64, error)

	AppendAudit(context.Context, AuditEvent) error
}
