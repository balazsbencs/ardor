package server

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"mime"
	"net/http"
	"net/url"
	"strings"
	"time"

	"ardor.local/controlplane/internal/auth"
	"ardor.local/controlplane/internal/store"
)

const (
	maxJSONBody = 64 * 1024
	sessionTTL  = 30 * 24 * time.Hour
)

type Config struct {
	PublicOrigin  string
	SecureCookies bool
	Logger        *log.Logger
}

type Server struct {
	config     Config
	repository store.Repository
	hub        *deviceHub
	mux        *http.ServeMux
	dummyHash  string
	limiter    *attemptLimiter
}

type contextKey string

const accountContextKey contextKey = "account"

func New(config Config, repository store.Repository) (*Server, error) {
	if repository == nil {
		return nil, errors.New("control plane requires a repository")
	}
	origin, err := url.Parse(config.PublicOrigin)
	if err != nil || origin.Host == "" || (origin.Scheme != "https" && !(origin.Scheme == "http" && !config.SecureCookies)) || origin.Path != "" {
		return nil, errors.New("public origin must be an HTTPS origin (HTTP is allowed only for insecure development)")
	}
	if config.Logger == nil {
		config.Logger = log.Default()
	}
	dummyHash, err := auth.HashPassword("timing-only dummy password")
	if err != nil {
		return nil, err
	}
	server := &Server{config: config, repository: repository, hub: newDeviceHub(), mux: http.NewServeMux(), dummyHash: dummyHash, limiter: newAttemptLimiter()}
	server.routes()
	return server, nil
}

func (server *Server) Close() {
	server.hub.closeAll()
}

func (server *Server) Handler() http.Handler {
	return http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		writer.Header().Set("X-Content-Type-Options", "nosniff")
		writer.Header().Set("Referrer-Policy", "no-referrer")
		writer.Header().Set("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'")
		writer.Header().Set("Cache-Control", "no-store")
		server.mux.ServeHTTP(writer, request)
	})
}

func (server *Server) routes() {
	server.mux.HandleFunc("POST /v1/auth/register", server.register)
	server.mux.HandleFunc("POST /v1/auth/login", server.login)
	server.mux.HandleFunc("POST /v1/auth/logout", server.logout)
	server.mux.Handle("POST /v1/auth/logout-all", server.requireAccount(http.HandlerFunc(server.logoutAll)))
	server.mux.HandleFunc("POST /v1/auth/recover", server.recoverAccount)
	server.mux.Handle("GET /v1/auth/me", server.requireAccount(http.HandlerFunc(server.me)))

	server.mux.Handle("GET /v1/devices", server.requireAccount(http.HandlerFunc(server.listDevices)))
	server.mux.Handle("DELETE /v1/devices/{deviceId}/membership", server.requireAccount(http.HandlerFunc(server.unclaimDevice)))
	server.mux.Handle("POST /v1/device-claims", server.requireAccount(http.HandlerFunc(server.beginClaim)))
	server.mux.Handle("GET /v1/device-claims/{claimId}", server.requireAccount(http.HandlerFunc(server.getClaim)))

	server.mux.HandleFunc("POST /v1/device/connection-challenge", server.deviceChallenge)
	server.mux.HandleFunc("POST /v1/device/connection-token", server.deviceToken)
	server.mux.HandleFunc("GET /v1/device/connect", server.deviceConnect)
	server.mux.HandleFunc("GET /healthz", func(writer http.ResponseWriter, _ *http.Request) {
		writeJSON(writer, http.StatusOK, map[string]any{"status": "ok"})
	})
}

func (server *Server) requireAccount(next http.Handler) http.Handler {
	return http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		cookie, err := request.Cookie(server.cookieName())
		if err != nil {
			writeError(writer, http.StatusUnauthorized, "authentication_required", "Sign in is required")
			return
		}
		account, err := server.repository.AccountBySession(request.Context(), auth.HashCredential(cookie.Value), time.Now().UTC())
		if err != nil {
			server.clearSessionCookie(writer)
			writeError(writer, http.StatusUnauthorized, "authentication_required", "Sign in is required")
			return
		}
		next.ServeHTTP(writer, request.WithContext(context.WithValue(request.Context(), accountContextKey, account)))
	})
}

func accountFromContext(ctx context.Context) store.Account {
	return ctx.Value(accountContextKey).(store.Account)
}

func (server *Server) requireBrowserOrigin(writer http.ResponseWriter, request *http.Request) bool {
	if request.Header.Get("Origin") != server.config.PublicOrigin {
		writeError(writer, http.StatusForbidden, "invalid_origin", "Request origin is not allowed")
		return false
	}
	return true
}

func (server *Server) cookieName() string {
	if server.config.SecureCookies {
		return "__Host-ardor_session"
	}
	return "ardor_session"
}

func (server *Server) setSessionCookie(writer http.ResponseWriter, token string, expiresAt time.Time) {
	http.SetCookie(writer, &http.Cookie{
		Name: server.cookieName(), Value: token, Path: "/", Expires: expiresAt,
		MaxAge: int(time.Until(expiresAt).Seconds()), HttpOnly: true, Secure: server.config.SecureCookies, SameSite: http.SameSiteStrictMode,
	})
}

func (server *Server) clearSessionCookie(writer http.ResponseWriter) {
	http.SetCookie(writer, &http.Cookie{
		Name: server.cookieName(), Value: "", Path: "/", MaxAge: -1,
		HttpOnly: true, Secure: server.config.SecureCookies, SameSite: http.SameSiteStrictMode,
	})
}

func decodeJSON(writer http.ResponseWriter, request *http.Request, value any) bool {
	mediaType, _, err := mime.ParseMediaType(request.Header.Get("Content-Type"))
	if err != nil || mediaType != "application/json" {
		writeError(writer, http.StatusUnsupportedMediaType, "json_required", "Content-Type must be application/json")
		return false
	}
	request.Body = http.MaxBytesReader(writer, request.Body, maxJSONBody)
	decoder := json.NewDecoder(request.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		writeError(writer, http.StatusBadRequest, "invalid_json", "Request body is invalid")
		return false
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		writeError(writer, http.StatusBadRequest, "invalid_json", "Request must contain one JSON value")
		return false
	}
	return true
}

func decodeStrictPayload(payload json.RawMessage, value any) error {
	decoder := json.NewDecoder(bytes.NewReader(payload))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("payload has multiple JSON values")
	}
	return nil
}

func writeJSON(writer http.ResponseWriter, status int, value any) {
	writer.Header().Set("Content-Type", "application/json")
	writer.WriteHeader(status)
	_ = json.NewEncoder(writer).Encode(value)
}

func writeError(writer http.ResponseWriter, status int, code, message string) {
	writeJSON(writer, status, map[string]any{"error": code, "message": message})
}

func bearerToken(request *http.Request) (string, bool) {
	value := request.Header.Get("Authorization")
	if !strings.HasPrefix(value, "Bearer ") || strings.ContainsAny(value[7:], " \t\r\n") || value[7:] == "" {
		return "", false
	}
	return value[7:], true
}

func serverError(server *Server, writer http.ResponseWriter, err error) {
	requestID := fmt.Sprintf("%d", time.Now().UnixNano())
	server.config.Logger.Printf("control plane request failed id=%s: %v", requestID, err)
	writeJSON(writer, http.StatusInternalServerError, map[string]any{"error": "internal_error", "message": "Request could not be completed", "requestId": requestID})
}
