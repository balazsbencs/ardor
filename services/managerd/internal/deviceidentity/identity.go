package deviceidentity

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	identityVersion = 1
	identityDir     = "identity"
	identityFile    = "device.json"
)

// Identity is the device's durable cloud identity. PrivateKey is intentionally
// excluded from JSON so logging or serializing this value cannot disclose it.
type Identity struct {
	DeviceID   string
	PublicKey  ed25519.PublicKey
	PrivateKey ed25519.PrivateKey `json:"-"`
	ClaimEpoch uint64
	CreatedAt  time.Time
}

type storedIdentity struct {
	Version    int    `json:"version"`
	DeviceID   string `json:"deviceId"`
	PublicKey  string `json:"publicKey"`
	PrivateKey string `json:"privateKey"`
	ClaimEpoch uint64 `json:"claimEpoch"`
	CreatedAt  string `json:"createdAt"`
}

// LoadOrCreate loads the stable device identity under dataRoot. A corrupt or
// insecure existing identity is reported and is never silently replaced.
func LoadOrCreate(dataRoot string) (*Identity, error) {
	return loadOrCreateAt(filepath.Join(dataRoot, identityDir, identityFile))
}

func loadOrCreateAt(path string) (*Identity, error) {
	info, err := os.Lstat(path)
	switch {
	case err == nil:
		return load(path, info)
	case !errors.Is(err, os.ErrNotExist):
		return nil, fmt.Errorf("inspect device identity: %w", err)
	}

	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return nil, fmt.Errorf("create identity directory: %w", err)
	}
	if err := os.Chmod(filepath.Dir(path), 0o700); err != nil {
		return nil, fmt.Errorf("secure identity directory: %w", err)
	}

	identity, err := generate()
	if err != nil {
		return nil, err
	}
	if err := persist(path, identity); err != nil {
		if errors.Is(err, os.ErrExist) {
			info, statErr := os.Lstat(path)
			if statErr != nil {
				return nil, fmt.Errorf("load concurrently created identity: %w", statErr)
			}
			return load(path, info)
		}
		return nil, err
	}
	return identity, nil
}

func generate() (*Identity, error) {
	publicKey, privateKey, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, fmt.Errorf("generate device key: %w", err)
	}
	deviceID, err := uuidV4()
	if err != nil {
		return nil, fmt.Errorf("generate device id: %w", err)
	}
	return &Identity{
		DeviceID:   deviceID,
		PublicKey:  publicKey,
		PrivateKey: privateKey,
		CreatedAt:  time.Now().UTC().Truncate(time.Second),
	}, nil
}

