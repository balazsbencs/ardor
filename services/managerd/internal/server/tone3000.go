package server

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"path"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode"

	"ardor.local/managerd/internal/assets"
	"ardor.local/managerd/internal/config"
	"ardor.local/managerd/internal/runtimecontrol"
)

const (
	tone3000FlowTTL       = 15 * time.Minute
	tone3000MaxModelBytes = 32 * 1024 * 1024
)

type localTone3000 struct {
	clientID string
	apiURL   *url.URL
	port     int
	iface    string
	client   *http.Client
	mu       sync.Mutex
	flows    map[string]*localTone3000Flow
}

type localTone3000Flow struct {
	id, state, verifier, callbackURL string
	status                           string
	expiresAt                        time.Time
	accessToken                      string
	tokenExpiry                      time.Time
	errorMessage                     string
	tone                             tone3000Tone
	models                           []tone3000Model
}

type tone3000Tone struct {
	ID          int64        `json:"id"`
	Title       string       `json:"title"`
	Description string       `json:"description"`
	Gear        string       `json:"gear"`
	Images      []string     `json:"images"`
	Format      string       `json:"format"`
	License     string       `json:"license"`
	User        tone3000User `json:"user"`
	URL         string       `json:"url"`
}

type tone3000User struct {
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

type tone3000Token struct {
	AccessToken string `json:"access_token"`
	ExpiresIn   int64  `json:"expires_in"`
}

func newLocalTone3000(cfg config.Config) (*localTone3000, error) {
	clientID := strings.TrimSpace(cfg.Tone3000ClientID)
	if clientID == "" {
		return nil, nil
	}
	base, err := url.Parse(strings.TrimRight(strings.TrimSpace(cfg.Tone3000BaseURL), "/"))
	if err != nil || base.Scheme != "https" || base.Host == "" || base.User != nil {
		return nil, errors.New("TONE3000_BASE_URL must be an HTTPS origin")
	}
	if !strings.HasSuffix(base.Path, "/api/v1") {
		base.Path = strings.TrimRight(base.Path, "/") + "/api/v1"
	}
	return &localTone3000{clientID: clientID, apiURL: base, port: cfg.Port, iface: cfg.WiFiInterface, client: &http.Client{Timeout: 30 * time.Second}, flows: map[string]*localTone3000Flow{}}, nil
}

func (integration *localTone3000) callbackURL() (string, error) {
	iface, err := net.InterfaceByName(integration.iface)
	if err != nil {
		return "", fmt.Errorf("find TONE3000 network interface: %w", err)
	}
	addresses, err := iface.Addrs()
	if err != nil {
		return "", fmt.Errorf("read TONE3000 network address: %w", err)
	}
	for _, address := range addresses {
		var ip net.IP
		switch value := address.(type) {
		case *net.IPNet:
			ip = value.IP
		case *net.IPAddr:
			ip = value.IP
		}
		if ipv4 := ip.To4(); ipv4 != nil && !ipv4.IsLoopback() && !ipv4.IsLinkLocalUnicast() {
			return fmt.Sprintf("http://%s:%d/api/integrations/tone3000/callback", ipv4.String(), integration.port), nil
		}
	}
	return "", errors.New("connect Ardor to Wi-Fi before browsing TONE3000")
}

func (integration *localTone3000) start() (*localTone3000Flow, string, error) {
	callbackURL, err := integration.callbackURL()
	if err != nil {
		return nil, "", err
	}
	flowID, err := randomHex(16)
	if err != nil {
		return nil, "", err
	}
	state, err := randomToken(24)
	if err != nil {
		return nil, "", err
	}
	verifier, err := randomToken(32)
	if err != nil {
		return nil, "", err
	}
	challenge := sha256.Sum256([]byte(verifier))
	flow := &localTone3000Flow{id: flowID, state: state, verifier: verifier, callbackURL: callbackURL, status: "pending", expiresAt: time.Now().UTC().Add(tone3000FlowTTL)}
	integration.mu.Lock()
	integration.removeExpiredLocked(time.Now().UTC())
	integration.flows[flow.id] = flow
	integration.mu.Unlock()
	authorize := integration.endpoint("oauth/authorize")
	query := authorize.Query()
	query.Set("client_id", integration.clientID)
	query.Set("redirect_uri", callbackURL)
	query.Set("response_type", "code")
	query.Set("code_challenge", base64.RawURLEncoding.EncodeToString(challenge[:]))
	query.Set("code_challenge_method", "S256")
	query.Set("state", state)
	query.Set("prompt", "select_tone")
	query.Set("format", "nam")
	query.Set("architecture", "2")
	query.Set("menubar", "true")
	query.Set("preview", "true")
	authorize.RawQuery = query.Encode()
	return flow, authorize.String(), nil
}

func (integration *localTone3000) complete(r *http.Request) *localTone3000Flow {
	state := r.URL.Query().Get("state")
	integration.mu.Lock()
	integration.removeExpiredLocked(time.Now().UTC())
	var flow *localTone3000Flow
	for _, candidate := range integration.flows {
		if candidate.status == "pending" && subtle.ConstantTimeCompare([]byte(candidate.state), []byte(state)) == 1 {
			candidate.status = "loading"
			flow = candidate
			break
		}
	}
	integration.mu.Unlock()
	if flow == nil {
		return nil
	}
	if r.URL.Query().Get("error") != "" || r.URL.Query().Get("canceled") == "true" {
		integration.fail(flow.id, "TONE3000 browsing was canceled.")
		return flow
	}
	code, toneID := r.URL.Query().Get("code"), r.URL.Query().Get("tone_id")
	if code == "" || toneID == "" {
		integration.fail(flow.id, "TONE3000 returned an incomplete selection.")
		return flow
	}
	token, err := integration.exchange(r.Context(), code, flow.verifier, flow.callbackURL)
	if err != nil {
		integration.fail(flow.id, "TONE3000 sign-in failed: "+tone3000ErrorMessage(err))
		return flow
	}
	tone, models, err := integration.selection(r.Context(), toneID, token.AccessToken)
	if err != nil {
		integration.fail(flow.id, err.Error())
		return flow
	}
	integration.mu.Lock()
	if current := integration.flows[flow.id]; current != nil {
		current.status, current.state, current.verifier = "ready", "", ""
		current.accessToken, current.tokenExpiry = token.AccessToken, time.Now().UTC().Add(time.Duration(token.ExpiresIn)*time.Second)
		current.tone, current.models = tone, models
	}
	integration.mu.Unlock()
	return flow
}

func (integration *localTone3000) exchange(ctx context.Context, code, verifier, callbackURL string) (tone3000Token, error) {
	form := url.Values{"grant_type": {"authorization_code"}, "code": {code}, "code_verifier": {verifier}, "redirect_uri": {callbackURL}, "client_id": {integration.clientID}}
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, integration.endpoint("oauth/token").String(), strings.NewReader(form.Encode()))
	if err != nil {
		return tone3000Token{}, err
	}
	request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	var token tone3000Token
	if err := integration.json(request, &token); err != nil {
		return token, err
	}
	if token.AccessToken == "" || token.ExpiresIn <= 0 {
		return token, errors.New("TONE3000 returned an invalid token")
	}
	return token, nil
}

