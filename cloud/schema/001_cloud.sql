-- Olho de Deus — Cloud schema v1 (SQLite)
-- Applied after the existing case_db schema. The case is the security boundary.
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at_utc TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS cloud_authorization (
    id TEXT PRIMARY KEY,
    case_id TEXT NOT NULL,
    legal_basis TEXT NOT NULL,
    approved_by TEXT NOT NULL,
    approved_at_utc TEXT NOT NULL,
    expires_at_utc TEXT,
    scope_note TEXT,
    created_at_utc TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS cloud_tenant (
    id TEXT PRIMARY KEY,
    provider TEXT NOT NULL,
    tenant_id TEXT NOT NULL,
    display_name TEXT,
    region TEXT,
    consent_status TEXT NOT NULL DEFAULT 'unknown',
    created_at_utc TEXT NOT NULL,
    UNIQUE(provider, tenant_id)
);

CREATE TABLE IF NOT EXISTS cloud_account (
    id TEXT PRIMARY KEY,
    tenant_pk TEXT NOT NULL REFERENCES cloud_tenant(id) ON DELETE CASCADE,
    account_id TEXT NOT NULL,
    user_principal_name TEXT,
    display_name TEXT,
    account_type TEXT,
    collected_at_utc TEXT,
    UNIQUE(tenant_pk, account_id)
);

CREATE TABLE IF NOT EXISTS cloud_job (
    id TEXT PRIMARY KEY,
    case_id TEXT NOT NULL,
    provider TEXT NOT NULL,
    tenant_pk TEXT REFERENCES cloud_tenant(id),
    account_pk TEXT REFERENCES cloud_account(id),
    authorization_pk TEXT REFERENCES cloud_authorization(id),
    action TEXT NOT NULL,
    scope_json TEXT,
    status TEXT NOT NULL,
    attempts INTEGER NOT NULL DEFAULT 0,
    checkpoint_json TEXT,
    started_at_utc TEXT,
    finished_at_utc TEXT,
    error_code TEXT,
    error_message TEXT
);

CREATE TABLE IF NOT EXISTS cloud_object (
    id TEXT PRIMARY KEY,
    job_pk TEXT NOT NULL REFERENCES cloud_job(id) ON DELETE CASCADE,
    account_pk TEXT REFERENCES cloud_account(id),
    parent_external_id TEXT,
    object_type TEXT NOT NULL,
    external_id TEXT NOT NULL,
    name TEXT,
    path TEXT,
    size_bytes INTEGER,
    content_type TEXT,
    etag TEXT,
    version TEXT,
    created_at_utc TEXT,
    modified_at_utc TEXT,
    collected_at_utc TEXT NOT NULL,
    metadata_json TEXT,
    UNIQUE(job_pk, object_type, external_id)
);

CREATE TABLE IF NOT EXISTS cloud_artifact (
    id TEXT PRIMARY KEY,
    object_pk TEXT NOT NULL REFERENCES cloud_object(id) ON DELETE CASCADE,
    storage_path TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    byte_size INTEGER NOT NULL,
    content_type TEXT,
    collected_at_utc TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS cloud_permission (
    id TEXT PRIMARY KEY,
    object_pk TEXT NOT NULL REFERENCES cloud_object(id) ON DELETE CASCADE,
    grantee TEXT,
    role TEXT,
    permission_type TEXT,
    inherited INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS cloud_audit (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    at_utc TEXT NOT NULL,
    actor TEXT NOT NULL,
    action TEXT NOT NULL,
    case_id TEXT NOT NULL,
    provider TEXT,
    tenant_id TEXT,
    job_id TEXT,
    result TEXT NOT NULL,
    error_code TEXT,
    previous_hash TEXT,
    entry_hash TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_cloud_job_case ON cloud_job(case_id, status);
CREATE INDEX IF NOT EXISTS idx_cloud_object_job ON cloud_object(job_pk);
CREATE INDEX IF NOT EXISTS idx_cloud_object_external ON cloud_object(external_id);
CREATE INDEX IF NOT EXISTS idx_cloud_audit_case ON cloud_audit(case_id, at_utc);
CREATE INDEX IF NOT EXISTS idx_cloud_authorization_case ON cloud_authorization(case_id);

INSERT OR IGNORE INTO schema_migrations(version, applied_at_utc)
VALUES (1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
