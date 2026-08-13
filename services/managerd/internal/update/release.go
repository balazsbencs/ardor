package update

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"
)

const (
	GitHubLatestReleaseURL = "https://api.github.com/repos/balazsbencs/ardor/releases/latest"
	githubRepository       = "balazsbencs/ardor"
	maxSignatureBytes      = 1024
)

type ReleaseClient struct {
	HTTPClient *http.Client
	LatestURL  string
}

type Selection struct {
	Version         string          `json:"version"`
	Tag             string          `json:"tag"`
	ReleaseURL      string          `json:"releaseUrl"`
	ReleaseNotes    string          `json:"releaseNotes,omitempty"`
	BundleURL       string          `json:"bundleUrl"`
	ManifestURL     string          `json:"manifestUrl"`
	SignatureURL    string          `json:"signatureUrl"`
	Manifest        json.RawMessage `json:"manifest"`
	Signature       string          `json:"signature"`
	BundleSize      int64           `json:"bundleSize"`
	BundleSHA256    string          `json:"bundleSha256"`
	ReflashRequired bool            `json:"reflashRequired"`
	Incompatibility string          `json:"incompatibility,omitempty"`
}

type githubRelease struct {
	TagName    string        `json:"tag_name"`
	HTMLURL    string        `json:"html_url"`
	Body       string        `json:"body"`
	Draft      bool          `json:"draft"`
	Prerelease bool          `json:"prerelease"`
	Assets     []githubAsset `json:"assets"`
}

type githubAsset struct {
	Name               string `json:"name"`
	Size               int64  `json:"size"`
	BrowserDownloadURL string `json:"browser_download_url"`
}

func LoadPublicKey(path string) (ed25519.PublicKey, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	decoded, err := base64.StdEncoding.Strict().DecodeString(strings.TrimSpace(string(data)))
	if err != nil || len(decoded) != ed25519.PublicKeySize {
		return nil, errors.New("OTA public key must be one base64-encoded Ed25519 public key")
	}
	return ed25519.PublicKey(decoded), nil
}

