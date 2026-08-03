package config

import (
	"strings"
	"testing"
)

func TestCloudIsDisabledByDefault(t *testing.T) {
	setBaseEnvironment(t)
	cfg, err := LoadFromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.CloudEnabled || cfg.CloudRemoteMutationsEnabled {
		t.Fatalf("cloud flags unexpectedly enabled: %+v", cfg)
	}
}

func TestCloudRequiresHTTPSOrigin(t *testing.T) {
	setBaseEnvironment(t)
	t.Setenv("ARDOR_CLOUD_ENABLED", "on")
	t.Setenv("ARDOR_CLOUD_URL", "http://control.example.test")
	if _, err := LoadFromEnv(); err == nil || !strings.Contains(err.Error(), "HTTPS origin") {
		t.Fatalf("error = %v, want HTTPS origin error", err)
	}
	t.Setenv("ARDOR_CLOUD_URL", "https://control.example.test")
	cfg, err := LoadFromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.CloudEnabled {
		t.Fatal("cloud should be enabled")
	}
}

func TestRemoteMutationsCannotBeEnabledInV1(t *testing.T) {
	setBaseEnvironment(t)
	t.Setenv("ARDOR_CLOUD_REMOTE_MUTATIONS", "on")
	if _, err := LoadFromEnv(); err == nil || !strings.Contains(err.Error(), "cannot be enabled") {
		t.Fatalf("error = %v, want disabled mutation error", err)
	}
}

func TestCloudFlagsAreStrict(t *testing.T) {
	setBaseEnvironment(t)
	t.Setenv("ARDOR_CLOUD_ENABLED", "yes")
	if _, err := LoadFromEnv(); err == nil || !strings.Contains(err.Error(), "must be on or off") {
		t.Fatalf("error = %v, want strict flag error", err)
	}
}

func setBaseEnvironment(t *testing.T) {
	t.Helper()
	t.Setenv("ARDOR_API_AUTH", "off")
	t.Setenv("ARDOR_API_TOKEN", "")
	t.Setenv("ARDOR_API_PORT", "8080")
	t.Setenv("ARDOR_CLOUD_ENABLED", "")
	t.Setenv("ARDOR_CLOUD_URL", "")
	t.Setenv("ARDOR_CLOUD_REMOTE_MUTATIONS", "")
}
