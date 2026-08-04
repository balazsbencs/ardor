package server

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"time"

	"ardor.local/cloudprotocol"
	"ardor.local/controlplane/internal/securevalue"
	"ardor.local/controlplane/internal/store"
)

const presetRequestTimeout = 20 * time.Second

type deviceOperationError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

func (server *Server) listDevicePresets(writer http.ResponseWriter, request *http.Request) {
	if !server.authorizeDevice(writer, request) {
		return
	}
	result, failure := server.relayPreset(request.Context(), request.PathValue("deviceId"), cloudprotocol.OperationPresetList, map[string]any{}, false)
	server.writeRelayResult(writer, result, failure, http.StatusOK)
}

func (server *Server) getDevicePreset(writer http.ResponseWriter, request *http.Request) {
	if !server.authorizeDevice(writer, request) {
		return
	}
	bank, slot, ok := parseCloudSlot(writer, request)
	if !ok {
		return
	}
	result, failure := server.relayPreset(request.Context(), request.PathValue("deviceId"), cloudprotocol.OperationPresetRead, map[string]any{"bank": bank, "slot": slot}, false)
	server.writeRelayResult(writer, result, failure, http.StatusOK)
}

func (server *Server) saveDevicePreset(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) || !server.authorizeDevice(writer, request) {
		return
	}
	bank, slot, ok := parseCloudSlot(writer, request)
	if !ok {
		return
	}
	var preset map[string]any
	if !decodeJSONLimit(writer, request, &preset, cloudprotocol.MaxMessageBytes-4096) {
		return
	}
	server.relayPresetMutation(writer, request, cloudprotocol.OperationPresetSave, map[string]any{"bank": bank, "slot": slot, "preset": preset}, http.StatusOK)
}

func (server *Server) applyDevicePreset(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) || !server.authorizeDevice(writer, request) {
		return
	}
	bank, slot, ok := parseCloudSlot(writer, request)
	if !ok {
		return
	}
	server.relayPresetMutation(writer, request, cloudprotocol.OperationPresetApply, map[string]any{"bank": bank, "slot": slot}, http.StatusAccepted)
}

func (server *Server) relayPresetMutation(writer http.ResponseWriter, request *http.Request, operation string, payload any, successStatus int) {
	idempotencyKey := request.Header.Get("Idempotency-Key")
	if !cloudprotocol.IsUUID(idempotencyKey) {
		writeError(writer, http.StatusBadRequest, "invalid_idempotency_key", "Idempotency-Key must be a UUID")
		return
	}
	canonical, err := json.Marshal(struct {
		Operation string `json:"operation"`
		Payload   any    `json:"payload"`
	}{Operation: operation, Payload: payload})
	if err != nil {
		serverError(server, writer, err)
		return
	}
	operationID, err := securevalue.UUID()
	if err != nil {
		serverError(server, writer, err)
		return
	}
	now := time.Now().UTC()
	account := accountFromContext(request.Context())
	record := store.DeviceOperation{
		ID: operationID, AccountID: account.ID, DeviceID: request.PathValue("deviceId"), IdempotencyKey: idempotencyKey,
		Operation: operation, RequestHash: sha256.Sum256(canonical), State: "pending", CreatedAt: now,
	}
	existing, created, err := server.repository.BeginDeviceOperation(request.Context(), record)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	if !created {
		if existing.Operation != record.Operation || existing.RequestHash != record.RequestHash {
			writeError(writer, http.StatusConflict, "idempotency_conflict", "Idempotency-Key was already used for a different request")
			return
		}
		if existing.State == "completed" {
			writeRawJSON(writer, existing.HTTPStatus, existing.Response)
			return
		}
		writeError(writer, http.StatusConflict, "operation_in_progress", "The original operation is still in progress")
		return
	}
	result, failure := server.relayPreset(request.Context(), record.DeviceID, operation, payload, true)
	status, response := relayHTTPResponse(result, failure, successStatus)
	audit := store.AuditEvent{
		ActorType: "account", ActorID: account.ID, EventType: "device.operation.completed", SubjectType: "device", SubjectID: record.DeviceID,
		Metadata: map[string]any{"operation": operation, "idempotencyKey": idempotencyKey, "httpStatus": status}, CreatedAt: time.Now().UTC(),
	}
	if err := server.repository.CompleteDeviceOperation(request.Context(), record.ID, status, response, audit, time.Now().UTC()); err != nil {
		serverError(server, writer, err)
		return
	}
	writeRawJSON(writer, status, response)
}

