package update

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"strings"
	"testing"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func (function roundTripFunc) RoundTrip(request *http.Request) (*http.Response, error) {
	return function(request)
}

func TestDiscoverVerifiesAndSelectsExactReleaseAssets(t *testing.T) {
	publicKey, privateKey, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	manifest := validManifest()
	manifestBytes, _ := json.Marshal(manifest)
	signature := base64.StdEncoding.EncodeToString(ed25519.Sign(privateKey, manifestBytes))
	prefix := "ardor-device-0.1.24-linux-aarch64"
	release, _ := json.Marshal(map[string]any{
		"tag_name": "v0.1.24", "html_url": "https://github.com/balazsbencs/ardor/releases/tag/v0.1.24",
		"body": "Safe update", "draft": false, "prerelease": false,
		"assets": []map[string]any{
			{"name": prefix + ".tar.gz", "size": int64(30), "browser_download_url": assetURL("v0.1.24", prefix+".tar.gz")},
			{"name": prefix + ".manifest.json", "size": len(manifestBytes), "browser_download_url": assetURL("v0.1.24", prefix+".manifest.json")},
			{"name": prefix + ".manifest.sig", "size": len(signature), "browser_download_url": assetURL("v0.1.24", prefix+".manifest.sig")},
		},
	})
	client := &http.Client{Transport: roundTripFunc(func(request *http.Request) (*http.Response, error) {
		body := release
		if strings.HasSuffix(request.URL.Path, ".manifest.json") {
			body = manifestBytes
		} else if strings.HasSuffix(request.URL.Path, ".manifest.sig") {
			body = []byte(signature)
		}
		return &http.Response{StatusCode: http.StatusOK, Body: io.NopCloser(strings.NewReader(string(body))), Header: make(http.Header)}, nil
	})}
	selection, err := (ReleaseClient{HTTPClient: client, LatestURL: "https://api.github.test/latest"}).Discover(
		context.Background(), publicKey, "0.1.23", "0.1.23",
	)
	if err != nil {
		t.Fatal(err)
	}
	if selection.Version != "0.1.24" || selection.BundleSHA256 != manifest.Bundle.SHA256 || selection.ReflashRequired {
		t.Fatalf("unexpected selection: %+v", selection)
	}
}

func TestDiscoverRejectsAlteredManifest(t *testing.T) {
	publicKey, privateKey, _ := ed25519.GenerateKey(rand.Reader)
	manifest := validManifest()
	manifestBytes, _ := json.Marshal(manifest)
	signature := base64.StdEncoding.EncodeToString(ed25519.Sign(privateKey, manifestBytes))
	manifestBytes = append(manifestBytes, ' ')
	prefix := "ardor-device-0.1.24-linux-aarch64"
	release, _ := json.Marshal(map[string]any{
		"tag_name": "v0.1.24", "html_url": "https://github.com/balazsbencs/ardor/releases/tag/v0.1.24",
		"draft": false, "prerelease": false,
		"assets": []map[string]any{
			{"name": prefix + ".tar.gz", "size": int64(30), "browser_download_url": assetURL("v0.1.24", prefix+".tar.gz")},
			{"name": prefix + ".manifest.json", "size": len(manifestBytes), "browser_download_url": assetURL("v0.1.24", prefix+".manifest.json")},
			{"name": prefix + ".manifest.sig", "size": len(signature), "browser_download_url": assetURL("v0.1.24", prefix+".manifest.sig")},
		},
	})
	client := &http.Client{Transport: roundTripFunc(func(request *http.Request) (*http.Response, error) {
		body := release
		if strings.HasSuffix(request.URL.Path, ".manifest.json") {
			body = manifestBytes
		} else if strings.HasSuffix(request.URL.Path, ".manifest.sig") {
			body = []byte(signature)
		}
		return &http.Response{StatusCode: http.StatusOK, Body: io.NopCloser(strings.NewReader(string(body))), Header: make(http.Header)}, nil
	})}
	if _, err := (ReleaseClient{HTTPClient: client, LatestURL: "https://api.github.test/latest"}).Discover(
		context.Background(), publicKey, "0.1.23", "0.1.23",
	); err == nil {
		t.Fatal("altered signed manifest was accepted")
	}
}

func assetURL(tag, name string) string {
	return "https://github.com/balazsbencs/ardor/releases/download/" + tag + "/" + name
}
