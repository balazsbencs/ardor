package server

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"mime"
	"net"
	"net/http"
	"net/url"
	"os"
	"path"
	"strconv"
	"strings"
	"sync"
	"time"

	"ardor.local/managerd/internal/assets"
	"ardor.local/managerd/internal/config"
	"ardor.local/managerd/internal/localauth"
	"ardor.local/managerd/internal/presets"
	resetmanager "ardor.local/managerd/internal/reset"
	"ardor.local/managerd/internal/runtimecontrol"
	"ardor.local/managerd/internal/webui"
	"ardor.local/managerd/internal/wifi"
)

type errorResponse struct {
	Error   string         `json:"error"`
	Message string         `json:"message"`
	Details map[string]any `json:"details,omitempty"`
}

func New(cfg config.Config) http.Handler {
	handler, err := Build(context.Background(), cfg, webui.Files())
	if err != nil {
		panic(err)
	}
	return handler
}

func NewWithWebUI(cfg config.Config, webFiles fs.FS) http.Handler {
	handler, err := Build(context.Background(), cfg, webFiles)
	if err != nil {
		panic(err)
	}
	return handler
}

func Build(ctx context.Context, cfg config.Config, webFiles fs.FS) (http.Handler, error) {
	mux := http.NewServeMux()
	assetStore := assets.NewStore(cfg.DataRoot)
	presetStore := presets.NewStore(cfg.DataRoot)
	wifiStore := wifi.NewStore(cfg.DataRoot, cfg.WiFiInterface, cfg.WiFiControlScript)
	var authStore *localauth.Store
	var resets *resetmanager.Manager
	authFailures := newAuthFailureLimiter(5, time.Minute)
	if cfg.AuthEnabled {
		var err error
		authStore, err = localauth.New(cfg.DataRoot)
		if err != nil {
			return nil, fmt.Errorf("initialize local authentication: %w", err)
		}
		resets, err = resetmanager.New(cfg.DataRoot, authStore)
		if err != nil {
			return nil, fmt.Errorf("recover reset state: %w", err)
		}
		go resets.Run(ctx)
	}

	mux.HandleFunc("GET /api/auth/status", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.AuthEnabled {
			writeJSON(w, http.StatusOK, map[string]any{"state": "disabled", "insecureTransport": true})
			return
		}
		if authStore.SetupRequired() {
			_ = authStore.EnsureSetupCode(time.Now().UTC())
			writeJSON(w, http.StatusOK, map[string]any{"state": "setup_required", "insecureTransport": true})
			return
		}
		if account, ok := authenticateRequest(r, authStore); ok {
			writeJSON(w, http.StatusOK, map[string]any{"state": "authenticated", "account": account, "insecureTransport": true})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"state": "login_required", "insecureTransport": true})
	})

	mux.HandleFunc("POST /api/auth/setup", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.AuthEnabled || !requireLocalOrigin(w, r) {
			return
		}
		client := requestClient(r)
		if authFailures.blocked(client, time.Now()) {
			w.Header().Set("Retry-After", "60")
			writeError(w, http.StatusTooManyRequests, "too_many_attempts", "Too many failed local authentication attempts; try again in one minute")
			return
		}
		var body struct {
			SetupCode string `json:"setupCode"`
			Username  string `json:"username"`
			Password  string `json:"password"`
		}
		if !decodeLocalJSON(w, r, &body, 8<<10) {
			return
		}
		account, token, err := authStore.Setup(body.SetupCode, body.Username, body.Password, time.Now().UTC())
		if err != nil {
			authFailures.recordFailure(client, time.Now())
			status, code := http.StatusBadRequest, "local_setup_failed"
			if errors.Is(err, localauth.ErrAlreadySetup) {
				status, code = http.StatusConflict, "already_setup"
			}
			writeError(w, status, code, err.Error())
			return
		}
		authFailures.clear(client)
		setLocalSessionCookie(w, token)
		writeLocalAuthResult(w, r, http.StatusCreated, account, token)
	})

	mux.HandleFunc("POST /api/auth/login", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.AuthEnabled || !requireLocalOrigin(w, r) {
			return
		}
		client := requestClient(r)
		if authFailures.blocked(client, time.Now()) {
			w.Header().Set("Retry-After", "60")
			writeError(w, http.StatusTooManyRequests, "too_many_attempts", "Too many failed local authentication attempts; try again in one minute")
			return
		}
		var body struct {
			Username string `json:"username"`
			Password string `json:"password"`
		}
		if !decodeLocalJSON(w, r, &body, 8<<10) {
			return
		}
		account, token, err := authStore.Login(body.Username, body.Password, time.Now().UTC())
		if err != nil {
			authFailures.recordFailure(client, time.Now())
			writeError(w, http.StatusUnauthorized, "invalid_credentials", "Username or password is incorrect")
			return
		}
		authFailures.clear(client)
		setLocalSessionCookie(w, token)
		writeLocalAuthResult(w, r, http.StatusOK, account, token)
	})

	mux.HandleFunc("POST /api/auth/logout", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.AuthEnabled || !requireLocalOrigin(w, r) {
			return
		}
		if token, ok := localSessionToken(r); ok {
			authStore.Logout(token)
		}
		clearLocalSessionCookie(w)
		w.WriteHeader(http.StatusNoContent)
	})

	mux.HandleFunc("POST /api/auth/reset-local-access", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.AuthEnabled || !requireLocalOrigin(w, r) || !authorized(w, r, cfg, authStore) {
			return
		}
		if err := resets.ResetLocalAccess(time.Now().UTC()); err != nil {
			writeError(w, http.StatusConflict, "reset_failed", err.Error())
			return
		}
		clearLocalSessionCookie(w)
		w.WriteHeader(http.StatusNoContent)
	})

	mux.HandleFunc("POST /api/reset/factory", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.AuthEnabled || !requireLocalOrigin(w, r) || !authorized(w, r, cfg, authStore) {
			return
		}
		status, err := resets.BeginFactoryReset(time.Now().UTC())
		if err != nil && !errors.Is(err, resetmanager.ErrResetPending) {
			writeError(w, http.StatusInternalServerError, "factory_reset_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusAccepted, status)
	})

	mux.HandleFunc("GET /api/reset/factory/{resetId}", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.AuthEnabled || !authorized(w, r, cfg, authStore) {
			return
		}
		status, ok := resets.Status(r.PathValue("resetId"), time.Now().UTC())
		if !ok {
			writeError(w, http.StatusNotFound, "reset_not_found", "Factory reset was not found")
			return
		}
		writeJSON(w, http.StatusOK, status)
	})

	mux.HandleFunc("GET /api/device", func(w http.ResponseWriter, r *http.Request) {
		authState := "disabled"
		if cfg.AuthEnabled {
			authState = "login_required"
			if authStore.SetupRequired() {
				authState = "setup_required"
			} else if _, ok := authenticateRequest(r, authStore); ok {
				authState = "authenticated"
			}
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"deviceName":             "Ardor Pedal",
			"apiVersion":             "0.1.0",
			"authEnabled":            cfg.AuthEnabled,
			"localAuthState":         authState,
			"dataRootWritable":       true,
			"maxBanks":               100,
			"slotsPerBank":           4,
			"supportedPresetVersion": 2,
			"capabilities": map[string]bool{
				"modelUpload": true, "irUpload": true, "presetRead": true,
				"presetWrite": true, "presetApply": true, "assetRename": true,
				"wifiSettings": true,
			},
		})
	})

	mux.HandleFunc("GET /api/settings/wifi", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		settings, err := wifiStore.Get()
		if err != nil {
			writeError(w, http.StatusInternalServerError, "wifi_settings_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, settings)
	})

	mux.HandleFunc("PUT /api/settings/wifi", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		var update wifi.Update
		decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 8<<10))
		decoder.DisallowUnknownFields()
		if err := decoder.Decode(&update); err != nil {
			writeError(w, http.StatusBadRequest, "invalid_wifi_settings", err.Error())
			return
		}
		settings, err := wifiStore.Save(update)
		if err != nil {
			writeError(w, http.StatusBadRequest, "invalid_wifi_settings", err.Error())
			return
		}
		writeJSON(w, http.StatusAccepted, settings)
		go func() {
			time.Sleep(750 * time.Millisecond)
			if err := wifiStore.Restart(); err != nil {
				log.Printf("restart Wi-Fi after settings update: %v", err)
			}
		}()
	})

	mux.HandleFunc("GET /api/assets/{kind}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		items, err := assetStore.List(kind)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "asset_list_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"assets": items})
	})

	mux.HandleFunc("POST /api/assets/{kind}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		file, header, err := r.FormFile("file")
		if err != nil {
			writeError(w, http.StatusBadRequest, "missing_file", err.Error())
			return
		}
		defer file.Close()
		overwrite := r.FormValue("overwrite") == "true"
		info, err := assetStore.Save(kind, header.Filename, file, overwrite)
		if err != nil {
			status := http.StatusBadRequest
			code := "asset_upload_failed"
			if errors.Is(err, assets.ErrExists) {
				status = http.StatusConflict
				code = "asset_exists"
			}
			writeError(w, status, code, err.Error())
			return
		}
		if err := runtimecontrol.QueueAssetReload(cfg.DataRoot); err != nil {
			// The asset is already durable and listed by the API. Keep the upload
			// successful, but make a missed runtime refresh diagnosable on-device.
			log.Printf("queue asset reload: %v", err)
		}
		writeJSON(w, http.StatusCreated, info)
	})

	mux.HandleFunc("DELETE /api/assets/{kind}/{assetId}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		if err := assetStore.Delete(kind, r.PathValue("assetId")); err != nil {
			writeError(w, http.StatusNotFound, "asset_delete_failed", err.Error())
			return
		}
		w.WriteHeader(http.StatusNoContent)
	})

	mux.HandleFunc("PATCH /api/assets/{kind}/{assetId}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		var body struct {
			Filename string `json:"filename"`
		}
		if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 8<<10)).Decode(&body); err != nil {
			writeError(w, http.StatusBadRequest, "invalid_rename", "request body must contain a filename")
			return
		}
		oldID := r.PathValue("assetId")
		oldPath := "models/" + oldID
		if kind == assets.KindIR {
			oldPath = "irs/" + oldID
		}
		info, err := assetStore.Rename(kind, oldID, body.Filename)
		if err != nil {
			status := http.StatusBadRequest
			code := "asset_rename_failed"
			if errors.Is(err, assets.ErrExists) {
				status, code = http.StatusConflict, "asset_exists"
			} else if errors.Is(err, os.ErrNotExist) {
				status, code = http.StatusNotFound, "asset_not_found"
			}
			writeError(w, status, code, err.Error())
			return
		}
		updated, err := presetStore.ReplaceAssetReferences(oldPath, info.Path)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "asset_reference_update_failed", err.Error())
			return
		}
		if err := runtimecontrol.QueueAssetReload(cfg.DataRoot); err != nil {
			log.Printf("queue asset reload: %v", err)
		}
		writeJSON(w, http.StatusOK, map[string]any{"asset": info, "updatedPresetCount": updated})
	})

	mux.HandleFunc("GET /api/presets", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		items, err := presetStore.List()
		if err != nil {
			writeError(w, http.StatusInternalServerError, "preset_list_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"presets": items})
	})

	mux.HandleFunc("GET /api/presets/banks/{bank}/slots/{slot}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		bank, slot, ok := parseSlot(w, r)
		if !ok {
			return
		}
		presetSlot, err := presetStore.Load(bank, slot)
		if err != nil {
			writeError(w, http.StatusNotFound, "preset_not_found", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, presetSlot)
	})

	mux.HandleFunc("PUT /api/presets/banks/{bank}/slots/{slot}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		bank, slot, ok := parseSlot(w, r)
		if !ok {
			return
		}
		var preset presets.Preset
		decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<20))
		if err := decoder.Decode(&preset); err != nil {
			writeError(w, http.StatusBadRequest, "invalid_json", err.Error())
			return
		}
		presetSlot, err := presetStore.Save(bank, slot, preset)
		if err != nil {
			writeError(w, http.StatusBadRequest, "preset_save_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, presetSlot)
	})

	mux.HandleFunc("POST /api/presets/banks/{bank}/slots/{slot}/apply", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg, authStore) {
			return
		}
		bank, slot, ok := parseSlot(w, r)
		if !ok {
			return
		}
		if _, err := presetStore.Load(bank, slot); err != nil {
			writeError(w, http.StatusNotFound, "preset_not_found", err.Error())
			return
		}
		if err := runtimecontrol.QueueApplyPreset(cfg.DataRoot, bank, slot); err != nil {
			writeError(w, http.StatusServiceUnavailable, "runtime_command_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusAccepted, map[string]any{
			"accepted": true, "bank": bank, "slot": slot,
			"message": "apply request queued",
		})
	})

	mux.Handle("GET /", webUIHandler(webFiles))

	return withCORS(mux), nil
}

