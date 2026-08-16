package server

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"testing/fstest"
	"time"

	"ardor.local/managerd/internal/config"
)

func TestDeviceHostedManagerUI(t *testing.T) {
	webFiles := fstest.MapFS{
		"index.html":     {Data: []byte(`<!doctype html><title>Ardor Manager</title><div id="root"></div>`)},
		"assets/app.js":  {Data: []byte(`console.log("ardor")`)},
		"assets/app.css": {Data: []byte(`body { color: green; }`)},
	}
	handler := NewWithWebUI(config.Config{DataRoot: t.TempDir(), AuthEnabled: false}, webFiles)

	for _, requestPath := range []string{"/", "/presets/banks/2"} {
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, httptest.NewRequest(http.MethodGet, requestPath, nil))
		if response.Code != http.StatusOK || !bytes.Contains(response.Body.Bytes(), []byte("Ardor Manager")) {
			t.Fatalf("ui path=%s status=%d body=%s", requestPath, response.Code, response.Body.String())
		}
		if cache := response.Header().Get("Cache-Control"); cache != "no-cache" {
			t.Fatalf("ui cache=%q", cache)
		}
		if csp := response.Header().Get("Content-Security-Policy"); csp == "" {
			t.Fatal("device-hosted UI is missing a content security policy")
		}
	}

	asset := httptest.NewRecorder()
	handler.ServeHTTP(asset, httptest.NewRequest(http.MethodGet, "/assets/app.js", nil))
	if asset.Code != http.StatusOK || !bytes.Contains(asset.Body.Bytes(), []byte("ardor")) {
		t.Fatalf("asset status=%d body=%s", asset.Code, asset.Body.String())
	}
	if cache := asset.Header().Get("Cache-Control"); cache != "public, max-age=31536000, immutable" {
		t.Fatalf("asset cache=%q", cache)
	}

	missingAPI := httptest.NewRecorder()
	handler.ServeHTTP(missingAPI, httptest.NewRequest(http.MethodGet, "/api/missing", nil))
	if missingAPI.Code != http.StatusNotFound || bytes.Contains(missingAPI.Body.Bytes(), []byte("Ardor Manager")) {
		t.Fatalf("missing API status=%d body=%s", missingAPI.Code, missingAPI.Body.String())
	}

	missingAsset := httptest.NewRecorder()
	handler.ServeHTTP(missingAsset, httptest.NewRequest(http.MethodGet, "/assets/missing.js", nil))
	if missingAsset.Code != http.StatusNotFound {
		t.Fatalf("missing asset status=%d body=%s", missingAsset.Code, missingAsset.Body.String())
	}
}

func TestDeviceAndAuth(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: true})

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

