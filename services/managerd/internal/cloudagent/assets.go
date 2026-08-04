package cloudagent

import (
	"bufio"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"hash"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"ardor.local/cloudprotocol"
	"ardor.local/managerd/internal/assets"
	"ardor.local/managerd/internal/presets"
	"ardor.local/managerd/internal/runtimecontrol"
	"github.com/coder/websocket"
)

const staleAssetTransferAge = 24 * time.Hour

type assetTransfer struct {
	kind      assets.Kind
	filename  string
	overwrite bool
	size      int64
	written   int64
	path      string
	file      *os.File
	hash      hash.Hash
	source    json.RawMessage
}

type assetTransferRegistry struct {
	mu        sync.Mutex
	dataRoot  string
	transfers map[string]*assetTransfer
}

func newAssetTransferRegistry(dataRoot string) *assetTransferRegistry {
	registry := &assetTransferRegistry{dataRoot: dataRoot, transfers: map[string]*assetTransfer{}}
	registry.removeStaleParts(time.Now().UTC())
	return registry
}

func (registry *assetTransferRegistry) removeStaleParts(now time.Time) {
	directory := filepath.Join(registry.dataRoot, "cloud-transfers")
	entries, err := os.ReadDir(directory)
	if err != nil {
		return
	}
	for _, entry := range entries {
		info, err := entry.Info()
		if err == nil && !entry.IsDir() && now.Sub(info.ModTime()) >= staleAssetTransferAge {
			_ = os.Remove(filepath.Join(directory, entry.Name()))
		}
	}
}

func (agent *Agent) handleAssetOperation(ctx context.Context, connection *websocket.Conn, request cloudprotocol.Envelope) error {
	var result any
	var failure *operationError
	if request.Operation != cloudprotocol.OperationAssetList && !agent.config.RemoteMutationsEnabled {
		failure = &operationError{Code: "remote_mutations_disabled", Message: "remote asset mutations are disabled on this pedal"}
	} else {
		result, failure = agent.executeAssetOperation(request)
	}
	payload := map[string]any{"ok": true, "result": result}
	if failure != nil {
		payload = map[string]any{"ok": false, "error": failure}
	}
	response, err := cloudprotocol.NewEnvelope(cloudprotocol.KindResponse, request.Operation, request.MessageID, payload, time.Now().UTC())
	if err != nil {
		return err
	}
	return writeEnvelope(ctx, connection, response)
}