func webUIHandler(webFiles fs.FS) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/api" || strings.HasPrefix(r.URL.Path, "/api/") {
			http.NotFound(w, r)
			return
		}

		setWebSecurityHeaders(w)
		assetPath := strings.TrimPrefix(path.Clean(r.URL.Path), "/")
		if assetPath == "." || assetPath == "" {
			serveWebFile(w, r, webFiles, "index.html", false)
			return
		}
		if _, err := fs.Stat(webFiles, assetPath); err == nil {
			serveWebFile(w, r, webFiles, assetPath, strings.HasPrefix(assetPath, "assets/"))
			return
		}
		if path.Ext(assetPath) != "" {
			http.NotFound(w, r)
			return
		}
		serveWebFile(w, r, webFiles, "index.html", false)
	})
}

func serveWebFile(w http.ResponseWriter, r *http.Request, webFiles fs.FS, name string, immutable bool) {
	contents, err := fs.ReadFile(webFiles, name)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	contentType := mime.TypeByExtension(path.Ext(name))
	if contentType != "" {
		w.Header().Set("Content-Type", contentType)
	}
	if immutable {
		w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	} else {
		w.Header().Set("Cache-Control", "no-cache")
	}
	http.ServeContent(w, r, name, time.Time{}, bytes.NewReader(contents))
}

