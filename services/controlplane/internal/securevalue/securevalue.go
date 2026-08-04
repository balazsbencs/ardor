package securevalue

import (
	"crypto/rand"
	"encoding/base32"
	"encoding/base64"
	"encoding/hex"
	"strings"
)

func UUID() (string, error) {
	var value [16]byte
	if _, err := rand.Read(value[:]); err != nil {
		return "", err
	}
	value[6] = value[6]&0x0f | 0x40
	value[8] = value[8]&0x3f | 0x80
	encoded := hex.EncodeToString(value[:])
	return encoded[0:8] + "-" + encoded[8:12] + "-" + encoded[12:16] + "-" + encoded[16:20] + "-" + encoded[20:32], nil
}

func Bytes(size int) ([]byte, error) {
	value := make([]byte, size)
	if _, err := rand.Read(value); err != nil {
		return nil, err
	}
	return value, nil
}

func Token(size int) (string, error) {
	value, err := Bytes(size)
	if err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(value), nil
}

func ManualCode() (string, error) {
	value, err := Bytes(5)
	if err != nil {
		return "", err
	}
	encoded := strings.TrimRight(base32.StdEncoding.EncodeToString(value), "=")
	encoded = strings.NewReplacer("0", "2", "1", "3", "I", "4", "O", "5").Replace(encoded)
	return encoded[0:4] + "-" + encoded[4:8], nil
}