func (integration *localTone3000) selection(ctx context.Context, toneID, accessToken string) (tone3000Tone, []tone3000Model, error) {
	var tone tone3000Tone
	if err := integration.authJSON(ctx, "tones/"+url.PathEscape(toneID), nil, accessToken, &tone); err != nil {
		return tone, nil, err
	}
	if tone.Format != "nam" {
		return tone, nil, errors.New("The selected TONE3000 tone is not a NAM tone.")
	}
	var page struct {
		Data []tone3000Model `json:"data"`
	}
	if err := integration.authJSON(ctx, "models", url.Values{"tone_id": {toneID}, "page_size": {"300"}, "architecture": {"2"}}, accessToken, &page); err != nil {
		return tone, nil, err
	}
	models := page.Data[:0]
	for _, model := range page.Data {
		if model.ToneID == tone.ID && model.ArchitectureVersion != nil && *model.ArchitectureVersion == "2" {
			models = append(models, model)
		}
	}
	if len(models) == 0 {
		return tone, nil, errors.New("This TONE3000 tone has no compatible NAM A2 models.")
	}
	return tone, models, nil
}

func (integration *localTone3000) authJSON(ctx context.Context, path string, query url.Values, accessToken string, target any) error {
	endpoint := integration.endpoint(path)
	endpoint.RawQuery = query.Encode()
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, endpoint.String(), nil)
	if err != nil {
		return err
	}
	request.Header.Set("Authorization", "Bearer "+accessToken)
	return integration.json(request, target)
}

