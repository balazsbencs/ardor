package backup

import (
	"archive/zip"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"ardor.local/managerd/internal/presets"
)

const FormatVersion = 1

var presetPath = regexp.MustCompile(`^presets/bank-\d{3}/preset-[0-3]\.json$`)

type Manifest struct {
	Format    string    `json:"format"`
	Version   int       `json:"version"`
	CreatedAt time.Time `json:"createdAt"`
	FileCount int       `json:"fileCount"`
}

type Result struct {
	AssetCount  int `json:"assetCount"`
	PresetCount int `json:"presetCount"`
}

var dataDirs = []string{"models", "irs", "reverb-irs", "presets"}

func Export(root string, dst io.Writer, now time.Time) (Manifest, error) {
	manifest := Manifest{Format: "ardor-backup", Version: FormatVersion, CreatedAt: now.UTC()}
	zw := zip.NewWriter(dst)
	for _, dir := range dataDirs {
		base := filepath.Join(root, dir)
		err := filepath.WalkDir(base, func(filename string, entry os.DirEntry, err error) error {
			if errors.Is(err, os.ErrNotExist) {
				return nil
			}
			if err != nil {
				return err
			}
			if entry.IsDir() {
				return nil
			}
			if !entry.Type().IsRegular() {
				return nil
			}
			rel, err := filepath.Rel(root, filename)
			if err != nil {
				return err
			}
			name := filepath.ToSlash(rel)
			if !validDataPath(name) {
				return nil
			}
			in, err := os.Open(filename)
			if err != nil {
				return err
			}
			info, err := entry.Info()
			if err != nil {
				in.Close()
				return err
			}
			header, err := zip.FileInfoHeader(info)
			if err != nil {
				in.Close()
				return err
			}
			header.Name, header.Method = name, zip.Deflate
			out, err := zw.CreateHeader(header)
			if err == nil {
				_, err = io.Copy(out, in)
			}
			closeErr := in.Close()
			if err != nil {
				return err
			}
			if closeErr != nil {
				return closeErr
			}
			manifest.FileCount++
			return nil
		})
		if err != nil && !errors.Is(err, os.ErrNotExist) {
			return Manifest{}, err
		}
	}
	body, _ := json.MarshalIndent(manifest, "", "  ")
	w, err := zw.Create("manifest.json")
	if err != nil {
		return Manifest{}, err
	}
	if _, err = w.Write(append(body, '\n')); err != nil {
		return Manifest{}, err
	}
	if err = zw.Close(); err != nil {
		return Manifest{}, err
	}
	return manifest, nil
}

func Import(root string, archive io.ReaderAt, size int64) (Result, error) {
	zr, err := zip.NewReader(archive, size)
	if err != nil {
		return Result{}, fmt.Errorf("open backup: %w", err)
	}
	stage, err := os.MkdirTemp(filepath.Dir(root), ".ardor-restore-")
	if err != nil {
		return Result{}, err
	}
	defer os.RemoveAll(stage)
	var manifest Manifest
	result := Result{}
	foundManifest := false
	var expanded uint64
	for _, file := range zr.File {
		name := path.Clean(file.Name)
		if name != file.Name || strings.HasPrefix(name, "/") || strings.HasPrefix(name, "../") {
			return Result{}, fmt.Errorf("unsafe backup path %q", file.Name)
		}
		if file.FileInfo().IsDir() {
			continue
		}
		expanded += file.UncompressedSize64
		if expanded > 8<<30 {
			return Result{}, errors.New("backup expands beyond 8 GiB limit")
		}
		if name == "manifest.json" {
			if foundManifest {
				return Result{}, errors.New("backup contains more than one manifest")
			}
			foundManifest = true
			r, err := file.Open()
			if err != nil {
				return Result{}, err
			}
			err = json.NewDecoder(io.LimitReader(r, 64<<10)).Decode(&manifest)
			r.Close()
			if err != nil {
				return Result{}, fmt.Errorf("read manifest: %w", err)
			}
			continue
		}
		if !validDataPath(name) {
			return Result{}, fmt.Errorf("unsupported backup file %q", name)
		}
		target := filepath.Join(stage, filepath.FromSlash(name))
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return Result{}, err
		}
		r, err := file.Open()
		if err != nil {
			return Result{}, err
		}
		out, err := os.OpenFile(target, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0o644)
		if err == nil {
			_, err = io.Copy(out, r)
		}
		if out != nil {
			if closeErr := out.Close(); err == nil {
				err = closeErr
			}
		}
		r.Close()
		if err != nil {
			return Result{}, fmt.Errorf("extract %s: %w", name, err)
		}
		if strings.HasPrefix(name, "presets/") {
			var preset presets.Preset
			body, err := os.ReadFile(target)
			if err != nil {
				return Result{}, err
			}
			if err = json.Unmarshal(body, &preset); err != nil {
				return Result{}, fmt.Errorf("invalid preset %s: %w", name, err)
			}
			if err = presets.Validate(preset); err != nil {
				return Result{}, fmt.Errorf("invalid preset %s: %w", name, err)
			}
			result.PresetCount++
		} else {
			result.AssetCount++
		}
	}
	if !foundManifest || manifest.Format != "ardor-backup" || manifest.Version != FormatVersion {
		return Result{}, errors.New("file is not a supported Ardor backup")
	}
	if manifest.FileCount != result.AssetCount+result.PresetCount {
		return Result{}, errors.New("backup manifest file count does not match archive")
	}
	for _, dir := range dataDirs {
		if err := os.MkdirAll(filepath.Join(stage, dir), 0o755); err != nil {
			return Result{}, err
		}
	}
	if err := replaceDirs(root, stage); err != nil {
		return Result{}, err
	}
	return result, nil
}

func replaceDirs(root, stage string) error {
	rollback := root + ".restore-rollback"
	_ = os.RemoveAll(rollback)
	if err := os.MkdirAll(rollback, 0o755); err != nil {
		return err
	}
	moved := []string{}
	for _, dir := range dataDirs {
		current := filepath.Join(root, dir)
		if _, err := os.Stat(current); err == nil {
			if err := os.Rename(current, filepath.Join(rollback, dir)); err != nil {
				restoreMoved(root, rollback, moved)
				return err
			}
			moved = append(moved, dir)
		} else if !errors.Is(err, os.ErrNotExist) {
			restoreMoved(root, rollback, moved)
			return err
		}
	}
	installed := []string{}
	for _, dir := range dataDirs {
		if err := os.Rename(filepath.Join(stage, dir), filepath.Join(root, dir)); err != nil {
			for _, item := range installed {
				_ = os.RemoveAll(filepath.Join(root, item))
			}
			restoreMoved(root, rollback, moved)
			return err
		}
		installed = append(installed, dir)
	}
	return os.RemoveAll(rollback)
}

func restoreMoved(root, rollback string, dirs []string) {
	for _, dir := range dirs {
		_ = os.Rename(filepath.Join(rollback, dir), filepath.Join(root, dir))
	}
}
func validDataPath(name string) bool {
	dir, ext := path.Dir(name), strings.ToLower(path.Ext(name))
	return dir == "models" && ext == ".nam" || dir == "irs" && ext == ".wav" || dir == "reverb-irs" && ext == ".wav" || presetPath.MatchString(name)
}
