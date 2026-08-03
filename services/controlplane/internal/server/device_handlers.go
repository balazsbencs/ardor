package server

import (
	"context"
	"crypto/ed25519"
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"
	"strings"
	"time"

	"ardor.local/cloudprotocol"
	"ardor.local/controlplane/internal/auth"
	"ardor.local/controlplane/internal/securevalue"
	"ardor.local/controlplane/internal/store"
	"github.com/coder/websocket"
)

const (
	deviceChallengeTTL = 2 * time.Minute
	connectionTokenTTL = 2 * time.Minute
	claimFlowTTL       = 10 * time.Minute
)

type deviceChallengeRequest struct {
	Version   int    `json:"version"`
	DeviceID  string `json:"deviceId"`
	PublicKey string `json:"publicKey"`
}

type deviceTokenRequest struct {
	Version         int       `json:"version"`
	DeviceID        string    `json:"deviceId"`
	ChallengeID     string    `json:"challengeId"`
	Timestamp       time.Time `json:"timestamp"`
	ProtocolVersion int       `json:"protocolVersion"`
	Signature       string    `json:"signature"`
	ClaimEpoch      uint64    `json:"claimEpoch"`
}

func (server *Server) deviceChallenge(writer http.ResponseWriter, request *http.Request) {
	var body deviceChallengeRequest
	if !decodeJSON(writer, request, &body) {
		return
	}
	publicKey, err := base64.StdEncoding.DecodeString(body.PublicKey)
	if body.Version != cloudprotocol.Version || !cloudprotocol.IsUUID(body.DeviceID) || err != nil || len(publicKey) != ed25519.PublicKeySize {
		writeError(writer, http.StatusBadRequest, "invalid_device_identity", "Device identity is invalid")
		return
	}
	now := time.Now().UTC()
	device, err := server.repository.EnsureDevice(request.Context(), store.Device{ID: body.DeviceID, PublicKey: publicKey, CreatedAt: now, UpdatedAt: now})
	if errors.Is(err, store.ErrConflict) {
		writeError(writer, http.StatusUnauthorized, "device_key_mismatch", "Device identity is invalid")
		return
	} else if err != nil {
		serverError(server, writer, err)
		return
	}
	challengeID, err := securevalue.UUID()
	if err != nil {
		serverError(server, writer, err)
		return
	}
	nonce, err := securevalue.Bytes(32)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	challenge := store.DeviceChallenge{ID: challengeID, DeviceID: device.ID, Nonce: nonce, Purpose: "connection", CreatedAt: now, ExpiresAt: now.Add(deviceChallengeTTL)}
	if err := server.repository.CreateDeviceChallenge(request.Context(), challenge); err != nil {
		serverError(server, writer, err)
		return
	}
	writeJSON(writer, http.StatusCreated, map[string]any{
		"version": cloudprotocol.Version, "challengeId": challenge.ID,
		"nonce": base64.StdEncoding.EncodeToString(challenge.Nonce), "expiresAt": challenge.ExpiresAt,
	})
}