func (agent *Agent) executeAssetOperation(request cloudprotocol.Envelope) (any, *operationError) {
	store := assets.NewStore(agent.config.DataRoot)
	switch request.Operation {
	case cloudprotocol.OperationAssetList:
		kind, failure := decodeAssetKind(request.Payload)
		if failure != nil {
			return nil, failure
		}
		items, err := store.List(kind)
		if err != nil {
			return nil, assetFailure("asset_list_failed", err)
		}
		return map[string]any{"assets": items}, nil
	case cloudprotocol.OperationAssetDelete:
		var payload struct {
			Kind string `json:"kind"`
			ID   string `json:"id"`
		}
		if err := decodePayload(request.Payload, &payload); err != nil {
			return nil, assetFailure("invalid_asset_request", err)
		}
		kind, err := assetKind(payload.Kind)
		if err != nil {
			return nil, assetFailure("invalid_asset_kind", err)
		}
		if err := store.Delete(kind, payload.ID); err != nil {
			code := "asset_delete_failed"
			if errors.Is(err, os.ErrNotExist) {
				code = "asset_not_found"
			}
			return nil, assetFailure(code, err)
		}
		agent.assets.removeMetadata(kind, payload.ID)
		_ = runtimecontrol.QueueAssetReload(agent.config.DataRoot)
		return map[string]any{"deleted": true}, nil
	case cloudprotocol.OperationAssetRename:
		var payload struct {
			Kind     string `json:"kind"`
			ID       string `json:"id"`
			Filename string `json:"filename"`
		}
		if err := decodePayload(request.Payload, &payload); err != nil {
			return nil, assetFailure("invalid_asset_request", err)
		}
		kind, err := assetKind(payload.Kind)
		if err != nil {
			return nil, assetFailure("invalid_asset_kind", err)
		}
		oldPath := assetRelativePath(kind, payload.ID)
		info, err := store.Rename(kind, payload.ID, payload.Filename)
		if err != nil {
			code := "asset_rename_failed"
			if errors.Is(err, assets.ErrExists) {
				code = "asset_exists"
			} else if errors.Is(err, os.ErrNotExist) {
				code = "asset_not_found"
			}
			return nil, assetFailure(code, err)
		}
		updated, err := presets.NewStore(agent.config.DataRoot).ReplaceAssetReferences(oldPath, info.Path)
		if err != nil {
			return nil, assetFailure("asset_reference_update_failed", err)
		}
		agent.assets.renameMetadata(kind, payload.ID, info.ID)
		_ = runtimecontrol.QueueAssetReload(agent.config.DataRoot)
		return map[string]any{"asset": info, "updatedPresetCount": updated}, nil
	case cloudprotocol.OperationAssetBegin:
		return agent.assets.begin(request.Payload)
	case cloudprotocol.OperationAssetChunk:
		return agent.assets.chunk(request.Payload)
	case cloudprotocol.OperationAssetCommit:
		result, failure := agent.assets.commit(request.Payload)
		if failure == nil {
			_ = runtimecontrol.QueueAssetReload(agent.config.DataRoot)
		}
		return result, failure
	case cloudprotocol.OperationAssetAbort:
		return agent.assets.abort(request.Payload)
	default:
		return nil, &operationError{Code: "invalid_operation", Message: "unsupported asset operation"}
	}
}

func (registry *assetTransferRegistry) begin(raw json.RawMessage) (any, *operationError) {
	var payload struct {
		TransferID string          `json:"transferId"`
		Kind       string          `json:"kind"`
		Filename   string          `json:"filename"`
		Overwrite  bool            `json:"overwrite"`
		Size       int64           `json:"size"`
		Source     json.RawMessage `json:"source,omitempty"`
	}
	if err := decodePayload(raw, &payload); err != nil {
		return nil, assetFailure("invalid_asset_request", err)
	}
	if !cloudprotocol.IsUUID(payload.TransferID) {
		return nil, assetFailure("invalid_transfer_id", errors.New("transferId must be a UUID"))
	}
	kind, err := assetKind(payload.Kind)
	if err != nil {
		return nil, assetFailure("invalid_asset_kind", err)
	}
	filename, err := assets.SanitizeFilename(payload.Filename, kind)
	if err != nil {
		return nil, assetFailure("invalid_asset", err)
	}
	if payload.Size <= 0 || payload.Size > assetLimit(kind) {
		return nil, assetFailure("asset_too_large", fmt.Errorf("asset size must be between 1 and %d bytes", assetLimit(kind)))
	}
	if len(payload.Source) > 8*1024 {
		return nil, assetFailure("invalid_source_metadata", errors.New("source metadata is too large"))
	}
	registry.mu.Lock()
	defer registry.mu.Unlock()
	if _, exists := registry.transfers[payload.TransferID]; exists {
		return nil, assetFailure("transfer_exists", errors.New("transfer already exists"))
	}
	directory := filepath.Join(registry.dataRoot, "cloud-transfers")
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return nil, assetFailure("asset_stage_failed", err)
	}
	path := filepath.Join(directory, payload.TransferID+".part")
	file, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0o600)
	if errors.Is(err, os.ErrExist) {
		_ = os.Remove(path)
		file, err = os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0o600)
	}
	if err != nil {
		return nil, assetFailure("asset_stage_failed", err)
	}
	registry.transfers[payload.TransferID] = &assetTransfer{
		kind: kind, filename: filename, overwrite: payload.Overwrite, size: payload.Size,
		path: path, file: file, hash: sha256.New(), source: bytes.Clone(payload.Source),
	}
	return map[string]any{"transferId": payload.TransferID, "nextOffset": 0}, nil
}

