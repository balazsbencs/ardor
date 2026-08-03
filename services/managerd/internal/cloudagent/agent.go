package cloudagent

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"strings"
	"time"

	"ardor.local/managerd/internal/cloudprotocol"
	"ardor.local/managerd/internal/deviceidentity"
	"github.com/coder/websocket"
)

const (
	maxAuthResponseBytes = 32 * 1024
	defaultAuthTimeout   = 15 * time.Second
	defaultBackoff       = time.Second
	defaultMaxBackoff    = 5 * time.Minute
)

type Config struct {
	BaseURL          string
	HTTPClient       *http.Client
	Logger           *log.Logger
	ReconnectInitial time.Duration
	ReconnectMax     time.Duration

	// AllowInsecure is only intended for local integration tests. Production
	// configuration never sets it and therefore requires HTTPS and WSS.
	AllowInsecure bool
}

type Agent struct {
	config   Config
	baseURL  *url.URL
	identity *deviceidentity.Identity
	replays  *cloudprotocol.ReplayGuard
}

type challengeRequest struct {
	Version   int    `json:"version"`
	DeviceID  string `json:"deviceId"`
	PublicKey string `json:"publicKey"`
}

type challengeResponse struct {
	Version     int       `json:"version"`
	ChallengeID string    `json:"challengeId"`
	Nonce       string    `json:"nonce"`
	ExpiresAt   time.Time `json:"expiresAt"`
}

type tokenRequest struct {
	Version         int       `json:"version"`
	DeviceID        string    `json:"deviceId"`
	ChallengeID     string    `json:"challengeId"`
	Timestamp       time.Time `json:"timestamp"`
	ProtocolVersion int       `json:"protocolVersion"`
	Signature       string    `json:"signature"`
	ClaimEpoch      uint64    `json:"claimEpoch"`
}

type tokenResponse struct {
	Version      int       `json:"version"`
	Token        string    `json:"token"`
	WebSocketURL string    `json:"websocketUrl"`
	ExpiresAt    time.Time `json:"expiresAt"`
}

func New(config Config, identity *deviceidentity.Identity) (*Agent, error) {
	if identity == nil || len(identity.PrivateKey) == 0 {
		return nil, errors.New("cloud agent requires a device identity")
	}
	baseURL, err := validateBaseURL(config.BaseURL, config.AllowInsecure)
	if err != nil {
		return nil, err
	}
	if config.HTTPClient == nil {
		config.HTTPClient = http.DefaultClient
	}
	if config.Logger == nil {
		config.Logger = log.Default()
	}
	if config.ReconnectInitial <= 0 {
		config.ReconnectInitial = defaultBackoff
	}
	if config.ReconnectMax <= 0 {
		config.ReconnectMax = defaultMaxBackoff
	}
	if config.ReconnectMax < config.ReconnectInitial {
		return nil, errors.New("cloud reconnect maximum must not be less than initial delay")
	}
	return &Agent{
		config:   config,
		baseURL:  baseURL,
		identity: identity,
		replays:  cloudprotocol.NewReplayGuard(1024),
	}, nil
}

// Run maintains the outbound connection until context cancellation. All
// retries re-authenticate so connection tokens remain short-lived.
func (agent *Agent) Run(ctx context.Context) {
	backoff := agent.config.ReconnectInitial
	for {
		err := agent.runOnce(ctx)
		if ctx.Err() != nil {
			return
		}
		retryDelay := jitter(backoff, agent.config.ReconnectMax)
		agent.config.Logger.Printf("ardor cloud connection ended: %v; retrying in %s", err, retryDelay)
		timer := time.NewTimer(retryDelay)
		select {
		case <-ctx.Done():
			timer.Stop()
			return
		case <-timer.C:
		}
		backoff *= 2
		if backoff > agent.config.ReconnectMax {
			backoff = agent.config.ReconnectMax
		}
	}
}

