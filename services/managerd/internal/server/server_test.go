package server

import (
	"bytes"
	"encoding/json"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"

	"ardor.local/managerd/internal/config"
)

func TestDeviceAndAuth(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: true, Token: "secret"})

	device := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/device", nil)
	handler.ServeHTTP(device, req)
	if device.Code != http.StatusOK {
		t.Fatalf("device status=%d", device.Code)
	}
	if !bytes.Contains(device.Body.Bytes(), []byte(`"authEnabled":true`)) {
		t.Fatalf("device body=%s", device.Body.String())
	}

	unauthorized := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodGet, "/api/assets/models", nil)
	handler.ServeHTTP(unauthorized, req)
	if unauthorized.Code != http.StatusUnauthorized {
		t.Fatalf("unauthorized status=%d", unauthorized.Code)
	}
}

func TestAuthCanBeDisabledForTesting(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: false})
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, httptest.NewRequest(http.MethodGet, "/api/assets/models", nil))
	if response.Code != http.StatusOK {
		t.Fatalf("auth-off status=%d body=%s", response.Code, response.Body.String())
	}
}

func TestTauriCORS(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: false})

	request := httptest.NewRequest(http.MethodGet, "/api/device", nil)
	request.Header.Set("Origin", "tauri://localhost")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("device status=%d", response.Code)
	}
	if origin := response.Header().Get("Access-Control-Allow-Origin"); origin != "tauri://localhost" {
		t.Fatalf("allow origin=%q", origin)
	}

	preflight := httptest.NewRequest(http.MethodOptions, "/api/assets/models", nil)
	preflight.Header.Set("Origin", "tauri://localhost")
	preflight.Header.Set("Access-Control-Request-Method", http.MethodPost)
	preflight.Header.Set("Access-Control-Request-Headers", "authorization,content-type")
	preflightResponse := httptest.NewRecorder()
	handler.ServeHTTP(preflightResponse, preflight)
	if preflightResponse.Code != http.StatusNoContent {
		t.Fatalf("preflight status=%d", preflightResponse.Code)
	}
	if headers := preflightResponse.Header().Get("Access-Control-Allow-Headers"); headers != "Authorization, Content-Type" {
		t.Fatalf("allow headers=%q", headers)
	}
}

func TestAssetUploadPresetSaveAndApply(t *testing.T) {
	dataRoot := t.TempDir()
	handler := New(config.Config{DataRoot: dataRoot, AuthEnabled: true, Token: "secret"})

	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	part, err := writer.CreateFormFile("file", "Clean Amp.nam")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := part.Write([]byte("nam-bytes")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}

	upload := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPost, "/api/assets/models", body)
	req.Header.Set("Content-Type", writer.FormDataContentType())
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(upload, req)
	if upload.Code != http.StatusCreated {
		t.Fatalf("upload status=%d body=%s", upload.Code, upload.Body.String())
	}
	if !bytes.Contains(upload.Body.Bytes(), []byte(`models/Clean_Amp.nam`)) {
		t.Fatalf("upload body=%s", upload.Body.String())
	}
	assertQueuedCommand(t, dataRoot, "reload_assets")

	preset := map[string]any{
		"version": float64(1),
		"name":    "HTTP Preset",
		"routing": "serial",
		"global": map[string]any{
			"inputGainDb":   float64(0),
			"outputGainDb":  float64(0),
			"safetyLimitDb": float64(-1),
		},
		"blocks": []any{
			map[string]any{"id": "block-1", "type": "nam", "enabled": true, "asset": "models/Clean_Amp.nam", "params": map[string]any{}},
		},
	}
	presetBytes, _ := json.Marshal(preset)
	save := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodPut, "/api/presets/banks/0/slots/0", bytes.NewReader(presetBytes))
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(save, req)
	if save.Code != http.StatusOK {
		t.Fatalf("save status=%d body=%s", save.Code, save.Body.String())
	}

	get := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodGet, "/api/presets/banks/0/slots/0", nil)
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(get, req)
	if get.Code != http.StatusOK || !bytes.Contains(get.Body.Bytes(), []byte("HTTP Preset")) {
		t.Fatalf("get status=%d body=%s", get.Code, get.Body.String())
	}

	apply := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodPost, "/api/presets/banks/0/slots/0/apply", nil)
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(apply, req)
	if apply.Code != http.StatusAccepted {
		t.Fatalf("apply status=%d body=%s", apply.Code, apply.Body.String())
	}
	assertQueuedCommand(t, dataRoot, "apply_preset")
}

