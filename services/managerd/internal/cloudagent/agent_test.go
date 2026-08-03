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
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"ardor.local/managerd/internal/cloudprotocol"
	"ardor.local/managerd/internal/deviceidentity"
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

func TestAgentRejectsMutationEnvelope(t *testing.T) {
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
		_, _, err := connection.Read(ctx)
		if websocket.CloseStatus(err) != websocket.StatusPolicyViolation {
			return fmt.Errorf("mutation close status = %v, error = %v", websocket.CloseStatus(err), err)
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

func TestProductionAgentRequiresSecureSameOriginEndpoints(t *testing.T) {
	identity := testIdentity(t)
	if _, err := New(Config{BaseURL: "http://control.example.test"}, identity); err == nil {
		t.Fatal("expected insecure cloud URL rejection")
	}
	agent, err := New(Config{BaseURL: "https://control.example.test"}, identity)
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
	if payload.DeviceID != deviceID || payload.ProtocolVersion != cloudprotocol.Version || payload.RemoteMutationsEnabled == nil || *payload.RemoteMutationsEnabled {
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
