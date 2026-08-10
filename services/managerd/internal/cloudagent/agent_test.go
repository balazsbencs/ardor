package cloudagent

import (
	"context"
	"crypto/ed25519"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"ardor.local/cloudprotocol"
	"ardor.local/managerd/internal/deviceclaim"
	"ardor.local/managerd/internal/deviceidentity"
	"ardor.local/managerd/internal/presets"
	"github.com/coder/websocket"
)

const testChallengeID = "018f7f1a-8b25-7e31-a951-5c43272e1903"

func TestAgentAuthenticatesAndReconnects(t *testing.T) {
	identity := testIdentity(t)
	connected := make(chan struct{}, 1)
	errorsFromServer := make(chan error, 4)
	var websocketConnections atomic.Int32
	server := newTestControlPlane(t, identity, func(ctx context.Context, connection *websocket.Conn) error {
		attempt := websocketConnections.Add(1)
		if err := readHello(ctx, connection, identity.DeviceID); err != nil {
			return fmt.Errorf("read hello: %w", err)
		}
		if attempt == 1 {
			return connection.Close(websocket.StatusGoingAway, "exercise reconnect")
		}
		ping, err := cloudprotocol.NewEnvelope(cloudprotocol.KindRequest, cloudprotocol.OperationPing, "", map[string]any{}, time.Now().UTC())
		if err != nil {
			return err
		}
		if err := writeTestEnvelope(ctx, connection, ping); err != nil {
			return err
		}
		response, err := readEnvelope(ctx, connection)
		if err != nil {
			return err
		}
		if response.Kind != cloudprotocol.KindResponse || response.CorrelationID != ping.MessageID {
			return fmt.Errorf("unexpected ping response: %+v", response)
		}
		connected <- struct{}{}
		return connection.Close(websocket.StatusNormalClosure, "done")
	}, errorsFromServer)
	defer server.Close()

	agent := testAgent(t, server.URL, identity)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go agent.Run(ctx)
	select {
	case <-connected:
		cancel()
	case err := <-errorsFromServer:
		t.Fatal(err)
	case <-time.After(5 * time.Second):
		t.Fatal("agent did not reconnect")
	}
	if websocketConnections.Load() < 2 {
		t.Fatalf("websocket connections = %d, want at least 2", websocketConnections.Load())
	}
}

func TestAgentRejectsReplayedEnvelope(t *testing.T) {
	identity := testIdentity(t)
	rejected := make(chan struct{}, 1)
	errorsFromServer := make(chan error, 4)
	server := newTestControlPlane(t, identity, func(ctx context.Context, connection *websocket.Conn) error {
		if err := readHello(ctx, connection, identity.DeviceID); err != nil {
			return err
		}
		ping, err := cloudprotocol.NewEnvelope(cloudprotocol.KindRequest, cloudprotocol.OperationPing, "", map[string]any{}, time.Now().UTC())
		if err != nil {
			return err
		}
		if err := writeTestEnvelope(ctx, connection, ping); err != nil {
			return err
		}
		if _, err := readEnvelope(ctx, connection); err != nil {
			return err
		}
		if err := writeTestEnvelope(ctx, connection, ping); err != nil {
			return err
		}
		_, _, err = connection.Read(ctx)
		if websocket.CloseStatus(err) != websocket.StatusPolicyViolation {
			return fmt.Errorf("replay close status = %v, error = %v", websocket.CloseStatus(err), err)
		}
		rejected <- struct{}{}
		return nil
	}, errorsFromServer)
	defer server.Close()

	agent := testAgent(t, server.URL, identity)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go agent.Run(ctx)
	select {
	case <-rejected:
		cancel()
	case err := <-errorsFromServer:
		t.Fatal(err)
	case <-time.After(5 * time.Second):
		t.Fatal("agent did not reject replayed envelope")
	}
}