func assertQueuedCommand(t *testing.T, dataRoot, commandType string) {
	t.Helper()
	entries, err := os.ReadDir(filepath.Join(dataRoot, "runtime", "commands"))
	if err != nil {
		t.Fatal(err)
	}
	for _, entry := range entries {
		body, err := os.ReadFile(filepath.Join(dataRoot, "runtime", "commands", entry.Name()))
		if err != nil {
			t.Fatal(err)
		}
		if bytes.Contains(body, []byte(`"type":"`+commandType+`"`)) {
			return
		}
	}
	t.Fatalf("missing queued command %q", commandType)
}

func TestDuplicateUploadReturnsConflict(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: false})
	for range 2 {
		body := &bytes.Buffer{}
		writer := multipart.NewWriter(body)
		part, err := writer.CreateFormFile("file", "same.nam")
		if err != nil {
			t.Fatal(err)
		}
		_, _ = part.Write([]byte("nam"))
		_ = writer.Close()
		request := httptest.NewRequest(http.MethodPost, "/api/assets/models", body)
		request.Header.Set("Content-Type", writer.FormDataContentType())
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, request)
		if response.Code == http.StatusCreated {
			continue
		}
		if response.Code != http.StatusConflict {
			t.Fatalf("duplicate status=%d body=%s", response.Code, response.Body.String())
		}
	}
}

func TestRenameAssetUpdatesSavedPresetReferences(t *testing.T) {
	dataRoot := t.TempDir()
	handler := New(config.Config{DataRoot: dataRoot, AuthEnabled: false})
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	part, err := writer.CreateFormFile("file", "raw capture.nam")
	if err != nil {
		t.Fatal(err)
	}
	_, _ = part.Write([]byte("nam"))
	_ = writer.Close()
	upload := httptest.NewRecorder()
	uploadRequest := httptest.NewRequest(http.MethodPost, "/api/assets/models", body)
	uploadRequest.Header.Set("Content-Type", writer.FormDataContentType())
	handler.ServeHTTP(upload, uploadRequest)
	if upload.Code != http.StatusCreated {
		t.Fatalf("upload status=%d body=%s", upload.Code, upload.Body.String())
	}

	preset := []byte(`{"version":1,"name":"Uses model","routing":"serial","global":{},"blocks":[{"id":"nam-1","type":"nam","enabled":true,"asset":"models/raw_capture.nam","params":{}}]}`)
	save := httptest.NewRecorder()
	handler.ServeHTTP(save, httptest.NewRequest(http.MethodPut, "/api/presets/banks/2/slots/1", bytes.NewReader(preset)))
	if save.Code != http.StatusOK {
		t.Fatalf("save status=%d body=%s", save.Code, save.Body.String())
	}

	rename := httptest.NewRecorder()
	handler.ServeHTTP(rename, httptest.NewRequest(http.MethodPatch, "/api/assets/models/raw_capture.nam", bytes.NewBufferString(`{"filename":"01-Clean.nam"}`)))
	if rename.Code != http.StatusOK || !bytes.Contains(rename.Body.Bytes(), []byte(`"updatedPresetCount":1`)) {
		t.Fatalf("rename status=%d body=%s", rename.Code, rename.Body.String())
	}
	get := httptest.NewRecorder()
	handler.ServeHTTP(get, httptest.NewRequest(http.MethodGet, "/api/presets/banks/2/slots/1", nil))
	if get.Code != http.StatusOK || !bytes.Contains(get.Body.Bytes(), []byte("models/01-Clean.nam")) {
		t.Fatalf("preset after rename status=%d body=%s", get.Code, get.Body.String())
	}
	if _, err := os.Stat(filepath.Join(dataRoot, "models", "01-Clean.nam")); err != nil {
		t.Fatal(err)
	}
}
