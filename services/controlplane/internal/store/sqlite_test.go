package store

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"errors"
	"path/filepath"
	"testing"
	"time"
)

func TestSQLitePersistsSessionsAndEnforcesPhysicalClaimGate(t *testing.T) {
	ctx := context.Background()
	path := filepath.Join(t.TempDir(), "controlplane.sqlite")
	repository := openTestSQLite(t, path)
	now := time.Now().UTC().Truncate(time.Millisecond)
	accountA := Account{ID: "018f7f1a-8b25-7e31-a951-5c43272e1910", UsernameNormalized: "alice", UsernameDisplay: "Alice", PasswordHash: "hash-a", State: "active", CreatedAt: now, UpdatedAt: now}
	accountB := Account{ID: "018f7f1a-8b25-7e31-a951-5c43272e1911", UsernameNormalized: "bob", UsernameDisplay: "Bob", PasswordHash: "hash-b", State: "active", CreatedAt: now, UpdatedAt: now}
	recovery := [32]byte{1}
	if err := repository.CreateAccount(ctx, accountA, [][32]byte{recovery}); err != nil {
		t.Fatal(err)
	}
	if err := repository.CreateAccount(ctx, accountB, [][32]byte{{2}}); err != nil {
		t.Fatal(err)
	}
	sessionHash := [32]byte{3}
	if err := repository.CreateSession(ctx, Session{ID: "018f7f1a-8b25-7e31-a951-5c43272e1912", AccountID: accountA.ID, TokenHash: sessionHash, CreatedAt: now, ExpiresAt: now.Add(time.Hour)}); err != nil {
		t.Fatal(err)
	}
	if err := repository.Close(); err != nil {
		t.Fatal(err)
	}
	repository = openTestSQLite(t, path)
	defer repository.Close()
	if account, err := repository.AccountBySession(ctx, sessionHash, now); err != nil || account.ID != accountA.ID {
		t.Fatalf("persisted session account = %+v, error = %v", account, err)
	}

	publicKey, _, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	deviceID := "018f7f1a-8b25-7e31-a951-5c43272e1913"
	if _, err := repository.EnsureDevice(ctx, Device{ID: deviceID, PublicKey: publicKey, CreatedAt: now, UpdatedAt: now}); err != nil {
		t.Fatal(err)
	}
	codeHash := [32]byte{4}
	flow := ClaimFlow{ID: "018f7f1a-8b25-7e31-a951-5c43272e1914", DeviceID: deviceID, ManualCodeHash: codeHash, Status: "code_visible", CreatedAt: now, ExpiresAt: now.Add(10 * time.Minute)}
	if err := repository.CreateClaimFlow(ctx, flow, now); err != nil {
		t.Fatal(err)
	}
	flow, err = repository.BeginClaim(ctx, codeHash, accountA, []byte("01234567890123456789012345678901"), now)
	if err != nil {
		t.Fatal(err)
	}
	if devices, err := repository.ListAccountDevices(ctx, accountA.ID); err != nil || len(devices) != 0 {
		t.Fatalf("device became a member before physical confirmation: %+v, error=%v", devices, err)
	}
	if _, err := repository.ClaimForAccount(ctx, flow.ID, accountB.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("other account read claim: %v", err)
	}
	if _, err := repository.CompleteClaim(ctx, flow.ID, true, now.Add(time.Second)); err != nil {
		t.Fatal(err)
	}
	devices, err := repository.ListAccountDevices(ctx, accountA.ID)
	if err != nil || len(devices) != 1 || devices[0].DeviceID != deviceID {
		t.Fatalf("claimed devices = %+v, error=%v", devices, err)
	}
	if _, err := repository.UnclaimDevice(ctx, accountB.ID, deviceID, now.Add(2*time.Second)); !errors.Is(err, ErrNotFound) {
		t.Fatalf("other account unclaimed device: %v", err)
	}
	epoch, err := repository.UnclaimDevice(ctx, accountA.ID, deviceID, now.Add(2*time.Second))
	if err != nil || epoch != 2 {
		t.Fatalf("unclaim epoch = %d, error=%v", epoch, err)
	}
	if _, err := repository.RecoverAccount(ctx, accountA.UsernameNormalized, recovery, "new-hash", now.Add(3*time.Second)); err != nil {
		t.Fatal(err)
	}
	if _, err := repository.AccountBySession(ctx, sessionHash, now.Add(4*time.Second)); !errors.Is(err, ErrUnauthorized) {
		t.Fatalf("recovery did not revoke session: %v", err)
	}
}

func openTestSQLite(t *testing.T, path string) *SQLite {
	t.Helper()
	repository, err := OpenSQLite(path)
	if err != nil {
		t.Fatal(err)
	}
	if err := repository.Migrate(context.Background()); err != nil {
		repository.Close()
		t.Fatal(err)
	}
	return repository
}
