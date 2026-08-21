package server

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
	"time"
)

func TestLocalTone3000CallbackUsesA2AndKeepsCredentialsPrivate(t *testing.T) {
	accessToken := "device-memory-only-token"
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/api/v1/oauth/token":
			if err := r.ParseForm(); err != nil || r.Form.Get("code_verifier") != "verifier" {
				t.Fatalf("token form=%v err=%v", r.Form, err)
			}
			writeJSON(w, http.StatusOK, map[string]any{"access_token": accessToken, "expires_in": 3600})
		case "/api/v1/tones/42":
			if r.Header.Get("Authorization") != "Bearer "+accessToken {
				t.Fatal("tone request did not use local token")
			}
			writeJSON(w, http.StatusOK, map[string]any{"id": 42, "title": "A2 Pack", "gear": "amp", "format": "nam", "license": "cc-by", "user": map[string]any{"username": "maker"}})
		case "/api/v1/models":
			if r.URL.Query().Get("architecture") != "2" {
				t.Fatalf("architecture query=%q", r.URL.RawQuery)
			}
			writeJSON(w, http.StatusOK, map[string]any{"data": []any{map[string]any{"id": 9, "model_url": apiURL(r) + "/api/v1/models/9/download", "name": "Lead", "size": "standard", "tone_id": 42, "architecture_version": "2"}}})
		default:
			http.NotFound(w, r)
		}
	}))
	defer api.Close()
	base, _ := url.Parse(api.URL + "/api/v1")
	integration := &localTone3000{clientID: "t3k_pub_test", apiURL: base, client: api.Client(), flows: map[string]*localTone3000Flow{
		"flow": {id: "flow", state: "state", verifier: "verifier", callbackURL: "http://192.168.1.2:8080/api/integrations/tone3000/callback", status: "pending", expiresAt: time.Now().Add(time.Minute)},
	}}
	callback := httptest.NewRequest(http.MethodGet, "/api/integrations/tone3000/callback?state=state&code=code&tone_id=42", nil)
	flow := integration.complete(callback)
	if flow == nil || flow.status != "ready" {
		t.Fatalf("flow=%+v", flow)
	}
	if flow.accessToken != accessToken || len(flow.models) != 1 || flow.models[0].ArchitectureVersion == nil || *flow.models[0].ArchitectureVersion != "2" {
		t.Fatalf("unexpected completed flow: %+v", flow)
	}
	response := map[string]any{"tone": flow.tone, "models": []map[string]any{{"id": flow.models[0].ID, "name": flow.models[0].Name}}}
	encoded, _ := json.Marshal(response)
	if strings.Contains(string(encoded), accessToken) || strings.Contains(string(encoded), "model_url") {
		t.Fatalf("selection leaked credentials: %s", encoded)
	}
}

func TestTone3000ModelDownloadAllowsChunkedResponse(t *testing.T) {
	if !validTone3000ModelDownload(http.StatusOK, -1) {
		t.Fatal("chunked TONE3000 model response was rejected")
	}
	if validTone3000ModelDownload(http.StatusOK, 0) || validTone3000ModelDownload(http.StatusOK, tone3000MaxModelBytes+1) {
		t.Fatal("invalid model lengths were accepted")
	}
}

func apiURL(r *http.Request) string { return (&url.URL{Scheme: "http", Host: r.Host}).String() }
