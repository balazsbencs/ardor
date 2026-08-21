package server

import (
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode"

	"ardor.local/cloudprotocol"
	"ardor.local/controlplane/internal/securevalue"
)

const (
	tone3000FlowTTL       = 15 * time.Minute
	tone3000ResponseLimit = 1 << 20
)

type tone3000Integration struct {
	clientID    string
	apiURL      *url.URL
	callbackURL string
	client      *http.Client
	mu          sync.Mutex
	flows       map[string]*tone3000Flow
}

type tone3000Flow struct {
	id           string
	accountID    string
	deviceID     string
	architecture string
	state        string
	verifier     string
	status       string
	errorMessage string
	accessToken  string
	tokenExpiry  time.Time
	tone         tone3000Tone
	models       []tone3000Model
	expiresAt    time.Time
}

type tone3000Tone struct {
	ID          int64        `json:"id"`
	Title       string       `json:"title"`
	Description *string      `json:"description"`
	Gear        string       `json:"gear"`
	Images      []string     `json:"images"`
	Format      string       `json:"format"`
	License     string       `json:"license"`
	User        tone3000User `json:"user"`
	URL         string       `json:"url"`
}

type tone3000User struct {
	ID        int64   `json:"id"`
	Username  string  `json:"username"`
	AvatarURL *string `json:"avatar_url"`
	URL       string  `json:"url"`
}

type tone3000Model struct {
	ID                  int64   `json:"id"`
	ModelURL            string  `json:"model_url"`
	Name                string  `json:"name"`
	Size                string  `json:"size"`
	ToneID              int64   `json:"tone_id"`
	ArchitectureVersion *string `json:"architecture_version"`
}

type tone3000TokenResponse struct {
	AccessToken string `json:"access_token"`
	ExpiresIn   int64  `json:"expires_in"`
}

func newTone3000Integration(config Config) (*tone3000Integration, error) {
	if strings.TrimSpace(config.Tone3000ClientID) == "" {
		return nil, nil
	}
	base := strings.TrimRight(strings.TrimSpace(config.Tone3000BaseURL), "/")
	if base == "" {
		base = "https://www.tone3000.com"
	}
	if !strings.HasSuffix(base, "/api/v1") {
		base += "/api/v1"
	}
	apiURL, err := url.Parse(base)
	if err != nil || apiURL.Host == "" || (apiURL.Scheme != "https" && config.SecureCookies) {
		return nil, errors.New("TONE3000_BASE_URL must be an HTTPS URL")
	}
	client := config.Tone3000Client
	if client == nil {
		client = &http.Client{Timeout: 30 * time.Second}
	}
	copyClient := *client
	copyClient.CheckRedirect = func(_ *http.Request, _ []*http.Request) error { return http.ErrUseLastResponse }
	return &tone3000Integration{
		clientID: strings.TrimSpace(config.Tone3000ClientID), apiURL: apiURL,
		callbackURL: config.PublicOrigin + "/v1/integrations/tone3000/callback",
		client:      &copyClient, flows: map[string]*tone3000Flow{},
	}, nil
}