func (client ReleaseClient) Discover(ctx context.Context, publicKey ed25519.PublicKey, installedVersion, baseVersion string) (Selection, error) {
	latestURL := client.LatestURL
	if latestURL == "" {
		latestURL = GitHubLatestReleaseURL
	}
	httpClient := client.HTTPClient
	if httpClient == nil {
		httpClient = &http.Client{Timeout: 30 * time.Second, CheckRedirect: safeReleaseRedirect}
	}
	var release githubRelease
	if err := getJSON(ctx, httpClient, latestURL, 1<<20, &release); err != nil {
		return Selection{}, fmt.Errorf("fetch latest GitHub release: %w", err)
	}
	if release.Draft || release.Prerelease {
		return Selection{}, errors.New("latest GitHub release is not stable")
	}
	if !strings.HasPrefix(release.TagName, "v") {
		return Selection{}, errors.New("latest GitHub release tag is invalid")
	}
	version := strings.TrimPrefix(release.TagName, "v")
	parsedVersion, err := ParseVersion(version)
	if err != nil || parsedVersion.String() != version {
		return Selection{}, errors.New("latest GitHub release tag is not a canonical version")
	}
	prefix := "ardor-device-" + version + "-linux-aarch64"
	expected := map[string]*githubAsset{
		prefix + ".tar.gz":        nil,
		prefix + ".manifest.json": nil,
		prefix + ".manifest.sig":  nil,
	}
	for index := range release.Assets {
		asset := &release.Assets[index]
		if _, wanted := expected[asset.Name]; !wanted {
			continue
		}
		if expected[asset.Name] != nil {
			return Selection{}, fmt.Errorf("release has duplicate OTA asset %q", asset.Name)
		}
		if err := validateReleaseAssetURL(asset.BrowserDownloadURL, release.TagName, asset.Name); err != nil {
			return Selection{}, err
		}
		expected[asset.Name] = asset
	}
	for name, asset := range expected {
		if asset == nil {
			return Selection{}, fmt.Errorf("release is missing OTA asset %q", name)
		}
	}
	bundleAsset := expected[prefix+".tar.gz"]
	manifestAsset := expected[prefix+".manifest.json"]
	signatureAsset := expected[prefix+".manifest.sig"]
	manifestBytes, err := getBytes(ctx, httpClient, manifestAsset.BrowserDownloadURL, maxManifestBytes)
	if err != nil {
		return Selection{}, fmt.Errorf("fetch update manifest: %w", err)
	}
	signatureBytes, err := getBytes(ctx, httpClient, signatureAsset.BrowserDownloadURL, maxSignatureBytes)
	if err != nil {
		return Selection{}, fmt.Errorf("fetch update signature: %w", err)
	}
	if err := VerifyManifestSignature(publicKey, manifestBytes, signatureBytes); err != nil {
		return Selection{}, err
	}
	manifest, err := ParseManifest(manifestBytes)
	if err != nil {
		return Selection{}, err
	}
	if manifest.Version != version || manifest.Bundle.Name != bundleAsset.Name || manifest.Bundle.Size != bundleAsset.Size {
		return Selection{}, errors.New("GitHub release assets do not match the signed manifest")
	}
	selection := Selection{
		Version: version, Tag: release.TagName, ReleaseURL: release.HTMLURL,
		ReleaseNotes: limitedReleaseNotes(release.Body), BundleURL: bundleAsset.BrowserDownloadURL,
		ManifestURL: manifestAsset.BrowserDownloadURL, SignatureURL: signatureAsset.BrowserDownloadURL,
		Manifest: append(json.RawMessage(nil), manifestBytes...), Signature: strings.TrimSpace(string(signatureBytes)),
		BundleSize: manifest.Bundle.Size, BundleSHA256: manifest.Bundle.SHA256,
	}
	if err := Compatible(manifest, installedVersion, baseVersion); err != nil {
		installed, installedErr := ParseVersion(installedVersion)
		if installedErr == nil && parsedVersion.Compare(installed) <= 0 {
			return Selection{}, err
		}
		selection.ReflashRequired = true
		selection.Incompatibility = err.Error()
	}
	return selection, nil
}

func limitedReleaseNotes(notes string) string {
	const maximum = 16 << 10
	if len(notes) > maximum {
		return notes[:maximum]
	}
	return notes
}

func validateReleaseAssetURL(raw, tag, name string) error {
	parsed, err := url.Parse(raw)
	if err != nil || parsed.Scheme != "https" || parsed.Host != "github.com" || parsed.User != nil || parsed.RawQuery != "" || parsed.Fragment != "" {
		return fmt.Errorf("OTA asset %q has an invalid GitHub URL", name)
	}
	expectedPath := "/" + githubRepository + "/releases/download/" + tag + "/" + name
	if parsed.EscapedPath() != expectedPath {
		return fmt.Errorf("OTA asset %q URL does not match its release", name)
	}
	return nil
}

func getJSON(ctx context.Context, client *http.Client, rawURL string, limit int64, target any) error {
	data, err := getBytes(ctx, client, rawURL, limit)
	if err != nil {
		return err
	}
	// GitHub adds fields frequently, so strictness belongs to the signed Ardor
	// manifest rather than the upstream release envelope.
	decoder := json.NewDecoder(bytes.NewReader(data))
	if err := decoder.Decode(target); err != nil {
		return err
	}
	return nil
}

func getBytes(ctx context.Context, client *http.Client, rawURL string, limit int64) ([]byte, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return nil, err
	}
	request.Header.Set("Accept", "application/vnd.github+json")
	request.Header.Set("X-GitHub-Api-Version", "2022-11-28")
	request.Header.Set("User-Agent", "ardor-device-updater/"+UpdaterVersion)
	response, err := client.Do(request)
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, 4<<10))
		return nil, fmt.Errorf("unexpected HTTP status %d", response.StatusCode)
	}
	reader := io.LimitReader(response.Body, limit+1)
	data, err := io.ReadAll(reader)
	if err != nil {
		return nil, err
	}
	if int64(len(data)) > limit {
		return nil, errors.New("response exceeds size limit")
	}
	return data, nil
}
