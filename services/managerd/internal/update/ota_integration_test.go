package update

// Apply runs against real temporary release trees and shell services. Only
// GitHub downloads and device health are simulated; no device is contacted.
import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

type otaFixture struct {
	root, request, log, telemetry, keyPath string
	bundle                                 []byte
	selection                              Selection
	installer                              Installer
}

func newOTAFixture(t *testing.T) *otaFixture {
	t.Helper()
	f := &otaFixture{root: t.TempDir()}
	f.log = filepath.Join(f.root, "services.log")
	f.telemetry = filepath.Join(f.root, "telemetry")
	files := map[string][]byte{"bin/ardor-pedal": []byte("candidate pedal"), "bin/ardor-managerd": []byte("candidate manager")}
	manifest := validManifest()
	for i := range manifest.Files {
		b := files[manifest.Files[i].Path]
		sum := sha256.Sum256(b)
		manifest.Files[i].Size = int64(len(b))
		manifest.Files[i].SHA256 = hex.EncodeToString(sum[:])
	}
	bundlePath := filepath.Join(f.root, "bundle.tar.gz")
	writeTestBundle(t, bundlePath, files)
	f.bundle, _ = os.ReadFile(bundlePath)
	sum := sha256.Sum256(f.bundle)
	manifest.Bundle.Size = int64(len(f.bundle))
	manifest.Bundle.SHA256 = hex.EncodeToString(sum[:])
	raw, _ := json.MarshalIndent(manifest, "", "  ")
	raw = append(raw, '\n')
	pub, priv, _ := ed25519.GenerateKey(rand.Reader)
	f.keyPath = filepath.Join(f.root, "update.pub")
	if err := os.WriteFile(f.keyPath, []byte(base64.StdEncoding.EncodeToString(pub)), 0600); err != nil {
		t.Fatal(err)
	}
	f.selection = Selection{Version: manifest.Version, Tag: manifest.Tag, BundleURL: assetURL(manifest.Tag, manifest.Bundle.Name), Manifest: raw, Signature: base64.StdEncoding.EncodeToString(ed25519.Sign(priv, raw)), BundleSize: manifest.Bundle.Size, BundleSHA256: manifest.Bundle.SHA256}
	f.request = filepath.Join(f.root, "request.json")
	if err := writeJSONAtomic(f.request, InstallRequest{SchemaVersion: 1, Selection: f.selection, PublicKeyPath: f.keyPath, SystemRoot: f.root, InstalledVersion: "0.1.23", BaseVersion: "0.1.23"}, 0600); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"pedal", "manager", "pidof"} {
		script := "#!/bin/sh\nexit 0\n"
		if name != "pidof" {
			script = "#!/bin/sh\necho '" + name + " ' $1 >> '" + f.log + "'\n"
		}
		if err := os.WriteFile(filepath.Join(f.root, name), []byte(script), 0755); err != nil {
			t.Fatal(err)
		}
	}
	t.Setenv("PATH", f.root+":"+os.Getenv("PATH"))
	f.installer = Installer{PedalService: filepath.Join(f.root, "pedal"), ManagerService: filepath.Join(f.root, "manager"), TelemetryPath: f.telemetry, ValidationDelay: 150 * time.Millisecond}
	f.installer.HTTPClient = &http.Client{Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
		if err := r.Context().Err(); err != nil {
			return nil, err
		}
		body := string(f.bundle)
		if r.URL.Path == "/api/device" {
			body = `{"softwareVersion":"0.1.24"}`
			_ = os.WriteFile(f.telemetry, []byte("live"), 0600)
		}
		return &http.Response{StatusCode: 200, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(body))}, nil
	})}
	return f
}
func (f *otaFixture) status(t *testing.T) Status {
	t.Helper()
	var s Status
	if err := readJSONFile(filepath.Join(f.root, "update/operation.json"), 256<<10, &s); err != nil {
		t.Fatal(err)
	}
	return s
}
func TestOTAApplyLifecycle(t *testing.T) {
	for _, scenario := range []string{"healthy", "unhealthy_factory", "unhealthy_existing", "download_corrupt", "restart_failure", "cancelled"} {
		t.Run(scenario, func(t *testing.T) {
			f := newOTAFixture(t)
			existing := scenario == "unhealthy_existing"
			if existing {
				_ = os.MkdirAll(filepath.Join(f.root, "releases/0.1.23"), 0755)
				if err := os.Symlink("releases/0.1.23", filepath.Join(f.root, "current")); err != nil {
					t.Fatal(err)
				}
			}
			if scenario == "healthy" {
				f.installer.ValidationDelay = 13 * time.Second
			}
			if scenario == "download_corrupt" {
				f.bundle[0] ^= 1
			}
			if scenario == "restart_failure" {
				if err := os.WriteFile(f.installer.ManagerService, []byte("#!/bin/sh\n[ \"$1\" != start ]\n"), 0755); err != nil {
					t.Fatal(err)
				}
			}
			ctx := context.Background()
			if scenario == "cancelled" {
				var cancel context.CancelFunc
				ctx, cancel = context.WithCancel(ctx)
				cancel()
			}
			err := f.installer.Apply(ctx, f.request)
			if err != nil {
				t.Logf("Apply error: %v", err)
			}
			status := f.status(t)
			current, linkErr := os.Readlink(filepath.Join(f.root, "current"))
			if scenario == "healthy" {
				if err != nil || status.State != "succeeded" || current != "releases/0.1.24" {
					t.Fatalf("err=%v status=%+v current=%q", err, status, current)
				}
				if _, err := os.Stat(filepath.Join(f.root, "update/transaction.json")); !os.IsNotExist(err) {
					t.Fatalf("transaction remains: %v", err)
				}
				log, _ := os.ReadFile(f.log)
				if string(log) != "pedal  stop\nmanager  stop\nmanager  start\npedal  start\n" {
					t.Fatalf("service order: %q", log)
				}
			} else {
				if err == nil {
					t.Fatal("failed scenario succeeded")
				}
				if scenario == "download_corrupt" || scenario == "cancelled" {
					if status.ErrorCode != "download_failed" {
						t.Fatalf("status=%+v", status)
					}
				} else if status.State != "rolled_back" {
					t.Fatalf("err=%v status=%+v", err, status)
				}
				if existing {
					if current != "releases/0.1.23" {
						t.Fatalf("previous release not restored: %q", current)
					}
				} else if !os.IsNotExist(linkErr) {
					t.Fatalf("factory fallback not restored: %q %v", current, linkErr)
				}
			}
			if _, err := os.Stat(filepath.Join(f.root, "update/lock")); !os.IsNotExist(err) {
				t.Fatalf("lock leaked: %v", err)
			}
		})
	}
}
func TestOTACheckCannotOverwriteActiveInstall(t *testing.T) {
	f := newOTAFixture(t)
	m := NewManager(ManagerConfig{SystemRoot: f.root, PublicKeyPath: f.keyPath, InstalledVersion: "0.1.23", BaseVersion: "0.1.23", ReleaseClient: ReleaseClient{HTTPClient: &http.Client{Transport: roundTripFunc(func(*http.Request) (*http.Response, error) {
		t.Error("discovery must not run during installation")
		return nil, context.Canceled
	})}}})
	for _, state := range []string{"downloading", "verifying", "staged", "restarting", "validating"} {
		t.Run(state, func(t *testing.T) {
			setOperationState(f.root, state, "", "")
			status, err := m.Check(context.Background())
			if err == nil || status.State != state || f.status(t).State != state {
				t.Fatalf("active installation overwritten: err=%v status=%+v disk=%+v", err, status, f.status(t))
			}
		})
	}
}