func TestAgentRefusesMutationWhenFeatureIsDisabled(t *testing.T) {
	identity := testIdentity(t)
	rejected := make(chan struct{}, 1)
	errorsFromServer := make(chan error, 4)
	server := newTestControlPlane(t, identity, func(ctx context.Context, connection *websocket.Conn) error {
		if err := readHello(ctx, connection, identity.DeviceID); err != nil {
			return err
		}
		now := time.Now().UTC()
		mutation := []byte(fmt.Sprintf(`{"version":1,"messageId":"018f7f1a-8b25-7e31-a951-5c43272e1902","kind":"request","operation":"preset.apply","issuedAt":%q,"expiresAt":%q,"payload":{"bank":0,"slot":0}}`, now.Format(time.RFC3339Nano), now.Add(30*time.Second).Format(time.RFC3339Nano)))
		if err := connection.Write(ctx, websocket.MessageText, mutation); err != nil {
			return err
		}
		response, err := readEnvelope(ctx, connection)
		if err != nil {
			return err
		}
		var payload struct {
			OK    bool `json:"ok"`
			Error struct {
				Code string `json:"code"`
			} `json:"error"`
		}
		if err := json.Unmarshal(response.Payload, &payload); err != nil || payload.OK || payload.Error.Code != "remote_mutations_disabled" {
			return fmt.Errorf("unexpected disabled mutation response: envelope=%+v payload=%+v error=%v", response, payload, err)
		}
		rejected <- struct{}{}
		return nil
	}, errorsFromServer)
	defer server.Close()

	agent := testAgent(t, server.URL, identity)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go agent.Run(ctx)
	select {
	case <-rejected:
		cancel()
	case err := <-errorsFromServer:
		t.Fatal(err)
	case <-time.After(5 * time.Second):
		t.Fatal("agent did not reject mutation envelope")
	}
}

func TestAgentCompletesPhysicallyApprovedClaim(t *testing.T) {
	dataRoot := t.TempDir()
	identity, err := deviceidentity.LoadOrCreate(dataRoot)
	if err != nil {
		t.Fatal(err)
	}
	gate, err := deviceclaim.NewFileGate(dataRoot)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go gate.Run(ctx)

	const (
		flowID    = "018f7f1a-8b25-7e31-a951-5c43272e1904"
		accountID = "018f7f1a-8b25-7e31-a951-5c43272e1905"
	)
	nonce := base64.StdEncoding.EncodeToString([]byte("claim-nonce-0123456789abcdef01234"))
	ackSent := make(chan struct{}, 1)
	errorsFromServer := make(chan error, 4)
	server := newTestControlPlane(t, identity, func(socketContext context.Context, connection *websocket.Conn) error {
		if err := readHello(socketContext, connection, identity.DeviceID); err != nil {
			return err
		}
		code, err := cloudprotocol.NewEnvelope(cloudprotocol.KindEvent, cloudprotocol.OperationClaimCode, "", map[string]any{
			"claimFlowId": flowID, "manualCode": "ABCD-EFGH", "expiresAt": time.Now().UTC().Add(5 * time.Minute),
		}, time.Now().UTC())
		if err != nil {
			return err
		}
		if err := writeTestEnvelope(socketContext, connection, code); err != nil {
			return err
		}
		pending, err := cloudprotocol.NewEnvelope(cloudprotocol.KindRequest, cloudprotocol.OperationClaimPending, "", map[string]any{
			"claimFlowId": flowID, "accountId": accountID, "accountDisplayName": "Riff Master",
			"nonce": nonce, "nextClaimEpoch": 1, "expiresAt": time.Now().UTC().Add(5 * time.Minute),
		}, time.Now().UTC())
		if err != nil {
			return err
		}
		if err := writeTestEnvelope(socketContext, connection, pending); err != nil {
			return err
		}
		decision, err := readEnvelope(socketContext, connection)
		if err != nil {
			return err
		}
		if decision.Kind != cloudprotocol.KindResponse || decision.Operation != cloudprotocol.OperationClaimDecision || decision.CorrelationID != pending.MessageID {
			return fmt.Errorf("unexpected claim decision: %+v", decision)
		}
		var payload struct {
			ClaimFlowID    string `json:"claimFlowId"`
			AccountID      string `json:"accountId"`
			Approved       bool   `json:"approved"`
			NextClaimEpoch uint64 `json:"nextClaimEpoch"`
			Signature      string `json:"signature"`
		}
		if err := json.Unmarshal(decision.Payload, &payload); err != nil {
			return err
		}
		signature, err := base64.StdEncoding.DecodeString(payload.Signature)
		transcript := cloudprotocol.ClaimDecisionTranscript(flowID, identity.DeviceID, accountID, nonce, 1, true)
		if err != nil || payload.ClaimFlowID != flowID || payload.AccountID != accountID || !payload.Approved || payload.NextClaimEpoch != 1 || !ed25519.Verify(identity.PublicKey, transcript, signature) {
			return errors.New("claim decision was not signed by the device identity")
		}
		ack, err := cloudprotocol.NewEnvelope(cloudprotocol.KindEvent, cloudprotocol.OperationClaimDecision, decision.MessageID, map[string]any{
			"claimFlowId": flowID, "status": "claimed", "claimEpoch": 1,
		}, time.Now().UTC())
		if err != nil {
			return err
		}
		if err := writeTestEnvelope(socketContext, connection, ack); err != nil {
			return err
		}
		ackSent <- struct{}{}
		<-socketContext.Done()
		return nil
	}, errorsFromServer)
	defer server.Close()

	agent, err := New(Config{
		BaseURL: server.URL, AllowInsecure: true, ClaimGate: gate, DataRoot: dataRoot,
		Logger: log.New(io.Discard, "", 0), ReconnectInitial: 10 * time.Millisecond, ReconnectMax: 20 * time.Millisecond,
	}, identity)
	if err != nil {
		t.Fatal(err)
	}
	go agent.Run(ctx)

	deadline := time.Now().Add(4 * time.Second)
	for {
		if _, err := gate.Pending(); err == nil {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("agent did not expose the pending physical confirmation")
		}
		time.Sleep(10 * time.Millisecond)
	}
	if err := gate.RecordDecision(true); err != nil {
		t.Fatal(err)
	}
	select {
	case <-ackSent:
	case err := <-errorsFromServer:
		t.Fatal(err)
	case <-time.After(4 * time.Second):
		t.Fatal("agent did not submit the physical decision")
	}
	for {
		reloaded, err := deviceidentity.LoadOrCreate(dataRoot)
		if err != nil {
			t.Fatal(err)
		}
		if reloaded.ClaimEpoch == 1 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("agent did not persist the claim epoch")
		}
		time.Sleep(10 * time.Millisecond)
	}
	if _, err := gate.Pending(); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("pending physical claim still exists: %v", err)
	}
}

