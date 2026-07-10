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

var ErrExists = errors.New("asset already exists")

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
	originalExt := filepath.Ext(base)
	ext := strings.ToLower(originalExt)
	if ext == "" {
		return "", errors.New("asset filename must include extension")
	}
	if ext != extension(kind) {
		return "", errors.New("asset extension does not match endpoint")
	}
	stem := strings.TrimSuffix(base, originalExt)
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
	infos := make([]Info, 0, len(entries))
	for _, entry := range entries {
		if entry.IsDir() || strings.ToLower(filepath.Ext(entry.Name())) != extension(kind) {
			continue
		}
		stat, err := entry.Info()
		if err != nil {
			return nil, err
		}
		infos = append(infos, Info{
			ID: entry.Name(), Kind: jsonKind(kind), Filename: entry.Name(),
			Path: s.rel(kind, entry.Name()), SizeBytes: stat.Size(),
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
			return Info{}, ErrExists
		} else if !errors.Is(err, os.ErrNotExist) {
			return Info{}, err
		}
	}
	tmp, err := os.OpenFile(tmpPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return Info{}, err
	}
	if _, err := io.Copy(tmp, r); err != nil {
		_ = tmp.Close()
		_ = os.Remove(tmpPath)
		return Info{}, err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		_ = os.Remove(tmpPath)
		return Info{}, err
	}
	if err := tmp.Close(); err != nil {
		_ = os.Remove(tmpPath)
		return Info{}, err
	}
	if err := os.Rename(tmpPath, finalPath); err != nil {
		_ = os.Remove(tmpPath)
		return Info{}, err
	}
	stat, err := os.Stat(finalPath)
	if err != nil {
		return Info{}, err
	}
	return Info{ID: safe, Kind: jsonKind(kind), Filename: safe, Path: s.rel(kind, safe), SizeBytes: stat.Size()}, nil
}

func (s Store) Delete(kind Kind, id string) error {
	if id == "" || filepath.Base(id) != id || strings.Contains(id, "..") {
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
