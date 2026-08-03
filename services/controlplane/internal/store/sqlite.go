package store

import (
	"bytes"
	"context"
	"database/sql"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"sort"
	"strings"
	"time"

	"ardor.local/controlplane/internal/securevalue"
	_ "modernc.org/sqlite"
)

//go:embed migrations/*.sql
var migrationFiles embed.FS

type SQLite struct {
	database *sql.DB
}

func OpenSQLite(path string) (*SQLite, error) {
	var dataSource string
	if path == ":memory:" {
		dataSource = "file:ardor-controlplane?mode=memory&cache=shared"
	} else {
		dataSource = (&url.URL{Scheme: "file", Path: path}).String()
	}
	separator := "?"
	if strings.Contains(dataSource, "?") {
		separator = "&"
	}
	dataSource += separator + "_pragma=busy_timeout(5000)&_pragma=journal_mode(WAL)&_pragma=foreign_keys(1)&_pragma=synchronous(NORMAL)"
	database, err := sql.Open("sqlite", dataSource)
	if err != nil {
		return nil, fmt.Errorf("open SQLite database: %w", err)
	}
	// A single writer connection makes transaction ordering deterministic. WAL
	// still lets external backup/read tooling operate without blocking writes.
	database.SetMaxOpenConns(1)
	database.SetMaxIdleConns(1)
	if err := database.Ping(); err != nil {
		database.Close()
		return nil, fmt.Errorf("ping SQLite database: %w", err)
	}
	return &SQLite{database: database}, nil
}

func (sqlite *SQLite) Close() error {
	return sqlite.database.Close()
}

func (sqlite *SQLite) Migrate(ctx context.Context) error {
	entries, err := migrationFiles.ReadDir("migrations")
	if err != nil {
		return err
	}
	sort.Slice(entries, func(left, right int) bool { return entries[left].Name() < entries[right].Name() })
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		migration, err := migrationFiles.ReadFile("migrations/" + entry.Name())
		if err != nil {
			return err
		}
		if _, err := sqlite.database.ExecContext(ctx, string(migration)); err != nil {
			return fmt.Errorf("apply migration %s: %w", entry.Name(), err)
		}
	}
	return nil
}

func (sqlite *SQLite) CreateAccount(ctx context.Context, account Account, recovery [][32]byte) error {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer transaction.Rollback()
	var exists int
	if err := transaction.QueryRowContext(ctx, `SELECT EXISTS(SELECT 1 FROM accounts WHERE username_normalized=?)`, account.UsernameNormalized).Scan(&exists); err != nil {
		return err
	}
	if exists != 0 {
		return ErrConflict
	}
	if _, err := transaction.ExecContext(ctx, `INSERT INTO accounts
        (id, username_normalized, username_display, password_hash, state, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)`, account.ID, account.UsernameNormalized, account.UsernameDisplay, account.PasswordHash, account.State, account.CreatedAt, account.UpdatedAt); err != nil {
		return err
	}
	for _, hash := range recovery {
		id, err := securevalue.UUID()
		if err != nil {
			return err
		}
		if _, err := transaction.ExecContext(ctx, `INSERT INTO recovery_codes (id, account_id, code_hash, created_at) VALUES (?, ?, ?, ?)`, id, account.ID, hash[:], account.CreatedAt); err != nil {
			return err
		}
	}
	if err := appendAuditSQL(ctx, transaction, AuditEvent{ActorType: "account", ActorID: account.ID, EventType: "account.registered", SubjectType: "account", SubjectID: account.ID, CreatedAt: account.CreatedAt}); err != nil {
		return err
	}
	return transaction.Commit()
}

func (sqlite *SQLite) AccountByUsername(ctx context.Context, username string) (Account, error) {
	var account Account
	err := sqlite.database.QueryRowContext(ctx, `SELECT id, username_normalized, username_display, password_hash, state, created_at, updated_at FROM accounts WHERE username_normalized=?`, username).Scan(
		&account.ID, &account.UsernameNormalized, &account.UsernameDisplay, &account.PasswordHash, &account.State, &account.CreatedAt, &account.UpdatedAt)
	return account, sqlNotFound(err)
}