func setWebSecurityHeaders(w http.ResponseWriter) {
	w.Header().Set("Content-Security-Policy", "default-src 'self'; connect-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'")
	w.Header().Set("Referrer-Policy", "no-referrer")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("X-Frame-Options", "DENY")
}

var allowedOrigins = map[string]struct{}{
	"tauri://localhost":       {},
	"https://tauri.localhost": {},
	"http://tauri.localhost":  {},
	"http://localhost:1420":   {},
	"http://127.0.0.1:1420":   {},
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		origin := r.Header.Get("Origin")
		_, allowed := allowedOrigins[origin]
		if allowed {
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Access-Control-Allow-Credentials", "true")
			w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS")
			w.Header().Set("Access-Control-Allow-Headers", "Authorization, Content-Type")
			w.Header().Set("Access-Control-Max-Age", "600")
			w.Header().Add("Vary", "Origin")
		}
		if r.Method == http.MethodOptions && allowed {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func authorized(w http.ResponseWriter, r *http.Request, cfg config.Config, authStore *localauth.Store) bool {
	if !cfg.AuthEnabled {
		return true
	}
	if r.Method != http.MethodGet && r.Method != http.MethodHead && !requireLocalOrigin(w, r) {
		return false
	}
	if _, ok := authenticateRequest(r, authStore); ok {
		return true
	}
	w.Header().Set("WWW-Authenticate", `Bearer realm="ardor-local-session"`)
	writeError(w, http.StatusUnauthorized, "unauthorized", "Local sign-in is required")
	return false
}

func authenticateRequest(r *http.Request, authStore *localauth.Store) (localauth.Account, bool) {
	token, ok := localSessionToken(r)
	if !ok || authStore == nil {
		return localauth.Account{}, false
	}
	return authStore.Authenticate(token, time.Now().UTC())
}

func localSessionToken(r *http.Request) (string, bool) {
	if authorization := r.Header.Get("Authorization"); strings.HasPrefix(authorization, "Bearer ") && !strings.ContainsAny(authorization[7:], " \t\r\n") && authorization[7:] != "" {
		return authorization[7:], true
	}
	cookie, err := r.Cookie("ardor_local_session")
	return cookieValue(cookie, err)
}

func cookieValue(cookie *http.Cookie, err error) (string, bool) {
	if cookie == nil || err != nil || cookie.Value == "" {
		return "", false
	}
	return cookie.Value, true
}

func setLocalSessionCookie(w http.ResponseWriter, token string) {
	http.SetCookie(w, &http.Cookie{
		Name: "ardor_local_session", Value: token, Path: "/", MaxAge: 24 * 60 * 60,
		HttpOnly: true, SameSite: http.SameSiteStrictMode,
	})
}

func clearLocalSessionCookie(w http.ResponseWriter) {
	http.SetCookie(w, &http.Cookie{
		Name: "ardor_local_session", Value: "", Path: "/", MaxAge: -1,
		HttpOnly: true, SameSite: http.SameSiteStrictMode,
	})
}

func writeLocalAuthResult(w http.ResponseWriter, r *http.Request, status int, account localauth.Account, token string) {
	result := map[string]any{"account": account}
	if _, allowedTauriOrigin := allowedOrigins[r.Header.Get("Origin")]; allowedTauriOrigin {
		result["sessionToken"] = token
	}
	writeJSON(w, status, result)
}

func requireLocalOrigin(w http.ResponseWriter, r *http.Request) bool {
	origin := r.Header.Get("Origin")
	if _, allowed := allowedOrigins[origin]; allowed {
		return true
	}
	parsed, err := url.Parse(origin)
	if err != nil || parsed.Scheme != "http" || parsed.Host != r.Host || parsed.User != nil {
		writeError(w, http.StatusForbidden, "invalid_origin", "Request origin is not allowed")
		return false
	}
	hostname := parsed.Hostname()
	if hostname != "localhost" && !strings.HasSuffix(hostname, ".local") && net.ParseIP(hostname) == nil {
		writeError(w, http.StatusForbidden, "invalid_host", "Request host is not a local device address")
		return false
	}
	return true
}

func decodeLocalJSON(w http.ResponseWriter, r *http.Request, value any, limit int64) bool {
	if mediaType, _, err := mime.ParseMediaType(r.Header.Get("Content-Type")); err != nil || mediaType != "application/json" {
		writeError(w, http.StatusUnsupportedMediaType, "json_required", "Content-Type must be application/json")
		return false
	}
	decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, limit))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		writeError(w, http.StatusBadRequest, "invalid_json", "Request body is invalid")
		return false
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		writeError(w, http.StatusBadRequest, "invalid_json", "Request body must contain exactly one JSON value")
		return false
	}
	return true
}