func (server *Server) startTone3000Selection(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	if server.tone3000 == nil {
		writeError(writer, http.StatusNotImplemented, "tone3000_not_configured", "TONE3000 is not configured on this server")
		return
	}
	var body struct {
		DeviceID     string `json:"deviceId"`
		Architecture string `json:"architecture"`
	}
	if !decodeJSON(writer, request, &body) {
		return
	}
	if body.Architecture != "legacy" && body.Architecture != "2" {
		writeError(writer, http.StatusBadRequest, "invalid_architecture", "Architecture must be legacy or 2")
		return
	}
	account := accountFromContext(request.Context())
	owner, err := server.repository.DeviceOwner(request.Context(), body.DeviceID)
	if err != nil || owner != account.ID {
		writeError(writer, http.StatusNotFound, "device_not_found", "Device was not found")
		return
	}
	flowID, err := securevalue.UUID()
	if err != nil {
		serverError(server, writer, err)
		return
	}
	state, err := securevalue.Token(24)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	verifier, err := securevalue.Token(32)
	if err != nil {
		serverError(server, writer, err)
		return
	}
	challenge := sha256.Sum256([]byte(verifier))
	now := time.Now().UTC()
	flow := &tone3000Flow{
		id: flowID, accountID: account.ID, deviceID: body.DeviceID, architecture: body.Architecture,
		state: state, verifier: verifier, status: "pending", expiresAt: now.Add(tone3000FlowTTL),
	}
	server.tone3000.mu.Lock()
	server.tone3000.removeExpiredLocked(now)
	server.tone3000.flows[flowID] = flow
	server.tone3000.mu.Unlock()
	time.AfterFunc(tone3000FlowTTL, func() { server.tone3000.expire(flowID, time.Now().UTC()) })
	authorize := server.tone3000.apiURL.ResolveReference(&url.URL{Path: strings.TrimRight(server.tone3000.apiURL.Path, "/") + "/oauth/authorize"})
	query := authorize.Query()
	query.Set("client_id", server.tone3000.clientID)
	query.Set("redirect_uri", server.tone3000.callbackURL)
	query.Set("response_type", "code")
	query.Set("code_challenge", base64.RawURLEncoding.EncodeToString(challenge[:]))
	query.Set("code_challenge_method", "S256")
	query.Set("state", state)
	query.Set("prompt", "select_tone")
	query.Set("format", "nam")
	query.Set("menubar", "true")
	if body.Architecture == "2" {
		query.Set("architecture", "2")
	}
	authorize.RawQuery = query.Encode()
	writeJSON(writer, http.StatusCreated, map[string]any{"flowId": flowID, "authorizeUrl": authorize.String(), "expiresAt": flow.expiresAt})
}

func (server *Server) completeTone3000Selection(writer http.ResponseWriter, request *http.Request) {
	if server.tone3000 == nil {
		writeTone3000Callback(writer, false, "TONE3000 is not configured.")
		return
	}
	state := request.URL.Query().Get("state")
	server.tone3000.mu.Lock()
	flow := server.tone3000.flowByStateLocked(state, time.Now().UTC())
	if flow != nil && flow.status == "pending" {
		flow.status = "loading"
	}
	server.tone3000.mu.Unlock()
	if flow == nil {
		writeTone3000Callback(writer, false, "This selection is invalid or expired.")
		return
	}
	if request.URL.Query().Get("canceled") == "true" || request.URL.Query().Get("error") != "" {
		server.tone3000.fail(flow.id, "TONE3000 browsing was canceled.")
		writeTone3000Callback(writer, false, "The selection was canceled.")
		return
	}
	code, toneID := request.URL.Query().Get("code"), request.URL.Query().Get("tone_id")
	if code == "" || toneID == "" {
		server.tone3000.fail(flow.id, "TONE3000 returned an incomplete selection.")
		writeTone3000Callback(writer, false, "The selection was incomplete.")
		return
	}
	token, err := server.tone3000.exchangeCode(request.Context(), code, flow.verifier)
	if err != nil {
		server.tone3000.fail(flow.id, "TONE3000 sign-in failed.")
		writeTone3000Callback(writer, false, "TONE3000 sign-in failed.")
		return
	}
	tone, models, err := server.tone3000.fetchSelection(request.Context(), toneID, flow.architecture, token.AccessToken)
	if err != nil {
		server.tone3000.fail(flow.id, err.Error())
		writeTone3000Callback(writer, false, "The selected models could not be loaded.")
		return
	}
	server.tone3000.mu.Lock()
	if current := server.tone3000.flows[flow.id]; current != nil {
		current.status, current.verifier, current.state = "ready", "", ""
		current.accessToken = token.AccessToken
		current.tokenExpiry = time.Now().UTC().Add(time.Duration(token.ExpiresIn) * time.Second)
		current.tone, current.models = tone, models
	}
	server.tone3000.mu.Unlock()
	writeTone3000Callback(writer, true, "Your tone is ready in Ardor.")
}

