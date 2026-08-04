package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/base32"
	"encoding/base64"
	"errors"
	"regexp"
	"strings"
)

var usernamePattern = regexp.MustCompile(`^[a-z0-9][a-z0-9._-]{2,31}$`)

func NormalizeUsername(value string) (string, error) {
	normalized := strings.ToLower(strings.TrimSpace(value))
	if !usernamePattern.MatchString(normalized) {
		return "", errors.New("username must be 3-32 characters using letters, numbers, dot, underscore, or hyphen")
	}
	return normalized, nil
}

func NewSessionToken() (string, [32]byte, error) {
	var raw [32]byte
	if _, err := rand.Read(raw[:]); err != nil {
		return "", [32]byte{}, err
	}
	token := base64.RawURLEncoding.EncodeToString(raw[:])
	return token, sha256.Sum256([]byte(token)), nil
}

func HashCredential(value string) [32]byte {
	return sha256.Sum256([]byte(value))
}

func NewRecoveryCodes(count int) ([]string, [][32]byte, error) {
	if count < 1 {
		return nil, nil, errors.New("recovery code count must be positive")
	}
	codes := make([]string, 0, count)
	hashes := make([][32]byte, 0, count)
	encoding := base32.StdEncoding.WithPadding(base32.NoPadding)
	for range count {
		var raw [15]byte
		if _, err := rand.Read(raw[:]); err != nil {
			return nil, nil, err
		}
		compact := encoding.EncodeToString(raw[:])
		code := strings.Join([]string{compact[0:6], compact[6:12], compact[12:18], compact[18:24]}, "-")
		codes = append(codes, code)
		hashes = append(hashes, HashCredential(code))
	}
	return codes, hashes, nil
}
