package server

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"

	"ardor.local/cloudprotocol"
	"ardor.local/controlplane/internal/store"
	"github.com/coder/websocket"
)

func TestAccountIsolationAndPhysicallyConfirmedClaim(t *testing.T) {
	controlPlane, httpServer := newTestServer(t)
	defer controlPlane.Close()
	defer httpServer.Close()
	origin := httpServer.URL
	aliceCookie, _ := registerTestAccount(t, origin, "Alice", "alice password is long", origin)
	bobCookie, _ := registerTestAccount(t, origin, "Bob", "bob password is long enough", origin)

	deviceID := "018f7f1a-8b25-7e31-a951-5c43272e1920"
	publicKey, privateKey, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	connection := connectTestDevice(t, origin, deviceID, publicKey, privateKey, 0)
	defer connection.CloseNow()
	claimCodeEnvelope := readTestEnvelope(t, connection)
	if claimCodeEnvelope.Operation != cloudprotocol.OperationClaimCode {
		t.Fatalf("first device operation = %s, want claim.code", claimCodeEnvelope.Operation)
	}
	var codePayload struct {
		ClaimFlowID string    `json:"claimFlowId"`
		ManualCode  string    `json:"manualCode"`
		ExpiresAt   time.Time `json:"expiresAt"`
	}
	if err := json.Unmarshal(claimCodeEnvelope.Payload, &codePayload); err != nil {
		t.Fatal(err)
	}

	response := jsonRequest(t, http.MethodPost, origin+"/v1/device-claims", origin, aliceCookie, map[string]any{"code": codePayload.ManualCode})
	if response.StatusCode != http.StatusAccepted {
		t.Fatalf("begin claim status = %d, body=%s", response.StatusCode, readBody(response))
	}
	response.Body.Close()
	pendingEnvelope := readTestEnvelope(t, connection)
	if pendingEnvelope.Operation != cloudprotocol.OperationClaimPending {
		t.Fatalf("pending operation = %s", pendingEnvelope.Operation)
	}
	var pending struct {
		ClaimFlowID        string    `json:"claimFlowId"`
		AccountID          string    `json:"accountId"`
		AccountDisplayName string    `json:"accountDisplayName"`
		Nonce              string    `json:"nonce"`
		NextClaimEpoch     uint64    `json:"nextClaimEpoch"`
		ExpiresAt          time.Time `json:"expiresAt"`
	}
	if err := json.Unmarshal(pendingEnvelope.Payload, &pending); err != nil {
		t.Fatal(err)
	}
	if pending.AccountDisplayName != "Alice" || pending.NextClaimEpoch != 1 {
		t.Fatalf("unexpected pending claim: %+v", pending)
	}

	assertDeviceCount(t, origin, aliceCookie, 0)
	response = jsonRequest(t, http.MethodPost, origin+"/v1/device-claims", origin, bobCookie, map[string]any{"code": codePayload.ManualCode})
	if response.StatusCode != http.StatusNotFound {
		t.Fatalf("reused claim code status = %d", response.StatusCode)
	}
	response.Body.Close()
	response = jsonRequest(t, http.MethodGet, origin+"/v1/device-claims/"+pending.ClaimFlowID, "", bobCookie, nil)
	if response.StatusCode != http.StatusNotFound {
		t.Fatalf("other account claim read status = %d", response.StatusCode)
	}
	response.Body.Close()

	transcript := cloudprotocol.ClaimDecisionTranscript(pending.ClaimFlowID, deviceID, pending.AccountID, pending.Nonce, pending.NextClaimEpoch, true)
	decision, err := cloudprotocol.NewEnvelope(cloudprotocol.KindResponse, cloudprotocol.OperationClaimDecision, pendingEnvelope.MessageID, map[string]any{
		"claimFlowId": pending.ClaimFlowID, "accountId": pending.AccountID, "approved": true,
		"nextClaimEpoch": pending.NextClaimEpoch, "signature": base64.StdEncoding.EncodeToString(ed25519.Sign(privateKey, transcript)),
	}, time.Now().UTC())
	if err != nil {
		t.Fatal(err)
	}
	writeTestEnvelope(t, connection, decision)
	ack := readTestEnvelope(t, connection)
	if ack.Operation != cloudprotocol.OperationClaimDecision {
		t.Fatalf("claim acknowledgement operation = %s", ack.Operation)
	}
	assertDeviceCount(t, origin, aliceCookie, 1)
	assertDeviceCount(t, origin, bobCookie, 0)

	response = jsonRequest(t, http.MethodGet, origin+"/v1/devices/"+deviceID+"/presets", "", bobCookie, nil)
	if response.StatusCode != http.StatusNotFound {
		t.Fatalf("other account preset list status = %d", response.StatusCode)
	}
	response.Body.Close()

	listResponse := startJSONRequest(t, http.MethodGet, origin+"/v1/devices/"+deviceID+"/presets", "", aliceCookie, nil, "")
	listRequest := readTestEnvelope(t, connection)
	if listRequest.Operation != cloudprotocol.OperationPresetList {
		t.Fatalf("relayed list operation = %s", listRequest.Operation)
	}
	writeOperationResult(t, connection, listRequest, map[string]any{"presets": []any{}})
	response = awaitHTTPResponse(t, listResponse)
	if response.StatusCode != http.StatusOK {
		t.Fatalf("preset list status = %d, body=%s", response.StatusCode, readBody(response))
	}
	response.Body.Close()

	preset := map[string]any{
		"version": 1, "name": "Cloud Clean", "routing": "serial",
		"global": map[string]any{"inputGainDb": 0, "outputGainDb": 0, "safetyLimitDb": -1}, "blocks": []any{},
	}
	const idempotencyKey = "018f7f1a-8b25-7e31-a951-5c43272e1999"
	saveResponse := startJSONRequest(t, http.MethodPut, origin+"/v1/devices/"+deviceID+"/presets/banks/2/slots/1", origin, aliceCookie, preset, idempotencyKey)
	saveRequest := readTestEnvelope(t, connection)
	if saveRequest.Operation != cloudprotocol.OperationPresetSave {
		t.Fatalf("relayed save operation = %s", saveRequest.Operation)
	}
	savedSlot := map[string]any{"bank": 2, "slot": 1, "preset": preset}
	writeOperationResult(t, connection, saveRequest, savedSlot)
	response = awaitHTTPResponse(t, saveResponse)
	firstSaveBody := readBody(response)
	if response.StatusCode != http.StatusOK {
		t.Fatalf("preset save status = %d, body=%s", response.StatusCode, firstSaveBody)
	}

	response = jsonRequestWithIdempotency(t, http.MethodPut, origin+"/v1/devices/"+deviceID+"/presets/banks/2/slots/1", origin, aliceCookie, preset, idempotencyKey)
	if duplicateBody := readBody(response); response.StatusCode != http.StatusOK || duplicateBody != firstSaveBody {
		t.Fatalf("idempotent save status=%d body=%q want=%q", response.StatusCode, duplicateBody, firstSaveBody)
	}
	changedPreset := map[string]any{
		"version": 1, "name": "Different", "routing": "serial",
		"global": map[string]any{"inputGainDb": 0, "outputGainDb": 0, "safetyLimitDb": -1}, "blocks": []any{},
	}
	response = jsonRequestWithIdempotency(t, http.MethodPut, origin+"/v1/devices/"+deviceID+"/presets/banks/2/slots/1", origin, aliceCookie, changedPreset, idempotencyKey)
	if response.StatusCode != http.StatusConflict {
		t.Fatalf("idempotency conflict status = %d, body=%s", response.StatusCode, readBody(response))
	}
	response.Body.Close()

	response = jsonRequest(t, http.MethodDelete, origin+"/v1/devices/"+deviceID+"/membership", origin, bobCookie, nil)
	if response.StatusCode != http.StatusNotFound {
		t.Fatalf("other account unclaim status = %d", response.StatusCode)
	}
	response.Body.Close()
	response = jsonRequest(t, http.MethodDelete, origin+"/v1/devices/"+deviceID+"/membership", origin, aliceCookie, nil)
	if response.StatusCode != http.StatusNoContent {
		t.Fatalf("owner unclaim status = %d, body=%s", response.StatusCode, readBody(response))
	}
	response.Body.Close()
	epochEnvelope := readTestEnvelope(t, connection)
	if epochEnvelope.Operation != cloudprotocol.OperationClaimEpoch {
		t.Fatalf("unclaim operation = %s", epochEnvelope.Operation)
	}
	assertDeviceCount(t, origin, aliceCookie, 0)
}