func (agent *Agent) runOnce(ctx context.Context) error {
	authContext, cancel := context.WithTimeout(ctx, defaultAuthTimeout)
	auth, err := agent.authenticate(authContext)
	cancel()
	if err != nil {
		return fmt.Errorf("authenticate device: %w", err)
	}

	header := make(http.Header)
	header.Set("Authorization", "Bearer "+auth.Token)
	dialContext, cancel := context.WithTimeout(ctx, defaultAuthTimeout)
	connection, response, err := websocket.Dial(dialContext, auth.WebSocketURL, &websocket.DialOptions{
		HTTPClient: agent.noRedirectClient(),
		HTTPHeader: header,
	})
	cancel()
	if err != nil {
		if response != nil {
			_ = response.Body.Close()
		}
		return fmt.Errorf("open websocket: %w", err)
	}
	defer connection.CloseNow()
	connection.SetReadLimit(cloudprotocol.MaxMessageBytes)

	if err := agent.writeHello(ctx, connection); err != nil {
		return err
	}
	for {
		messageType, data, err := connection.Read(ctx)
		if err != nil {
			return fmt.Errorf("read websocket: %w", err)
		}
		if messageType != websocket.MessageText {
			return agent.protocolViolation(connection, errors.New("binary cloud messages are not supported"))
		}
		envelope, err := cloudprotocol.Decode(data, time.Now().UTC())
		if err != nil {
			return agent.protocolViolation(connection, err)
		}
		if err := agent.replays.Accept(envelope, time.Now().UTC()); err != nil {
			return agent.protocolViolation(connection, err)
		}
		if envelope.Kind != cloudprotocol.KindRequest || envelope.Operation != cloudprotocol.OperationPing {
			return agent.protocolViolation(connection, fmt.Errorf("inbound cloud operation %s/%s is not enabled", envelope.Kind, envelope.Operation))
		}
		if err := agent.writePingResponse(ctx, connection, envelope.MessageID); err != nil {
			return err
		}
	}
}

func (agent *Agent) authenticate(ctx context.Context) (tokenResponse, error) {
	var challenge challengeResponse
	if err := agent.postJSON(ctx, "/v1/device/connection-challenge", challengeRequest{
		Version:   cloudprotocol.Version,
		DeviceID:  agent.identity.DeviceID,
		PublicKey: agent.identity.PublicKeyBase64(),
	}, &challenge); err != nil {
		return tokenResponse{}, err
	}
	if err := validateChallenge(challenge, time.Now().UTC()); err != nil {
		return tokenResponse{}, err
	}

	timestamp := time.Now().UTC()
	transcript := cloudprotocol.AuthenticationTranscript(
		agent.identity.DeviceID,
		challenge.ChallengeID,
		challenge.Nonce,
		timestamp,
		cloudprotocol.Version,
		agent.identity.ClaimEpoch,
	)
	var token tokenResponse
	if err := agent.postJSON(ctx, "/v1/device/connection-token", tokenRequest{
		Version:         cloudprotocol.Version,
		DeviceID:        agent.identity.DeviceID,
		ChallengeID:     challenge.ChallengeID,
		Timestamp:       timestamp,
		ProtocolVersion: cloudprotocol.Version,
		Signature:       base64.StdEncoding.EncodeToString(agent.identity.Sign(transcript)),
		ClaimEpoch:      agent.identity.ClaimEpoch,
	}, &token); err != nil {
		return tokenResponse{}, err
	}
	if err := agent.validateToken(token, time.Now().UTC()); err != nil {
		return tokenResponse{}, err
	}
	return token, nil
}

