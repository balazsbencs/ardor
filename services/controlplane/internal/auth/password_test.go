package auth

import "testing"

func TestPasswordHashRoundTrip(t *testing.T) {
	encoded, err := HashPassword("this is a good password")
	if err != nil {
		t.Fatal(err)
	}
	if !VerifyPassword(encoded, "this is a good password") {
		t.Fatal("password did not verify")
	}
	if VerifyPassword(encoded, "this is the wrong password") {
		t.Fatal("wrong password verified")
	}
}

func TestPasswordAndUsernameValidation(t *testing.T) {
	if _, err := HashPassword("too short"); err == nil {
		t.Fatal("expected short password rejection")
	}
	if got, err := NormalizeUsername("  Alice.Example "); err != nil || got != "alice.example" {
		t.Fatalf("normalized username = %q, error = %v", got, err)
	}
	if _, err := NormalizeUsername("not allowed!"); err == nil {
		t.Fatal("expected invalid username rejection")
	}
}

func TestRecoveryCodesAreUniqueAndHashable(t *testing.T) {
	codes, hashes, err := NewRecoveryCodes(8)
	if err != nil {
		t.Fatal(err)
	}
	seen := map[string]bool{}
	for index, code := range codes {
		if seen[code] || HashCredential(code) != hashes[index] {
			t.Fatal("recovery codes were duplicated or hashed incorrectly")
		}
		seen[code] = true
	}
}