func TestProductionAgentRequiresSecureSameOriginEndpoints(t *testing.T) {
	identity := testIdentity(t)
	if _, err := New(Config{BaseURL: "http://control.example.test"}, identity); err == nil {
		t.Fatal("expected insecure cloud URL rejection")
	}
	agent, err := New(Config{BaseURL: "https://control.example.test", DataRoot: t.TempDir()}, identity)
	if err != nil {
		t.Fatal(err)
	}
	token := tokenResponse{
		Version:      cloudprotocol.Version,
		Token:        "token",
		WebSocketURL: "wss://other.example.test/v1/device/connect",
		ExpiresAt:    time.Now().UTC().Add(time.Minute),
	}
	if err := agent.validateToken(token, time.Now().UTC()); err == nil {
		t.Fatal("expected cross-origin websocket rejection")
	}
}

func TestAgentSavesAndAppliesPresetThroughAllowlistedCloudOperations(t *testing.T) {
	dataRoot := t.TempDir()
	identity, err := deviceidentity.LoadOrCreate(dataRoot)
	if err != nil {
		t.Fatal(err)
	}
	completed := make(chan struct{}, 1)
	errorsFromServer := make(chan error, 4)
	server := newTestControlPlane(t, identity, func(ctx context.Context, connection *websocket.Conn) error {
		if err := readHelloWithMutations(ctx, connection, identity.DeviceID, true); err != nil {
			return err
		}
		preset := presets.Preset{
			"version": float64(1), "name": "Cloud Clean", "routing": "serial",
			"global": map[string]any{"inputGainDb": float64(0), "outputGainDb": float64(0), "safetyLimitDb": float64(-1)},
			"blocks": []any{},
		}
		save, err := cloudprotocol.NewEnvelope(cloudprotocol.KindRequest, cloudprotocol.OperationPresetSave, "", map[string]any{
			"bank": 3, "slot": 2, "preset": preset,
		}, time.Now().UTC())
		if err != nil {
			return err
		}
		if err := writeTestEnvelope(ctx, connection, save); err != nil {
			return err
		}
		if err := expectSuccessfulOperation(ctx, connection, save); err != nil {
			return err
		}
		loaded, err := presets.NewStore(dataRoot).Load(3, 2)
		if err != nil || loaded.Preset["name"] != "Cloud Clean" {
			return fmt.Errorf("cloud-saved preset = %+v, error = %v", loaded, err)
		}
		apply, err := cloudprotocol.NewEnvelope(cloudprotocol.KindRequest, cloudprotocol.OperationPresetApply, "", map[string]any{"bank": 3, "slot": 2}, time.Now().UTC())
		if err != nil {
			return err
		}
		if err := writeTestEnvelope(ctx, connection, apply); err != nil {
			return err
		}
		if err := expectSuccessfulOperation(ctx, connection, apply); err != nil {
			return err
		}
		entries, err := os.ReadDir(dataRoot + "/runtime/commands")
		if err != nil || len(entries) != 1 {
			return fmt.Errorf("apply command entries = %d, error = %v", len(entries), err)
		}
		completed <- struct{}{}
		<-ctx.Done()
		return nil
	}, errorsFromServer)
	defer server.Close()
	agent, err := New(Config{
		BaseURL: server.URL, AllowInsecure: true, DataRoot: dataRoot, RemoteMutationsEnabled: true,
		Logger: log.New(io.Discard, "", 0), ReconnectInitial: 10 * time.Millisecond, ReconnectMax: 20 * time.Millisecond,
	}, identity)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go agent.Run(ctx)
	select {
	case <-completed:
		cancel()
	case err := <-errorsFromServer:
		t.Fatal(err)
	case <-time.After(5 * time.Second):
		t.Fatal("cloud preset operations did not complete")
	}
}