func (server *Server) deviceToken(writer http.ResponseWriter, request *http.Request) {
	var body deviceTokenRequest
	if !decodeJSON(writer, request, &body) {
		return
	}
	now := time.Now().UTC()
	if body.Version != cloudprotocol.Version || body.ProtocolVersion != cloudprotocol.Version || !cloudprotocol.IsUUID(body.DeviceID) || !cloudprotocol.IsUUID(body.ChallengeID) || now.Sub(body.Timestamp).Abs() > cloudprotocol.MaxClockSkew {
		writeError(writer, http.StatusUnauthorized, "invalid_device_proof", "Device proof is invalid")
		return
	}
	challenge, err := server.repository.ConsumeDeviceChallenge(request.Context(), body.ChallengeID, body.DeviceID, now)
	if err != nil {
		writeError(writer, http.StatusUnauthorized, "invalid_device_proof", "Device proof is invalid")
		return
	}
	device, err := server.repository.Device(request.Context(), body.DeviceID)
	if err != nil {
		writeError(writer, http.StatusUnauthorized, "invalid_device_proof", "Device proof is invalid")
		return
	}
	signature, err := base64.StdEncoding.DecodeString(body.Signature)
	transcript := cloudprotocol.AuthenticationTranscript(body.DeviceID, body.ChallengeID, base64.StdEncoding.EncodeToString(challenge.Nonce), body.Timestamp, body.ProtocolVersion, body.ClaimEpoch)
	if err != nil || len(signature) != ed25519.SignatureSize || !ed25519.Verify(ed25519.PublicKey(device.PublicKey), transcript, signature) {
		writeError(writer, http.StatusUnauthorized, "invalid_device_proof", "Device proof is invalid")
		return
	}
	if body.ClaimEpoch != device.ClaimEpoch {
		if _, ownerErr := server.repository.DeviceOwner(request.Context(), device.ID); ownerErr == nil || body.ClaimEpoch > device.ClaimEpoch {
			writeError(writer, http.StatusUnauthorized, "stale_claim_epoch", "Device claim state must be reconciled")
			return
		} else if !errors.Is(ownerErr, store.ErrNotFound) {
			serverError(server, writer, ownerErr)
			return
		}
	}
	rawToken, err := securevalue.Token(32)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	tokenID, err := securevalue.UUID()
	if err != nil {
		serverError(server, writer, err)
		return
	}
	token := store.ConnectionToken{ID: tokenID, DeviceID: device.ID, TokenHash: auth.HashCredential(rawToken), CreatedAt: now, ExpiresAt: now.Add(connectionTokenTTL)}
	if err := server.repository.CreateConnectionToken(request.Context(), token); err != nil {
		serverError(server, writer, err)
		return
	}
	websocketURL := strings.Replace(server.config.PublicOrigin, "https://", "wss://", 1)
	websocketURL = strings.Replace(websocketURL, "http://", "ws://", 1) + "/v1/device/connect"
	writeJSON(writer, http.StatusCreated, map[string]any{
		"version": cloudprotocol.Version, "token": rawToken, "websocketUrl": websocketURL,
		"claimEpoch": device.ClaimEpoch, "expiresAt": token.ExpiresAt,
	})
}

func (server *Server) deviceConnect(writer http.ResponseWriter, request *http.Request) {
	rawToken, ok := bearerToken(request)
	if !ok {
		writeError(writer, http.StatusUnauthorized, "invalid_device_token", "Device token is invalid")
		return
	}
	token, err := server.repository.ConsumeConnectionToken(request.Context(), auth.HashCredential(rawToken), time.Now().UTC())
	if err != nil {
		writeError(writer, http.StatusUnauthorized, "invalid_device_token", "Device token is invalid")
		return
	}
	connection, err := websocket.Accept(writer, request, nil)
	if err != nil {
		server.config.Logger.Printf("accept device websocket: %v", err)
		return
	}
	defer connection.CloseNow()
	connection.SetReadLimit(cloudprotocol.MaxMessageBytes)
	ctx := context.Background()
	remoteMutationsEnabled, err := server.readDeviceHello(ctx, connection, token.DeviceID)
	if err != nil {
		_ = connection.Close(websocket.StatusPolicyViolation, "invalid device hello")
		return
	}
	socket := server.hub.attach(token.DeviceID, connection, remoteMutationsEnabled)
	defer server.hub.detach(token.DeviceID, socket)
	_ = server.repository.SetDevicePresence(ctx, token.DeviceID, time.Now().UTC())
	if err := server.resumeOrCreateClaim(ctx, token.DeviceID, socket); err != nil && !errors.Is(err, store.ErrConflict) {
		server.config.Logger.Printf("prepare device claim flow device=%s: %v", token.DeviceID, err)
	}

	replays := cloudprotocol.NewReplayGuard(1024)
	for {
		messageType, data, err := connection.Read(ctx)
		if err != nil {
			return
		}
		if messageType != websocket.MessageText {
			_ = connection.Close(websocket.StatusPolicyViolation, "text messages required")
			return
		}
		envelope, err := cloudprotocol.Decode(data, time.Now().UTC())
		if err != nil || replays.Accept(envelope, time.Now().UTC()) != nil {
			_ = connection.Close(websocket.StatusPolicyViolation, "invalid cloud envelope")
			return
		}
		if envelope.Operation == cloudprotocol.OperationClaimDecision && envelope.Kind == cloudprotocol.KindResponse {
			if err := server.handleClaimDecision(ctx, token.DeviceID, socket, envelope); err != nil {
				_ = connection.Close(websocket.StatusPolicyViolation, "invalid claim decision")
				return
			}
			_ = server.repository.SetDevicePresence(ctx, token.DeviceID, time.Now().UTC())
			continue
		}
		if envelope.Kind == cloudprotocol.KindResponse && isPresetOperation(envelope.Operation) && socket.deliver(envelope) {
			_ = server.repository.SetDevicePresence(ctx, token.DeviceID, time.Now().UTC())
			continue
		}
		{
			_ = connection.Close(websocket.StatusPolicyViolation, "operation is not accepted from device")
			return
		}
	}
}

