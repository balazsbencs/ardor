package wifi

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSaveAndReadSettingsWithoutExposingPassword(t *testing.T) {
	dataRoot := t.TempDir()
	store := NewStore(dataRoot, "", "")
	password := `backstage"wifi`

	settings, err := store.Save(Update{SSID: "Ardor Stage", Password: &password, Country: "hu"})
	if err != nil {
		t.Fatal(err)
	}
	if !settings.Configured || settings.SSID != "Ardor Stage" || settings.Country != "HU" {
		t.Fatalf("settings=%+v", settings)
	}

	body, err := os.ReadFile(filepath.Join(dataRoot, "wifi", "wpa_supplicant.conf"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(body), `ssid="Ardor Stage"`) || !strings.Contains(string(body), `psk="backstage\"wifi"`) {
		t.Fatalf("config=%s", body)
	}
	info, err := os.Stat(filepath.Join(dataRoot, "wifi", "wpa_supplicant.conf"))
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("mode=%o", info.Mode().Perm())
	}

	read, err := store.Get()
	if err != nil {
		t.Fatal(err)
	}
	if read.SSID != "Ardor Stage" || read.Country != "HU" {
		t.Fatalf("read=%+v", read)
	}
}

func TestSaveKeepsExistingPasswordWhenOmitted(t *testing.T) {
	dataRoot := t.TempDir()
	store := NewStore(dataRoot, "", "")
	password := "original-secret"
	if _, err := store.Save(Update{SSID: "First", Password: &password, Country: "HU"}); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Save(Update{SSID: "Renamed", Country: "DE"}); err != nil {
		t.Fatal(err)
	}
	body, err := os.ReadFile(filepath.Join(dataRoot, "wifi", "wpa_supplicant.conf"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(body), `psk="original-secret"`) {
		t.Fatalf("password was not preserved: %s", body)
	}
}

func TestSaveRejectsInvalidSettings(t *testing.T) {
	store := NewStore(t.TempDir(), "", "")
	short := "short"
	if _, err := store.Save(Update{SSID: "Ardor", Password: &short, Country: "HU"}); err == nil {
		t.Fatal("expected short password to fail")
	}
	valid := "long-enough"
	if _, err := store.Save(Update{SSID: "", Password: &valid, Country: "HU"}); err == nil {
		t.Fatal("expected empty SSID to fail")
	}
	if _, err := store.Save(Update{SSID: "Ardor", Password: &valid, Country: "HUN"}); err == nil {
		t.Fatal("expected invalid country to fail")
	}
}
