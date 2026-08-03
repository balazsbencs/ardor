package deviceclaim

import (
	"context"
	"errors"
	"os"
	"testing"
	"time"
)

func TestDecisionRequiresExplicitPhysicalRecord(t *testing.T) {
	gate, err := NewFileGate(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go gate.Run(ctx)
	pending := Pending{
		ClaimFlowID: "flow", CorrelationID: "message", AccountID: "account", AccountDisplayName: "Alice",
		Nonce: "nonce", NextClaimEpoch: 1, ExpiresAt: time.Now().UTC().Add(time.Minute),
	}
	if err := gate.Begin(pending); err != nil {
		t.Fatal(err)
	}
	select {
	case <-gate.Decisions():
		t.Fatal("claim decision emitted without physical record")
	case <-time.After(600 * time.Millisecond):
	}
	if err := gate.RecordDecision(true); err != nil {
		t.Fatal(err)
	}
	select {
	case decision := <-gate.Decisions():
		if !decision.Approved || decision.Pending.ClaimFlowID != pending.ClaimFlowID {
			t.Fatalf("unexpected decision: %+v", decision)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("physical decision was not emitted")
	}
	if err := gate.Complete(pending.ClaimFlowID); err != nil {
		t.Fatal(err)
	}
	if _, err := gate.Pending(); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("pending claim remains after completion: %v", err)
	}
}

func TestExpiredClaimCodeIsRemoved(t *testing.T) {
	gate, err := NewFileGate(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	if err := atomicJSON(gate.codePath, Code{Version: fileVersion, ClaimFlowID: "expired-flow", ManualCode: "ABCD-EFGH", ExpiresAt: time.Now().UTC().Add(-time.Second)}); err != nil {
		t.Fatal(err)
	}
	gate.poll(time.Now().UTC())
	if _, err := gate.Code(); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("expired claim code remains: %v", err)
	}
}