func (server *Server) readDeviceHello(ctx context.Context, connection *websocket.Conn, deviceID string) (bool, error) {
	readContext, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	messageType, data, err := connection.Read(readContext)
	if err != nil || messageType != websocket.MessageText {
		return false, errors.New("missing device hello")
	}
	envelope, err := cloudprotocol.Decode(data, time.Now().UTC())
	if err != nil || envelope.Kind != cloudprotocol.KindEvent || envelope.Operation != cloudprotocol.OperationHello {
		return false, errors.New("invalid device hello")
	}
	var payload struct {
		DeviceID               string `json:"deviceId"`
		ProtocolVersion        int    `json:"protocolVersion"`
		RemoteMutationsEnabled bool   `json:"remoteMutationsEnabled"`
	}
	if err := decodeStrictPayload(envelope.Payload, &payload); err != nil || payload.DeviceID != deviceID || payload.ProtocolVersion != cloudprotocol.Version {
		return false, errors.New("invalid device hello payload")
	}
	return payload.RemoteMutationsEnabled, nil
}

func isPresetOperation(operation string) bool {
	switch operation {
	case cloudprotocol.OperationPresetList, cloudprotocol.OperationPresetRead, cloudprotocol.OperationPresetSave, cloudprotocol.OperationPresetApply:
		return true
	default:
		return false
	}
}

func (server *Server) resumeOrCreateClaim(ctx context.Context, deviceID string, socket *deviceSocket) error {
	if flow, err := server.repository.PendingClaimForDevice(ctx, deviceID, time.Now().UTC()); err == nil {
		return socket.write(ctx, pendingClaimEnvelope(flow))
	} else if !errors.Is(err, store.ErrNotFound) {
		return err
	}
	manualCode, err := securevalue.ManualCode()
	if err != nil {
		return err
	}
	flowID, err := securevalue.UUID()
	if err != nil {
		return err
	}
	now := time.Now().UTC()
	flow := store.ClaimFlow{ID: flowID, DeviceID: deviceID, ManualCodeHash: auth.HashCredential(manualCode), Status: "code_visible", CreatedAt: now, ExpiresAt: now.Add(claimFlowTTL)}
	if err := server.repository.CreateClaimFlow(ctx, flow, now); err != nil {
		return err
	}
	envelope, err := cloudprotocol.NewEnvelope(cloudprotocol.KindEvent, cloudprotocol.OperationClaimCode, "", map[string]any{
		"claimFlowId": flow.ID, "manualCode": manualCode, "expiresAt": flow.ExpiresAt,
	}, now)
	if err != nil {
		return err
	}
	return socket.write(ctx, envelope)
}

func pendingClaimEnvelope(flow store.ClaimFlow) cloudprotocol.Envelope {
	envelope, _ := cloudprotocol.NewEnvelope(cloudprotocol.KindRequest, cloudprotocol.OperationClaimPending, "", map[string]any{
		"claimFlowId": flow.ID, "accountId": flow.AccountID, "accountDisplayName": flow.AccountDisplayName,
		"nonce": base64.StdEncoding.EncodeToString(flow.ClaimNonce), "nextClaimEpoch": flow.NextClaimEpoch, "expiresAt": flow.ExpiresAt,
	}, time.Now().UTC())
	return envelope
}

func (server *Server) handleClaimDecision(ctx context.Context, deviceID string, socket *deviceSocket, envelope cloudprotocol.Envelope) error {
	var payload struct {
		ClaimFlowID    string `json:"claimFlowId"`
		AccountID      string `json:"accountId"`
		Approved       bool   `json:"approved"`
		NextClaimEpoch uint64 `json:"nextClaimEpoch"`
		Signature      string `json:"signature"`
	}
	if err := decodeStrictPayload(envelope.Payload, &payload); err != nil {
		return err
	}
	flow, err := server.repository.ClaimForAccount(ctx, payload.ClaimFlowID, payload.AccountID)
	if err != nil || flow.DeviceID != deviceID || flow.NextClaimEpoch != payload.NextClaimEpoch {
		return errors.New("claim decision does not match pending claim")
	}
	device, err := server.repository.Device(ctx, deviceID)
	if err != nil {
		return err
	}
	signature, err := base64.StdEncoding.DecodeString(payload.Signature)
	transcript := cloudprotocol.ClaimDecisionTranscript(flow.ID, deviceID, flow.AccountID, base64.StdEncoding.EncodeToString(flow.ClaimNonce), flow.NextClaimEpoch, payload.Approved)
	if err != nil || len(signature) != ed25519.SignatureSize || !ed25519.Verify(ed25519.PublicKey(device.PublicKey), transcript, signature) {
		return errors.New("claim decision signature is invalid")
	}
	if flow.Status == "confirm_on_device" {
		flow, err = server.repository.CompleteClaim(ctx, flow.ID, payload.Approved, time.Now().UTC())
		if err != nil {
			return err
		}
	} else if flow.Status != "claimed" && flow.Status != "rejected" {
		return errors.New("claim is not awaiting a decision")
	}
	claimEpoch := device.ClaimEpoch
	if flow.Status == "claimed" {
		claimEpoch = flow.NextClaimEpoch
	}
	ack, err := cloudprotocol.NewEnvelope(cloudprotocol.KindEvent, cloudprotocol.OperationClaimDecision, envelope.MessageID, map[string]any{
		"claimFlowId": flow.ID, "status": flow.Status, "claimEpoch": claimEpoch,
	}, time.Now().UTC())
	if err != nil {
		return err
	}
	return socket.write(ctx, ack)
}

