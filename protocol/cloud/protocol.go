package cloudprotocol

import (
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"strings"
	"sync"
	"time"
)

const (
	Version             = 1
	MaxMessageBytes     = 1024 * 1024
	MaxEnvelopeLifetime = time.Minute
	MaxClockSkew        = 30 * time.Second
)

const (
	KindRequest  = "request"
	KindResponse = "response"
	KindEvent    = "event"

	OperationHello         = "system.hello"
	OperationPing          = "system.ping"
	OperationError         = "system.error"
	OperationClaimCode     = "claim.code"
	OperationClaimPending  = "claim.pending"
	OperationClaimDecision = "claim.decision"
	OperationClaimEpoch    = "claim.epoch"
	OperationPresetList    = "preset.list"
	OperationPresetRead    = "preset.read"
	OperationPresetSave    = "preset.save"
	OperationPresetApply   = "preset.apply"
)

var ErrReplay = errors.New("cloud message was already processed")

type Envelope struct {
	Version       int             `json:"version"`
	MessageID     string          `json:"messageId"`
	CorrelationID string          `json:"correlationId,omitempty"`
	Kind          string          `json:"kind"`
	Operation     string          `json:"operation"`
	IssuedAt      time.Time       `json:"issuedAt"`
	ExpiresAt     time.Time       `json:"expiresAt"`
	Payload       json.RawMessage `json:"payload"`
}