func (integration *localTone3000) json(request *http.Request, target any) error {
	response, err := integration.client.Do(request)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		body, _ := io.ReadAll(io.LimitReader(response.Body, 4<<10))
		return tone3000HTTPError(response.StatusCode, body)
	}
	return json.NewDecoder(io.LimitReader(response.Body, 1<<20)).Decode(target)
}

func tone3000HTTPError(status int, body []byte) error {
	var apiError struct {
		Error            string `json:"error"`
		ErrorDescription string `json:"error_description"`
		Message          string `json:"message"`
	}
	_ = json.Unmarshal(body, &apiError)
	message := apiError.ErrorDescription
	if message == "" {
		message = apiError.Message
	}
	if message == "" {
		message = apiError.Error
	}
	if message == "" {
		return fmt.Errorf("TONE3000 returned HTTP %d", status)
	}
	return fmt.Errorf("TONE3000 returned HTTP %d: %s", status, tone3000ErrorMessage(errors.New(message)))
}

func tone3000ErrorMessage(err error) string {
	message := strings.Join(strings.Fields(err.Error()), " ")
	if len(message) > 240 {
		return message[:240] + "…"
	}
	return message
}

func (integration *localTone3000) endpoint(rawPath string) *url.URL {
	endpoint := *integration.apiURL
	endpoint.Path = path.Join(integration.apiURL.Path, rawPath)
	endpoint.RawQuery = ""
	return &endpoint
}

func (integration *localTone3000) snapshot(id string) *localTone3000Flow {
	integration.mu.Lock()
	defer integration.mu.Unlock()
	integration.removeExpiredLocked(time.Now().UTC())
	flow := integration.flows[id]
	if flow == nil {
		return nil
	}
	copyFlow := *flow
	copyFlow.models = append([]tone3000Model(nil), flow.models...)
	return &copyFlow
}