func (sqlite *SQLite) CreateSession(ctx context.Context, session Session) error {
	_, err := sqlite.database.ExecContext(ctx, `INSERT INTO sessions (id, account_id, token_hash, created_at, expires_at) VALUES (?,?,?,?,?)`, session.ID, session.AccountID, session.TokenHash[:], session.CreatedAt, session.ExpiresAt)
	return err
}

func (sqlite *SQLite) AccountBySession(ctx context.Context, hash [32]byte, now time.Time) (Account, error) {
	var account Account
	err := sqlite.database.QueryRowContext(ctx, `SELECT a.id, a.username_normalized, a.username_display, a.password_hash, a.state, a.created_at, a.updated_at
        FROM sessions s JOIN accounts a ON a.id=s.account_id
        WHERE s.token_hash=? AND s.revoked_at IS NULL AND s.expires_at>? AND a.state='active'`, hash[:], now).Scan(
		&account.ID, &account.UsernameNormalized, &account.UsernameDisplay, &account.PasswordHash, &account.State, &account.CreatedAt, &account.UpdatedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Account{}, ErrUnauthorized
	}
	return account, err
}

func (sqlite *SQLite) RevokeSession(ctx context.Context, hash [32]byte, now time.Time) error {
	result, err := sqlite.database.ExecContext(ctx, `UPDATE sessions SET revoked_at=? WHERE token_hash=? AND revoked_at IS NULL`, now, hash[:])
	return affectedOrNotFound(result, err)
}

func (sqlite *SQLite) RevokeAllSessions(ctx context.Context, accountID string, now time.Time) error {
	_, err := sqlite.database.ExecContext(ctx, `UPDATE sessions SET revoked_at=? WHERE account_id=? AND revoked_at IS NULL`, now, accountID)
	return err
}

