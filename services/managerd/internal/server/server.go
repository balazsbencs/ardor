package server

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"

	"ardor.local/managerd/internal/assets"
	"ardor.local/managerd/internal/config"
	"ardor.local/managerd/internal/presets"
	"ardor.local/managerd/internal/runtimecontrol"
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
				"modelUpload": true, "irUpload": true, "presetRead": true,
				"presetWrite": true, "presetApply": true, "assetRename": true,
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
			writeError(w, http.StatusInternalServerError, "asset_list_failed", err.Error())
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
			status := http.StatusBadRequest
			code := "asset_upload_failed"
			if errors.Is(err, assets.ErrExists) {
				status = http.StatusConflict
				code = "asset_exists"
			}
			writeError(w, status, code, err.Error())
			return
		}
		if err := runtimecontrol.QueueAssetReload(cfg.DataRoot); err != nil {
			// The asset is already durable and listed by the API. Keep the upload
			// successful, but make a missed runtime refresh diagnosable on-device.
			log.Printf("queue asset reload: %v", err)
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

	mux.HandleFunc("PATCH /api/assets/{kind}/{assetId}", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		kind, ok := assetKindFromPath(r.PathValue("kind"))
		if !ok {
			writeError(w, http.StatusNotFound, "not_found", "asset kind not found")
			return
		}
		var body struct {
			Filename string `json:"filename"`
		}
		if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 8<<10)).Decode(&body); err != nil {
			writeError(w, http.StatusBadRequest, "invalid_rename", "request body must contain a filename")
			return
		}
		oldID := r.PathValue("assetId")
		oldPath := "models/" + oldID
		if kind == assets.KindIR {
			oldPath = "irs/" + oldID
		}
		info, err := assetStore.Rename(kind, oldID, body.Filename)
		if err != nil {
			status := http.StatusBadRequest
			code := "asset_rename_failed"
			if errors.Is(err, assets.ErrExists) {
				status, code = http.StatusConflict, "asset_exists"
			} else if errors.Is(err, os.ErrNotExist) {
				status, code = http.StatusNotFound, "asset_not_found"
			}
			writeError(w, status, code, err.Error())
			return
		}
		updated, err := presetStore.ReplaceAssetReferences(oldPath, info.Path)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "asset_reference_update_failed", err.Error())
			return
		}
		if err := runtimecontrol.QueueAssetReload(cfg.DataRoot); err != nil {
			log.Printf("queue asset reload: %v", err)
		}
		writeJSON(w, http.StatusOK, map[string]any{"asset": info, "updatedPresetCount": updated})
	})

	mux.HandleFunc("GET /api/presets", func(w http.ResponseWriter, r *http.Request) {
		if !authorized(w, r, cfg) {
			return
		}
		items, err := presetStore.List()
		if err != nil {
			writeError(w, http.StatusInternalServerError, "preset_list_failed", err.Error())
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
		decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<20))
		if err := decoder.Decode(&preset); err != nil {
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
		if _, err := presetStore.Load(bank, slot); err != nil {
			writeError(w, http.StatusNotFound, "preset_not_found", err.Error())
			return
		}
		if err := runtimecontrol.QueueApplyPreset(cfg.DataRoot, bank, slot); err != nil {
			writeError(w, http.StatusServiceUnavailable, "runtime_command_failed", err.Error())
			return
		}
		writeJSON(w, http.StatusAccepted, map[string]any{
			"accepted": true, "bank": bank, "slot": slot,
			"message": "apply request queued",
		})
	})

	return withCORS(mux)
}

var allowedOrigins = map[string]struct{}{
	"tauri://localhost":       {},
	"https://tauri.localhost": {},
	"http://tauri.localhost":  {},
	"http://localhost:1420":   {},
	"http://127.0.0.1:1420":   {},
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		origin := r.Header.Get("Origin")
		_, allowed := allowedOrigins[origin]
		if allowed {
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
			w.Header().Set("Access-Control-Allow-Headers", "Authorization, Content-Type")
			w.Header().Set("Access-Control-Max-Age", "600")
			w.Header().Add("Vary", "Origin")
		}
		if r.Method == http.MethodOptions && allowed {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func authorized(w http.ResponseWriter, r *http.Request, cfg config.Config) bool {
	if !cfg.AuthEnabled || r.Header.Get("Authorization") == "Bearer "+cfg.Token {
		return true
	}
	w.Header().Set("WWW-Authenticate", `Bearer realm="ardor-managerd"`)
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