func TestOTASignedManifestSurvivesPersistence(t *testing.T) {
	f := newOTAFixture(t)
	request, _, _, err := loadInstallRequest(f.request)
	if err != nil {
		t.Fatal(err)
	}
	if string(request.Selection.Manifest) != string(f.selection.Manifest) {
		t.Fatal("signed bytes changed in request")
	}
	m := NewManager(ManagerConfig{SystemRoot: f.root, PublicKeyPath: f.keyPath, InstalledVersion: "0.1.23", BaseVersion: "0.1.23"})
	m.status.State = "available"
	m.status.Available = &f.selection
	if err := m.persistLocked(); err != nil {
		t.Fatal(err)
	}
	m = NewManager(ManagerConfig{SystemRoot: f.root, PublicKeyPath: f.keyPath, InstalledVersion: "0.1.23", BaseVersion: "0.1.23"})
	_, _, err = m.PrepareInstall("0.1.24")
	if err != nil {
		t.Fatal(err)
	}
	if _, _, _, err := loadInstallRequest(filepath.Join(f.root, "update/install-request.json")); err != nil {
		t.Fatalf("persisted check -> restarted manager -> install: %v", err)
	}
}

func TestOTAInvalidRequestReportsFailure(t *testing.T) {
	f := newOTAFixture(t)
	var r InstallRequest
	if err := readJSONFile(f.request, 512<<10, &r); err != nil {
		t.Fatal(err)
	}
	r.Selection.Signature = base64.StdEncoding.EncodeToString(make([]byte, ed25519.SignatureSize))
	if err := writeJSONAtomic(f.request, r, 0600); err != nil {
		t.Fatal(err)
	}
	setOperationState(f.root, "downloading", "", "")
	if err := f.installer.Apply(context.Background(), f.request); err == nil {
		t.Fatal("invalid signature accepted")
	}
	if s := f.status(t); s.State != "failed" || s.ErrorMessage == "" {
		t.Fatalf("stuck launch state: %+v", s)
	}
}