func (sqlite *SQLite) RecoverAccount(ctx context.Context, username string, codeHash [32]byte, passwordHash string, now time.Time) (Account, error) {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return Account{}, err
	}
	defer transaction.Rollback()
	var account Account
	err = transaction.QueryRowContext(ctx, `SELECT a.id, a.username_normalized, a.username_display, a.password_hash, a.state, a.created_at, a.updated_at
        FROM accounts a JOIN recovery_codes r ON r.account_id=a.id
        WHERE a.username_normalized=? AND r.code_hash=? AND r.used_at IS NULL`, username, codeHash[:]).Scan(
		&account.ID, &account.UsernameNormalized, &account.UsernameDisplay, &account.PasswordHash, &account.State, &account.CreatedAt, &account.UpdatedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Account{}, ErrUnauthorized
	}
	if err != nil {
		return Account{}, err
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE recovery_codes SET used_at=? WHERE account_id=? AND code_hash=? AND used_at IS NULL`, now, account.ID, codeHash[:]); err != nil {
		return Account{}, err
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE accounts SET password_hash=?, updated_at=? WHERE id=?`, passwordHash, now, account.ID); err != nil {
		return Account{}, err
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE sessions SET revoked_at=? WHERE account_id=? AND revoked_at IS NULL`, now, account.ID); err != nil {
		return Account{}, err
	}
	account.PasswordHash = passwordHash
	account.UpdatedAt = now
	if err := appendAuditSQL(ctx, transaction, AuditEvent{ActorType: "account", ActorID: account.ID, EventType: "account.recovered", SubjectType: "account", SubjectID: account.ID, CreatedAt: now}); err != nil {
		return Account{}, err
	}
	return account, transaction.Commit()
}

func (sqlite *SQLite) EnsureDevice(ctx context.Context, device Device) (Device, error) {
	if _, err := sqlite.database.ExecContext(ctx, `INSERT OR IGNORE INTO devices (id, public_key, claim_epoch, created_at, updated_at) VALUES (?,?,?,?,?)`, device.ID, device.PublicKey, device.ClaimEpoch, device.CreatedAt, device.UpdatedAt); err != nil {
		return Device{}, err
	}
	existing, err := sqlite.Device(ctx, device.ID)
	if err != nil {
		return Device{}, err
	}
	if !bytes.Equal(existing.PublicKey, device.PublicKey) {
		return Device{}, ErrConflict
	}
	return existing, nil
}

func (sqlite *SQLite) Device(ctx context.Context, id string) (Device, error) {
	var device Device
	var lastSeen sql.NullTime
	err := sqlite.database.QueryRowContext(ctx, `SELECT id, public_key, claim_epoch, last_seen_at, created_at, updated_at FROM devices WHERE id=?`, id).Scan(
		&device.ID, &device.PublicKey, &device.ClaimEpoch, &lastSeen, &device.CreatedAt, &device.UpdatedAt)
	if lastSeen.Valid {
		device.LastSeenAt = &lastSeen.Time
	}
	return device, sqlNotFound(err)
}

func (sqlite *SQLite) DeviceOwner(ctx context.Context, deviceID string) (string, error) {
	var accountID string
	err := sqlite.database.QueryRowContext(ctx, `SELECT account_id FROM device_memberships WHERE device_id=?`, deviceID).Scan(&accountID)
	return accountID, sqlNotFound(err)
}

func (sqlite *SQLite) CreateDeviceChallenge(ctx context.Context, challenge DeviceChallenge) error {
	_, err := sqlite.database.ExecContext(ctx, `INSERT INTO device_challenges (id, device_id, nonce, purpose, created_at, expires_at) VALUES (?,?,?,?,?,?)`, challenge.ID, challenge.DeviceID, challenge.Nonce, challenge.Purpose, challenge.CreatedAt, challenge.ExpiresAt)
	return err
}

func (sqlite *SQLite) ConsumeDeviceChallenge(ctx context.Context, id, deviceID string, now time.Time) (DeviceChallenge, error) {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return DeviceChallenge{}, err
	}
	defer transaction.Rollback()
	var challenge DeviceChallenge
	err = transaction.QueryRowContext(ctx, `SELECT id, device_id, nonce, purpose, created_at, expires_at, consumed_at FROM device_challenges WHERE id=? AND device_id=?`, id, deviceID).Scan(
		&challenge.ID, &challenge.DeviceID, &challenge.Nonce, &challenge.Purpose, &challenge.CreatedAt, &challenge.ExpiresAt, &challenge.ConsumedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return DeviceChallenge{}, ErrNotFound
	}
	if err != nil {
		return DeviceChallenge{}, err
	}
	if challenge.ConsumedAt != nil {
		return DeviceChallenge{}, ErrConsumed
	}
	if !challenge.ExpiresAt.After(now) {
		return DeviceChallenge{}, ErrExpired
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE device_challenges SET consumed_at=? WHERE id=?`, now, id); err != nil {
		return DeviceChallenge{}, err
	}
	challenge.ConsumedAt = &now
	return challenge, transaction.Commit()
}

func (sqlite *SQLite) CreateConnectionToken(ctx context.Context, token ConnectionToken) error {
	_, err := sqlite.database.ExecContext(ctx, `INSERT INTO device_connection_tokens (id, device_id, token_hash, created_at, expires_at) VALUES (?,?,?,?,?)`, token.ID, token.DeviceID, token.TokenHash[:], token.CreatedAt, token.ExpiresAt)
	return err
}

