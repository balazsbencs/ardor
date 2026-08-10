package discovery

import "testing"

func TestStableHostnameUsesShortDeviceID(t *testing.T) {
	hostname, err := Hostname("018f7f1a-8b25-7e31-a951-5c43272e1920")
	if err != nil {
		t.Fatal(err)
	}
	if hostname != "ardor-018f7f1a" {
		t.Fatalf("hostname=%q", hostname)
	}
	if _, err := Hostname("not-a-device"); err == nil {
		t.Fatal("invalid device ID produced a hostname")
	}
}