func (server *Server) authorizeDevice(writer http.ResponseWriter, request *http.Request) bool {
	account := accountFromContext(request.Context())
	owner, err := server.repository.DeviceOwner(request.Context(), request.PathValue("deviceId"))
	if err != nil || owner != account.ID {
		writeError(writer, http.StatusNotFound, "device_not_found", "Device was not found")
		return false
	}
	return true
}

func parseCloudSlot(writer http.ResponseWriter, request *http.Request) (int, int, bool) {
	bank, bankErr := strconv.Atoi(request.PathValue("bank"))
	slot, slotErr := strconv.Atoi(request.PathValue("slot"))
	if bankErr != nil || slotErr != nil || bank < 0 || bank > 99 || slot < 0 || slot > 3 {
		writeError(writer, http.StatusBadRequest, "invalid_preset_slot", "Preset slot is out of range")
		return 0, 0, false
	}
	return bank, slot, true
}

func (server *Server) relayPreset(ctx context.Context, deviceID, operation string, payload any, mutation bool) (json.RawMessage, *deviceOperationError) {
	envelope, err := cloudprotocol.NewEnvelope(cloudprotocol.KindRequest, operation, "", payload, time.Now().UTC())
	if err != nil {
		return nil, &deviceOperationError{Code: "invalid_operation", Message: err.Error()}
	}
	relayContext, cancel := context.WithTimeout(ctx, presetRequestTimeout)
	defer cancel()
	response, err := server.hub.request(relayContext, deviceID, envelope, mutation)
	if errors.Is(err, errDeviceOffline) {
		return nil, &deviceOperationError{Code: "device_offline", Message: "Device is offline"}
	}
	if errors.Is(err, context.DeadlineExceeded) {
		return nil, &deviceOperationError{Code: "device_timeout", Message: "Device did not respond in time"}
	}
	if err != nil {
		if err.Error() == "remote mutations are disabled on the device" {
			return nil, &deviceOperationError{Code: "remote_mutations_disabled", Message: err.Error()}
		}
		return nil, &deviceOperationError{Code: "device_unavailable", Message: "Device request failed"}
	}
	if response.Kind != cloudprotocol.KindResponse || response.Operation != operation || response.CorrelationID != envelope.MessageID {
		return nil, &deviceOperationError{Code: "invalid_device_response", Message: "Device returned an invalid response"}
	}
	var wrapper struct {
		OK     bool                  `json:"ok"`
		Result json.RawMessage       `json:"result"`
		Error  *deviceOperationError `json:"error"`
	}
	decoder := json.NewDecoder(bytes.NewReader(response.Payload))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&wrapper); err != nil || (wrapper.OK && len(wrapper.Result) == 0) || (!wrapper.OK && wrapper.Error == nil) {
		return nil, &deviceOperationError{Code: "invalid_device_response", Message: "Device returned an invalid response"}
	}
	if !wrapper.OK {
		return nil, wrapper.Error
	}
	return wrapper.Result, nil
}

func (server *Server) writeRelayResult(writer http.ResponseWriter, result json.RawMessage, failure *deviceOperationError, successStatus int) {
	status, response := relayHTTPResponse(result, failure, successStatus)
	writeRawJSON(writer, status, response)
}

func relayHTTPResponse(result json.RawMessage, failure *deviceOperationError, successStatus int) (int, []byte) {
	if failure == nil {
		return successStatus, bytes.Clone(result)
	}
	status := http.StatusBadGateway
	switch failure.Code {
	case "preset_not_found", "device_not_found", "asset_not_found":
		status = http.StatusNotFound
	case "preset_save_failed", "invalid_operation", "invalid_preset_slot", "invalid_asset_request",
		"invalid_asset_kind", "invalid_asset", "invalid_asset_chunk", "invalid_asset_offset",
		"asset_integrity_failed", "asset_size_changed", "asset_rename_failed", "asset_delete_failed":
		status = http.StatusBadRequest
	case "remote_mutations_disabled", "asset_exists", "transfer_exists":
		status = http.StatusConflict
	case "device_offline", "device_unavailable", "runtime_command_failed":
		status = http.StatusServiceUnavailable
	case "device_timeout":
		status = http.StatusGatewayTimeout
	case "asset_too_large":
		status = http.StatusRequestEntityTooLarge
	}
	response, _ := json.Marshal(map[string]any{"error": failure.Code, "message": failure.Message})
	return status, response
}

func writeRawJSON(writer http.ResponseWriter, status int, response []byte) {
	writer.Header().Set("Content-Type", "application/json")
	writer.WriteHeader(status)
	_, _ = writer.Write(append(response, '\n'))
}