func (sqlite *SQLite) ConsumeConnectionToken(ctx context.Context, hash [32]byte, now time.Time) (ConnectionToken, error) {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return ConnectionToken{}, err
	}
	defer transaction.Rollback()
	var token ConnectionToken
	var tokenHash []byte
	err = transaction.QueryRowContext(ctx, `SELECT id, device_id, token_hash, created_at, expires_at, consumed_at FROM device_connection_tokens WHERE token_hash=?`, hash[:]).Scan(
		&token.ID, &token.DeviceID, &tokenHash, &token.CreatedAt, &token.ExpiresAt, &token.ConsumedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return ConnectionToken{}, ErrUnauthorized
	}
	if err != nil {
		return ConnectionToken{}, err
	}
	copy(token.TokenHash[:], tokenHash)
	if token.ConsumedAt != nil {
		return ConnectionToken{}, ErrConsumed
	}
	if !token.ExpiresAt.After(now) {
		return ConnectionToken{}, ErrExpired
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE device_connection_tokens SET consumed_at=? WHERE id=?`, now, token.ID); err != nil {
		return ConnectionToken{}, err
	}
	token.ConsumedAt = &now
	return token, transaction.Commit()
}

func (sqlite *SQLite) SetDevicePresence(ctx context.Context, deviceID string, now time.Time) error {
	result, err := sqlite.database.ExecContext(ctx, `UPDATE devices SET last_seen_at=?, updated_at=? WHERE id=?`, now, now, deviceID)
	return affectedOrNotFound(result, err)
}

func (sqlite *SQLite) CreateClaimFlow(ctx context.Context, flow ClaimFlow, now time.Time) error {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer transaction.Rollback()
	var claimed int
	if err := transaction.QueryRowContext(ctx, `SELECT EXISTS(SELECT 1 FROM device_memberships WHERE device_id=?)`, flow.DeviceID).Scan(&claimed); err != nil {
		return err
	}
	if claimed != 0 {
		return ErrConflict
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE claim_flows SET status='cancelled', consumed_at=? WHERE device_id=? AND consumed_at IS NULL`, now, flow.DeviceID); err != nil {
		return err
	}
	if _, err := transaction.ExecContext(ctx, `INSERT INTO claim_flows (id, device_id, manual_code_hash, status, created_at, expires_at) VALUES (?,?,?,?,?,?)`, flow.ID, flow.DeviceID, flow.ManualCodeHash[:], flow.Status, flow.CreatedAt, flow.ExpiresAt); err != nil {
		return err
	}
	return transaction.Commit()
}

func (sqlite *SQLite) BeginClaim(ctx context.Context, codeHash [32]byte, account Account, nonce []byte, now time.Time) (ClaimFlow, error) {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return ClaimFlow{}, err
	}
	defer transaction.Rollback()
	flow, err := scanClaimSQL(transaction.QueryRowContext(ctx, `SELECT id, device_id, manual_code_hash, status, account_id, account_display_name, claim_nonce, next_claim_epoch, created_at, expires_at, consumed_at FROM claim_flows WHERE manual_code_hash=?`, codeHash[:]))
	if errors.Is(err, sql.ErrNoRows) {
		return ClaimFlow{}, ErrNotFound
	}
	if err != nil {
		return ClaimFlow{}, err
	}
	if flow.ConsumedAt != nil || flow.Status != "code_visible" {
		return ClaimFlow{}, ErrConsumed
	}
	if !flow.ExpiresAt.After(now) {
		return ClaimFlow{}, ErrExpired
	}
	var epoch uint64
	var claimed int
	if err := transaction.QueryRowContext(ctx, `SELECT d.claim_epoch, EXISTS(SELECT 1 FROM device_memberships m WHERE m.device_id=d.id) FROM devices d WHERE d.id=?`, flow.DeviceID).Scan(&epoch, &claimed); err != nil {
		return ClaimFlow{}, err
	}
	if claimed != 0 {
		return ClaimFlow{}, ErrConflict
	}
	flow.Status = "confirm_on_device"
	flow.AccountID = account.ID
	flow.AccountDisplayName = account.UsernameDisplay
	flow.ClaimNonce = bytes.Clone(nonce)
	flow.NextClaimEpoch = epoch + 1
	if _, err := transaction.ExecContext(ctx, `UPDATE claim_flows SET status=?, account_id=?, account_display_name=?, claim_nonce=?, next_claim_epoch=? WHERE id=?`, flow.Status, flow.AccountID, flow.AccountDisplayName, flow.ClaimNonce, flow.NextClaimEpoch, flow.ID); err != nil {
		return ClaimFlow{}, err
	}
	if err := appendAuditSQL(ctx, transaction, AuditEvent{ActorType: "account", ActorID: account.ID, EventType: "claim.awaiting_physical_confirmation", SubjectType: "device", SubjectID: flow.DeviceID, Metadata: map[string]any{"claimFlowId": flow.ID}, CreatedAt: now}); err != nil {
		return ClaimFlow{}, err
	}
	return flow, transaction.Commit()
}