func expectSuccessfulOperation(ctx context.Context, connection *websocket.Conn, request cloudprotocol.Envelope) error {
	response, err := readEnvelope(ctx, connection)
	if err != nil {
		return err
	}
	var payload struct {
		OK bool `json:"ok"`
	}
	if err := json.Unmarshal(response.Payload, &payload); err != nil {
		return err
	}
	if response.Kind != cloudprotocol.KindResponse || response.Operation != request.Operation || response.CorrelationID != request.MessageID || !payload.OK {
		return fmt.Errorf("unexpected operation response: %+v payload=%s", response, response.Payload)
	}
	return nil
}

func newTestControlPlane(
	t *testing.T,
	identity *deviceidentity.Identity,
	onConnect func(context.Context, *websocket.Conn) error,
	errorsFromServer chan<- error,
) *httptest.Server {
	t.Helper()
	nonce := base64.StdEncoding.EncodeToString([]byte("0123456789abcdef0123456789abcdef"))
	var serverURL string
	mux := http.NewServeMux()
	mux.HandleFunc("/v1/device/connection-challenge", func(writer http.ResponseWriter, request *http.Request) {
		var body challengeRequest
		if err := decodeRequest(request, &body); err != nil {
			http.Error(writer, err.Error(), http.StatusBadRequest)
			return
		}
		if body.Version != cloudprotocol.Version || body.DeviceID != identity.DeviceID || body.PublicKey != identity.PublicKeyBase64() {
			http.Error(writer, "invalid device", http.StatusUnauthorized)
			return
		}
		writeJSON(writer, challengeResponse{
			Version:     cloudprotocol.Version,
			ChallengeID: testChallengeID,
			Nonce:       nonce,
			ExpiresAt:   time.Now().UTC().Add(time.Minute),
		})
	})
	mux.HandleFunc("/v1/device/connection-token", func(writer http.ResponseWriter, request *http.Request) {
		var body tokenRequest
		if err := decodeRequest(request, &body); err != nil {
			http.Error(writer, err.Error(), http.StatusBadRequest)
			return
		}
		signature, err := base64.StdEncoding.DecodeString(body.Signature)
		transcript := cloudprotocol.AuthenticationTranscript(body.DeviceID, body.ChallengeID, nonce, body.Timestamp, body.ProtocolVersion, body.ClaimEpoch)
		if err != nil || body.DeviceID != identity.DeviceID || body.ChallengeID != testChallengeID || body.ProtocolVersion != cloudprotocol.Version || time.Since(body.Timestamp).Abs() > cloudprotocol.MaxClockSkew || !ed25519.Verify(identity.PublicKey, transcript, signature) {
			http.Error(writer, "invalid signature", http.StatusUnauthorized)
			return
		}
		writeJSON(writer, tokenResponse{
			Version:      cloudprotocol.Version,
			Token:        "local-test-token",
			WebSocketURL: strings.Replace(serverURL, "http://", "ws://", 1) + "/v1/device/connect",
			ClaimEpoch:   identity.ClaimEpoch,
			ExpiresAt:    time.Now().UTC().Add(5 * time.Minute),
		})
	})
	mux.HandleFunc("/v1/device/connect", func(writer http.ResponseWriter, request *http.Request) {
		if request.Header.Get("Authorization") != "Bearer local-test-token" {
			http.Error(writer, "unauthorized", http.StatusUnauthorized)
			return
		}
		connection, err := websocket.Accept(writer, request, nil)
		if err != nil {
			errorsFromServer <- err
			return
		}
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		defer connection.CloseNow()
		if err := onConnect(ctx, connection); err != nil {
			errorsFromServer <- err
		}
	})
	server := httptest.NewServer(mux)
	serverURL = server.URL
	return server
}

