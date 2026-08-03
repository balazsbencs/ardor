package localauth

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestSetupRequiresPhysicalCodeAndPersistsOnlyPasswordHash(t *testing.T) {
	root := t.TempDir()
	store, err := New(root)
	if err != nil {
		t.Fatal(err)
	}
	if !store.SetupRequired() {
		t.Fatal("new store should require setup")
	}
	if _, _, err := store.Setup("WRONG-CODE", "PedalOwner", "a distinct local password", time.Now().UTC()); err != ErrInvalidSetup {
		t.Fatalf("wrong code error=%v", err)
	}
	var setup setupCode
	setupBytes, err := os.ReadFile(filepath.Join(root, "runtime", "local-access", "setup.json"))
	if err != nil || json.Unmarshal(setupBytes, &setup) != nil {
		t.Fatal("setup code was not published for the physical UI")
	}
	account, token, err := store.Setup(setup.ManualCode, "PedalOwner", "a distinct local password", time.Now().UTC())
	if err != nil || account.Username != "PedalOwner" || token == "" {
		t.Fatalf("account=%+v token=%q err=%v", account, token, err)
	}
	accountBytes, err := os.ReadFile(filepath.Join(root, "auth", "account.json"))
	if err != nil {
		t.Fatal(err)
	}
	if string(accountBytes) == "" || bytes.Contains(accountBytes, []byte("a distinct local password")) {
		t.Fatal("account file exposed the password")
	}
	if _, ok := store.Authenticate(token, time.Now().UTC()); !ok {
		t.Fatal("new session was not accepted")
	}
}

func TestLoginSessionsAndLocalReset(t *testing.T) {
	root := t.TempDir()
	store, _ := New(root)
	var setup setupCode
	data, _ := os.ReadFile(filepath.Join(root, "runtime", "local-access", "setup.json"))
	_ = json.Unmarshal(data, &setup)
	_, _, _ = store.Setup(setup.ManualCode, "owner", "another local password", time.Now().UTC())
	if _, _, err := store.Login("owner", "wrong password value", time.Now().UTC()); err != ErrUnauthorized {
		t.Fatalf("wrong password error=%v", err)
	}
	_, token, err := store.Login("OWNER", "another local password", time.Now().UTC())
	if err != nil {
		t.Fatal(err)
	}
	store.Logout(token)
	if _, ok := store.Authenticate(token, time.Now().UTC()); ok {
		t.Fatal("logged-out session remained valid")
	}
	if err := store.ResetLocalAccess(time.Now().UTC()); err != nil || !store.SetupRequired() {
		t.Fatalf("reset err=%v setup=%v", err, store.SetupRequired())
	}
	if _, err := os.Stat(filepath.Join(root, "auth", "account.json")); !os.IsNotExist(err) {
		t.Fatal("account survived local reset")
	}
}
