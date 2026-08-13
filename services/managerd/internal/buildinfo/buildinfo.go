package buildinfo

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	"ardor.local/managerd/internal/update"
)

var (
	Version = "0.0.0"
	Commit  = "unknown"
)

type Info struct {
	SoftwareVersion string `json:"softwareVersion"`
	BuildCommit     string `json:"buildCommit"`
	BaseVersion     string `json:"baseSystemVersion"`
	UpdaterVersion  string `json:"updaterVersion"`
}

type releaseMetadata struct {
	Version string `json:"version"`
	Commit  string `json:"commit"`
}

func Load(activeManifestPath, baseMetadataPath string) (Info, error) {
	base := releaseMetadata{Version: Version, Commit: Commit}
	if err := readMetadata(baseMetadataPath, &base); err != nil && !errors.Is(err, os.ErrNotExist) {
		return Info{}, fmt.Errorf("read base release metadata: %w", err)
	}
	active := base
	if activeManifestPath != "" {
		if err := readMetadata(activeManifestPath, &active); err != nil {
			return Info{}, fmt.Errorf("read active release metadata: %w", err)
		}
	}
	if _, err := update.ParseVersion(base.Version); err != nil {
		return Info{}, fmt.Errorf("base release version: %w", err)
	}
	if _, err := update.ParseVersion(active.Version); err != nil {
		return Info{}, fmt.Errorf("active release version: %w", err)
	}
	return Info{
		SoftwareVersion: active.Version,
		BuildCommit:     active.Commit,
		BaseVersion:     base.Version,
		UpdaterVersion:  update.UpdaterVersion,
	}, nil
}

func readMetadata(path string, result *releaseMetadata) error {
	if path == "" {
		return os.ErrNotExist
	}
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	decoder := json.NewDecoder(io.LimitReader(file, 64<<10))
	if err := decoder.Decode(result); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("release metadata must contain exactly one JSON value")
	}
	if strings.TrimSpace(result.Commit) == "" {
		return errors.New("release metadata commit is empty")
	}
	return nil
}