func load(path string, info os.FileInfo) (*Identity, error) {
	if info.Mode()&os.ModeSymlink != 0 || !info.Mode().IsRegular() {
		return nil, errors.New("device identity must be a regular file")
	}
	if info.Mode().Perm()&0o077 != 0 {
		return nil, fmt.Errorf("device identity permissions %04o expose private key", info.Mode().Perm())
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read device identity: %w", err)
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	var stored storedIdentity
	if err := decoder.Decode(&stored); err != nil {
		return nil, fmt.Errorf("decode device identity: %w", err)
	}
	if err := ensureJSONEOF(decoder); err != nil {
		return nil, fmt.Errorf("decode device identity: %w", err)
	}
	return validateStored(stored)
}

func validateStored(stored storedIdentity) (*Identity, error) {
	if stored.Version != identityVersion {
		return nil, fmt.Errorf("unsupported device identity version %d", stored.Version)
	}
	if !validUUID(stored.DeviceID) {
		return nil, errors.New("device identity has invalid deviceId")
	}
	publicKey, err := base64.StdEncoding.DecodeString(stored.PublicKey)
	if err != nil || len(publicKey) != ed25519.PublicKeySize {
		return nil, errors.New("device identity has invalid public key")
	}
	privateKey, err := base64.StdEncoding.DecodeString(stored.PrivateKey)
	if err != nil || len(privateKey) != ed25519.PrivateKeySize {
		return nil, errors.New("device identity has invalid private key")
	}
	derivedPublic := ed25519.PrivateKey(privateKey).Public().(ed25519.PublicKey)
	if !bytes.Equal(publicKey, derivedPublic) {
		return nil, errors.New("device identity public and private keys do not match")
	}
	createdAt, err := time.Parse(time.RFC3339, stored.CreatedAt)
	if err != nil {
		return nil, errors.New("device identity has invalid createdAt")
	}
	return &Identity{
		DeviceID:   stored.DeviceID,
		PublicKey:  ed25519.PublicKey(publicKey),
		PrivateKey: ed25519.PrivateKey(privateKey),
		ClaimEpoch: stored.ClaimEpoch,
		CreatedAt:  createdAt,
	}, nil
}

func persist(path string, identity *Identity) error {
	stored := storedIdentity{
		Version:    identityVersion,
		DeviceID:   identity.DeviceID,
		PublicKey:  base64.StdEncoding.EncodeToString(identity.PublicKey),
		PrivateKey: base64.StdEncoding.EncodeToString(identity.PrivateKey),
		ClaimEpoch: identity.ClaimEpoch,
		CreatedAt:  identity.CreatedAt.Format(time.RFC3339),
	}
	data, err := json.MarshalIndent(stored, "", "  ")
	if err != nil {
		return fmt.Errorf("encode device identity: %w", err)
	}
	data = append(data, '\n')

	temp, err := os.CreateTemp(filepath.Dir(path), ".device-*.json")
	if err != nil {
		return fmt.Errorf("create temporary device identity: %w", err)
	}
	tempPath := temp.Name()
	removeTemp := true
	defer func() {
		_ = temp.Close()
		if removeTemp {
			_ = os.Remove(tempPath)
		}
	}()
	if err := temp.Chmod(0o600); err != nil {
		return fmt.Errorf("secure temporary device identity: %w", err)
	}
	if _, err := temp.Write(data); err != nil {
		return fmt.Errorf("write device identity: %w", err)
	}
	if err := temp.Sync(); err != nil {
		return fmt.Errorf("sync device identity: %w", err)
	}
	if err := temp.Close(); err != nil {
		return fmt.Errorf("close device identity: %w", err)
	}
	// Linking is an atomic no-replace install: concurrent first boots cannot
	// overwrite one another's identity between a destination check and rename.
	if err := os.Link(tempPath, path); err != nil {
		if errors.Is(err, os.ErrExist) {
			return os.ErrExist
		}
		return fmt.Errorf("install device identity: %w", err)
	}
	if err := os.Remove(tempPath); err != nil {
		return fmt.Errorf("remove temporary device identity: %w", err)
	}
	removeTemp = false
	directory, err := os.Open(filepath.Dir(path))
	if err != nil {
		return fmt.Errorf("open identity directory for sync: %w", err)
	}
	defer directory.Close()
	if err := directory.Sync(); err != nil {
		return fmt.Errorf("sync identity directory: %w", err)
	}
	return nil
}

func (identity *Identity) PublicKeyBase64() string {
	return base64.StdEncoding.EncodeToString(identity.PublicKey)
}

func (identity *Identity) Sign(message []byte) []byte {
	return ed25519.Sign(identity.PrivateKey, message)
}

func uuidV4() (string, error) {
	var value [16]byte
	if _, err := rand.Read(value[:]); err != nil {
		return "", err
	}
	value[6] = value[6]&0x0f | 0x40
	value[8] = value[8]&0x3f | 0x80
	encoded := hex.EncodeToString(value[:])
	return encoded[0:8] + "-" + encoded[8:12] + "-" + encoded[12:16] + "-" + encoded[16:20] + "-" + encoded[20:32], nil
}

func validUUID(value string) bool {
	if len(value) != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-' {
		return false
	}
	raw := strings.ReplaceAll(value, "-", "")
	decoded, err := hex.DecodeString(raw)
	return err == nil && len(decoded) == 16
}

func ensureJSONEOF(decoder *json.Decoder) error {
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return errors.New("multiple JSON values")
		}
		return err
	}
	return nil
}