func TestRecoveryCodeIsSingleUseAndRevokesSessions(t *testing.T) {
	controlPlane, httpServer := newTestServer(t)
	defer controlPlane.Close()
	defer httpServer.Close()
	cookie, recoveryCodes := registerTestAccount(t, httpServer.URL, "RecoveryUser", "original password long", httpServer.URL)
	response := jsonRequest(t, http.MethodGet, httpServer.URL+"/v1/auth/me", "", cookie, nil)
	if response.StatusCode != http.StatusOK {
		t.Fatalf("initial session status = %d", response.StatusCode)
	}
	response.Body.Close()
	response = jsonRequest(t, http.MethodPost, httpServer.URL+"/v1/auth/recover", httpServer.URL, nil, map[string]any{
		"username": "RecoveryUser", "recoveryCode": recoveryCodes[0], "newPassword": "replacement password long",
	})
	if response.StatusCode != http.StatusOK {
		t.Fatalf("recovery status = %d, body=%s", response.StatusCode, readBody(response))
	}
	newCookie := response.Cookies()[0]
	response.Body.Close()
	response = jsonRequest(t, http.MethodGet, httpServer.URL+"/v1/auth/me", "", cookie, nil)
	if response.StatusCode != http.StatusUnauthorized {
		t.Fatalf("old session after recovery status = %d", response.StatusCode)
	}
	response.Body.Close()
	response = jsonRequest(t, http.MethodGet, httpServer.URL+"/v1/auth/me", "", newCookie, nil)
	if response.StatusCode != http.StatusOK {
		t.Fatalf("new session after recovery status = %d", response.StatusCode)
	}
	response.Body.Close()
	response = jsonRequest(t, http.MethodPost, httpServer.URL+"/v1/auth/recover", httpServer.URL, nil, map[string]any{
		"username": "RecoveryUser", "recoveryCode": recoveryCodes[0], "newPassword": "another replacement long",
	})
	if response.StatusCode != http.StatusUnauthorized {
		t.Fatalf("reused recovery code status = %d", response.StatusCode)
	}
	response.Body.Close()
}

