# Manager Daemon And Tauri App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a REST-managed asset and preset workflow for Ardor, with a Go `ardor-managerd` daemon on the pedal and a Mac-first Tauri desktop manager app.

**Architecture:** Keep realtime audio/UI in the existing C++ pedal process. Add a separate Go REST daemon under `services/managerd` that owns HTTP, auth, uploads, preset JSON validation, and atomic file writes according to `docs/api/ardor-managerd.openapi.yaml`. Add a Tauri v2 + React + TypeScript + Tailwind/DaisyUI desktop app under `apps/manager` that edits presets offline and syncs through the daemon.

**Tech Stack:** Go 1.22+ standard library, Buildroot `golang-package`, SysV init, Tauri v2, React, TypeScript, Vite, Tailwind CSS, DaisyUI, Vitest.

## Global Constraints

- macOS is first-class for desktop development and packaging.
- Windows is second; keep client code platform-neutral.
- The device API contract lives in `docs/api/ardor-managerd.openapi.yaml`.
- `ardor-managerd` is a separate process from realtime audio and LVGL.
- The daemon must not parse JSON, read assets, allocate DSP buffers, or perform blocking management work inside the realtime audio callback.
- Preset edits in the Tauri app are offline until save/sync.
- Auth is enabled by default and can be disabled for testing with `ARDOR_API_AUTH=off`.
- `GET /api/device` reports `authEnabled`.
- Bank range is `0..99`; slot range is `0..3`.
- Preset asset paths must be relative and must not contain `..`.
- Preset writes and asset uploads use temporary files before final rename.
- Unknown preset fields and unknown block fields are preserved where practical.
- Use only Go standard library packages for the daemon in V1.
- Do not modify unrelated dirty files in the existing worktree.

---

## File Structure

Create Go daemon files:

- `services/managerd/go.mod`
- `services/managerd/cmd/ardor-managerd/main.go`
- `services/managerd/internal/config/config.go`
- `services/managerd/internal/assets/assets.go`
- `services/managerd/internal/presets/presets.go`
- `services/managerd/internal/server/server.go`
- `services/managerd/internal/server/server_test.go`
- `services/managerd/internal/assets/assets_test.go`
- `services/managerd/internal/presets/presets_test.go`

Create Buildroot package files:

- `buildroot/external/package/ardor-managerd/Config.in`
- `buildroot/external/package/ardor-managerd/ardor-managerd.mk`
- `buildroot/external/package/ardor-managerd/S98ardor-managerd`
- `buildroot/external/board/ardor-pedal/rootfs-overlay/etc/ardor-managerd.env`

Modify Buildroot integration:

- `buildroot/external/Config.in`
- `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`

Create Tauri app files:

- `apps/manager/package.json`
- `apps/manager/index.html`
- `apps/manager/vite.config.ts`
- `apps/manager/tsconfig.json`
- `apps/manager/postcss.config.cjs`
- `apps/manager/tailwind.config.cjs`
- `apps/manager/src-tauri/Cargo.toml`
- `apps/manager/src-tauri/tauri.conf.json`
- `apps/manager/src-tauri/src/main.rs`
- `apps/manager/src/main.tsx`
- `apps/manager/src/App.tsx`
- `apps/manager/src/api/client.ts`
- `apps/manager/src/api/types.ts`
- `apps/manager/src/presets/presetDraft.ts`
- `apps/manager/src/presets/presetDraft.test.ts`
- `apps/manager/src/api/client.test.ts`
- `apps/manager/src/styles.css`

Modify docs:

- `README.md`

---

### Task 1: Add Go Manager Daemon Core

**Files:**
- Create: `services/managerd/go.mod`
- Create: `services/managerd/internal/config/config.go`
- Create: `services/managerd/internal/assets/assets.go`
- Create: `services/managerd/internal/assets/assets_test.go`
- Create: `services/managerd/internal/presets/presets.go`
- Create: `services/managerd/internal/presets/presets_test.go`

**Interfaces:**
- Produces:
  - `config.LoadFromEnv() (config.Config, error)`
  - `assets.Store`
  - `assets.Store.List(kind assets.Kind) ([]assets.Info, error)`
  - `assets.Store.Save(kind assets.Kind, filename string, r io.Reader, overwrite bool) (assets.Info, error)`
  - `assets.Store.Delete(kind assets.Kind, id string) error`
  - `assets.SanitizeFilename(filename string, kind assets.Kind) (string, error)`
  - `presets.Store`
  - `presets.Store.List() ([]presets.Summary, error)`
  - `presets.Store.Load(bank int, slot int) (presets.Slot, error)`
  - `presets.Store.Save(bank int, slot int, preset presets.Preset) (presets.Slot, error)`

- [ ] **Step 1: Create the Go module**

Create `services/managerd/go.mod`:

```go
module ardor.local/managerd

go 1.22
```

- [ ] **Step 2: Write failing asset tests**

Create `services/managerd/internal/assets/assets_test.go`:

```go
package assets

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSanitizeFilename(t *testing.T) {
	got, err := SanitizeFilename("../../Clean Amp!.nam", KindModel)
	if err != nil {
		t.Fatal(err)
	}
	if got != "Clean_Amp_.nam" {
		t.Fatalf("got %q", got)
	}

	got, err = SanitizeFilename("Room 1.WAV", KindIR)
	if err != nil {
		t.Fatal(err)
	}
	if got != "Room_1.wav" {
		t.Fatalf("got %q", got)
	}

	if _, err := SanitizeFilename("cab.wav", KindModel); err == nil {
		t.Fatal("expected wrong extension to fail")
	}
}

func TestStoreSaveListDelete(t *testing.T) {
	root := t.TempDir()
	store := NewStore(root)

	info, err := store.Save(KindModel, "Clean Amp!.nam", strings.NewReader("nam-bytes"), false)
	if err != nil {
		t.Fatal(err)
	}
	if info.ID != "Clean_Amp_.nam" || info.Path != "models/Clean_Amp_.nam" || info.Kind != "model" {
		t.Fatalf("bad info: %+v", info)
	}
	if _, err := os.Stat(filepath.Join(root, "models", "Clean_Amp_.nam")); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, "models", "Clean_Amp_.nam.tmp")); !os.IsNotExist(err) {
		t.Fatalf("tmp file should be gone, err=%v", err)
	}

	if _, err := store.Save(KindModel, "Clean Amp!.nam", strings.NewReader("new"), false); err == nil {
		t.Fatal("duplicate without overwrite should fail")
	}
	if _, err := store.Save(KindModel, "Clean Amp!.nam", strings.NewReader("new"), true); err != nil {
		t.Fatal(err)
	}

	if _, err := store.Save(KindIR, "Open Back.wav", strings.NewReader("wav"), false); err != nil {
		t.Fatal(err)
	}
	models, err := store.List(KindModel)
	if err != nil {
		t.Fatal(err)
	}
	if len(models) != 1 {
		t.Fatalf("models len=%d", len(models))
	}
	irs, err := store.List(KindIR)
	if err != nil {
		t.Fatal(err)
	}
	if len(irs) != 1 || irs[0].Path != "irs/Open_Back.wav" {
		t.Fatalf("bad irs: %+v", irs)
	}

	if err := store.Delete(KindModel, "Clean_Amp_.nam"); err != nil {
		t.Fatal(err)
	}
	models, err = store.List(KindModel)
	if err != nil {
		t.Fatal(err)
	}
	if len(models) != 0 {
		t.Fatalf("models len after delete=%d", len(models))
	}
}
```

- [ ] **Step 3: Write failing preset tests**

Create `services/managerd/internal/presets/presets_test.go`:

```go
package presets

import (
	"os"
	"path/filepath"
	"testing"
)

func validPreset() Preset {
	return Preset{
		"version": float64(1),
		"name":    "Clean",
		"routing": "serial",
		"global": map[string]any{
			"inputGainDb":   float64(0),
			"outputGainDb":  float64(0),
			"safetyLimitDb": float64(-1),
		},
		"blocks": []any{
			map[string]any{
				"id":      "block-1",
				"type":    "nam",
				"enabled": true,
				"asset":   "models/clean.nam",
				"params":  map[string]any{},
				"future":  "preserve",
			},
		},
		"futureRoot": true,
	}
}

func TestValidatePreset(t *testing.T) {
	if err := Validate(validPreset()); err != nil {
		t.Fatal(err)
	}

	badRouting := validPreset()
	badRouting["routing"] = "parallel"
	if err := Validate(badRouting); err == nil {
		t.Fatal("parallel routing should fail")
	}

	badAsset := validPreset()
	badAsset["blocks"].([]any)[0].(map[string]any)["asset"] = "../escape.nam"
	if err := Validate(badAsset); err == nil {
		t.Fatal("traversal asset should fail")
	}
}

func TestStoreSaveLoadList(t *testing.T) {
	root := t.TempDir()
	store := NewStore(root)
	preset := validPreset()

	slot, err := store.Save(2, 3, preset)
	if err != nil {
		t.Fatal(err)
	}
	if slot.Bank != 2 || slot.Slot != 3 {
		t.Fatalf("bad slot: %+v", slot)
	}
	path := filepath.Join(root, "presets", "bank-002", "preset-3.json")
	if _, err := os.Stat(path); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(path + ".tmp"); !os.IsNotExist(err) {
		t.Fatalf("tmp should be gone, err=%v", err)
	}

	loaded, err := store.Load(2, 3)
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Preset["futureRoot"] != true {
		t.Fatalf("future root field not preserved: %+v", loaded.Preset)
	}

	summaries, err := store.List()
	if err != nil {
		t.Fatal(err)
	}
	if len(summaries) != 400 {
		t.Fatalf("summary len=%d", len(summaries))
	}
	found := summaries[2*4+3]
	if !found.Exists || found.Name != "Clean" {
		t.Fatalf("bad summary: %+v", found)
	}
}
```

- [ ] **Step 4: Run tests to verify they fail**

Run:

```bash
cd services/managerd
go test ./...
```

Expected: tests fail because `assets` and `presets` implementations do not exist.

- [ ] **Step 5: Implement config**

Create `services/managerd/internal/config/config.go`:

```go
package config

import (
	"errors"
	"os"
	"strconv"
)

type Config struct {
	DataRoot    string
	Bind        string
	Port        int
	AuthEnabled bool
	Token       string
}

func LoadFromEnv() (Config, error) {
	cfg := Config{
		DataRoot:    env("ARDOR_DATA_ROOT", "/opt/ardor-pedal"),
		Bind:        env("ARDOR_API_BIND", "0.0.0.0"),
		AuthEnabled: env("ARDOR_API_AUTH", "on") != "off",
		Token:       os.Getenv("ARDOR_API_TOKEN"),
	}
	port, err := strconv.Atoi(env("ARDOR_API_PORT", "8080"))
	if err != nil {
		return Config{}, err
	}
	cfg.Port = port
	if cfg.AuthEnabled && cfg.Token == "" {
		return Config{}, errors.New("ARDOR_API_TOKEN is required when auth is enabled")
	}
	return cfg, nil
}

func env(key string, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}
```

- [ ] **Step 6: Implement asset storage**

Create `services/managerd/internal/assets/assets.go`:

```go
package assets

import (
	"errors"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"unicode"
)

type Kind string

const (
	KindModel Kind = "model"
	KindIR    Kind = "ir"
)

type Info struct {
	ID        string `json:"id"`
	Kind      string `json:"kind"`
	Filename  string `json:"filename"`
	Path      string `json:"path"`
	SizeBytes int64  `json:"sizeBytes"`
}

type Store struct {
	root string
}

func NewStore(root string) Store {
	return Store{root: root}
}

func SanitizeFilename(filename string, kind Kind) (string, error) {
	base := filepath.Base(strings.ReplaceAll(filename, "\\", "/"))
	ext := strings.ToLower(filepath.Ext(base))
	if ext == "" {
		return "", errors.New("asset filename must include extension")
	}
	if ext != extension(kind) {
		return "", errors.New("asset extension does not match endpoint")
	}
	stem := strings.TrimSuffix(base, filepath.Ext(base))
	var out strings.Builder
	for _, r := range stem {
		if unicode.IsLetter(r) || unicode.IsDigit(r) || r == '_' || r == '-' || r == '.' {
			out.WriteRune(r)
		} else {
			out.WriteByte('_')
		}
	}
	clean := out.String()
	if clean == "" || clean == "." || clean == ".." {
		return "", errors.New("asset filename is empty after sanitization")
	}
	return clean + ext, nil
}

func (s Store) List(kind Kind) ([]Info, error) {
	dir := s.dir(kind)
	entries, err := os.ReadDir(dir)
	if errors.Is(err, os.ErrNotExist) {
		return []Info{}, nil
	}
	if err != nil {
		return nil, err
	}
	var infos []Info
	for _, entry := range entries {
		if entry.IsDir() || strings.ToLower(filepath.Ext(entry.Name())) != extension(kind) {
			continue
		}
		stat, err := entry.Info()
		if err != nil {
			return nil, err
		}
		infos = append(infos, Info{
			ID:        entry.Name(),
			Kind:      jsonKind(kind),
			Filename:  entry.Name(),
			Path:      s.rel(kind, entry.Name()),
			SizeBytes: stat.Size(),
		})
	}
	sort.Slice(infos, func(i, j int) bool { return infos[i].Filename < infos[j].Filename })
	return infos, nil
}

func (s Store) Save(kind Kind, filename string, r io.Reader, overwrite bool) (Info, error) {
	safe, err := SanitizeFilename(filename, kind)
	if err != nil {
		return Info{}, err
	}
	dir := s.dir(kind)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return Info{}, err
	}
	finalPath := filepath.Join(dir, safe)
	tmpPath := finalPath + ".tmp"
	if !overwrite {
		if _, err := os.Stat(finalPath); err == nil {
			return Info{}, errors.New("asset already exists")
		}
	}
	tmp, err := os.OpenFile(tmpPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return Info{}, err
	}
	if _, err := io.Copy(tmp, r); err != nil {
		_ = tmp.Close()
		return Info{}, err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		return Info{}, err
	}
	if err := tmp.Close(); err != nil {
		return Info{}, err
	}
	if err := os.Rename(tmpPath, finalPath); err != nil {
		return Info{}, err
	}
	stat, err := os.Stat(finalPath)
	if err != nil {
		return Info{}, err
	}
	return Info{ID: safe, Kind: jsonKind(kind), Filename: safe, Path: s.rel(kind, safe), SizeBytes: stat.Size()}, nil
}

func (s Store) Delete(kind Kind, id string) error {
	if id == "" || strings.Contains(id, "/") || strings.Contains(id, "\\") || strings.Contains(id, "..") {
		return errors.New("invalid asset id")
	}
	return os.Remove(filepath.Join(s.dir(kind), id))
}

func (s Store) dir(kind Kind) string {
	if kind == KindModel {
		return filepath.Join(s.root, "models")
	}
	return filepath.Join(s.root, "irs")
}

func (s Store) rel(kind Kind, name string) string {
	if kind == KindModel {
		return "models/" + name
	}
	return "irs/" + name
}

func extension(kind Kind) string {
	if kind == KindModel {
		return ".nam"
	}
	return ".wav"
}

func jsonKind(kind Kind) string {
	if kind == KindModel {
		return "model"
	}
	return "ir"
}
```

- [ ] **Step 7: Implement preset storage**

Create `services/managerd/internal/presets/presets.go`:

```go
package presets

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"
)

type Preset map[string]any

type Slot struct {
	Bank   int    `json:"bank"`
	Slot   int    `json:"slot"`
	Preset Preset `json:"preset"`
}

type Summary struct {
	Bank   int    `json:"bank"`
	Slot   int    `json:"slot"`
	Exists bool   `json:"exists"`
	Name   string `json:"name,omitempty"`
}

type Store struct {
	root string
}

func NewStore(root string) Store {
	return Store{root: root}
}

func Validate(preset Preset) error {
	if version, ok := preset["version"].(float64); !ok || version != 1 {
		return errors.New("preset version must be 1")
	}
	if routing, ok := preset["routing"].(string); !ok || routing != "serial" {
		return errors.New("preset routing must be serial")
	}
	if _, ok := preset["global"].(map[string]any); !ok {
		return errors.New("preset global must be an object")
	}
	blocks, ok := preset["blocks"].([]any)
	if !ok {
		return errors.New("preset blocks must be an array")
	}
	for _, item := range blocks {
		block, ok := item.(map[string]any)
		if !ok {
			return errors.New("preset block must be an object")
		}
		asset, _ := block["asset"].(string)
		if asset != "" && !validRelativeAsset(asset) {
			return errors.New("preset asset must stay under data root")
		}
	}
	return nil
}

func (s Store) List() ([]Summary, error) {
	out := make([]Summary, 0, 400)
	for bank := 0; bank < 100; bank++ {
		for slot := 0; slot < 4; slot++ {
			summary := Summary{Bank: bank, Slot: slot}
			presetSlot, err := s.Load(bank, slot)
			if err == nil {
				summary.Exists = true
				if name, ok := presetSlot.Preset["name"].(string); ok {
					summary.Name = name
				}
			} else if !errors.Is(err, os.ErrNotExist) {
				summary.Exists = true
			}
			out = append(out, summary)
		}
	}
	return out, nil
}

func (s Store) Load(bank int, slot int) (Slot, error) {
	if err := validateSlot(bank, slot); err != nil {
		return Slot{}, err
	}
	bytes, err := os.ReadFile(s.pathFor(bank, slot))
	if err != nil {
		return Slot{}, err
	}
	var preset Preset
	if err := json.Unmarshal(bytes, &preset); err != nil {
		return Slot{}, err
	}
	if err := Validate(preset); err != nil {
		return Slot{}, err
	}
	return Slot{Bank: bank, Slot: slot, Preset: preset}, nil
}

func (s Store) Save(bank int, slot int, preset Preset) (Slot, error) {
	if err := validateSlot(bank, slot); err != nil {
		return Slot{}, err
	}
	if err := Validate(preset); err != nil {
		return Slot{}, err
	}
	finalPath := s.pathFor(bank, slot)
	if err := os.MkdirAll(filepath.Dir(finalPath), 0o755); err != nil {
		return Slot{}, err
	}
	tmpPath := finalPath + ".tmp"
	bytes, err := json.MarshalIndent(preset, "", "  ")
	if err != nil {
		return Slot{}, err
	}
	tmp, err := os.OpenFile(tmpPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return Slot{}, err
	}
	if _, err := tmp.Write(append(bytes, '\n')); err != nil {
		_ = tmp.Close()
		return Slot{}, err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		return Slot{}, err
	}
	if err := tmp.Close(); err != nil {
		return Slot{}, err
	}
	if err := os.Rename(tmpPath, finalPath); err != nil {
		return Slot{}, err
	}
	return Slot{Bank: bank, Slot: slot, Preset: preset}, nil
}

func (s Store) pathFor(bank int, slot int) string {
	return filepath.Join(s.root, "presets", fmt.Sprintf("bank-%03d", bank), fmt.Sprintf("preset-%d.json", slot))
}

func validateSlot(bank int, slot int) error {
	if bank < 0 || bank > 99 || slot < 0 || slot > 3 {
		return errors.New("preset slot out of range")
	}
	return nil
}

func validRelativeAsset(asset string) bool {
	if filepath.IsAbs(asset) || strings.Contains(asset, "\\") {
		return false
	}
	clean := path.Clean(asset)
	if clean == "." || strings.HasPrefix(clean, "../") || clean == ".." {
		return false
	}
	return clean == asset
}
```

- [ ] **Step 8: Run core tests**

Run:

```bash
cd services/managerd
gofmt -w .
go test ./...
```

Expected: all Go tests pass.

- [ ] **Step 9: Commit**

```bash
git add services/managerd
git commit -m "feat: add go manager daemon core"
```

---

### Task 2: Add Go REST API Server

**Files:**
- Create: `services/managerd/internal/server/server.go`
- Create: `services/managerd/internal/server/server_test.go`
- Create: `services/managerd/cmd/ardor-managerd/main.go`

**Interfaces:**
- Consumes: `config.Config`, `assets.Store`, `presets.Store`.
- Produces:
  - `server.New(config.Config) http.Handler`
  - executable command `cmd/ardor-managerd`

- [ ] **Step 1: Write failing server tests**

Create `services/managerd/internal/server/server_test.go`:

```go
package server

import (
	"bytes"
	"encoding/json"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"testing"

	"ardor.local/managerd/internal/config"
)

func TestDeviceAndAuth(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: true, Token: "secret"})

	device := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/device", nil)
	handler.ServeHTTP(device, req)
	if device.Code != http.StatusOK {
		t.Fatalf("device status=%d", device.Code)
	}
	if !bytes.Contains(device.Body.Bytes(), []byte(`"authEnabled":true`)) {
		t.Fatalf("device body=%s", device.Body.String())
	}

	unauthorized := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodGet, "/api/assets/models", nil)
	handler.ServeHTTP(unauthorized, req)
	if unauthorized.Code != http.StatusUnauthorized {
		t.Fatalf("unauthorized status=%d", unauthorized.Code)
	}
}

func TestAssetUploadPresetSaveAndApply(t *testing.T) {
	handler := New(config.Config{DataRoot: t.TempDir(), AuthEnabled: true, Token: "secret"})

	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	part, err := writer.CreateFormFile("file", "Clean Amp.nam")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := part.Write([]byte("nam-bytes")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}

	upload := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPost, "/api/assets/models", body)
	req.Header.Set("Content-Type", writer.FormDataContentType())
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(upload, req)
	if upload.Code != http.StatusCreated {
		t.Fatalf("upload status=%d body=%s", upload.Code, upload.Body.String())
	}
	if !bytes.Contains(upload.Body.Bytes(), []byte(`models/Clean_Amp.nam`)) {
		t.Fatalf("upload body=%s", upload.Body.String())
	}

	preset := map[string]any{
		"version": float64(1),
		"name":    "HTTP Preset",
		"routing": "serial",
		"global": map[string]any{
			"inputGainDb":   float64(0),
			"outputGainDb":  float64(0),
			"safetyLimitDb": float64(-1),
		},
		"blocks": []any{
			map[string]any{"id": "block-1", "type": "nam", "enabled": true, "asset": "models/Clean_Amp.nam", "params": map[string]any{}},
		},
	}
	presetBytes, _ := json.Marshal(preset)
	save := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodPut, "/api/presets/banks/0/slots/0", bytes.NewReader(presetBytes))
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(save, req)
	if save.Code != http.StatusOK {
		t.Fatalf("save status=%d body=%s", save.Code, save.Body.String())
	}

	get := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodGet, "/api/presets/banks/0/slots/0", nil)
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(get, req)
	if get.Code != http.StatusOK || !bytes.Contains(get.Body.Bytes(), []byte("HTTP Preset")) {
		t.Fatalf("get status=%d body=%s", get.Code, get.Body.String())
	}

	apply := httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodPost, "/api/presets/banks/0/slots/0/apply", nil)
	req.Header.Set("Authorization", "Bearer secret")
	handler.ServeHTTP(apply, req)
	if apply.Code != http.StatusAccepted {
		t.Fatalf("apply status=%d body=%s", apply.Code, apply.Body.String())
	}
}
```

- [ ] **Step 2: Run server tests to verify they fail**

Run:

```bash
cd services/managerd
go test ./internal/server
```

Expected: package fails because `New` does not exist.

- [ ] **Step 3: Implement HTTP server**

Create `services/managerd/internal/server/server.go`:

```go
package server

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"strings"

	"ardor.local/managerd/internal/assets"
	"ardor.local/managerd/internal/config"
	"ardor.local/managerd/internal/presets"
)

type errorResponse struct {
	Error   string         `json:"error"`
	Message string         `json:"message"`
	Details map[string]any `json:"details,omitempty"`
}

func New(cfg config.Config) http.Handler {
	mux := http.NewServeMux()
	assetStore := assets.NewStore(cfg.DataRoot)
	presetStore := presets.NewStore(cfg.DataRoot)

	mux.HandleFunc("GET /api/device", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{
			"deviceName":             "Ardor Pedal",
			"apiVersion":             "0.1.0",
			"authEnabled":            cfg.AuthEnabled,
			"dataRootWritable":       true,
			"maxBanks":               100,
			"slotsPerBank":           4,
			"supportedPresetVersion": 1,
			"capabilities": map[string]bool{
				"modelUpload": true,
				"irUpload":    true,
				"presetRead":  true,
				"presetWrite": true,
				"presetApply": true,
			},
		})
	})

	mux.HandleFunc("GET /api/assets/{kind}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		items, err := assetStore.List(kind)
		if err != nil {
			writeError(w, http.StatusBadRequest, "asset_list_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"assets": items})
	})

	mux.HandleFunc("POST /api/assets/{kind}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		file, header, err := r.FormFile("file")
		if err != nil {
			writeError(w, http.StatusBadRequest, "missing_file", err.Error())
			return
		}
		defer file.Close()
		overwrite := r.FormValue("overwrite") == "true"
		info, err := assetStore.Save(kind, header.Filename, file, overwrite)
		if err != nil {
			writeError(w, http.StatusBadRequest, "asset_upload_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusCreated, info)
	})

	mux.HandleFunc("DELETE /api/assets/{kind}/{assetId}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		if err := assetStore.Delete(kind, r.PathValue("assetId")); err != nil {
			writeError(w, http.StatusNotFound, "asset_delete_failed", err.Error())
			return
		}
		w.WriteHeader(http.StatusNoContent)
	})

	mux.HandleFunc("GET /api/presets", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		items, err := presetStore.List()
		if err != nil {
			writeError(w, http.StatusBadRequest, "preset_list_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"presets": items})
	})

	mux.HandleFunc("GET /api/presets/banks/{bank}/slots/{slot}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		bank, slot, ok := parseSlot(w, r)
		if !ok {
			return
		}
		presetSlot, err := presetStore.Load(bank, slot)
		if err != nil {
			writeError(w, http.StatusNotFound, "preset_not_found", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, presetSlot)
	})

	mux.HandleFunc("PUT /api/presets/banks/{bank}/slots/{slot}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		bank, slot, ok := parseSlot(w, r)
		if !ok {
			return
		}
		var preset presets.Preset
		if err := json.NewDecoder(r.Body).Decode(&preset); err != nil {
			writeError(w, http.StatusBadRequest, "invalid_json", err.Error())
			return
		}
		presetSlot, err := presetStore.Save(bank, slot, preset)
		if err != nil {
			writeError(w, http.StatusBadRequest, "preset_save_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusOK, presetSlot)
	})

	mux.HandleFunc("POST /api/presets/banks/{bank}/slots/{slot}/apply", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		bank, slot, ok := parseSlot(w, r)
		if !ok {
			return
		}
		writeJSON(w, http.StatusAccepted, map[string]any{
			"accepted": true,
			"bank":     bank,
			"slot":     slot,
			"message":  "apply request accepted",
		})
	})

	return mux
}

func authorized(w http.ResponseWriter, r *http.Request, cfg config.Config) bool {
	if !cfg.AuthEnabled {
		return true
	}
	if r.Header.Get("Authorization") == "Bearer "+cfg.Token {
		return true
	}
	writeError(w, http.StatusUnauthorized, "unauthorized", "missing or invalid bearer token")
	return false
}

func assetKindFromPath(value string) (assets.Kind, bool) {
	switch value {
	case "models":
		return assets.KindModel, true
	case "irs":
		return assets.KindIR, true
	default:
		return "", false
	}
}

func parseSlot(w http.ResponseWriter, r *http.Request) (int, int, bool) {
	bank, err := strconv.Atoi(r.PathValue("bank"))
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid_bank", err.Error())
		return 0, 0, false
	}
	slot, err := strconv.Atoi(r.PathValue("slot"))
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid_slot", err.Error())
		return 0, 0, false
	}
	if bank < 0 || bank > 99 || slot < 0 || slot > 3 {
		writeError(w, http.StatusBadRequest, "slot_out_of_range", "preset slot out of range")
		return 0, 0, false
	}
	return bank, slot, true
}

func writeJSON(w http.ResponseWriter, status int, body any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}

func writeError(w http.ResponseWriter, status int, code string, message string) {
	writeJSON(w, status, errorResponse{Error: code, Message: message})
}

func ListenAddress(cfg config.Config) string {
	return fmt.Sprintf("%s:%d", strings.TrimSpace(cfg.Bind), cfg.Port)
}
```

- [ ] **Step 4: Add command main**

Create `services/managerd/cmd/ardor-managerd/main.go`:

```go
package main

import (
	"log"
	"net/http"

	"ardor.local/managerd/internal/config"
	"ardor.local/managerd/internal/server"
)

func main() {
	cfg, err := config.LoadFromEnv()
	if err != nil {
		log.Fatal(err)
	}
	addr := server.ListenAddress(cfg)
	log.Printf("ardor-managerd listening on %s dataRoot=%s auth=%t", addr, cfg.DataRoot, cfg.AuthEnabled)
	if err := http.ListenAndServe(addr, server.New(cfg)); err != nil {
		log.Fatal(err)
	}
}
```

- [ ] **Step 5: Run server tests**

Run:

```bash
cd services/managerd
gofmt -w .
go test ./...
go build ./cmd/ardor-managerd
```

Expected: tests pass and the command builds.

- [ ] **Step 6: Run the daemon locally**

Run:

```bash
cd services/managerd
ARDOR_API_AUTH=off ARDOR_DATA_ROOT=../.. ARDOR_API_BIND=127.0.0.1 ARDOR_API_PORT=8080 go run ./cmd/ardor-managerd
```

In another terminal:

```bash
curl http://127.0.0.1:8080/api/device
```

Expected: JSON includes `"authEnabled":false`.

- [ ] **Step 7: Commit**

```bash
git add services/managerd
git commit -m "feat: add go manager rest api"
```

---

### Task 3: Package Go Daemon In Buildroot

**Files:**
- Create: `buildroot/external/package/ardor-managerd/Config.in`
- Create: `buildroot/external/package/ardor-managerd/ardor-managerd.mk`
- Create: `buildroot/external/package/ardor-managerd/S98ardor-managerd`
- Create: `buildroot/external/board/ardor-pedal/rootfs-overlay/etc/ardor-managerd.env`
- Modify: `buildroot/external/Config.in`
- Modify: `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig`
- Modify: `README.md`

**Interfaces:**
- Consumes: Go command `services/managerd/cmd/ardor-managerd`.
- Produces: Buildroot package `BR2_PACKAGE_ARDOR_MANAGERD`.

- [ ] **Step 1: Add package Config.in**

Create `buildroot/external/package/ardor-managerd/Config.in`:

```make
config BR2_PACKAGE_ARDOR_MANAGERD
	bool "ardor-managerd"
	depends on BR2_PACKAGE_HOST_GO_TARGET_ARCH_SUPPORTS
	help
	  REST management daemon for Ardor pedal assets and presets.
```

- [ ] **Step 2: Add package makefile**

Create `buildroot/external/package/ardor-managerd/ardor-managerd.mk`:

```make
ARDOR_MANAGERD_VERSION = 1.0
ARDOR_MANAGERD_SITE = $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../..
ARDOR_MANAGERD_SITE_METHOD = local
ARDOR_MANAGERD_SUBDIR = services/managerd
ARDOR_MANAGERD_GOMOD = ardor.local/managerd
ARDOR_MANAGERD_BUILD_TARGETS = ./cmd/ardor-managerd
ARDOR_MANAGERD_INSTALL_BINS = ardor-managerd
ARDOR_MANAGERD_LICENSE = Proprietary

define ARDOR_MANAGERD_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-managerd/S98ardor-managerd \
		$(TARGET_DIR)/etc/init.d/S98ardor-managerd
endef

$(eval $(golang-package))
```

- [ ] **Step 3: Add init script**

Create `buildroot/external/package/ardor-managerd/S98ardor-managerd`:

```sh
#!/bin/sh

PID=/var/run/ardor-managerd.pid
ENV=/etc/ardor-managerd.env

[ -r "$ENV" ] && . "$ENV"

: "${ARDOR_DATA_ROOT:=/opt/ardor-pedal}"
: "${ARDOR_API_BIND:=0.0.0.0}"
: "${ARDOR_API_PORT:=8080}"
: "${ARDOR_API_AUTH:=on}"

export ARDOR_DATA_ROOT ARDOR_API_BIND ARDOR_API_PORT ARDOR_API_AUTH ARDOR_API_TOKEN

case "$1" in
  start)
    echo "Starting ardor-managerd"
    start-stop-daemon -S -b -m -p "$PID" --exec /usr/bin/ardor-managerd
    ;;
  stop)
    echo "Stopping ardor-managerd"
    start-stop-daemon -K -p "$PID"
    ;;
  restart)
    "$0" stop
    "$0" start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
esac
```

- [ ] **Step 4: Add default env file**

Create `buildroot/external/board/ardor-pedal/rootfs-overlay/etc/ardor-managerd.env`:

```sh
ARDOR_DATA_ROOT=/opt/ardor-pedal
ARDOR_API_BIND=0.0.0.0
ARDOR_API_PORT=8080
ARDOR_API_AUTH=off
ARDOR_API_TOKEN=
```

- [ ] **Step 5: Include package in external tree**

Modify `buildroot/external/Config.in`:

```make
source "$BR2_EXTERNAL_ARDOR_PEDAL_PATH/package/ardor-pedal/Config.in"
source "$BR2_EXTERNAL_ARDOR_PEDAL_PATH/package/ardor-managerd/Config.in"
```

- [ ] **Step 6: Enable package in defconfig**

Add to `buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig` near the Ardor additions:

```make
BR2_PACKAGE_ARDOR_MANAGERD=y
```

- [ ] **Step 7: Document daemon workflow**

Add to `README.md`:

```markdown
## Manager Daemon

The REST manager daemon lives in `services/managerd`.

Run locally without auth:

```sh
cd services/managerd
ARDOR_API_AUTH=off \
ARDOR_DATA_ROOT=../.. \
ARDOR_API_BIND=127.0.0.1 \
ARDOR_API_PORT=8080 \
go run ./cmd/ardor-managerd
```

Device status:

```sh
curl http://127.0.0.1:8080/api/device
```
```

- [ ] **Step 8: Run verification**

Run:

```bash
cd services/managerd
go test ./...
go build ./cmd/ardor-managerd
cd ../..
git diff --check
```

Expected: Go tests pass, local binary builds, and whitespace check prints nothing.

- [ ] **Step 9: Commit**

```bash
git add services/managerd buildroot/external/package/ardor-managerd buildroot/external/Config.in buildroot/external/configs/raspberrypi4_ardor_pedal_defconfig buildroot/external/board/ardor-pedal/rootfs-overlay/etc/ardor-managerd.env README.md
git commit -m "feat: package go manager daemon"
```

---

### Task 4: Scaffold Tauri App And API Client

**Files:**
- Create: `apps/manager/package.json`
- Create: `apps/manager/index.html`
- Create: `apps/manager/vite.config.ts`
- Create: `apps/manager/tsconfig.json`
- Create: `apps/manager/postcss.config.cjs`
- Create: `apps/manager/tailwind.config.cjs`
- Create: `apps/manager/src-tauri/Cargo.toml`
- Create: `apps/manager/src-tauri/tauri.conf.json`
- Create: `apps/manager/src-tauri/src/main.rs`
- Create: `apps/manager/src/main.tsx`
- Create: `apps/manager/src/App.tsx`
- Create: `apps/manager/src/api/types.ts`
- Create: `apps/manager/src/api/client.ts`
- Create: `apps/manager/src/api/client.test.ts`
- Create: `apps/manager/src/styles.css`

**Interfaces:**
- Consumes: OpenAPI response shapes from `docs/api/ardor-managerd.openapi.yaml`.
- Produces:
  - `type ApiClientConfig = { baseUrl: string; token?: string; fetchImpl?: typeof fetch };`
  - `class ArdorApiClient`
  - `getDevice`, `listAssets`, `uploadAsset`, `listPresets`, `getPreset`, `savePreset`, `applyPreset`

- [ ] **Step 1: Create package and config files**

Create `apps/manager/package.json`:

```json
{
  "name": "ardor-manager",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "test": "vitest run",
    "tauri": "tauri"
  },
  "dependencies": {
    "@tauri-apps/api": "^2.0.0",
    "@vitejs/plugin-react": "^4.0.0",
    "daisyui": "^4.0.0",
    "react": "^18.2.0",
    "react-dom": "^18.2.0"
  },
  "devDependencies": {
    "@tauri-apps/cli": "^2.0.0",
    "@testing-library/jest-dom": "^6.0.0",
    "@testing-library/react": "^15.0.0",
    "@types/react": "^18.2.0",
    "@types/react-dom": "^18.2.0",
    "autoprefixer": "^10.4.0",
    "jsdom": "^24.0.0",
    "postcss": "^8.4.0",
    "tailwindcss": "^3.4.0",
    "typescript": "^5.4.0",
    "vite": "^5.0.0",
    "vitest": "^1.6.0"
  }
}
```

Create `apps/manager/index.html`:

```html
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Ardor Manager</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
```

Create `apps/manager/vite.config.ts`:

```ts
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

export default defineConfig({
  plugins: [react()],
  clearScreen: false,
  server: { port: 1420, strictPort: true },
});
```

Create `apps/manager/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "lib": ["DOM", "DOM.Iterable", "ES2020"],
    "allowJs": false,
    "skipLibCheck": true,
    "esModuleInterop": true,
    "allowSyntheticDefaultImports": true,
    "strict": true,
    "forceConsistentCasingInFileNames": true,
    "module": "ESNext",
    "moduleResolution": "Node",
    "resolveJsonModule": true,
    "isolatedModules": true,
    "noEmit": true,
    "jsx": "react-jsx",
    "types": ["vitest/globals", "@testing-library/jest-dom"]
  },
  "include": ["src"],
  "references": []
}
```

Create `apps/manager/tailwind.config.cjs`:

```js
module.exports = {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: { extend: {} },
  plugins: [require("daisyui")],
  daisyui: { themes: ["light", "dark"] },
};
```

Create `apps/manager/postcss.config.cjs`:

```js
module.exports = {
  plugins: {
    tailwindcss: {},
    autoprefixer: {},
  },
};
```

- [ ] **Step 2: Create minimal Tauri files**

Create `apps/manager/src-tauri/Cargo.toml`:

```toml
[package]
name = "ardor-manager"
version = "0.1.0"
description = "Ardor pedal manager"
edition = "2021"

[build-dependencies]
tauri-build = { version = "2", features = [] }

[dependencies]
tauri = { version = "2", features = [] }
tauri-plugin-shell = "2"
```

Create `apps/manager/src-tauri/src/main.rs`:

```rust
fn main() {
    tauri::Builder::default()
        .run(tauri::generate_context!())
        .expect("error while running Ardor Manager");
}
```

Create `apps/manager/src-tauri/tauri.conf.json`:

```json
{
  "$schema": "https://schema.tauri.app/config/2",
  "productName": "Ardor Manager",
  "version": "0.1.0",
  "identifier": "com.ardor.pedal.manager",
  "build": {
    "beforeDevCommand": "npm run dev",
    "devUrl": "http://localhost:1420",
    "beforeBuildCommand": "npm run build",
    "frontendDist": "../dist"
  },
  "app": {
    "windows": [
      {
        "title": "Ardor Manager",
        "width": 1180,
        "height": 780,
        "minWidth": 960,
        "minHeight": 640
      }
    ]
  },
  "bundle": {
    "active": true,
    "targets": "all"
  }
}
```

- [ ] **Step 3: Add API types and client test**

Create `apps/manager/src/api/types.ts`:

```ts
export type AssetKind = "models" | "irs";

export type DeviceStatus = {
  deviceName: string;
  apiVersion: string;
  authEnabled: boolean;
  dataRootWritable: boolean;
  maxBanks: 100;
  slotsPerBank: 4;
  supportedPresetVersion: 1;
  capabilities: {
    modelUpload: boolean;
    irUpload: boolean;
    presetRead: boolean;
    presetWrite: boolean;
    presetApply: boolean;
  };
};

export type Asset = {
  id: string;
  kind: "model" | "ir";
  filename: string;
  path: string;
  sizeBytes: number;
};

export type PresetBlock = {
  id: string;
  type: string;
  enabled: boolean;
  asset: string;
  params: Record<string, unknown>;
  [key: string]: unknown;
};

export type Preset = {
  version: 1;
  name: string;
  routing: "serial";
  global: {
    inputGainDb: number;
    outputGainDb: number;
    safetyLimitDb: number;
    [key: string]: unknown;
  };
  blocks: PresetBlock[];
  [key: string]: unknown;
};

export type PresetSlotSummary = {
  bank: number;
  slot: number;
  exists: boolean;
  name?: string;
  unsupportedBlockCount?: number;
  missingAssetCount?: number;
};

export type PresetSlot = {
  bank: number;
  slot: number;
  preset: Preset;
};

export type ApplyPresetResponse = {
  accepted: boolean;
  bank: number;
  slot: number;
  message?: string;
};
```

Create `apps/manager/src/api/client.test.ts`:

```ts
import { describe, expect, it, vi } from "vitest";
import { ArdorApiClient } from "./client";

describe("ArdorApiClient", () => {
  it("adds bearer token when present", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      deviceName: "Ardor Pedal",
      apiVersion: "0.1.0",
      authEnabled: true,
      dataRootWritable: true,
      maxBanks: 100,
      slotsPerBank: 4,
      supportedPresetVersion: 1,
      capabilities: { modelUpload: true, irUpload: true, presetRead: true, presetWrite: true, presetApply: true },
    })));
    const client = new ArdorApiClient({ baseUrl: "http://pedal", token: "secret", fetchImpl: fetchMock });
    await client.getDevice();
    expect(fetchMock).toHaveBeenCalledWith("http://pedal/api/device", expect.objectContaining({
      headers: expect.objectContaining({ Authorization: "Bearer secret" }),
    }));
  });

  it("saves presets through the slot endpoint", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      bank: 1,
      slot: 2,
      preset: { version: 1, name: "A", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [] },
    })));
    const client = new ArdorApiClient({ baseUrl: "http://pedal/", fetchImpl: fetchMock });
    await client.savePreset(1, 2, { version: 1, name: "A", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [] });
    expect(fetchMock).toHaveBeenCalledWith("http://pedal/api/presets/banks/1/slots/2", expect.objectContaining({
      method: "PUT",
      body: expect.stringContaining("\"name\":\"A\""),
    }));
  });
});
```

- [ ] **Step 4: Implement API client**

Create `apps/manager/src/api/client.ts` with:

```ts
import type { ApplyPresetResponse, Asset, AssetKind, DeviceStatus, Preset, PresetSlot, PresetSlotSummary } from "./types";

type FetchImpl = typeof fetch;

export type ApiClientConfig = {
  baseUrl: string;
  token?: string;
  fetchImpl?: FetchImpl;
};

export class ArdorApiClient {
  private readonly baseUrl: string;
  private readonly token?: string;
  private readonly fetchImpl: FetchImpl;

  constructor(config: ApiClientConfig) {
    this.baseUrl = config.baseUrl.replace(/\/+$/, "");
    this.token = config.token;
    this.fetchImpl = config.fetchImpl ?? fetch;
  }

  getDevice(): Promise<DeviceStatus> {
    return this.request<DeviceStatus>("/api/device");
  }

  async listAssets(kind: AssetKind): Promise<Asset[]> {
    const response = await this.request<{ assets: Asset[] }>(`/api/assets/${kind}`);
    return response.assets;
  }

  uploadAsset(kind: AssetKind, file: File, overwrite: boolean): Promise<Asset> {
    const body = new FormData();
    body.set("file", file);
    body.set("overwrite", overwrite ? "true" : "false");
    return this.request<Asset>(`/api/assets/${kind}`, { method: "POST", body });
  }

  async listPresets(): Promise<PresetSlotSummary[]> {
    const response = await this.request<{ presets: PresetSlotSummary[] }>("/api/presets");
    return response.presets;
  }

  getPreset(bank: number, slot: number): Promise<PresetSlot> {
    return this.request<PresetSlot>(`/api/presets/banks/${bank}/slots/${slot}`);
  }

  savePreset(bank: number, slot: number, preset: Preset): Promise<PresetSlot> {
    return this.request<PresetSlot>(`/api/presets/banks/${bank}/slots/${slot}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(preset),
    });
  }

  applyPreset(bank: number, slot: number): Promise<ApplyPresetResponse> {
    return this.request<ApplyPresetResponse>(`/api/presets/banks/${bank}/slots/${slot}/apply`, { method: "POST" });
  }

  private async request<T>(path: string, init: RequestInit = {}): Promise<T> {
    const headers = new Headers(init.headers);
    if (this.token) {
      headers.set("Authorization", `Bearer ${this.token}`);
    }
    const response = await this.fetchImpl(`${this.baseUrl}${path}`, { ...init, headers });
    if (!response.ok) {
      throw new Error(`API request failed: ${response.status}`);
    }
    return response.json() as Promise<T>;
  }
}
```

- [ ] **Step 5: Add minimal React entrypoint**

Create `apps/manager/src/main.tsx`:

```tsx
import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import "./styles.css";

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
```

Create `apps/manager/src/App.tsx`:

```tsx
export default function App() {
  return (
    <main className="min-h-screen bg-base-100 text-base-content" data-theme="light">
      <div className="navbar border-b border-base-300 px-4">
        <div className="flex-1 text-lg font-semibold">Ardor Manager</div>
        <input type="checkbox" className="toggle" aria-label="Toggle theme" />
      </div>
      <section className="p-4">
        <div className="alert">Connect to an Ardor pedal to manage assets and presets.</div>
      </section>
    </main>
  );
}
```

Create `apps/manager/src/styles.css`:

```css
@tailwind base;
@tailwind components;
@tailwind utilities;

html,
body {
  margin: 0;
  min-height: 100%;
}
```

Create `apps/manager/src/main.tsx`, `apps/manager/src/App.tsx`, and `apps/manager/src/styles.css`. The first screen must be the manager workspace shell with connection controls, not a landing page.

- [ ] **Step 6: Run frontend tests and build**

Run:

```bash
cd apps/manager
npm install
npm test
npm run build
```

Expected: tests pass and Vite builds.

- [ ] **Step 7: Commit**

```bash
git add apps/manager
git commit -m "feat: scaffold tauri manager app"
```

---

### Task 5: Add Offline Preset Draft Editing UI

**Files:**
- Create: `apps/manager/src/presets/presetDraft.ts`
- Create: `apps/manager/src/presets/presetDraft.test.ts`
- Modify: `apps/manager/src/App.tsx`

**Interfaces:**
- Consumes: `Preset`, `PresetBlock`, `Asset`.
- Produces:
  - `clonePreset(preset: Preset): Preset`
  - `setPresetName(preset: Preset, name: string): Preset`
  - `setGlobalParam(preset: Preset, key: "inputGainDb" | "outputGainDb", value: number): Preset`
  - `setBlockAsset(preset: Preset, blockId: string, asset: string): Preset`
  - `setBlockParam(preset: Preset, blockId: string, key: string, value: number): Preset`
  - `isKnownEditableBlock(block: PresetBlock): boolean`

- [ ] **Step 1: Write failing draft tests**

Create `apps/manager/src/presets/presetDraft.test.ts`:

```ts
import { describe, expect, it } from "vitest";
import type { Preset } from "../api/types";
import { isKnownEditableBlock, setBlockAsset, setBlockParam, setGlobalParam, setPresetName } from "./presetDraft";