type authFailureWindow struct {
	count     int
	expiresAt time.Time
}

type authFailureLimiter struct {
	mu       sync.Mutex
	maximum  int
	duration time.Duration
	windows  map[string]authFailureWindow
}

func newAuthFailureLimiter(maximum int, duration time.Duration) *authFailureLimiter {
	return &authFailureLimiter{maximum: maximum, duration: duration, windows: make(map[string]authFailureWindow)}
}

func (limiter *authFailureLimiter) blocked(client string, now time.Time) bool {
	limiter.mu.Lock()
	defer limiter.mu.Unlock()
	window, ok := limiter.windows[client]
	if !ok || !window.expiresAt.After(now) {
		delete(limiter.windows, client)
		return false
	}
	return window.count >= limiter.maximum
}

func (limiter *authFailureLimiter) recordFailure(client string, now time.Time) {
	limiter.mu.Lock()
	defer limiter.mu.Unlock()
	window := limiter.windows[client]
	if !window.expiresAt.After(now) {
		window = authFailureWindow{expiresAt: now.Add(limiter.duration)}
	}
	window.count++
	limiter.windows[client] = window
}

func (limiter *authFailureLimiter) clear(client string) {
	limiter.mu.Lock()
	defer limiter.mu.Unlock()
	delete(limiter.windows, client)
}

func requestClient(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err == nil && host != "" {
		return host
	}
	return r.RemoteAddr
}

func assetKindFromPath(value string) (assets.Kind, bool) {
	switch value {
	case "models":
		return assets.KindModel, true
	case "irs":
		return assets.KindIR, true
	default:
		return "", false
	}
}

func parseSlot(w http.ResponseWriter, r *http.Request) (int, int, bool) {
	bank, err := strconv.Atoi(r.PathValue("bank"))
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid_bank", err.Error())
		return 0, 0, false
	}
	slot, err := strconv.Atoi(r.PathValue("slot"))
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid_slot", err.Error())
		return 0, 0, false
	}
	if bank < 0 || bank > 99 || slot < 0 || slot > 3 {
		writeError(w, http.StatusBadRequest, "slot_out_of_range", "preset slot out of range")
		return 0, 0, false
	}
	return bank, slot, true
}

func writeJSON(w http.ResponseWriter, status int, body any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}

func writeError(w http.ResponseWriter, status int, code string, message string) {
	writeJSON(w, status, errorResponse{Error: code, Message: message})
}

func ListenAddress(cfg config.Config) string {
	return fmt.Sprintf("%s:%d", strings.TrimSpace(cfg.Bind), cfg.Port)
}
