package server

import (
	"context"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
	"time"

	"ardor.local/controlplane/internal/store"
)

func TestTone3000CallbackKeepsCredentialsServerSide(t *testing.T) {
	accessToken := "server-only-access-token"
	toneAPI := httptest.NewServer(http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		switch request.URL.Path {
		case "/api/v1/oauth/token":
			if err := request.ParseForm(); err != nil || request.Form.Get("code_verifier") != "pkce-verifier" {
				t.Fatalf("token form=%v err=%v", request.Form, err)
			}
			writeJSON(writer, http.StatusOK, map[string]any{"access_token": accessToken, "refresh_token": "ignored-refresh", "expires_in": 3600})
		case "/api/v1/tones/42":
			if request.Header.Get("Authorization") != "Bearer "+accessToken {
				t.Fatal("tone request did not use server token")
			}
			writeJSON(writer, http.StatusOK, map[string]any{
				"id": 42, "title": "A2 Pack", "description": nil, "gear": "amp-cab", "images": []string{},
				"format": "nam", "license": "cc-by", "url": "https://www.tone3000.com/tones/a2-42",
				"user": map[string]any{"id": 7, "username": "maker", "avatar_url": nil, "url": "https://www.tone3000.com/users/maker"},
			})
		case "/api/v1/models":
			if request.URL.Query().Get("architecture") != "2" {
				t.Fatalf("architecture query=%q", request.URL.RawQuery)
			}
			writeJSON(writer, http.StatusOK, map[string]any{"data": []any{map[string]any{
				"id": 9, "model_url": toneAPIURL(request) + "/api/v1/models/9/download", "name": "Lead", "size": "standard",
				"tone_id": 42, "architecture_version": "2",
			}}})
		default:
			http.NotFound(writer, request)
		}
	}))
	defer toneAPI.Close()

	server, err := New(Config{
		PublicOrigin: "http://ardor.example", SecureCookies: false, Logger: log.New(io.Discard, "", 0),
		Tone3000ClientID: "publishable-client", Tone3000BaseURL: toneAPI.URL, Tone3000Client: toneAPI.Client(),
	}, store.NewMemory())
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	flow := &tone3000Flow{
		id: "018f7f1a-8b25-7e31-a951-5c43272e2010", accountID: "account-1", deviceID: "device-1",
		architecture: "2", state: "oauth-state", verifier: "pkce-verifier", status: "pending", expiresAt: time.Now().Add(time.Minute),
	}
	server.tone3000.flows[flow.id] = flow

	callback := httptest.NewRequest(http.MethodGet, "/v1/integrations/tone3000/callback?state=oauth-state&code=oauth-code&tone_id=42", nil)
	callbackResponse := httptest.NewRecorder()
	server.completeTone3000Selection(callbackResponse, callback)
	if callbackResponse.Code != http.StatusOK || !strings.Contains(callbackResponse.Body.String(), "Selection complete") {
		t.Fatalf("callback status=%d body=%s", callbackResponse.Code, callbackResponse.Body.String())
	}

	selectionRequest := httptest.NewRequest(http.MethodGet, "/v1/integrations/tone3000/selections/"+flow.id, nil)
	selectionRequest.SetPathValue("flowId", flow.id)
	selectionRequest = selectionRequest.WithContext(context.WithValue(selectionRequest.Context(), accountContextKey, store.Account{ID: "account-1"}))
	selectionResponse := httptest.NewRecorder()
	server.getTone3000Selection(selectionResponse, selectionRequest)
	body := selectionResponse.Body.String()
	if selectionResponse.Code != http.StatusOK || !strings.Contains(body, `"status":"ready"`) || !strings.Contains(body, `"architecture_version":"2"`) {
		t.Fatalf("selection status=%d body=%s", selectionResponse.Code, body)
	}
	if strings.Contains(body, accessToken) || strings.Contains(body, "ignored-refresh") || strings.Contains(body, "model_url") || strings.Contains(body, "/download") {
		t.Fatalf("selection exposed server-only credentials or download URL: %s", body)
	}
}

func toneAPIURL(request *http.Request) string {
	return (&url.URL{Scheme: "http", Host: request.Host}).String()
}