func writeEnvelope(ctx context.Context, connection *websocket.Conn, envelope cloudprotocol.Envelope) error {
	data, err := json.Marshal(envelope)
	if err != nil {
		return err
	}
	return connection.Write(ctx, websocket.MessageText, data)
}

func (server *Server) listDevices(writer http.ResponseWriter, request *http.Request) {
	account := accountFromContext(request.Context())
	devices, err := server.repository.ListAccountDevices(request.Context(), account.ID)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	result := make([]map[string]any, 0, len(devices))
	for _, device := range devices {
		online, remoteMutationsEnabled := server.hub.status(device.DeviceID)
		result = append(result, map[string]any{
			"id": device.DeviceID, "role": device.Role, "claimEpoch": device.ClaimEpoch,
			"online": online, "remoteMutationsEnabled": remoteMutationsEnabled, "lastSeenAt": device.LastSeenAt,
		})
	}
	writeJSON(writer, http.StatusOK, map[string]any{"devices": result})
}

func (server *Server) beginClaim(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	var body struct {
		Code string `json:"code"`
	}
	if !decodeJSON(writer, request, &body) {
		return
	}
	code := strings.ToUpper(strings.TrimSpace(body.Code))
	account := accountFromContext(request.Context())
	now := time.Now().UTC()
	limitKeys := claimRateLimitKeys(request, account.ID)
	if !server.limiter.allow(limitKeys, now) {
		writeError(writer, http.StatusTooManyRequests, "rate_limited", "Try again later")
		return
	}
	nonce, err := securevalue.Bytes(32)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	flow, err := server.repository.BeginClaim(request.Context(), auth.HashCredential(code), account, nonce, now)
	if err != nil {
		server.limiter.failure(limitKeys, now)
		writeError(writer, http.StatusNotFound, "invalid_claim_code", "Claim code is invalid or expired")
		return
	}
	server.limiter.success(limitKeys)
	envelope := pendingClaimEnvelope(flow)
	online := server.hub.send(request.Context(), flow.DeviceID, envelope) == nil
	writeJSON(writer, http.StatusAccepted, map[string]any{
		"id": flow.ID, "status": flow.Status, "deviceOnline": online, "expiresAt": flow.ExpiresAt,
	})
}

func (server *Server) getClaim(writer http.ResponseWriter, request *http.Request) {
	account := accountFromContext(request.Context())
	flow, err := server.repository.ClaimForAccount(request.Context(), request.PathValue("claimId"), account.ID)
	if err != nil {
		writeError(writer, http.StatusNotFound, "claim_not_found", "Claim was not found")
		return
	}
	writeJSON(writer, http.StatusOK, map[string]any{"id": flow.ID, "status": flow.Status, "deviceId": flow.DeviceID, "expiresAt": flow.ExpiresAt})
}

func (server *Server) unclaimDevice(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	account := accountFromContext(request.Context())
	deviceID := request.PathValue("deviceId")
	epoch, err := server.repository.UnclaimDevice(request.Context(), account.ID, deviceID, time.Now().UTC())
	if errors.Is(err, store.ErrNotFound) {
		writeError(writer, http.StatusNotFound, "device_not_found", "Device was not found")
		return
	} else if err != nil {
		serverError(server, writer, err)
		return
	}
	envelope, _ := cloudprotocol.NewEnvelope(cloudprotocol.KindEvent, cloudprotocol.OperationClaimEpoch, "", map[string]any{"claimEpoch": epoch}, time.Now().UTC())
	_ = server.hub.send(request.Context(), deviceID, envelope)
	writer.WriteHeader(http.StatusNoContent)
}
