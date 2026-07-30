package wifi

import (
	"bufio"
	"context"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
	"unicode"
)

type Settings struct {
	Configured bool   `json:"configured"`
	SSID       string `json:"ssid,omitempty"`
	Country    string `json:"country"`
	Status     string `json:"status"`
	IPAddress  string `json:"ipAddress,omitempty"`
}

type Update struct {
	SSID     string  `json:"ssid"`
	Password *string `json:"password"`
	Country  string  `json:"country"`
}

type Store struct {
	configPath    string
	iface         string
	controlScript string
}

func NewStore(dataRoot, iface, controlScript string) Store {
	return Store{
		configPath:    filepath.Join(dataRoot, "wifi", "wpa_supplicant.conf"),
		iface:         strings.TrimSpace(iface),
		controlScript: strings.TrimSpace(controlScript),
	}
}

func (s Store) Get() (Settings, error) {
	settings := Settings{Country: "HU", Status: "disconnected"}
	body, err := os.ReadFile(s.configPath)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return Settings{}, err
	}
	if err == nil {
		values := parseConfig(string(body))
		settings.Configured = values["ssid"] != ""
		settings.SSID = values["ssid"]
		if values["country"] != "" {
			settings.Country = values["country"]
		}
	}

	if status, ip := s.runtimeStatus(); status != "" {
		settings.Status = status
		settings.IPAddress = ip
	}
	return settings, nil
}

func (s Store) Save(update Update) (Settings, error) {
	update.SSID = strings.TrimSpace(update.SSID)
	update.Country = strings.ToUpper(strings.TrimSpace(update.Country))
	if err := validateSSID(update.SSID); err != nil {
		return Settings{}, err
	}
	if len(update.Country) != 2 || update.Country[0] < 'A' || update.Country[0] > 'Z' ||
		update.Country[1] < 'A' || update.Country[1] > 'Z' {
		return Settings{}, errors.New("country must be a two-letter ISO country code")
	}

	password := ""
	if update.Password != nil {
		password = *update.Password
	} else if body, err := os.ReadFile(s.configPath); err == nil {
		password = parseConfig(string(body))["psk"]
	}
	if err := validatePassword(password); err != nil {
		return Settings{}, err
	}

	dir := filepath.Dir(s.configPath)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return Settings{}, err
	}
	config := fmt.Sprintf(
		"ctrl_interface=/run/wpa_supplicant\nupdate_config=0\ncountry=%s\n\nnetwork={\n    ssid=%s\n    psk=%s\n}\n",
		update.Country,
		strconv.Quote(update.SSID),
		formatPassword(password),
	)
	tmp, err := os.CreateTemp(dir, ".wpa_supplicant.conf-*")
	if err != nil {
		return Settings{}, err
	}
	tmpPath := tmp.Name()
	defer os.Remove(tmpPath)
	if err := tmp.Chmod(0o600); err != nil {
		_ = tmp.Close()
		return Settings{}, err
	}
	if _, err := tmp.WriteString(config); err != nil {
		_ = tmp.Close()
		return Settings{}, err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		return Settings{}, err
	}
	if err := tmp.Close(); err != nil {
		return Settings{}, err
	}
	if err := os.Rename(tmpPath, s.configPath); err != nil {
		return Settings{}, err
	}
	return Settings{
		Configured: true,
		SSID:       update.SSID,
		Country:    update.Country,
		Status:     "restarting",
	}, nil
}

func (s Store) Restart() error {
	if s.controlScript == "" {
		return nil
	}
	command := exec.Command(s.controlScript, "restart")
	output, err := command.CombinedOutput()
	if err != nil {
		return fmt.Errorf("restart Wi-Fi: %w: %s", err, strings.TrimSpace(string(output)))
	}
	return nil
}

func (s Store) runtimeStatus() (string, string) {
	if s.iface == "" {
		return "", ""
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	output, err := exec.CommandContext(ctx, "wpa_cli", "-i", s.iface, "status").Output()
	if err != nil {
		return "", ""
	}
	values := parseLines(string(output))
	status := values["wpa_state"]
	switch status {
	case "COMPLETED":
		status = "connected"
	case "SCANNING", "ASSOCIATING", "ASSOCIATED", "4WAY_HANDSHAKE", "GROUP_HANDSHAKE":
		status = "connecting"
	case "DISCONNECTED", "INACTIVE", "INTERFACE_DISABLED":
		status = "disconnected"
	default:
		status = strings.ToLower(status)
	}
	return status, values["ip_address"]
}

func parseConfig(body string) map[string]string {
	values := parseLines(body)
	for _, key := range []string{"ssid", "psk"} {
		if raw := values[key]; raw != "" {
			if decoded, err := strconv.Unquote(raw); err == nil {
				values[key] = decoded
			}
		}
	}
	return values
}

func parseLines(body string) map[string]string {
	values := map[string]string{}
	scanner := bufio.NewScanner(strings.NewReader(body))
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, value, ok := strings.Cut(line, "=")
		if ok {
			values[strings.TrimSpace(key)] = strings.TrimSpace(value)
		}
	}
	return values
}

func validateSSID(ssid string) error {
	if len(ssid) < 1 || len(ssid) > 32 {
		return errors.New("network name must be between 1 and 32 bytes")
	}
	for _, character := range ssid {
		if unicode.IsControl(character) {
			return errors.New("network name cannot contain control characters")
		}
	}
	return nil
}

func validatePassword(password string) error {
	if len(password) >= 8 && len(password) <= 63 {
		for _, character := range password {
			if unicode.IsControl(character) {
				return errors.New("Wi-Fi password cannot contain control characters")
			}
		}
		return nil
	}
	if len(password) == 64 {
		if _, err := hex.DecodeString(password); err == nil {
			return nil
		}
	}
	return errors.New("Wi-Fi password must be 8–63 characters, or a 64-character hexadecimal key")
}

func formatPassword(password string) string {
	if len(password) == 64 {
		if _, err := hex.DecodeString(password); err == nil {
			return password
		}
	}
	return strconv.Quote(password)
}