func (registry *assetTransferRegistry) chunk(raw json.RawMessage) (any, *operationError) {
	var payload struct {
		TransferID string `json:"transferId"`
		Offset     int64  `json:"offset"`
		Data       string `json:"data"`
	}
	if err := decodePayload(raw, &payload); err != nil {
		return nil, assetFailure("invalid_asset_request", err)
	}
	data, err := base64.StdEncoding.DecodeString(payload.Data)
	if err != nil || len(data) == 0 || len(data) > cloudprotocol.AssetChunkBytes {
		return nil, assetFailure("invalid_asset_chunk", errors.New("chunk is not valid bounded base64 data"))
	}
	registry.mu.Lock()
	defer registry.mu.Unlock()
	transfer := registry.transfers[payload.TransferID]
	if transfer == nil {
		return nil, assetFailure("transfer_not_found", errors.New("transfer was not found"))
	}
	if payload.Offset != transfer.written || transfer.written+int64(len(data)) > transfer.size {
		return nil, assetFailure("invalid_asset_offset", errors.New("chunk offset or size is invalid"))
	}
	if _, err := transfer.file.Write(data); err != nil {
		registry.discardLocked(payload.TransferID)
		return nil, assetFailure("asset_stage_failed", err)
	}
	_, _ = transfer.hash.Write(data)
	transfer.written += int64(len(data))
	return map[string]any{"transferId": payload.TransferID, "nextOffset": transfer.written}, nil
}

func (registry *assetTransferRegistry) commit(raw json.RawMessage) (any, *operationError) {
	var payload struct {
		TransferID string `json:"transferId"`
		SHA256     string `json:"sha256"`
	}
	if err := decodePayload(raw, &payload); err != nil {
		return nil, assetFailure("invalid_asset_request", err)
	}
	registry.mu.Lock()
	defer registry.mu.Unlock()
	transfer := registry.transfers[payload.TransferID]
	if transfer == nil {
		return nil, assetFailure("transfer_not_found", errors.New("transfer was not found"))
	}
	if transfer.written != transfer.size || !strings.EqualFold(payload.SHA256, hex.EncodeToString(transfer.hash.Sum(nil))) {
		registry.discardLocked(payload.TransferID)
		return nil, assetFailure("asset_integrity_failed", errors.New("asset size or SHA-256 did not match"))
	}
	if err := transfer.file.Sync(); err != nil {
		registry.discardLocked(payload.TransferID)
		return nil, assetFailure("asset_stage_failed", err)
	}
	if err := transfer.file.Close(); err != nil {
		registry.discardLocked(payload.TransferID)
		return nil, assetFailure("asset_stage_failed", err)
	}
	transfer.file = nil
	if err := validateAssetFile(transfer.path, transfer.kind); err != nil {
		registry.discardLocked(payload.TransferID)
		return nil, assetFailure("invalid_asset", err)
	}
	staged, err := os.Open(transfer.path)
	if err != nil {
		registry.discardLocked(payload.TransferID)
		return nil, assetFailure("asset_stage_failed", err)
	}
	info, err := assets.NewStore(registry.dataRoot).Save(transfer.kind, transfer.filename, staged, transfer.overwrite)
	_ = staged.Close()
	if err != nil {
		registry.discardLocked(payload.TransferID)
		code := "asset_install_failed"
		if errors.Is(err, assets.ErrExists) {
			code = "asset_exists"
		}
		return nil, assetFailure(code, err)
	}
	_ = os.Remove(transfer.path)
	delete(registry.transfers, payload.TransferID)
	if len(transfer.source) > 0 && string(transfer.source) != "null" {
		_ = registry.writeMetadata(transfer.kind, info.ID, transfer.source)
	}
	return info, nil
}