func Decode(data []byte, now time.Time) (Envelope, error) {
	if len(data) == 0 || len(data) > MaxMessageBytes {
		return Envelope{}, fmt.Errorf("cloud message size must be between 1 and %d bytes", MaxMessageBytes)
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	var envelope Envelope
	if err := decoder.Decode(&envelope); err != nil {
		return Envelope{}, fmt.Errorf("decode cloud envelope: %w", err)
	}
	if err := ensureEOF(decoder); err != nil {
		return Envelope{}, fmt.Errorf("decode cloud envelope: %w", err)
	}
	if err := envelope.Validate(now); err != nil {
		return Envelope{}, err
	}
	return envelope, nil
}

func (envelope Envelope) Validate(now time.Time) error {
	if envelope.Version != Version {
		return fmt.Errorf("unsupported cloud protocol version %d", envelope.Version)
	}
	if !IsUUID(envelope.MessageID) {
		return errors.New("cloud envelope has invalid messageId")
	}
	if envelope.CorrelationID != "" && !IsUUID(envelope.CorrelationID) {
		return errors.New("cloud envelope has invalid correlationId")
	}
	switch envelope.Kind {
	case KindRequest, KindResponse, KindEvent:
	default:
		return fmt.Errorf("cloud envelope has unsupported kind %q", envelope.Kind)
	}
	switch envelope.Operation {
	case OperationHello, OperationPing, OperationError, OperationClaimCode, OperationClaimPending, OperationClaimDecision, OperationClaimEpoch,
		OperationPresetList, OperationPresetRead, OperationPresetSave, OperationPresetApply:
	default:
		return fmt.Errorf("cloud operation %q is not enabled", envelope.Operation)
	}
	if envelope.IssuedAt.IsZero() || envelope.ExpiresAt.IsZero() {
		return errors.New("cloud envelope timestamps are required")
	}
	if !envelope.ExpiresAt.After(envelope.IssuedAt) {
		return errors.New("cloud envelope expiresAt must be after issuedAt")
	}
	if envelope.ExpiresAt.Sub(envelope.IssuedAt) > MaxEnvelopeLifetime {
		return errors.New("cloud envelope lifetime exceeds limit")
	}
	if envelope.IssuedAt.After(now.Add(MaxClockSkew)) {
		return errors.New("cloud envelope issuedAt is in the future")
	}
	if !envelope.ExpiresAt.After(now) {
		return errors.New("cloud envelope has expired")
	}
	if !payloadIsObject(envelope.Payload) {
		return errors.New("cloud envelope payload must be a JSON object")
	}
	return nil
}

func NewEnvelope(kind, operation, correlationID string, payload any, now time.Time) (Envelope, error) {
	messageID, err := newUUID()
	if err != nil {
		return Envelope{}, fmt.Errorf("generate cloud message id: %w", err)
	}
	rawPayload, err := json.Marshal(payload)
	if err != nil {
		return Envelope{}, fmt.Errorf("encode cloud payload: %w", err)
	}
	envelope := Envelope{
		Version:       Version,
		MessageID:     messageID,
		CorrelationID: correlationID,
		Kind:          kind,
		Operation:     operation,
		IssuedAt:      now.UTC(),
		ExpiresAt:     now.UTC().Add(MaxEnvelopeLifetime),
		Payload:       rawPayload,
	}
	if err := envelope.Validate(now); err != nil {
		return Envelope{}, err
	}
	return envelope, nil
}

type ReplayGuard struct {
	mu      sync.Mutex
	entries map[string]time.Time
	limit   int
}

func NewReplayGuard(limit int) *ReplayGuard {
	if limit < 1 {
		limit = 1
	}
	return &ReplayGuard{entries: make(map[string]time.Time), limit: limit}
}

func (guard *ReplayGuard) Accept(envelope Envelope, now time.Time) error {
	guard.mu.Lock()
	defer guard.mu.Unlock()
	for messageID, expiresAt := range guard.entries {
		if !expiresAt.After(now) {
			delete(guard.entries, messageID)
		}
	}
	if _, exists := guard.entries[envelope.MessageID]; exists {
		return ErrReplay
	}
	if len(guard.entries) >= guard.limit {
		return errors.New("cloud replay cache capacity exceeded")
	}
	guard.entries[envelope.MessageID] = envelope.ExpiresAt
	return nil
}

func AuthenticationTranscript(deviceID, challengeID, nonce string, timestamp time.Time, protocolVersion int, claimEpoch uint64) []byte {
	return []byte(fmt.Sprintf(
		"ardor-cloud-auth-v1\n%s\n%s\n%s\n%s\n%d\n%d\n",
		deviceID,
		challengeID,
		nonce,
		timestamp.UTC().Format(time.RFC3339Nano),
		protocolVersion,
		claimEpoch,
	))
}

func ClaimDecisionTranscript(flowID, deviceID, accountID, nonce string, nextClaimEpoch uint64, approved bool) []byte {
	return []byte(fmt.Sprintf(
		"ardor-cloud-claim-v1\n%s\n%s\n%s\n%s\n%d\n%t\n",
		flowID,
		deviceID,
		accountID,
		nonce,
		nextClaimEpoch,
		approved,
	))
}

func payloadIsObject(payload json.RawMessage) bool {
	trimmed := bytes.TrimSpace(payload)
	if len(trimmed) < 2 || trimmed[0] != '{' || trimmed[len(trimmed)-1] != '}' || !json.Valid(trimmed) {
		return false
	}
	var object map[string]json.RawMessage
	return json.Unmarshal(trimmed, &object) == nil && object != nil
}

func IsUUID(value string) bool {
	if len(value) != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-' {
		return false
	}
	raw := strings.ReplaceAll(value, "-", "")
	decoded, err := hex.DecodeString(raw)
	return err == nil && len(decoded) == 16
}

func newUUID() (string, error) {
	var value [16]byte
	if _, err := rand.Read(value[:]); err != nil {
		return "", err
	}
	value[6] = value[6]&0x0f | 0x40
	value[8] = value[8]&0x3f | 0x80
	encoded := hex.EncodeToString(value[:])
	return encoded[0:8] + "-" + encoded[8:12] + "-" + encoded[12:16] + "-" + encoded[16:20] + "-" + encoded[20:32], nil
}

func ensureEOF(decoder *json.Decoder) error {
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return errors.New("multiple JSON values")
		}
		return err
	}
	return nil
}