func (sqlite *SQLite) ClaimForAccount(ctx context.Context, flowID, accountID string) (ClaimFlow, error) {
	flow, err := scanClaimSQL(sqlite.database.QueryRowContext(ctx, `SELECT id, device_id, manual_code_hash, status, account_id, account_display_name, claim_nonce, next_claim_epoch, created_at, expires_at, consumed_at FROM claim_flows WHERE id=? AND account_id=?`, flowID, accountID))
	return flow, sqlNotFound(err)
}

func (sqlite *SQLite) PendingClaimForDevice(ctx context.Context, deviceID string, now time.Time) (ClaimFlow, error) {
	flow, err := scanClaimSQL(sqlite.database.QueryRowContext(ctx, `SELECT id, device_id, manual_code_hash, status, account_id, account_display_name, claim_nonce, next_claim_epoch, created_at, expires_at, consumed_at
        FROM claim_flows WHERE device_id=? AND status='confirm_on_device' AND consumed_at IS NULL AND expires_at>? ORDER BY created_at DESC LIMIT 1`, deviceID, now))
	return flow, sqlNotFound(err)
}

func (sqlite *SQLite) CompleteClaim(ctx context.Context, flowID string, approved bool, now time.Time) (ClaimFlow, error) {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return ClaimFlow{}, err
	}
	defer transaction.Rollback()
	flow, err := scanClaimSQL(transaction.QueryRowContext(ctx, `SELECT id, device_id, manual_code_hash, status, account_id, account_display_name, claim_nonce, next_claim_epoch, created_at, expires_at, consumed_at FROM claim_flows WHERE id=?`, flowID))
	if errors.Is(err, sql.ErrNoRows) {
		return ClaimFlow{}, ErrNotFound
	}
	if err != nil {
		return ClaimFlow{}, err
	}
	if flow.Status != "confirm_on_device" || flow.ConsumedAt != nil {
		return ClaimFlow{}, ErrConsumed
	}
	if !flow.ExpiresAt.After(now) {
		return ClaimFlow{}, ErrExpired
	}
	status := "rejected"
	if approved {
		status = "claimed"
		membershipID, err := securevalue.UUID()
		if err != nil {
			return ClaimFlow{}, err
		}
		if _, err := transaction.ExecContext(ctx, `INSERT INTO device_memberships (id, account_id, device_id, role, created_at) VALUES (?,?,?,'owner',?)`, membershipID, flow.AccountID, flow.DeviceID, now); err != nil {
			return ClaimFlow{}, ErrConflict
		}
		if _, err := transaction.ExecContext(ctx, `UPDATE devices SET claim_epoch=?, updated_at=? WHERE id=?`, flow.NextClaimEpoch, now, flow.DeviceID); err != nil {
			return ClaimFlow{}, err
		}
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE claim_flows SET status=?, consumed_at=? WHERE id=?`, status, now, flow.ID); err != nil {
		return ClaimFlow{}, err
	}
	flow.Status = status
	flow.ConsumedAt = &now
	eventType := "claim.rejected"
	if approved {
		eventType = "claim.completed"
	}
	if err := appendAuditSQL(ctx, transaction, AuditEvent{ActorType: "device", ActorID: flow.DeviceID, EventType: eventType, SubjectType: "account", SubjectID: flow.AccountID, Metadata: map[string]any{"claimFlowId": flow.ID}, CreatedAt: now}); err != nil {
		return ClaimFlow{}, err
	}
	return flow, transaction.Commit()
}

func (sqlite *SQLite) ListAccountDevices(ctx context.Context, accountID string) ([]AccountDevice, error) {
	rows, err := sqlite.database.QueryContext(ctx, `SELECT d.id, m.role, d.claim_epoch, d.last_seen_at FROM device_memberships m JOIN devices d ON d.id=m.device_id WHERE m.account_id=? ORDER BY d.created_at`, accountID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	result := []AccountDevice{}
	for rows.Next() {
		var item AccountDevice
		var lastSeen sql.NullTime
		if err := rows.Scan(&item.DeviceID, &item.Role, &item.ClaimEpoch, &lastSeen); err != nil {
			return nil, err
		}
		if lastSeen.Valid {
			item.LastSeenAt = &lastSeen.Time
		}
		result = append(result, item)
	}
	return result, rows.Err()
}

func (sqlite *SQLite) UnclaimDevice(ctx context.Context, accountID, deviceID string, now time.Time) (uint64, error) {
	transaction, err := sqlite.database.BeginTx(ctx, nil)
	if err != nil {
		return 0, err
	}
	defer transaction.Rollback()
	result, err := transaction.ExecContext(ctx, `DELETE FROM device_memberships WHERE account_id=? AND device_id=?`, accountID, deviceID)
	if err != nil {
		return 0, err
	}
	if affected, _ := result.RowsAffected(); affected == 0 {
		return 0, ErrNotFound
	}
	if _, err := transaction.ExecContext(ctx, `UPDATE devices SET claim_epoch=claim_epoch+1, updated_at=? WHERE id=?`, now, deviceID); err != nil {
		return 0, err
	}
	var epoch uint64
	if err := transaction.QueryRowContext(ctx, `SELECT claim_epoch FROM devices WHERE id=?`, deviceID).Scan(&epoch); err != nil {
		return 0, err
	}
	if err := appendAuditSQL(ctx, transaction, AuditEvent{ActorType: "account", ActorID: accountID, EventType: "device.unclaimed", SubjectType: "device", SubjectID: deviceID, Metadata: map[string]any{"claimEpoch": epoch}, CreatedAt: now}); err != nil {
		return 0, err
	}
	return epoch, transaction.Commit()
}

func (sqlite *SQLite) AppendAudit(ctx context.Context, event AuditEvent) error {
	return appendAuditSQL(ctx, sqlite.database, event)
}

type sqlExecutor interface {
	ExecContext(context.Context, string, ...any) (sql.Result, error)
}

func appendAuditSQL(ctx context.Context, executor sqlExecutor, event AuditEvent) error {
	metadata, err := json.Marshal(event.Metadata)
	if err != nil {
		return err
	}
	if event.Metadata == nil {
		metadata = []byte(`{}`)
	}
	_, err = executor.ExecContext(ctx, `INSERT INTO audit_events (actor_type, actor_id, event_type, subject_type, subject_id, metadata, created_at) VALUES (?,?,?,?,?,?,?)`, event.ActorType, nullableText(event.ActorID), event.EventType, event.SubjectType, nullableText(event.SubjectID), string(metadata), event.CreatedAt)
	return err
}

type sqlRow interface {
	Scan(...any) error
}

func scanClaimSQL(row sqlRow) (ClaimFlow, error) {
	var flow ClaimFlow
	var codeHash []byte
	var accountID, displayName sql.NullString
	var nextEpoch sql.NullInt64
	err := row.Scan(&flow.ID, &flow.DeviceID, &codeHash, &flow.Status, &accountID, &displayName, &flow.ClaimNonce, &nextEpoch, &flow.CreatedAt, &flow.ExpiresAt, &flow.ConsumedAt)
	copy(flow.ManualCodeHash[:], codeHash)
	if accountID.Valid {
		flow.AccountID = accountID.String
	}
	if displayName.Valid {
		flow.AccountDisplayName = displayName.String
	}
	if nextEpoch.Valid {
		flow.NextClaimEpoch = uint64(nextEpoch.Int64)
	}
	return flow, err
}

func sqlNotFound(err error) error {
	if errors.Is(err, sql.ErrNoRows) {
		return ErrNotFound
	}
	return err
}

func affectedOrNotFound(result sql.Result, err error) error {
	if err != nil {
		return err
	}
	affected, err := result.RowsAffected()
	if err != nil {
		return err
	}
	if affected == 0 {
		return ErrNotFound
	}
	return nil
}

func nullableText(value string) any {
	if value == "" {
		return nil
	}
	return value
}
