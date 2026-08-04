package server

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"

	"ardor.local/cloudprotocol"
	"ardor.local/controlplane/internal/securevalue"
)

const multipartOverheadAllowance = 1 << 20

func (server *Server) listDeviceAssets(writer http.ResponseWriter, request *http.Request) {
	if !server.authorizeDevice(writer, request) {
		return
	}
	kind, ok := cloudAssetKind(writer, request.PathValue("kind"))
	if !ok {
		return
	}
	result, failure := server.relayPreset(request.Context(), request.PathValue("deviceId"), cloudprotocol.OperationAssetList, map[string]any{"kind": kind}, false)
	server.writeRelayResult(writer, result, failure, http.StatusOK)
}

func (server *Server) uploadDeviceAsset(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) || !server.authorizeDevice(writer, request) {
		return
	}
	kind, ok := cloudAssetKind(writer, request.PathValue("kind"))
	if !ok {
		return
	}
	limit := int64(cloudprotocol.MaxIRAssetBytes)
	if kind == "models" {
		limit = cloudprotocol.MaxModelAssetBytes
	}
	request.Body = http.MaxBytesReader(writer, request.Body, limit+multipartOverheadAllowance)
	if err := request.ParseMultipartForm(1 << 20); err != nil {
		writeError(writer, http.StatusBadRequest, "invalid_asset_upload", "Upload must be bounded multipart form data")
		return
	}
	if request.MultipartForm != nil {
		defer request.MultipartForm.RemoveAll()
	}
	file, header, err := request.FormFile("file")
	if err != nil {
		writeError(writer, http.StatusBadRequest, "missing_file", "A file is required")
		return
	}
	defer file.Close()
	if header.Size <= 0 || header.Size > limit {
		writeError(writer, http.StatusRequestEntityTooLarge, "asset_too_large", fmt.Sprintf("Asset must be between 1 and %d bytes", limit))
		return
	}
	overwrite := request.FormValue("overwrite") == "true"
	result, failure := server.installDeviceAsset(request.Context(), request.PathValue("deviceId"), kind, header.Filename, header.Size, file, overwrite, nil)
	server.writeRelayResult(writer, result, failure, http.StatusCreated)
}

func (server *Server) deleteDeviceAsset(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) || !server.authorizeDevice(writer, request) {
		return
	}
	kind, ok := cloudAssetKind(writer, request.PathValue("kind"))
	if !ok {
		return
	}
	result, failure := server.relayPreset(request.Context(), request.PathValue("deviceId"), cloudprotocol.OperationAssetDelete, map[string]any{
		"kind": kind, "id": request.PathValue("assetId"),
	}, true)
	server.writeRelayResult(writer, result, failure, http.StatusOK)
}

func (server *Server) renameDeviceAsset(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) || !server.authorizeDevice(writer, request) {
		return
	}
	kind, ok := cloudAssetKind(writer, request.PathValue("kind"))
	if !ok {
		return
	}
	var body struct {
		Filename string `json:"filename"`
	}
	if !decodeJSONLimit(writer, request, &body, 8<<10) {
		return
	}
	result, failure := server.relayPreset(request.Context(), request.PathValue("deviceId"), cloudprotocol.OperationAssetRename, map[string]any{
		"kind": kind, "id": request.PathValue("assetId"), "filename": body.Filename,
	}, true)
	server.writeRelayResult(writer, result, failure, http.StatusOK)
}

func (server *Server) installDeviceAsset(ctx context.Context, deviceID, kind, filename string, size int64, reader io.Reader, overwrite bool, source any) (json.RawMessage, *deviceOperationError) {
	transferID, err := securevalue.UUID()
	if err != nil {
		return nil, &deviceOperationError{Code: "transfer_setup_failed", Message: "Could not start asset transfer"}
	}
	begin := map[string]any{
		"transferId": transferID, "kind": kind, "filename": filename,
		"overwrite": overwrite, "size": size,
	}
	if source != nil {
		begin["source"] = source
	}
	if _, failure := server.relayPreset(ctx, deviceID, cloudprotocol.OperationAssetBegin, begin, true); failure != nil {
		return nil, failure
	}
	abort := func() {
		_, _ = server.relayPreset(context.Background(), deviceID, cloudprotocol.OperationAssetAbort, map[string]any{"transferId": transferID}, true)
	}
	hasher := sha256.New()
	buffer := make([]byte, cloudprotocol.AssetChunkBytes)
	var offset int64
	for {
		count, readErr := reader.Read(buffer)
		if count > 0 {
			if offset+int64(count) > size {
				abort()
				return nil, &deviceOperationError{Code: "asset_size_changed", Message: "Asset was larger than its declared size"}
			}
			chunk := buffer[:count]
			_, _ = hasher.Write(chunk)
			if _, failure := server.relayPreset(ctx, deviceID, cloudprotocol.OperationAssetChunk, map[string]any{
				"transferId": transferID, "offset": offset, "data": base64.StdEncoding.EncodeToString(chunk),
			}, true); failure != nil {
				abort()
				return nil, failure
			}
			offset += int64(count)
		}
		if errors.Is(readErr, io.EOF) {
			break
		}
		if readErr != nil {
			abort()
			return nil, &deviceOperationError{Code: "asset_read_failed", Message: "Asset could not be read"}
		}
	}
	if offset != size {
		abort()
		return nil, &deviceOperationError{Code: "asset_size_changed", Message: "Asset was smaller than its declared size"}
	}
	return server.relayPreset(ctx, deviceID, cloudprotocol.OperationAssetCommit, map[string]any{
		"transferId": transferID, "sha256": hex.EncodeToString(hasher.Sum(nil)),
	}, true)
}

func cloudAssetKind(writer http.ResponseWriter, value string) (string, bool) {
	value = strings.ToLower(value)
	if value != "models" && value != "irs" {
		writeError(writer, http.StatusNotFound, "asset_kind_not_found", "Asset kind was not found")
		return "", false
	}
	return value, true
}