func (registry *assetTransferRegistry) abort(raw json.RawMessage) (any, *operationError) {
	var payload struct {
		TransferID string `json:"transferId"`
	}
	if err := decodePayload(raw, &payload); err != nil {
		return nil, assetFailure("invalid_asset_request", err)
	}
	registry.mu.Lock()
	defer registry.mu.Unlock()
	registry.discardLocked(payload.TransferID)
	return map[string]any{"aborted": true}, nil
}

func (registry *assetTransferRegistry) discardLocked(id string) {
	transfer := registry.transfers[id]
	if transfer == nil {
		return
	}
	if transfer.file != nil {
		_ = transfer.file.Close()
	}
	_ = os.Remove(transfer.path)
	delete(registry.transfers, id)
}

func validateAssetFile(path string, kind assets.Kind) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	if kind == assets.KindIR {
		header := make([]byte, 12)
		if _, err := io.ReadFull(file, header); err != nil || string(header[:4]) != "RIFF" || string(header[8:12]) != "WAVE" {
			return errors.New("cabinet IR must be a RIFF/WAVE file")
		}
		return nil
	}
	reader := bufio.NewReader(file)
	decoder := json.NewDecoder(reader)
	var model map[string]json.RawMessage
	if err := decoder.Decode(&model); err != nil || len(model) == 0 {
		return errors.New("NAM model must contain a JSON object")
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("NAM model must contain one JSON object")
	}
	return nil
}

func decodeAssetKind(raw json.RawMessage) (assets.Kind, *operationError) {
	var payload struct {
		Kind string `json:"kind"`
	}
	if err := decodePayload(raw, &payload); err != nil {
		return "", assetFailure("invalid_asset_request", err)
	}
	kind, err := assetKind(payload.Kind)
	if err != nil {
		return "", assetFailure("invalid_asset_kind", err)
	}
	return kind, nil
}

func assetKind(value string) (assets.Kind, error) {
	switch value {
	case "models":
		return assets.KindModel, nil
	case "irs":
		return assets.KindIR, nil
	default:
		return "", errors.New("asset kind must be models or irs")
	}
}

func assetLimit(kind assets.Kind) int64 {
	if kind == assets.KindModel {
		return cloudprotocol.MaxModelAssetBytes
	}
	return cloudprotocol.MaxIRAssetBytes
}

func assetRelativePath(kind assets.Kind, id string) string {
	if kind == assets.KindModel {
		return "models/" + id
	}
	return "irs/" + id
}

func assetFailure(code string, err error) *operationError {
	return &operationError{Code: code, Message: err.Error()}
}

func (registry *assetTransferRegistry) metadataPath(kind assets.Kind, id string) string {
	return filepath.Join(registry.dataRoot, "assets", "metadata", string(kind)+"-"+id+".json")
}

func (registry *assetTransferRegistry) writeMetadata(kind assets.Kind, id string, raw []byte) error {
	directory := filepath.Dir(registry.metadataPath(kind, id))
	if err := os.MkdirAll(directory, 0o755); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(directory, ".source-*.tmp")
	if err != nil {
		return err
	}
	path := temporary.Name()
	defer os.Remove(path)
	if _, err := temporary.Write(raw); err != nil {
		temporary.Close()
		return err
	}
	if err := temporary.Sync(); err != nil {
		temporary.Close()
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	return os.Rename(path, registry.metadataPath(kind, id))
}

func (registry *assetTransferRegistry) removeMetadata(kind assets.Kind, id string) {
	_ = os.Remove(registry.metadataPath(kind, id))
}

func (registry *assetTransferRegistry) renameMetadata(kind assets.Kind, oldID, newID string) {
	if oldID != newID {
		_ = os.Rename(registry.metadataPath(kind, oldID), registry.metadataPath(kind, newID))
	}
}