const basePreset: Preset = {
  version: 1,
  name: "Clean",
  routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1, future: "keep" },
  blocks: [
    { id: "nam-1", type: "nam", enabled: true, asset: "models/a.nam", params: {}, future: "keep" },
    { id: "cab-1", type: "cab", enabled: true, asset: "irs/a.wav", params: { mix: 1 } },
    { id: "future-1", type: "future", enabled: true, asset: "", params: { value: 3 } },
  ],
  futureRoot: true,
};

describe("preset drafts", () => {
  it("edits name and globals immutably", () => {
    const renamed = setPresetName(basePreset, "Lead");
    expect(renamed.name).toBe("Lead");
    expect(basePreset.name).toBe("Clean");

    const gained = setGlobalParam(basePreset, "inputGainDb", -6);
    expect(gained.global.inputGainDb).toBe(-6);
    expect(gained.global.safetyLimitDb).toBe(-1);
    expect(gained.global.future).toBe("keep");
  });

  it("edits known block assets and params while preserving future fields", () => {
    const withAsset = setBlockAsset(basePreset, "nam-1", "models/b.nam");
    expect(withAsset.blocks[0].asset).toBe("models/b.nam");
    expect(withAsset.blocks[0].future).toBe("keep");

    const withMix = setBlockParam(basePreset, "cab-1", "mix", 0.5);
    expect(withMix.blocks[1].params.mix).toBe(0.5);
    expect(withMix.blocks[2].params.value).toBe(3);
  });

  it("classifies editable blocks", () => {
    expect(isKnownEditableBlock(basePreset.blocks[0])).toBe(true);
    expect(isKnownEditableBlock(basePreset.blocks[1])).toBe(true);
    expect(isKnownEditableBlock(basePreset.blocks[2])).toBe(false);
  });
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cd apps/manager
npm test -- presetDraft
```

Expected: test fails because `presetDraft.ts` does not exist.

- [ ] **Step 3: Implement draft helpers**

Create `apps/manager/src/presets/presetDraft.ts`:

```ts
import type { Preset, PresetBlock } from "../api/types";

export function clonePreset(preset: Preset): Preset {
  return structuredClone(preset);
}

export function setPresetName(preset: Preset, name: string): Preset {
  return { ...clonePreset(preset), name };
}

export function setGlobalParam(preset: Preset, key: "inputGainDb" | "outputGainDb", value: number): Preset {
  const draft = clonePreset(preset);
  draft.global[key] = value;
  return draft;
}

export function setBlockAsset(preset: Preset, blockId: string, asset: string): Preset {
  const draft = clonePreset(preset);
  draft.blocks = draft.blocks.map((block) => (block.id === blockId ? { ...block, asset } : block));
  return draft;
}

export function setBlockParam(preset: Preset, blockId: string, key: string, value: number): Preset {
  const draft = clonePreset(preset);
  draft.blocks = draft.blocks.map((block) =>
    block.id === blockId ? { ...block, params: { ...block.params, [key]: value } } : block,
  );
  return draft;
}

export function isKnownEditableBlock(block: PresetBlock): boolean {
  return ["nam", "cab", "mod", "delay", "reverb"].includes(block.type);
}
```

- [ ] **Step 4: Replace initial UI with the manager workspace**

Replace `apps/manager/src/App.tsx` with:

```tsx
import { useMemo, useState } from "react";
import { ArdorApiClient } from "./api/client";
import type { Asset, Preset, PresetSlotSummary } from "./api/types";
import { isKnownEditableBlock, setBlockAsset, setBlockParam, setGlobalParam, setPresetName } from "./presets/presetDraft";

const emptyPreset: Preset = {
  version: 1,
  name: "New Preset",
  routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [],
};

export default function App() {
  const [theme, setTheme] = useState<"light" | "dark">("light");
  const [baseUrl, setBaseUrl] = useState("http://127.0.0.1:8080");
  const [token, setToken] = useState("");
  const [authEnabled, setAuthEnabled] = useState<boolean | null>(null);
  const [connected, setConnected] = useState(false);
  const [models, setModels] = useState<Asset[]>([]);
  const [irs, setIrs] = useState<Asset[]>([]);
  const [presets, setPresets] = useState<PresetSlotSummary[]>([]);
  const [selected, setSelected] = useState({ bank: 0, slot: 0 });
  const [savedPreset, setSavedPreset] = useState<Preset>(emptyPreset);
  const [draft, setDraft] = useState<Preset>(emptyPreset);
  const [status, setStatus] = useState("");

  const client = useMemo(() => new ArdorApiClient({ baseUrl, token: token || undefined }), [baseUrl, token]);
  const dirty = JSON.stringify(savedPreset) !== JSON.stringify(draft);

  async function connect() {
    const device = await client.getDevice();
    setAuthEnabled(device.authEnabled);
    setConnected(true);
    setModels(await client.listAssets("models"));
    setIrs(await client.listAssets("irs"));
    setPresets(await client.listPresets());
    setStatus(`Connected to ${device.deviceName}`);
  }

  async function loadPreset(bank: number, slot: number) {
    const response = await client.getPreset(bank, slot);
    setSelected({ bank, slot });
    setSavedPreset(response.preset);
    setDraft(response.preset);
    setStatus(`Loaded ${bank}-${slot}`);
  }

  async function savePreset() {
    const response = await client.savePreset(selected.bank, selected.slot, draft);
    setSavedPreset(response.preset);
    setDraft(response.preset);
    setStatus("Preset saved");
  }

  async function applyPreset() {
    await client.applyPreset(selected.bank, selected.slot);
    setStatus("Apply request sent");
  }

  async function upload(kind: "models" | "irs", file?: File) {
    if (!file) {
      return;
    }
    await client.uploadAsset(kind, file, false);
    setModels(await client.listAssets("models"));
    setIrs(await client.listAssets("irs"));
    setStatus(`${file.name} uploaded`);
  }

  return (
    <main data-theme={theme} className="min-h-screen bg-base-100 text-base-content">
      <div className="navbar border-b border-base-300 px-4">
        <div className="flex-1 text-lg font-semibold">Ardor Manager</div>
        <label className="flex items-center gap-2 text-sm">
          Dark
          <input
            type="checkbox"
            className="toggle"
            checked={theme === "dark"}
            onChange={(event) => setTheme(event.target.checked ? "dark" : "light")}
          />
        </label>
      </div>

      <div className="grid grid-cols-[280px_1fr_340px] gap-4 p-4">
        <section className="space-y-3">
          <h2 className="text-sm font-semibold uppercase">Connection</h2>
          <input className="input input-bordered w-full" value={baseUrl} onChange={(event) => setBaseUrl(event.target.value)} />
          <input className="input input-bordered w-full" aria-label="Token" value={token} onChange={(event) => setToken(event.target.value)} />
          <button className="btn btn-primary w-full" onClick={connect}>Connect</button>
          <div className="flex gap-2">
            <span className="badge">{connected ? "Connected" : "Disconnected"}</span>
            {authEnabled !== null && <span className="badge badge-outline">Auth {authEnabled ? "on" : "off"}</span>}
          </div>

          <h2 className="pt-4 text-sm font-semibold uppercase">Presets</h2>
          <div className="grid grid-cols-4 gap-2">
            {[0, 1, 2, 3].map((slot) => (
              <button key={slot} className="btn btn-sm" onClick={() => loadPreset(0, slot)}>
                0-{slot}
              </button>
            ))}
          </div>
          <div className="max-h-72 overflow-auto text-sm">
            {presets.filter((preset) => preset.exists).slice(0, 40).map((preset) => (
              <button
                key={`${preset.bank}-${preset.slot}`}
                className="btn btn-ghost btn-sm w-full justify-start"
                onClick={() => loadPreset(preset.bank, preset.slot)}
              >
                {preset.bank}-{preset.slot} {preset.name ?? "Unnamed"}
              </button>
            ))}
          </div>
        </section>

        <section className="space-y-4">
          <div className="flex items-center justify-between gap-3">
            <h1 className="text-xl font-semibold">{draft.name}</h1>
            <div className="flex gap-2">
              {dirty && <span className="badge badge-warning">Unsaved</span>}
              <button className="btn btn-sm" onClick={() => setDraft(savedPreset)} disabled={!dirty}>Discard</button>
              <button className="btn btn-sm btn-primary" onClick={savePreset} disabled={!connected || !dirty}>Save</button>
              <button className="btn btn-sm" onClick={applyPreset} disabled={!connected}>Apply</button>
            </div>
          </div>

          <div className="grid grid-cols-3 gap-3">
            <label className="form-control">
              <span className="label-text">Name</span>
              <input className="input input-bordered" value={draft.name} onChange={(event) => setDraft(setPresetName(draft, event.target.value))} />
            </label>
            <label className="form-control">
              <span className="label-text">Input gain</span>
              <input type="number" className="input input-bordered" value={draft.global.inputGainDb} onChange={(event) => setDraft(setGlobalParam(draft, "inputGainDb", Number(event.target.value)))} />
            </label>
            <label className="form-control">
              <span className="label-text">Output gain</span>
              <input type="number" className="input input-bordered" value={draft.global.outputGainDb} onChange={(event) => setDraft(setGlobalParam(draft, "outputGainDb", Number(event.target.value)))} />
            </label>
          </div>

          <div className="space-y-3">
            {draft.blocks.map((block) => (
              <div key={block.id} className="rounded border border-base-300 p-3">
                <div className="mb-2 flex items-center justify-between">
                  <div className="font-medium">{block.type}</div>
                  {!isKnownEditableBlock(block) && <span className="badge badge-outline">Unsupported</span>}
                </div>
                {isKnownEditableBlock(block) && (
                  <div className="grid grid-cols-2 gap-3">
                    {(block.type === "nam" || block.type === "cab") && (
                      <select className="select select-bordered" value={block.asset} onChange={(event) => setDraft(setBlockAsset(draft, block.id, event.target.value))}>
                        <option value="">No asset</option>
                        {(block.type === "nam" ? models : irs).map((asset) => (
                          <option key={asset.id} value={asset.path}>{asset.filename}</option>
                        ))}
                      </select>
                    )}
                    {block.type === "cab" && (
                      <>
                        <input type="number" className="input input-bordered" value={Number(block.params.levelDb ?? 0)} onChange={(event) => setDraft(setBlockParam(draft, block.id, "levelDb", Number(event.target.value)))} />
                        <input type="number" step="0.01" min="0" max="1" className="input input-bordered" value={Number(block.params.mix ?? 1)} onChange={(event) => setDraft(setBlockParam(draft, block.id, "mix", Number(event.target.value)))} />
                      </>
                    )}
                  </div>
                )}
              </div>
            ))}
          </div>
        </section>

        <section className="space-y-3">
          <h2 className="text-sm font-semibold uppercase">Assets</h2>
          <input type="file" accept=".nam" className="file-input file-input-bordered w-full" onChange={(event) => upload("models", event.target.files?.[0])} />
          <input type="file" accept=".wav" className="file-input file-input-bordered w-full" onChange={(event) => upload("irs", event.target.files?.[0])} />
          <div className="text-sm font-medium">Models</div>
          {models.map((asset) => <div key={asset.id} className="text-sm">{asset.filename}</div>)}
          <div className="text-sm font-medium">IRs</div>
          {irs.map((asset) => <div key={asset.id} className="text-sm">{asset.filename}</div>)}
          {status && <div className="alert alert-info text-sm">{status}</div>}
        </section>
      </div>
    </main>
  );
}
```

- [ ] **Step 5: Run tests and build**

Run:

```bash
cd apps/manager
npm test
npm run build
```

Expected: tests pass and build succeeds.

- [ ] **Step 6: Commit**

```bash
git add apps/manager/src
git commit -m "feat: add offline preset editor"
```

---

### Task 6: Run End-To-End Verification And Update Docs

**Files:**
- Modify: `README.md`
- Modify: `docs/api/ardor-managerd.openapi.yaml` only if implementation exposes a materially different field or status code.

**Interfaces:**
- Consumes: all earlier tasks.
- Produces: documented local workflow for Go daemon plus Tauri manager.

- [ ] **Step 1: Run Go daemon tests**

Run:

```bash
cd services/managerd
go test ./...
go build ./cmd/ardor-managerd
```

Expected: Go tests pass and command builds.

- [ ] **Step 2: Run frontend tests and build**

Run:

```bash
cd apps/manager
npm test
npm run build
```

Expected: Vitest passes and Vite builds.

- [ ] **Step 3: Run local daemon integration manually**

Terminal 1:

```bash
cd services/managerd
ARDOR_API_AUTH=off ARDOR_DATA_ROOT=../.. ARDOR_API_BIND=127.0.0.1 ARDOR_API_PORT=8080 go run ./cmd/ardor-managerd
```

Terminal 2:

```bash
curl http://127.0.0.1:8080/api/device
curl http://127.0.0.1:8080/api/presets
```

Expected: `/api/device` returns JSON with `"authEnabled":false`; `/api/presets` returns a `presets` array.

- [ ] **Step 4: Run the Tauri app locally**

Run:

```bash
cd apps/manager
npm run tauri dev
```

Expected: the desktop window opens on macOS, connects to `http://127.0.0.1:8080`, lists assets/presets, edits a preset draft without network writes, saves through `PUT`, and sends apply through `POST`.

- [ ] **Step 5: Update README with app workflow**

Add:

```markdown
## Desktop Manager

The desktop manager lives in `apps/manager`.

```sh
cd apps/manager
npm install
npm run tauri dev
```

For local testing, run the Go daemon with auth disabled:

```sh
cd services/managerd
ARDOR_API_AUTH=off ARDOR_DATA_ROOT=../.. ARDOR_API_BIND=127.0.0.1 ARDOR_API_PORT=8080 go run ./cmd/ardor-managerd
```

Use `http://127.0.0.1:8080` as the manager base URL.
```

- [ ] **Step 6: Run final whitespace check**

Run:

```bash
git diff --check
```

Expected: no output.

- [ ] **Step 7: Commit**

```bash
git add README.md docs/api/ardor-managerd.openapi.yaml
git commit -m "docs: document manager workflow"
```

---

## Final Gate

Run:

```bash
cd services/managerd && go test ./... && go build ./cmd/ardor-managerd
cd ../../apps/manager && npm test && npm run build
cd ../.. && git diff --check
```

Expected:

- Go daemon tests pass.
- Go daemon builds.
- Tauri frontend tests pass.
- Tauri frontend builds.
- No whitespace errors.

Manual macOS gate:

```bash
cd services/managerd
ARDOR_API_AUTH=off ARDOR_DATA_ROOT=../.. ARDOR_API_BIND=127.0.0.1 ARDOR_API_PORT=8080 go run ./cmd/ardor-managerd
```

In another terminal:

```bash
cd apps/manager
npm run tauri dev
```

Expected:

- The app connects to the local daemon.
- `authEnabled` displays as off.
- `.nam` upload appears in model assets.
- `.wav` upload appears in IR assets.
- Preset draft edits do not call the API until Save.
- Save persists a preset JSON file under `presets/bank-000/preset-N.json`.
- Apply returns accepted without touching realtime audio callback behavior.
