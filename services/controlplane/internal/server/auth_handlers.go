package server

import (
	"errors"
	"net/http"
	"strings"
	"time"

	"ardor.local/controlplane/internal/auth"
	"ardor.local/controlplane/internal/securevalue"
	"ardor.local/controlplane/internal/store"
)

type credentialsRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

func (server *Server) register(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	var body credentialsRequest
	if !decodeJSON(writer, request, &body) {
		return
	}
	normalized, err := auth.NormalizeUsername(body.Username)
	if err != nil {
		writeError(writer, http.StatusBadRequest, "invalid_username", err.Error())
		return
	}
	passwordHash, err := auth.HashPassword(body.Password)
	if err != nil {
		writeError(writer, http.StatusBadRequest, "invalid_password", err.Error())
		return
	}
	recoveryCodes, recoveryHashes, err := auth.NewRecoveryCodes(10)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	accountID, err := securevalue.UUID()
	if err != nil {
		serverError(server, writer, err)
		return
	}
	now := time.Now().UTC()
	account := store.Account{ID: accountID, UsernameNormalized: normalized, UsernameDisplay: strings.TrimSpace(body.Username), PasswordHash: passwordHash, State: "active", CreatedAt: now, UpdatedAt: now}
	if err := server.repository.CreateAccount(request.Context(), account, recoveryHashes); errors.Is(err, store.ErrConflict) {
		writeError(writer, http.StatusConflict, "username_unavailable", "Username is unavailable")
		return
	} else if err != nil {
		serverError(server, writer, err)
		return
	}
	token, expiresAt, err := server.createSession(request, account.ID)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	server.setSessionCookie(writer, token, expiresAt)
	writeJSON(writer, http.StatusCreated, map[string]any{
		"account":       map[string]any{"id": account.ID, "username": account.UsernameDisplay},
		"recoveryCodes": recoveryCodes,
	})
}

func (server *Server) login(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	var body credentialsRequest
	if !decodeJSON(writer, request, &body) {
		return
	}
	now := time.Now().UTC()
	limitKeys := rateLimitKeys(request, body.Username)
	if !server.limiter.allow(limitKeys, now) {
		writeError(writer, http.StatusTooManyRequests, "rate_limited", "Try again later")
		return
	}
	normalized, normalizeErr := auth.NormalizeUsername(body.Username)
	account, lookupErr := server.repository.AccountByUsername(request.Context(), normalized)
	encoded := server.dummyHash
	if lookupErr == nil {
		encoded = account.PasswordHash
	}
	passwordValid := auth.VerifyPassword(encoded, body.Password)
	if normalizeErr != nil || lookupErr != nil || !passwordValid || account.State != "active" {
		server.limiter.failure(limitKeys, now)
		writeError(writer, http.StatusUnauthorized, "invalid_credentials", "Username or password is incorrect")
		return
	}
	server.limiter.success(limitKeys)
	token, expiresAt, err := server.createSession(request, account.ID)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	_ = server.repository.AppendAudit(request.Context(), store.AuditEvent{ActorType: "account", ActorID: account.ID, EventType: "session.created", SubjectType: "account", SubjectID: account.ID, CreatedAt: time.Now().UTC()})
	server.setSessionCookie(writer, token, expiresAt)
	writeJSON(writer, http.StatusOK, map[string]any{"account": map[string]any{"id": account.ID, "username": account.UsernameDisplay}})
}

func (server *Server) logout(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	if cookie, err := request.Cookie(server.cookieName()); err == nil {
		_ = server.repository.RevokeSession(request.Context(), auth.HashCredential(cookie.Value), time.Now().UTC())
	}
	server.clearSessionCookie(writer)
	writer.WriteHeader(http.StatusNoContent)
}

func (server *Server) logoutAll(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	account := accountFromContext(request.Context())
	if err := server.repository.RevokeAllSessions(request.Context(), account.ID, time.Now().UTC()); err != nil {
		serverError(server, writer, err)
		return
	}
	server.clearSessionCookie(writer)
	writer.WriteHeader(http.StatusNoContent)
}

func (server *Server) recoverAccount(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	var body struct {
		Username     string `json:"username"`
		RecoveryCode string `json:"recoveryCode"`
		NewPassword  string `json:"newPassword"`
	}
	if !decodeJSON(writer, request, &body) {
		return
	}
	now := time.Now().UTC()
	limitKeys := rateLimitKeys(request, body.Username)
	if !server.limiter.allow(limitKeys, now) {
		writeError(writer, http.StatusTooManyRequests, "rate_limited", "Try again later")
		return
	}
	normalized, err := auth.NormalizeUsername(body.Username)
	if err != nil {
		server.limiter.failure(limitKeys, now)
		writeError(writer, http.StatusUnauthorized, "invalid_recovery", "Recovery credentials are invalid")
		return
	}
	passwordHash, err := auth.HashPassword(body.NewPassword)
	if err != nil {
		writeError(writer, http.StatusBadRequest, "invalid_password", err.Error())
		return
	}
	account, err := server.repository.RecoverAccount(request.Context(), normalized, auth.HashCredential(strings.ToUpper(strings.TrimSpace(body.RecoveryCode))), passwordHash, now)
	if err != nil {
		server.limiter.failure(limitKeys, now)
		writeError(writer, http.StatusUnauthorized, "invalid_recovery", "Recovery credentials are invalid")
		return
	}
	server.limiter.success(limitKeys)
	token, expiresAt, err := server.createSession(request, account.ID)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	server.setSessionCookie(writer, token, expiresAt)
	writeJSON(writer, http.StatusOK, map[string]any{"account": map[string]any{"id": account.ID, "username": account.UsernameDisplay}})
}

func (server *Server) me(writer http.ResponseWriter, request *http.Request) {
	account := accountFromContext(request.Context())
	writeJSON(writer, http.StatusOK, map[string]any{"id": account.ID, "username": account.UsernameDisplay})
}

func (server *Server) createSession(request *http.Request, accountID string) (string, time.Time, error) {
	token, hash, err := auth.NewSessionToken()
	if err != nil {
		return "", time.Time{}, err
	}
	id, err := securevalue.UUID()
	if err != nil {
		return "", time.Time{}, err
	}
	now := time.Now().UTC()
	expiresAt := now.Add(sessionTTL)
	err = server.repository.CreateSession(request.Context(), store.Session{ID: id, AccountID: accountID, TokenHash: hash, CreatedAt: now, ExpiresAt: expiresAt})
	return token, expiresAt, err
}
