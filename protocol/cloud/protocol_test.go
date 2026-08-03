package cloudprotocol

import (
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

var fixtureNow = time.Date(2030, 1, 1, 12, 0, 10, 0, time.UTC)

func TestValidSharedFixtureAndReplayRejection(t *testing.T) {
	data := readFixture(t, "valid-ping.json")
	envelope, err := Decode(data, fixtureNow)
	if err != nil {
		t.Fatal(err)
	}
	guard := NewReplayGuard(16)
	if err := guard.Accept(envelope, fixtureNow); err != nil {
		t.Fatal(err)
	}
	if err := guard.Accept(envelope, fixtureNow); !errors.Is(err, ErrReplay) {
		t.Fatalf("replay error = %v, want ErrReplay", err)
	}
}

func TestMutationFixtureIsDisabled(t *testing.T) {
	_, err := Decode(readFixture(t, "invalid-mutation.json"), fixtureNow)
	if err == nil || !strings.Contains(err.Error(), "not enabled") {
		t.Fatalf("error = %v, want disabled operation", err)
	}
}

func TestExpiredEnvelopeIsRejected(t *testing.T) {
	_, err := Decode(readFixture(t, "valid-ping.json"), fixtureNow.Add(time.Minute))
	if err == nil || !strings.Contains(err.Error(), "expired") {
		t.Fatalf("error = %v, want expiry error", err)
	}
}

func TestUnknownEnvelopeFieldIsRejected(t *testing.T) {
	data := []byte(`{"version":1,"messageId":"018f7f1a-8b25-7e31-a951-5c43272e1901","kind":"request","operation":"system.ping","issuedAt":"2030-01-01T12:00:00Z","expiresAt":"2030-01-01T12:00:30Z","payload":{},"unexpected":true}`)
	if _, err := Decode(data, fixtureNow); err == nil {
		t.Fatal("expected unknown field error")
	}
}

func TestNewEnvelopeIsValid(t *testing.T) {
	envelope, err := NewEnvelope(KindEvent, OperationHello, "", map[string]any{"remoteMutationsEnabled": false}, fixtureNow)
	if err != nil {
		t.Fatal(err)
	}
	if envelope.Version != Version || envelope.Operation != OperationHello {
		t.Fatalf("unexpected envelope: %+v", envelope)
	}
}

func readFixture(t *testing.T, name string) []byte {
	t.Helper()
	_, currentFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("locate test source")
	}
	path := filepath.Join(filepath.Dir(currentFile), "v1", "fixtures", name)
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return data
}
