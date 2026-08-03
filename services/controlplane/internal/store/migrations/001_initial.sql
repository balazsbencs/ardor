BEGIN IMMEDIATE;

CREATE TABLE IF NOT EXISTS accounts (
    id text PRIMARY KEY,
    username_normalized text NOT NULL UNIQUE,
    username_display text NOT NULL,
    password_hash text NOT NULL,
    state text NOT NULL DEFAULT 'active' CHECK (state IN ('active', 'disabled')),
    created_at datetime NOT NULL,
    updated_at datetime NOT NULL
);

CREATE TABLE IF NOT EXISTS recovery_codes (
    id text PRIMARY KEY,
    account_id text NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    code_hash blob NOT NULL UNIQUE,
    created_at datetime NOT NULL,
    used_at datetime
);

CREATE TABLE IF NOT EXISTS sessions (
    id text PRIMARY KEY,
    account_id text NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    token_hash blob NOT NULL UNIQUE,
    created_at datetime NOT NULL,
    expires_at datetime NOT NULL,
    revoked_at datetime
);
CREATE INDEX IF NOT EXISTS sessions_account_active_idx ON sessions(account_id, expires_at) WHERE revoked_at IS NULL;

CREATE TABLE IF NOT EXISTS devices (
    id text PRIMARY KEY,
    public_key blob NOT NULL,
    claim_epoch integer NOT NULL DEFAULT 0 CHECK (claim_epoch >= 0),
    last_seen_at datetime,
    created_at datetime NOT NULL,
    updated_at datetime NOT NULL
);

CREATE TABLE IF NOT EXISTS device_memberships (
    id text PRIMARY KEY,
    account_id text NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    device_id text NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    role text NOT NULL CHECK (role IN ('owner')),
    created_at datetime NOT NULL,
    UNIQUE(account_id, device_id),
    UNIQUE(device_id)
);

CREATE TABLE IF NOT EXISTS device_challenges (
    id text PRIMARY KEY,
    device_id text NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    nonce blob NOT NULL,
    purpose text NOT NULL CHECK (purpose IN ('connection')),
    created_at datetime NOT NULL,
    expires_at datetime NOT NULL,
    consumed_at datetime
);

CREATE TABLE IF NOT EXISTS device_connection_tokens (
    id text PRIMARY KEY,
    device_id text NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    token_hash blob NOT NULL UNIQUE,
    created_at datetime NOT NULL,
    expires_at datetime NOT NULL,
    consumed_at datetime
);

CREATE TABLE IF NOT EXISTS claim_flows (
    id text PRIMARY KEY,
    device_id text NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    manual_code_hash blob NOT NULL UNIQUE,
    status text NOT NULL CHECK (status IN ('code_visible', 'confirm_on_device', 'claimed', 'rejected', 'expired', 'cancelled')),
    account_id text REFERENCES accounts(id) ON DELETE SET NULL,
    account_display_name text,
    claim_nonce blob,
    next_claim_epoch integer CHECK (next_claim_epoch IS NULL OR next_claim_epoch > 0),
    created_at datetime NOT NULL,
    expires_at datetime NOT NULL,
    consumed_at datetime
);
CREATE INDEX IF NOT EXISTS claim_flows_device_active_idx ON claim_flows(device_id, expires_at) WHERE consumed_at IS NULL;

CREATE TABLE IF NOT EXISTS audit_events (
    id integer PRIMARY KEY AUTOINCREMENT,
    actor_type text NOT NULL CHECK (actor_type IN ('account', 'device', 'service')),
    actor_id text,
    event_type text NOT NULL,
    subject_type text NOT NULL,
    subject_id text,
    metadata text NOT NULL DEFAULT '{}',
    created_at datetime NOT NULL
);
CREATE INDEX IF NOT EXISTS audit_events_actor_idx ON audit_events(actor_type, actor_id, created_at DESC);

COMMIT;