func (server *Server) getTone3000Selection(writer http.ResponseWriter, request *http.Request) {
	if server.tone3000 == nil {
		writeError(writer, http.StatusNotImplemented, "tone3000_not_configured", "TONE3000 is not configured on this server")
		return
	}
	account := accountFromContext(request.Context())
	server.tone3000.mu.Lock()
	flow := server.tone3000.flows[request.PathValue("flowId")]
	if flow == nil || flow.accountID != account.ID || !flow.expiresAt.After(time.Now().UTC()) {
		server.tone3000.mu.Unlock()
		writeError(writer, http.StatusNotFound, "selection_not_found", "TONE3000 selection was not found")
		return
	}
	response := tone3000FlowResponse(flow)
	server.tone3000.mu.Unlock()
	writeJSON(writer, http.StatusOK, response)
}

func (server *Server) installTone3000Selection(writer http.ResponseWriter, request *http.Request) {
	if !server.requireBrowserOrigin(writer, request) {
		return
	}
	if server.tone3000 == nil {
		writeError(writer, http.StatusNotImplemented, "tone3000_not_configured", "TONE3000 is not configured on this server")
		return
	}
	var body struct {
		ModelID int64 `json:"modelId"`
	}
	if !decodeJSON(writer, request, &body) {
		return
	}
	account := accountFromContext(request.Context())
	server.tone3000.mu.Lock()
	flow := server.tone3000.flows[request.PathValue("flowId")]
	if flow == nil || flow.accountID != account.ID || flow.status != "ready" || !flow.expiresAt.After(time.Now().UTC()) {
		server.tone3000.mu.Unlock()
		writeError(writer, http.StatusNotFound, "selection_not_found", "TONE3000 selection is not ready")
		return
	}
	var model *tone3000Model
	for index := range flow.models {
		if flow.models[index].ID == body.ModelID {
			copyModel := flow.models[index]
			model = &copyModel
			break
		}
	}
	if model != nil {
		flow.status = "installing"
	}
	deviceID, accessToken, tokenExpiry, tone := flow.deviceID, flow.accessToken, flow.tokenExpiry, flow.tone
	server.tone3000.mu.Unlock()
	if model == nil {
		writeError(writer, http.StatusBadRequest, "model_not_selected", "Model does not belong to this selection")
		return
	}
	if !tokenExpiry.After(time.Now().UTC()) {
		server.tone3000.fail(flow.id, "TONE3000 selection expired; choose the tone again.")
		writeError(writer, http.StatusConflict, "tone3000_session_expired", "TONE3000 selection expired; choose the tone again")
		return
	}
	owner, err := server.repository.DeviceOwner(request.Context(), deviceID)
	if err != nil || owner != account.ID {
		server.tone3000.retry(flow.id)
		writeError(writer, http.StatusNotFound, "device_not_found", "Device was not found")
		return
	}
	download, err := server.tone3000.downloadModel(request.Context(), model.ModelURL, accessToken)
	if err != nil {
		server.tone3000.retry(flow.id)
		writeError(writer, http.StatusBadGateway, "tone3000_download_failed", err.Error())
		return
	}
	defer download.Body.Close()
	if download.ContentLength <= 0 || download.ContentLength > cloudprotocol.MaxModelAssetBytes {
		server.tone3000.retry(flow.id)
		writeError(writer, http.StatusBadGateway, "tone3000_invalid_download", "TONE3000 model has no valid bounded size")
		return
	}
	filename := tone3000Filename(tone.User.Username, model.Name, model.ID)
	source := map[string]any{
		"provider": "tone3000", "toneId": tone.ID, "modelId": model.ID,
		"architecture": model.ArchitectureVersion, "license": tone.License,
		"creator": tone.User.Username, "toneUrl": tone.URL,
	}
	result, failure := server.installDeviceAsset(request.Context(), deviceID, "models", filename, download.ContentLength, download.Body, false, source)
	if failure != nil {
		server.tone3000.retry(flow.id)
		server.writeRelayResult(writer, nil, failure, http.StatusCreated)
		return
	}
	server.tone3000.mu.Lock()
	delete(server.tone3000.flows, flow.id)
	server.tone3000.mu.Unlock()
	writeRawJSON(writer, http.StatusCreated, result)
}

