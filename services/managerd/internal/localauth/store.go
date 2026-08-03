package localauth

import (
	"bytes"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base32"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"golang.org/x/crypto/argon2"
)

const (
	accountVersion     = 1
	setupVersion       = 1
	setupTTL           = 15 * time.Minute
	sessionTTL         = 24 * time.Hour
	minimumPasswordLen = 12
	maximumPasswordLen = 128
	argonMemoryKiB     = 32 * 1024
	argonIterations    = 3
	argonParallelism   = 2
	argonSaltBytes     = 16
	argonKeyBytes      = 32
)

var (
	ErrSetupRequired = errors.New("local account setup is required")
	ErrAlreadySetup  = errors.New("local account is already configured")
	ErrInvalidSetup  = errors.New("setup code is invalid or expired")
	ErrUnauthorized  = errors.New("invalid local username or password")
	usernamePattern  = regexp.MustCompile(`^[a-z0-9][a-z0-9._-]{2,31}$`)
)

type Account struct {
	Username string `json:"username"`
}

type storedAccount struct {
	Version            int       `json:"version"`
	UsernameNormalized string    `json:"usernameNormalized"`
	UsernameDisplay    string    `json:"usernameDisplay"`
	PasswordHash       string    `json:"passwordHash"`
	CreatedAt          time.Time `json:"createdAt"`
	UpdatedAt          time.Time `json:"updatedAt"`
}

type setupCode struct {
	Version    int       `json:"version"`
	SetupID    string    `json:"setupId"`
	ManualCode string    `json:"manualCode"`
	ExpiresAt  time.Time `json:"expiresAt"`
}

type Store struct {
	mu          sync.Mutex
	dataRoot    string
	accountPath string
	setupPath   string
	account     *storedAccount
	sessions    map[[32]byte]time.Time
	dummyHash   string
}

func New(dataRoot string) (*Store, error) {
	if strings.TrimSpace(dataRoot) == "" {
		return nil, errors.New("local auth requires a data root")
	}
	directory := filepath.Join(dataRoot, "auth")
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return nil, fmt.Errorf("create local auth directory: %w", err)
	}
	if err := os.Chmod(directory, 0o700); err != nil {
		return nil, fmt.Errorf("secure local auth directory: %w", err)
	}
	dummyHash, err := hashPassword("local-auth timing dummy")
	if err != nil {
		return nil, err
	}
	store := &Store{
		dataRoot: dataRoot, accountPath: filepath.Join(directory, "account.json"),
		setupPath: filepath.Join(dataRoot, "runtime", "local-access", "setup.json"),
		sessions:  map[[32]byte]time.Time{}, dummyHash: dummyHash,
	}
	if err := store.loadAccount(); err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	if store.account == nil {
		if _, err := store.ensureSetupLocked(time.Now().UTC()); err != nil {
			return nil, err
		}
	}
	return store, nil
}

func (store *Store) SetupRequired() bool {
	store.mu.Lock()
	defer store.mu.Unlock()
	return store.account == nil
}

func (store *Store) EnsureSetupCode(now time.Time) error {
	store.mu.Lock()
	defer store.mu.Unlock()
	if store.account != nil {
		return ErrAlreadySetup
	}
	_, err := store.ensureSetupLocked(now)
	return err
}

func (store *Store) Setup(code, username, password string, now time.Time) (Account, string, error) {
	store.mu.Lock()
	defer store.mu.Unlock()
	if store.account != nil {
		return Account{}, "", ErrAlreadySetup
	}
	setup, err := store.ensureSetupLocked(now)
	if err != nil {
		return Account{}, "", err
	}
	provided := strings.ToUpper(strings.TrimSpace(code))
	if subtle.ConstantTimeCompare([]byte(provided), []byte(setup.ManualCode)) != 1 {
		return Account{}, "", ErrInvalidSetup
	}
	normalized, display, err := normalizeUsername(username)
	if err != nil {
		return Account{}, "", err
	}
	hash, err := hashPassword(password)
	if err != nil {
		return Account{}, "", err
	}
	account := &storedAccount{
		Version: accountVersion, UsernameNormalized: normalized, UsernameDisplay: display,
		PasswordHash: hash, CreatedAt: now, UpdatedAt: now,
	}
	if err := writeAtomicJSON(store.accountPath, account, 0o600); err != nil {
		return Account{}, "", err
	}
	store.account = account
	_ = os.Remove(store.setupPath)
	token, err := store.newSessionLocked(now)
	return Account{Username: display}, token, err
}