func TestDeviceReportsSignedUpdateCapability(t *testing.T) {
	root := t.TempDir()
	publicKey, _, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	keyPath := filepath.Join(root, "update.pub")
	if err := os.WriteFile(keyPath, []byte(base64.StdEncoding.EncodeToString(publicKey)+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	handler := New(config.Config{
		DataRoot: root, AuthEnabled: false, UpdatePublicKey: keyPath,
		SoftwareVersion: "0.1.24", BaseSystemVersion: "0.1.24", UpdaterVersion: "1.0.0",
	})
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, httptest.NewRequest(http.MethodGet, "/api/device", nil))
	if response.Code != http.StatusOK || !bytes.Contains(response.Body.Bytes(), []byte(`"softwareUpdate":true`)) || !bytes.Contains(response.Body.Bytes(), []byte(`"softwareVersion":"0.1.24"`)) {
		t.Fatalf("device status=%d body=%s", response.Code, response.Body.String())
	}
	status := httptest.NewRecorder()
	handler.ServeHTTP(status, httptest.NewRequest(http.MethodGet, "/api/system/update/status", nil))
	if status.Code != http.StatusOK || !bytes.Contains(status.Body.Bytes(), []byte(`"installedVersion":"0.1.24"`)) {
		t.Fatalf("update status=%d body=%s", status.Code, status.Body.String())
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

func TestAuthFailureLimiterExpiresAndClears(t *testing.T) {
	limiter := newAuthFailureLimiter(2, time.Minute)
	now := time.Unix(100, 0)
	limiter.recordFailure("192.0.2.1", now)
	if limiter.blocked("192.0.2.1", now) {
		t.Fatal("client was blocked before reaching the failure limit")
	}
	limiter.recordFailure("192.0.2.1", now)
	if !limiter.blocked("192.0.2.1", now) {
		t.Fatal("client was not blocked after reaching the failure limit")
	}
	if limiter.blocked("192.0.2.1", now.Add(time.Minute)) {
		t.Fatal("expired failure window remained blocked")
	}
	limiter.recordFailure("192.0.2.1", now)
	limiter.clear("192.0.2.1")
	if limiter.blocked("192.0.2.1", now) {
		t.Fatal("successful authentication did not clear failure window")
	}
}

func TestLocalJSONRejectsTrailingValues(t *testing.T) {
	request := httptest.NewRequest(http.MethodPost, "/", bytes.NewBufferString(`{"username":"owner"} {"username":"other"}`))
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	var body struct {
		Username string `json:"username"`
	}
	if decodeLocalJSON(response, request, &body, 1024) {
		t.Fatal("multiple JSON values were accepted")
	}
	if response.Code != http.StatusBadRequest {
		t.Fatalf("trailing JSON status=%d body=%s", response.Code, response.Body.String())
	}
}

func TestLocalSetupLoginAndSessionOriginChecks(t *testing.T) {
	dataRoot := t.TempDir()
	handler := New(config.Config{DataRoot: dataRoot, AuthEnabled: true})

	status := httptest.NewRecorder()
	handler.ServeHTTP(status, httptest.NewRequest(http.MethodGet, "/api/auth/status", nil))
	if status.Code != http.StatusOK || !bytes.Contains(status.Body.Bytes(), []byte(`"state":"setup_required"`)) {
		t.Fatalf("initial auth status=%d body=%s", status.Code, status.Body.String())
	}
	var setup struct {
		ManualCode string `json:"manualCode"`
	}
	setupBytes, err := os.ReadFile(filepath.Join(dataRoot, "runtime", "local-access", "setup.json"))
	if err != nil || json.Unmarshal(setupBytes, &setup) != nil || setup.ManualCode == "" {
		t.Fatalf("physical setup code=%q err=%v", setupBytes, err)
	}

	setupBody := bytes.NewBufferString(fmt.Sprintf(`{"setupCode":%q,"username":"owner","password":"a separate local password"}`, setup.ManualCode))
	setupRequest := httptest.NewRequest(http.MethodPost, "/api/auth/setup", setupBody)
	setupRequest.Host = "pedal.local"
	setupRequest.Header.Set("Content-Type", "application/json")
	setupRequest.Header.Set("Origin", "http://pedal.local")
	setupResponse := httptest.NewRecorder()
	handler.ServeHTTP(setupResponse, setupRequest)
	if setupResponse.Code != http.StatusCreated || len(setupResponse.Result().Cookies()) != 1 {
		t.Fatalf("setup status=%d body=%s", setupResponse.Code, setupResponse.Body.String())
	}
	cookie := setupResponse.Result().Cookies()[0]
	if !cookie.HttpOnly || cookie.SameSite != http.SameSiteStrictMode || cookie.Secure {
		t.Fatalf("local session cookie flags=%+v", cookie)
	}
	if bytes.Contains(setupResponse.Body.Bytes(), []byte("sessionToken")) {
		t.Fatal("browser setup response exposed the local session token")
	}

	loginRequest := httptest.NewRequest(http.MethodPost, "/api/auth/login", bytes.NewBufferString(`{"username":"owner","password":"a separate local password"}`))
	loginRequest.Header.Set("Content-Type", "application/json")
	loginRequest.Header.Set("Origin", "http://localhost:1420")
	loginResponse := httptest.NewRecorder()
	handler.ServeHTTP(loginResponse, loginRequest)
	if loginResponse.Code != http.StatusOK || !bytes.Contains(loginResponse.Body.Bytes(), []byte(`"sessionToken"`)) {
		t.Fatalf("development-origin login status=%d body=%s", loginResponse.Code, loginResponse.Body.String())
	}

	assetsRequest := httptest.NewRequest(http.MethodGet, "/api/assets/models", nil)
	assetsRequest.AddCookie(cookie)
	assetsResponse := httptest.NewRecorder()
	handler.ServeHTTP(assetsResponse, assetsRequest)
	if assetsResponse.Code != http.StatusOK {
		t.Fatalf("session read status=%d body=%s", assetsResponse.Code, assetsResponse.Body.String())
	}

	mutationRequest := httptest.NewRequest(http.MethodPost, "/api/presets/banks/0/slots/0/apply", nil)
	mutationRequest.AddCookie(cookie)
	mutationResponse := httptest.NewRecorder()
	handler.ServeHTTP(mutationResponse, mutationRequest)
	if mutationResponse.Code != http.StatusForbidden {
		t.Fatalf("originless mutation status=%d body=%s", mutationResponse.Code, mutationResponse.Body.String())
	}

	resetRequest := httptest.NewRequest(http.MethodPost, "/api/auth/reset-local-access", nil)
	resetRequest.Host = "pedal.local"
	resetRequest.Header.Set("Origin", "http://pedal.local")
	resetRequest.AddCookie(cookie)
	resetResponse := httptest.NewRecorder()
	handler.ServeHTTP(resetResponse, resetRequest)
	if resetResponse.Code != http.StatusNoContent {
		t.Fatalf("local reset status=%d body=%s", resetResponse.Code, resetResponse.Body.String())
	}
	afterReset := httptest.NewRecorder()
	handler.ServeHTTP(afterReset, httptest.NewRequest(http.MethodGet, "/api/auth/status", nil))
	if !bytes.Contains(afterReset.Body.Bytes(), []byte(`"state":"setup_required"`)) {
		t.Fatalf("auth state after reset=%s", afterReset.Body.String())
	}
}

func TestWiFiSettingsCanBeProvisionedAtRuntime(t *testing.T) {
	dataRoot := t.TempDir()
	handler := New(config.Config{DataRoot: dataRoot, AuthEnabled: false})

	update := httptest.NewRecorder()
	handler.ServeHTTP(update, httptest.NewRequest(
		http.MethodPut,
		"/api/settings/wifi",
		bytes.NewBufferString(`{"ssid":"Stage Network","password":"pedal-secret","country":"HU"}`),
	))
	if update.Code != http.StatusAccepted {
		t.Fatalf("update status=%d body=%s", update.Code, update.Body.String())
	}

	body, err := os.ReadFile(filepath.Join(dataRoot, "wifi", "wpa_supplicant.conf"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(body, []byte(`ssid="Stage Network"`)) || !bytes.Contains(body, []byte(`psk="pedal-secret"`)) {
		t.Fatalf("config=%s", body)
	}

	get := httptest.NewRecorder()
	handler.ServeHTTP(get, httptest.NewRequest(http.MethodGet, "/api/settings/wifi", nil))
	if get.Code != http.StatusOK || !bytes.Contains(get.Body.Bytes(), []byte(`"ssid":"Stage Network"`)) {
		t.Fatalf("get status=%d body=%s", get.Code, get.Body.String())
	}
	if bytes.Contains(get.Body.Bytes(), []byte("pedal-secret")) {
		t.Fatalf("GET exposed password: %s", get.Body.String())
	}
}

func TestDevelopmentOriginCORS(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: false})

	request := httptest.NewRequest(http.MethodGet, "/api/device", nil)
	request.Header.Set("Origin", "http://localhost:1420")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("device status=%d", response.Code)
	}
	if origin := response.Header().Get("Access-Control-Allow-Origin"); origin != "http://localhost:1420" {
		t.Fatalf("allow origin=%q", origin)
	}

	preflight := httptest.NewRequest(http.MethodOptions, "/api/assets/models", nil)
	preflight.Header.Set("Origin", "http://localhost:1420")
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
	handler := New(config.Config{DataRoot: dataRoot, AuthEnabled: false})

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