func TestClaimCodeAttemptsAreRateLimited(t *testing.T) {
	controlPlane, httpServer := newTestServer(t)
	defer controlPlane.Close()
	defer httpServer.Close()
	cookie, _ := registerTestAccount(t, httpServer.URL, "ClaimLimiter", "claim limiter password", httpServer.URL)
	for attempt := 0; attempt < 5; attempt++ {
		response := jsonRequest(t, http.MethodPost, httpServer.URL+"/v1/device-claims", httpServer.URL, cookie, map[string]any{"code": "WRONG-CODE"})
		if response.StatusCode != http.StatusNotFound {
			t.Fatalf("invalid claim attempt %d status = %d", attempt+1, response.StatusCode)
		}
		response.Body.Close()
	}
	response := jsonRequest(t, http.MethodPost, httpServer.URL+"/v1/device-claims", httpServer.URL, cookie, map[string]any{"code": "STILL-WRONG"})
	if response.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("rate-limited claim status = %d, body=%s", response.StatusCode, readBody(response))
	}
	response.Body.Close()
}

func newTestServer(t *testing.T) (*Server, *httptest.Server) {
	t.Helper()
	repository, err := store.OpenSQLite(filepath.Join(t.TempDir(), "controlplane.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { repository.Close() })
	if err := repository.Migrate(context.Background()); err != nil {
		t.Fatal(err)
	}
	httpServer := httptest.NewUnstartedServer(nil)
	origin := "http://" + httpServer.Listener.Addr().String()
	controlPlane, err := New(Config{PublicOrigin: origin, SecureCookies: false, Logger: log.New(io.Discard, "", 0)}, repository)
	if err != nil {
		t.Fatal(err)
	}
	httpServer.Config.Handler = controlPlane.Handler()
	httpServer.Start()
	return controlPlane, httpServer
}

func registerTestAccount(t *testing.T, origin, username, password, requestOrigin string) (*http.Cookie, []string) {
	t.Helper()
	response := jsonRequest(t, http.MethodPost, origin+"/v1/auth/register", requestOrigin, nil, map[string]any{"username": username, "password": password})
	if response.StatusCode != http.StatusCreated {
		t.Fatalf("register %s status = %d, body=%s", username, response.StatusCode, readBody(response))
	}
	var body struct {
		RecoveryCodes []string `json:"recoveryCodes"`
	}
	if err := json.NewDecoder(response.Body).Decode(&body); err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	return response.Cookies()[0], body.RecoveryCodes
}

func connectTestDevice(t *testing.T, origin, deviceID string, publicKey ed25519.PublicKey, privateKey ed25519.PrivateKey, claimEpoch uint64) *websocket.Conn {
	t.Helper()
	response := jsonRequest(t, http.MethodPost, origin+"/v1/device/connection-challenge", "", nil, map[string]any{
		"version": cloudprotocol.Version, "deviceId": deviceID, "publicKey": base64.StdEncoding.EncodeToString(publicKey),
	})
	if response.StatusCode != http.StatusCreated {
		t.Fatalf("challenge status = %d, body=%s", response.StatusCode, readBody(response))
	}
	var challenge struct {
		ChallengeID string `json:"challengeId"`
		Nonce       string `json:"nonce"`
	}
	if err := json.NewDecoder(response.Body).Decode(&challenge); err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	timestamp := time.Now().UTC()
	transcript := cloudprotocol.AuthenticationTranscript(deviceID, challenge.ChallengeID, challenge.Nonce, timestamp, cloudprotocol.Version, claimEpoch)
	response = jsonRequest(t, http.MethodPost, origin+"/v1/device/connection-token", "", nil, map[string]any{
		"version": cloudprotocol.Version, "deviceId": deviceID, "challengeId": challenge.ChallengeID,
		"timestamp": timestamp, "protocolVersion": cloudprotocol.Version, "claimEpoch": claimEpoch,
		"signature": base64.StdEncoding.EncodeToString(ed25519.Sign(privateKey, transcript)),
	})
	if response.StatusCode != http.StatusCreated {
		t.Fatalf("token status = %d, body=%s", response.StatusCode, readBody(response))
	}
	var token struct {
		Token        string `json:"token"`
		WebsocketURL string `json:"websocketUrl"`
	}
	if err := json.NewDecoder(response.Body).Decode(&token); err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	header := make(http.Header)
	header.Set("Authorization", "Bearer "+token.Token)
	connection, _, err := websocket.Dial(context.Background(), token.WebsocketURL, &websocket.DialOptions{HTTPHeader: header})
	if err != nil {
		t.Fatal(err)
	}
	hello, err := cloudprotocol.NewEnvelope(cloudprotocol.KindEvent, cloudprotocol.OperationHello, "", map[string]any{
		"deviceId": deviceID, "protocolVersion": cloudprotocol.Version, "remoteMutationsEnabled": true,
	}, time.Now().UTC())
	if err != nil {
		t.Fatal(err)
	}
	writeTestEnvelope(t, connection, hello)
	return connection
}

func jsonRequest(t *testing.T, method, target, origin string, cookie *http.Cookie, value any) *http.Response {
	return jsonRequestWithIdempotency(t, method, target, origin, cookie, value, "")
}

func jsonRequestWithIdempotency(t *testing.T, method, target, origin string, cookie *http.Cookie, value any, idempotencyKey string) *http.Response {
	t.Helper()
	return awaitHTTPResponse(t, startJSONRequest(t, method, target, origin, cookie, value, idempotencyKey))
}

type pendingHTTPResponse struct {
	response *http.Response
	err      error
}

func startJSONRequest(t *testing.T, method, target, origin string, cookie *http.Cookie, value any, idempotencyKey string) <-chan pendingHTTPResponse {
	t.Helper()
	var body io.Reader
	if value != nil {
		data, err := json.Marshal(value)
		if err != nil {
			t.Fatal(err)
		}
		body = bytes.NewReader(data)
	}
	request, err := http.NewRequest(method, target, body)
	if err != nil {
		t.Fatal(err)
	}
	if value != nil {
		request.Header.Set("Content-Type", "application/json")
	}
	if origin != "" {
		request.Header.Set("Origin", origin)
	}
	if cookie != nil {
		request.AddCookie(cookie)
	}
	if idempotencyKey != "" {
		request.Header.Set("Idempotency-Key", idempotencyKey)
	}
	result := make(chan pendingHTTPResponse, 1)
	go func() {
		response, err := http.DefaultClient.Do(request)
		result <- pendingHTTPResponse{response: response, err: err}
	}()
	return result
}

func awaitHTTPResponse(t *testing.T, pending <-chan pendingHTTPResponse) *http.Response {
	t.Helper()
	select {
	case result := <-pending:
		if result.err != nil {
			t.Fatal(result.err)
		}
		return result.response
	case <-time.After(5 * time.Second):
		t.Fatal("HTTP request timed out")
		return nil
	}
}

func writeOperationResult(t *testing.T, connection *websocket.Conn, request cloudprotocol.Envelope, result any) {
	t.Helper()
	response, err := cloudprotocol.NewEnvelope(cloudprotocol.KindResponse, request.Operation, request.MessageID, map[string]any{"ok": true, "result": result}, time.Now().UTC())
	if err != nil {
		t.Fatal(err)
	}
	writeTestEnvelope(t, connection, response)
}

func assertDeviceCount(t *testing.T, origin string, cookie *http.Cookie, want int) {
	t.Helper()
	response := jsonRequest(t, http.MethodGet, origin+"/v1/devices", "", cookie, nil)
	if response.StatusCode != http.StatusOK {
		t.Fatalf("list devices status = %d, body=%s", response.StatusCode, readBody(response))
	}
	var body struct {
		Devices []json.RawMessage `json:"devices"`
	}
	if err := json.NewDecoder(response.Body).Decode(&body); err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	if len(body.Devices) != want {
		t.Fatalf("device count = %d, want %d", len(body.Devices), want)
	}
}

func writeTestEnvelope(t *testing.T, connection *websocket.Conn, envelope cloudprotocol.Envelope) {
	t.Helper()
	data, err := json.Marshal(envelope)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if err := connection.Write(ctx, websocket.MessageText, data); err != nil {
		t.Fatal(err)
	}
}

func readTestEnvelope(t *testing.T, connection *websocket.Conn) cloudprotocol.Envelope {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	messageType, data, err := connection.Read(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if messageType != websocket.MessageText {
		t.Fatal("expected text websocket message")
	}
	envelope, err := cloudprotocol.Decode(data, time.Now().UTC())
	if err != nil {
		t.Fatal(err)
	}
	return envelope
}

func readBody(response *http.Response) string {
	data, _ := io.ReadAll(response.Body)
	response.Body.Close()
	return fmt.Sprintf("%s", data)
}