func (store *Store) Login(username, password string, now time.Time) (Account, string, error) {
	store.mu.Lock()
	defer store.mu.Unlock()
	if store.account == nil {
		return Account{}, "", ErrSetupRequired
	}
	normalized, _, normalizeErr := normalizeUsername(username)
	hash := store.account.PasswordHash
	if normalizeErr != nil || normalized != store.account.UsernameNormalized {
		hash = store.dummyHash
	}
	if !verifyPassword(hash, password) || normalizeErr != nil || normalized != store.account.UsernameNormalized {
		return Account{}, "", ErrUnauthorized
	}
	token, err := store.newSessionLocked(now)
	return Account{Username: store.account.UsernameDisplay}, token, err
}

func (store *Store) Authenticate(token string, now time.Time) (Account, bool) {
	if token == "" {
		return Account{}, false
	}
	hash := sha256.Sum256([]byte(token))
	store.mu.Lock()
	defer store.mu.Unlock()
	expiresAt, ok := store.sessions[hash]
	if !ok || !expiresAt.After(now) || store.account == nil {
		delete(store.sessions, hash)
		return Account{}, false
	}
	return Account{Username: store.account.UsernameDisplay}, true
}

func (store *Store) Logout(token string) {
	store.mu.Lock()
	defer store.mu.Unlock()
	delete(store.sessions, sha256.Sum256([]byte(token)))
}

func (store *Store) ResetLocalAccess(now time.Time) error {
	store.mu.Lock()
	defer store.mu.Unlock()
	if err := os.Remove(store.accountPath); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	if err := syncDirectory(filepath.Dir(store.accountPath)); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	store.account = nil
	store.sessions = map[[32]byte]time.Time{}
	_, err := store.ensureSetupLocked(now)
	return err
}

func (store *Store) loadAccount() error {
	info, err := os.Lstat(store.accountPath)
	if err != nil {
		return err
	}
	if !info.Mode().IsRegular() || info.Mode().Perm()&0o077 != 0 {
		return errors.New("local account file must be a private regular file")
	}
	var account storedAccount
	if err := readStrictJSON(store.accountPath, &account, 16*1024); err != nil {
		return fmt.Errorf("read local account: %w", err)
	}
	if account.Version != accountVersion || account.UsernameNormalized == "" || account.UsernameDisplay == "" || !validPasswordHash(account.PasswordHash) {
		return errors.New("local account file is invalid")
	}
	store.account = &account
	return nil
}

func (store *Store) ensureSetupLocked(now time.Time) (setupCode, error) {
	var current setupCode
	if err := readStrictJSON(store.setupPath, &current, 8*1024); err == nil && current.Version == setupVersion && current.SetupID != "" && current.ManualCode != "" && current.ExpiresAt.After(now) {
		return current, nil
	}
	code, err := newManualCode()
	if err != nil {
		return setupCode{}, err
	}
	setupID, err := newToken(16)
	if err != nil {
		return setupCode{}, err
	}
	current = setupCode{Version: setupVersion, SetupID: setupID, ManualCode: code, ExpiresAt: now.Add(setupTTL)}
	if err := writeAtomicJSON(store.setupPath, current, 0o600); err != nil {
		return setupCode{}, err
	}
	return current, nil
}

func (store *Store) newSessionLocked(now time.Time) (string, error) {
	token, err := newToken(32)
	if err != nil {
		return "", err
	}
	for hash, expiry := range store.sessions {
		if !expiry.After(now) {
			delete(store.sessions, hash)
		}
	}
	store.sessions[sha256.Sum256([]byte(token))] = now.Add(sessionTTL)
	return token, nil
}

func normalizeUsername(value string) (string, string, error) {
	display := strings.TrimSpace(value)
	normalized := strings.ToLower(display)
	if !usernamePattern.MatchString(normalized) {
		return "", "", errors.New("username must be 3-32 characters using letters, numbers, dot, underscore, or hyphen")
	}
	return normalized, display, nil
}