func (integration *tone3000Integration) exchangeCode(ctx context.Context, code, verifier string) (tone3000TokenResponse, error) {
	form := url.Values{
		"grant_type": {"authorization_code"}, "code": {code}, "code_verifier": {verifier},
		"redirect_uri": {integration.callbackURL}, "client_id": {integration.clientID},
	}
	endpoint := integration.endpoint("oauth/token")
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint.String(), strings.NewReader(form.Encode()))
	if err != nil {
		return tone3000TokenResponse{}, err
	}
	request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	request.Header.Set("Accept", "application/json")
	var token tone3000TokenResponse
	if err := integration.doJSON(request, &token); err != nil {
		return token, err
	}
	if token.AccessToken == "" || token.ExpiresIn <= 0 {
		return token, errors.New("TONE3000 returned an invalid token")
	}
	return token, nil
}

func (integration *tone3000Integration) fetchSelection(ctx context.Context, toneID, architecture, accessToken string) (tone3000Tone, []tone3000Model, error) {
	var tone tone3000Tone
	if err := integration.authenticatedJSON(ctx, "tones/"+url.PathEscape(toneID), nil, accessToken, &tone); err != nil {
		return tone, nil, err
	}
	if tone.Format != "nam" {
		return tone, nil, errors.New("The selected TONE3000 tone is not a NAM tone.")
	}
	query := url.Values{"tone_id": {toneID}, "page_size": {"300"}}
	if architecture == "2" {
		query.Set("architecture", "2")
	}
	var page struct {
		Data []tone3000Model `json:"data"`
	}
	if err := integration.authenticatedJSON(ctx, "models", query, accessToken, &page); err != nil {
		return tone, nil, err
	}
	filtered := page.Data[:0]
	for _, model := range page.Data {
		if model.ToneID != tone.ID {
			continue
		}
		if architecture == "2" && (model.ArchitectureVersion == nil || *model.ArchitectureVersion != "2") {
			continue
		}
		if architecture == "legacy" && model.ArchitectureVersion != nil && *model.ArchitectureVersion == "2" {
			continue
		}
		filtered = append(filtered, model)
	}
	if len(filtered) == 0 {
		return tone, nil, errors.New("This TONE3000 tone has no compatible NAM models.")
	}
	return tone, filtered, nil
}

func (integration *tone3000Integration) authenticatedJSON(ctx context.Context, path string, query url.Values, accessToken string, value any) error {
	endpoint := integration.endpoint(path)
	endpoint.RawQuery = query.Encode()
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, endpoint.String(), nil)
	if err != nil {
		return err
	}
	request.Header.Set("Authorization", "Bearer "+accessToken)
	request.Header.Set("Accept", "application/json")
	return integration.doJSON(request, value)
}

func (integration *tone3000Integration) doJSON(request *http.Request, value any) error {
	response, err := integration.client.Do(request)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return fmt.Errorf("TONE3000 returned HTTP %d", response.StatusCode)
	}
	body, err := io.ReadAll(io.LimitReader(response.Body, tone3000ResponseLimit+1))
	if err != nil {
		return err
	}
	if len(body) > tone3000ResponseLimit {
		return errors.New("TONE3000 response exceeds size limit")
	}
	if err := json.Unmarshal(body, value); err != nil {
		return err
	}
	return nil
}

func (integration *tone3000Integration) downloadModel(ctx context.Context, rawURL, accessToken string) (*http.Response, error) {
	modelURL, err := url.Parse(rawURL)
	if err != nil || modelURL.Scheme != integration.apiURL.Scheme || !strings.EqualFold(modelURL.Host, integration.apiURL.Host) {
		return nil, errors.New("TONE3000 returned an unexpected model address")
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, modelURL.String(), nil)
	if err != nil {
		return nil, err
	}
	request.Header.Set("Authorization", "Bearer "+accessToken)
	response, err := integration.client.Do(request)
	if err != nil {
		return nil, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		response.Body.Close()
		return nil, fmt.Errorf("TONE3000 returned HTTP %d", response.StatusCode)
	}
	return response, nil
}