func (agent *Agent) postJSON(ctx context.Context, path string, requestValue, responseValue any) error {
	data, err := json.Marshal(requestValue)
	if err != nil {
		return fmt.Errorf("encode request: %w", err)
	}
	endpoint := agent.baseURL.ResolveReference(&url.URL{Path: path})
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint.String(), bytes.NewReader(data))
	if err != nil {
		return fmt.Errorf("create request: %w", err)
	}
	request.Header.Set("Content-Type", "application/json")
	request.Header.Set("Accept", "application/json")

	response, err := agent.noRedirectClient().Do(request)
	if err != nil {
		return fmt.Errorf("send request: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return fmt.Errorf("control plane returned HTTP %d", response.StatusCode)
	}
	limited := io.LimitReader(response.Body, maxAuthResponseBytes+1)
	responseData, err := io.ReadAll(limited)
	if err != nil {
		return fmt.Errorf("read response: %w", err)
	}
	if len(responseData) > maxAuthResponseBytes {
		return errors.New("control plane response exceeds size limit")
	}
	decoder := json.NewDecoder(bytes.NewReader(responseData))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(responseValue); err != nil {
		return fmt.Errorf("decode response: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("decode response: multiple JSON values")
	}
	return nil
}

func (agent *Agent) writeHello(ctx context.Context, connection *websocket.Conn) error {
	envelope, err := cloudprotocol.NewEnvelope(cloudprotocol.KindEvent, cloudprotocol.OperationHello, "", map[string]any{
		"deviceId":               agent.identity.DeviceID,
		"protocolVersion":        cloudprotocol.Version,
		"remoteMutationsEnabled": false,
	}, time.Now().UTC())
	if err != nil {
		return err
	}
	return writeEnvelope(ctx, connection, envelope)
}

func (agent *Agent) writePingResponse(ctx context.Context, connection *websocket.Conn, correlationID string) error {
	envelope, err := cloudprotocol.NewEnvelope(cloudprotocol.KindResponse, cloudprotocol.OperationPing, correlationID, map[string]any{
		"ok": true,
	}, time.Now().UTC())
	if err != nil {
		return err
	}
	return writeEnvelope(ctx, connection, envelope)
}

func writeEnvelope(ctx context.Context, connection *websocket.Conn, envelope cloudprotocol.Envelope) error {
	data, err := json.Marshal(envelope)
	if err != nil {
		return fmt.Errorf("encode cloud envelope: %w", err)
	}
	if err := connection.Write(ctx, websocket.MessageText, data); err != nil {
		return fmt.Errorf("write websocket: %w", err)
	}
	return nil
}

func (agent *Agent) protocolViolation(connection *websocket.Conn, cause error) error {
	_ = connection.Close(websocket.StatusPolicyViolation, "cloud protocol violation")
	return fmt.Errorf("cloud protocol violation: %w", cause)
}

func (agent *Agent) noRedirectClient() *http.Client {
	client := *agent.config.HTTPClient
	client.CheckRedirect = func(_ *http.Request, _ []*http.Request) error {
		return http.ErrUseLastResponse
	}
	return &client
}

func jitter(delay, maximum time.Duration) time.Duration {
	if delay <= 1 {
		return delay
	}
	var random [8]byte
	if _, err := rand.Read(random[:]); err != nil {
		return delay
	}
	half := delay / 2
	span := delay
	value := half + time.Duration(binary.LittleEndian.Uint64(random[:])%uint64(span+1))
	if value > maximum {
		return maximum
	}
	return value
}

func validateBaseURL(raw string, allowInsecure bool) (*url.URL, error) {
	parsed, err := url.Parse(raw)
	if err != nil {
		return nil, fmt.Errorf("parse cloud URL: %w", err)
	}
	if parsed.Host == "" || parsed.User != nil || parsed.RawQuery != "" || parsed.Fragment != "" || (parsed.Path != "" && parsed.Path != "/") {
		return nil, errors.New("cloud URL must be an origin without credentials, path, query, or fragment")
	}
	wantedScheme := "https"
	if allowInsecure {
		wantedScheme = "http"
	}
	if parsed.Scheme != wantedScheme {
		return nil, fmt.Errorf("cloud URL must use %s", wantedScheme)
	}
	parsed.Path = "/"
	return parsed, nil
}

func validateChallenge(challenge challengeResponse, now time.Time) error {
	if challenge.Version != cloudprotocol.Version {
		return errors.New("challenge has unsupported protocol version")
	}
	if !cloudprotocol.IsUUID(challenge.ChallengeID) {
		return errors.New("challenge has invalid id")
	}
	nonce, err := base64.StdEncoding.DecodeString(challenge.Nonce)
	if err != nil || len(nonce) < 32 || len(nonce) > 128 {
		return errors.New("challenge has invalid nonce")
	}
	if !challenge.ExpiresAt.After(now) || challenge.ExpiresAt.After(now.Add(5*time.Minute)) {
		return errors.New("challenge expiry is invalid")
	}
	return nil
}

func (agent *Agent) validateToken(token tokenResponse, now time.Time) error {
	if token.Version != cloudprotocol.Version || token.Token == "" || len(token.Token) > 4096 {
		return errors.New("connection token response is invalid")
	}
	if !token.ExpiresAt.After(now) || token.ExpiresAt.After(now.Add(15*time.Minute)) {
		return errors.New("connection token expiry is invalid")
	}
	websocketURL, err := url.Parse(token.WebSocketURL)
	if err != nil || websocketURL.Host == "" || websocketURL.User != nil || websocketURL.RawQuery != "" || websocketURL.Fragment != "" {
		return errors.New("connection websocket URL is invalid")
	}
	wantedScheme := "wss"
	if agent.config.AllowInsecure {
		wantedScheme = "ws"
	}
	if websocketURL.Scheme != wantedScheme || !strings.EqualFold(websocketURL.Host, agent.baseURL.Host) {
		return errors.New("connection websocket URL must use the control plane origin")
	}
	return nil
}