func hashPassword(password string) (string, error) {
	if err := validatePassword(password); err != nil {
		return "", err
	}
	salt := make([]byte, argonSaltBytes)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key := argon2.IDKey([]byte(password), salt, argonIterations, argonMemoryKiB, argonParallelism, argonKeyBytes)
	return fmt.Sprintf("$argon2id$v=%d$m=%d,t=%d,p=%d$%s$%s", argon2.Version, argonMemoryKiB, argonIterations, argonParallelism, base64.RawStdEncoding.EncodeToString(salt), base64.RawStdEncoding.EncodeToString(key)), nil
}

func validatePassword(password string) error {
	length := utf8.RuneCountInString(password)
	if !utf8.ValidString(password) || length < minimumPasswordLen || length > maximumPasswordLen {
		return fmt.Errorf("password must contain between %d and %d characters", minimumPasswordLen, maximumPasswordLen)
	}
	return nil
}

func verifyPassword(encoded, password string) bool {
	parameters, salt, want, ok := decodePasswordHash(encoded)
	if !ok {
		return false
	}
	got := argon2.IDKey([]byte(password), salt, parameters.iterations, parameters.memory, parameters.parallelism, uint32(len(want)))
	return subtle.ConstantTimeCompare(got, want) == 1
}

type passwordParameters struct {
	memory, iterations uint32
	parallelism        uint8
}

func decodePasswordHash(encoded string) (passwordParameters, []byte, []byte, bool) {
	parts := strings.Split(encoded, "$")
	if len(parts) != 6 || parts[1] != "argon2id" {
		return passwordParameters{}, nil, nil, false
	}
	var version int
	var parameters passwordParameters
	if _, err := fmt.Sscanf(parts[2], "v=%d", &version); err != nil || version != argon2.Version {
		return passwordParameters{}, nil, nil, false
	}
	if _, err := fmt.Sscanf(parts[3], "m=%d,t=%d,p=%d", &parameters.memory, &parameters.iterations, &parameters.parallelism); err != nil || parameters.memory < 8*1024 || parameters.memory > 256*1024 || parameters.iterations < 1 || parameters.iterations > 10 || parameters.parallelism < 1 || parameters.parallelism > 8 {
		return passwordParameters{}, nil, nil, false
	}
	salt, saltErr := base64.RawStdEncoding.DecodeString(parts[4])
	want, wantErr := base64.RawStdEncoding.DecodeString(parts[5])
	if saltErr != nil || wantErr != nil || len(salt) < 16 || len(salt) > 64 || len(want) < 16 || len(want) > 64 {
		return passwordParameters{}, nil, nil, false
	}
	return parameters, salt, want, true
}

func validPasswordHash(encoded string) bool {
	_, _, _, ok := decodePasswordHash(encoded)
	return ok
}

func newManualCode() (string, error) {
	var value [5]byte
	if _, err := rand.Read(value[:]); err != nil {
		return "", err
	}
	encoded := base32.StdEncoding.WithPadding(base32.NoPadding).EncodeToString(value[:])
	return encoded[:4] + "-" + encoded[4:8], nil
}

func newToken(size int) (string, error) {
	value := make([]byte, size)
	if _, err := rand.Read(value); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(value), nil
}

func writeAtomicJSON(path string, value any, mode os.FileMode) error {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(path), ".local-auth-*.tmp")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer func() { _ = temporary.Close(); _ = os.Remove(temporaryPath) }()
	if err := temporary.Chmod(mode); err != nil {
		return err
	}
	if _, err := temporary.Write(data); err != nil {
		return err
	}
	if err := temporary.Sync(); err != nil {
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	if err := os.Rename(temporaryPath, path); err != nil {
		return err
	}
	return syncDirectory(filepath.Dir(path))
}

func syncDirectory(path string) error {
	directory, err := os.Open(path)
	if err != nil {
		return err
	}
	defer directory.Close()
	return directory.Sync()
}

func readStrictJSON(path string, value any, limit int64) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if int64(len(data)) > limit {
		return errors.New("JSON file exceeds size limit")
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("JSON file contains multiple values")
	}
	return nil
}