func (integration *localTone3000) install(ctx context.Context, flowID string, modelID int64, store assets.Store, dataRoot string) (assets.Info, error) {
	integration.mu.Lock()
	integration.removeExpiredLocked(time.Now().UTC())
	flow := integration.flows[flowID]
	if flow == nil || flow.status != "ready" || !flow.tokenExpiry.After(time.Now().UTC()) {
		integration.mu.Unlock()
		return assets.Info{}, errors.New("TONE3000 selection is not ready")
	}
	var model *tone3000Model
	for index := range flow.models {
		if flow.models[index].ID == modelID {
			copyModel := flow.models[index]
			model = &copyModel
			break
		}
	}
	if model == nil {
		integration.mu.Unlock()
		return assets.Info{}, errors.New("model does not belong to this selection")
	}
	flow.status = "installing"
	token, tone := flow.accessToken, flow.tone
	integration.mu.Unlock()
	modelURL, err := url.Parse(model.ModelURL)
	if err != nil || modelURL.Scheme != integration.apiURL.Scheme || !strings.EqualFold(modelURL.Host, integration.apiURL.Host) {
		integration.retry(flowID)
		return assets.Info{}, errors.New("TONE3000 returned an unexpected model address")
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, modelURL.String(), nil)
	if err != nil {
		integration.retry(flowID)
		return assets.Info{}, err
	}
	request.Header.Set("Authorization", "Bearer "+token)
	response, err := integration.client.Do(request)
	if err != nil {
		integration.retry(flowID)
		return assets.Info{}, err
	}
	defer response.Body.Close()
	if !validTone3000ModelDownload(response.StatusCode, response.ContentLength) {
		integration.retry(flowID)
		return assets.Info{}, errors.New("TONE3000 returned an invalid model download")
	}
	info, err := store.Save(assets.KindModel, localTone3000Filename(tone.User.Username, model.Name, model.ID), io.LimitReader(response.Body, tone3000MaxModelBytes+1), false)
	if err != nil {
		integration.retry(flowID)
		return assets.Info{}, err
	}
	if info.SizeBytes > tone3000MaxModelBytes {
		_ = store.Delete(assets.KindModel, info.ID)
		integration.retry(flowID)
		return assets.Info{}, errors.New("TONE3000 model download exceeds the size limit")
	}
	_ = runtimecontrol.QueueAssetReload(dataRoot)
	integration.mu.Lock()
	delete(integration.flows, flowID)
	integration.mu.Unlock()
	return info, nil
}

func validTone3000ModelDownload(status int, contentLength int64) bool {
	return status >= http.StatusOK && status < http.StatusMultipleChoices && contentLength != 0 && contentLength <= tone3000MaxModelBytes
}

func (integration *localTone3000) fail(id, message string) {
	integration.mu.Lock()
	defer integration.mu.Unlock()
	if flow := integration.flows[id]; flow != nil {
		flow.status, flow.errorMessage, flow.state, flow.verifier, flow.accessToken = "failed", message, "", "", ""
	}
}
func (integration *localTone3000) retry(id string) {
	integration.mu.Lock()
	defer integration.mu.Unlock()
	if flow := integration.flows[id]; flow != nil && flow.status == "installing" {
		flow.status = "ready"
	}
}
func (integration *localTone3000) removeExpiredLocked(now time.Time) {
	for id, flow := range integration.flows {
		if !flow.expiresAt.After(now) {
			delete(integration.flows, id)
		}
	}
}

func localTone3000Filename(username, name string, id int64) string {
	var out strings.Builder
	for _, character := range fmt.Sprintf("T3K_%s_%s_%d", username, name, id) {
		if unicode.IsLetter(character) || unicode.IsDigit(character) || character == '_' || character == '-' || character == '.' {
			out.WriteRune(character)
		} else {
			out.WriteByte('_')
		}
		if out.Len() >= 110 {
			break
		}
	}
	stem := strings.Trim(out.String(), "-_.")
	if stem == "" {
		stem = "T3K_model_" + strconv.FormatInt(id, 10)
	}
	return stem + ".nam"
}

func randomToken(size int) (string, error) {
	bytes := make([]byte, size)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(bytes), nil
}
func randomHex(size int) (string, error) {
	bytes := make([]byte, size)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	return hex.EncodeToString(bytes), nil
}

func writeTone3000Callback(w http.ResponseWriter, success bool, message string) {
	title := "Selection failed"
	if success {
		title = "Selection complete"
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; frame-ancestors 'none'")
	_, _ = fmt.Fprintf(w, `<!doctype html><meta charset="utf-8"><title>%s</title><style>body{font:16px system-ui;color:#eee;background:#111;padding:3rem}p{color:#aaa}</style><h1>%s</h1><p>%s You can close this window.</p><script>setTimeout(function(){window.close()},500)</script>`, title, title, message)
}