func testIdentity(t *testing.T) *deviceidentity.Identity {
	t.Helper()
	identity, err := deviceidentity.LoadOrCreate(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	return identity
}

func testAgent(t *testing.T, baseURL string, identity *deviceidentity.Identity) *Agent {
	t.Helper()
	agent, err := New(Config{
		BaseURL:          baseURL,
		DataRoot:         t.TempDir(),
		AllowInsecure:    true,
		Logger:           log.New(io.Discard, "", 0),
		ReconnectInitial: 10 * time.Millisecond,
		ReconnectMax:     20 * time.Millisecond,
	}, identity)
	if err != nil {
		t.Fatal(err)
	}
	return agent
}

func readEnvelope(ctx context.Context, connection *websocket.Conn) (cloudprotocol.Envelope, error) {
	messageType, data, err := connection.Read(ctx)
	if err != nil {
		return cloudprotocol.Envelope{}, err
	}
	if messageType != websocket.MessageText {
		return cloudprotocol.Envelope{}, errors.New("expected text message")
	}
	return cloudprotocol.Decode(data, time.Now().UTC())
}

func readHello(ctx context.Context, connection *websocket.Conn, deviceID string) error {
	return readHelloWithMutations(ctx, connection, deviceID, false)
}

func readHelloWithMutations(ctx context.Context, connection *websocket.Conn, deviceID string, wantMutations bool) error {
	envelope, err := readEnvelope(ctx, connection)
	if err != nil {
		return err
	}
	if envelope.Kind != cloudprotocol.KindEvent || envelope.Operation != cloudprotocol.OperationHello {
		return fmt.Errorf("unexpected hello envelope: %+v", envelope)
	}
	var payload struct {
		DeviceID               string `json:"deviceId"`
		ProtocolVersion        int    `json:"protocolVersion"`
		RemoteMutationsEnabled *bool  `json:"remoteMutationsEnabled"`
	}
	decoder := json.NewDecoder(strings.NewReader(string(envelope.Payload)))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&payload); err != nil {
		return err
	}
	if payload.DeviceID != deviceID || payload.ProtocolVersion != cloudprotocol.Version || payload.RemoteMutationsEnabled == nil || *payload.RemoteMutationsEnabled != wantMutations {
		return fmt.Errorf("unexpected hello payload: %+v", payload)
	}
	return nil
}

func writeTestEnvelope(ctx context.Context, connection *websocket.Conn, envelope cloudprotocol.Envelope) error {
	data, err := json.Marshal(envelope)
	if err != nil {
		return err
	}
	return connection.Write(ctx, websocket.MessageText, data)
}

func decodeRequest(request *http.Request, value any) error {
	defer request.Body.Close()
	decoder := json.NewDecoder(io.LimitReader(request.Body, maxAuthResponseBytes))
	decoder.DisallowUnknownFields()
	return decoder.Decode(value)
}

func writeJSON(writer http.ResponseWriter, value any) {
	writer.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(writer).Encode(value)
}