func TestOTABusyInstallerDoesNotOverwriteOwnerStatus(t *testing.T) {
	f := newOTAFixture(t)
	setOperationState(f.root, "validating", "", "")
	if err := os.WriteFile(filepath.Join(f.root, "update/lock"), nil, 0600); err != nil {
		t.Fatal(err)
	}
	if err := f.installer.Apply(context.Background(), f.request); err == nil {
		t.Fatal("concurrent installer accepted")
	}
	if s := f.status(t); s.State != "validating" {
		t.Fatalf("owner state overwritten: %+v", s)
	}
}

func TestOTADownloadFailureMatrix(t *testing.T) {
	for _, scenario := range []string{"http_error", "truncated", "extra_bytes", "wrong_hash", "cancelled", "too_large"} {
		t.Run(scenario, func(t *testing.T) {
			f := newOTAFixture(t)
			var manifest Manifest
			if err := json.Unmarshal(f.selection.Manifest, &manifest); err != nil {
				t.Fatal(err)
			}
			data := f.bundle
			status := http.StatusOK
			switch scenario {
			case "http_error":
				status = 503
			case "truncated":
				data = data[:len(data)-1]
			case "extra_bytes":
				data = append(data, 'x')
			case "wrong_hash":
				manifest.Bundle.SHA256 = strings.Repeat("0", 64)
			case "too_large":
				manifest.Bundle.Size = maxBundleBytes + 1
			}
			client := &http.Client{Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
				if err := r.Context().Err(); err != nil {
					return nil, err
				}
				return &http.Response{StatusCode: status, Body: io.NopCloser(strings.NewReader(string(data))), Header: make(http.Header)}, nil
			})}
			ctx := context.Background()
			if scenario == "cancelled" {
				var cancel context.CancelFunc
				ctx, cancel = context.WithCancel(ctx)
				cancel()
			}
			destination := filepath.Join(f.root, "download.part")
			if err := (Installer{HTTPClient: client}).downloadBundle(ctx, f.selection.BundleURL, destination, manifest.Bundle); err == nil {
				t.Fatal("bad download accepted")
			}
			if _, err := os.Stat(destination); !os.IsNotExist(err) {
				t.Fatalf("partial download leaked: %v", err)
			}
		})
	}
}

func TestOTAPrepareInstallGuardsAndLaunchFailure(t *testing.T) {
	f := newOTAFixture(t)
	m := NewManager(ManagerConfig{SystemRoot: f.root, PublicKeyPath: f.keyPath, InstalledVersion: "0.1.23", BaseVersion: "0.1.23", UpdaterExecutable: filepath.Join(f.root, "missing-updater")})
	if _, _, err := m.PrepareInstall("0.1.24"); err == nil {
		t.Fatal("unchecked update accepted")
	}
	m.status.State = "available"
	m.status.Available = &f.selection
	if err := m.persistLocked(); err != nil {
		t.Fatal(err)
	}
	if _, _, err := m.PrepareInstall("0.1.25"); err == nil {
		t.Fatal("wrong version accepted")
	}
	m.status.Available.ReflashRequired = true
	if err := m.persistLocked(); err != nil {
		t.Fatal(err)
	}
	if _, _, err := m.PrepareInstall("0.1.24"); err == nil {
		t.Fatal("incompatible release accepted")
	}
	m.status.Available.ReflashRequired = false
	if err := m.persistLocked(); err != nil {
		t.Fatal(err)
	}
	_, launch, err := m.PrepareInstall("0.1.24")
	if err != nil {
		t.Fatal(err)
	}
	if _, _, err := m.PrepareInstall("0.1.24"); err == nil {
		t.Fatal("duplicate install accepted")
	}
	if err := launch(); err == nil {
		t.Fatal("missing updater launched")
	}
	if s := m.Status(); s.State != "failed" || s.ErrorCode != "updater_start_failed" {
		t.Fatalf("launch failure invisible: %+v", s)
	}
}
