package config

import (
	"errors"
	"fmt"
	"net/url"
	"os"
	"strconv"
)

type Config struct {
	DataRoot                    string
	Bind                        string
	Port                        int
	AuthEnabled                 bool
	WiFiInterface               string
	WiFiControlScript           string
	CloudEnabled                bool
	CloudURL                    string
	CloudRemoteMutationsEnabled bool
	Tone3000ClientID            string
	Tone3000BaseURL             string
	DiscoveryEnabled            bool
	ActiveReleaseManifest       string
	BaseReleaseMetadata         string
	UpdatePublicKey             string
	SystemRoot                  string
	UpdaterExecutable           string
	SoftwareVersion             string
	BuildCommit                 string
	BaseSystemVersion           string
	UpdaterVersion              string
}

func LoadFromEnv() (Config, error) {
	cfg := Config{
		DataRoot:              env("ARDOR_DATA_ROOT", "/opt/ardor-pedal"),
		Bind:                  env("ARDOR_API_BIND", "0.0.0.0"),
		AuthEnabled:           env("ARDOR_API_AUTH", "on") != "off",
		WiFiInterface:         env("ARDOR_WIFI_INTERFACE", "wlan0"),
		WiFiControlScript:     env("ARDOR_WIFI_CONTROL_SCRIPT", "/etc/init.d/S42wifi"),
		CloudURL:              os.Getenv("ARDOR_CLOUD_URL"),
		Tone3000ClientID:      os.Getenv("TONE3000_CLIENT_ID"),
		Tone3000BaseURL:       env("TONE3000_BASE_URL", "https://www.tone3000.com"),
		ActiveReleaseManifest: os.Getenv("ARDOR_RELEASE_MANIFEST"),
		BaseReleaseMetadata:   env("ARDOR_BASE_RELEASE_METADATA", "/etc/ardor-release.json"),
		UpdatePublicKey:       env("ARDOR_UPDATE_PUBLIC_KEY", "/etc/ardor-update.pub"),
		SystemRoot:            env("ARDOR_SYSTEM_ROOT", "/opt/ardor-pedal/system"),
		UpdaterExecutable:     env("ARDOR_UPDATER_EXECUTABLE", "/usr/bin/ardor-updater"),
	}
	cloudEnabled, err := onOff("ARDOR_CLOUD_ENABLED", "off")
	if err != nil {
		return Config{}, err
	}
	cfg.CloudEnabled = cloudEnabled
	remoteMutations, err := onOff("ARDOR_CLOUD_REMOTE_MUTATIONS", "off")
	if err != nil {
		return Config{}, err
	}
	cfg.CloudRemoteMutationsEnabled = remoteMutations
	discoveryEnabled, err := onOff("ARDOR_MDNS", "on")
	if err != nil {
		return Config{}, err
	}
	cfg.DiscoveryEnabled = discoveryEnabled
	if cfg.CloudRemoteMutationsEnabled && !cfg.CloudEnabled {
		return Config{}, errors.New("ARDOR_CLOUD_REMOTE_MUTATIONS requires ARDOR_CLOUD_ENABLED=on")
	}
	if cfg.CloudEnabled {
		if err := validateCloudURL(cfg.CloudURL); err != nil {
			return Config{}, err
		}
	}
	port, err := strconv.Atoi(env("ARDOR_API_PORT", "8080"))
	if err != nil || port < 1 || port > 65535 {
		if err == nil {
			err = errors.New("port must be between 1 and 65535")
		}
		return Config{}, err
	}
	cfg.Port = port
	return cfg, nil
}

func onOff(key, fallback string) (bool, error) {
	switch env(key, fallback) {
	case "on":
		return true, nil
	case "off":
		return false, nil
	default:
		return false, fmt.Errorf("%s must be on or off", key)
	}
}

func validateCloudURL(raw string) error {
	parsed, err := url.Parse(raw)
	if err != nil {
		return fmt.Errorf("ARDOR_CLOUD_URL is invalid: %w", err)
	}
	if parsed.Scheme != "https" || parsed.Host == "" || parsed.User != nil || parsed.RawQuery != "" || parsed.Fragment != "" || (parsed.Path != "" && parsed.Path != "/") {
		return errors.New("ARDOR_CLOUD_URL must be an HTTPS origin without credentials, path, query, or fragment")
	}
	return nil
}

func env(key string, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}