func (integration *tone3000Integration) endpoint(path string) *url.URL {
	copyURL := *integration.apiURL
	copyURL.Path = strings.TrimRight(copyURL.Path, "/") + "/" + strings.TrimLeft(path, "/")
	copyURL.RawQuery = ""
	return &copyURL
}

func (integration *tone3000Integration) flowByStateLocked(state string, now time.Time) *tone3000Flow {
	integration.removeExpiredLocked(now)
	if state == "" {
		return nil
	}
	for _, flow := range integration.flows {
		if flow.status == "pending" && subtle.ConstantTimeCompare([]byte(flow.state), []byte(state)) == 1 {
			return flow
		}
	}
	return nil
}

func (integration *tone3000Integration) removeExpiredLocked(now time.Time) {
	for id, flow := range integration.flows {
		if !flow.expiresAt.After(now) {
			delete(integration.flows, id)
		}
	}
}

func (integration *tone3000Integration) fail(flowID, message string) {
	integration.mu.Lock()
	defer integration.mu.Unlock()
	if flow := integration.flows[flowID]; flow != nil {
		flow.status, flow.errorMessage, flow.verifier, flow.state, flow.accessToken = "failed", message, "", "", ""
	}
}

func (integration *tone3000Integration) retry(flowID string) {
	integration.mu.Lock()
	defer integration.mu.Unlock()
	if flow := integration.flows[flowID]; flow != nil && flow.status == "installing" && flow.expiresAt.After(time.Now().UTC()) {
		flow.status = "ready"
	}
}

func (integration *tone3000Integration) expire(flowID string, now time.Time) {
	integration.mu.Lock()
	defer integration.mu.Unlock()
	if flow := integration.flows[flowID]; flow != nil && !flow.expiresAt.After(now) {
		delete(integration.flows, flowID)
	}
}

func tone3000FlowResponse(flow *tone3000Flow) map[string]any {
	response := map[string]any{"flowId": flow.id, "status": flow.status, "expiresAt": flow.expiresAt}
	if flow.errorMessage != "" {
		response["message"] = flow.errorMessage
	}
	if flow.status == "ready" {
		models := make([]map[string]any, 0, len(flow.models))
		for _, model := range flow.models {
			models = append(models, map[string]any{
				"id": model.ID, "name": model.Name, "size": model.Size,
				"tone_id": model.ToneID, "architecture_version": model.ArchitectureVersion,
			})
		}
		response["selection"] = map[string]any{"tone": flow.tone, "models": models}
	}
	return response
}

func tone3000Filename(username, modelName string, modelID int64) string {
	value := fmt.Sprintf("T3K_%s_%s_%d", username, modelName, modelID)
	var stem strings.Builder
	for _, character := range value {
		if unicode.IsLetter(character) || unicode.IsDigit(character) || character == '_' || character == '-' || character == '.' {
			stem.WriteRune(character)
		} else {
			stem.WriteByte('_')
		}
		if stem.Len() >= 110 {
			break
		}
	}
	clean := strings.Trim(stem.String(), "-_.")
	if clean == "" {
		clean = "T3K_model_" + strconv.FormatInt(modelID, 10)
	}
	return clean + ".nam"
}

func writeTone3000Callback(writer http.ResponseWriter, success bool, message string) {
	title := "Selection failed"
	if success {
		title = "Selection complete"
	}
	writer.Header().Set("Content-Type", "text/html; charset=utf-8")
	writer.Header().Set("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; frame-ancestors 'none'")
	_, _ = fmt.Fprintf(writer, `<!doctype html><meta charset="utf-8"><title>%s</title><style>body{font:16px system-ui;color:#eee;background:#111;padding:3rem}p{color:#aaa}</style><h1>%s</h1><p>%s</p><script>setTimeout(function(){window.close()},500)</script>`, title, title, message+" You can close this window.")
}
