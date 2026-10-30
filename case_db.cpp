// language: C++, file: case_db.cpp, target: Windows 10/11
// SQLite-backed case store. One row per artifact plus a normalized timeline.

#include "case_db.hpp"

#include <sqlite3.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace deep {
namespace {

const char* kSchema[] = {
    "PRAGMA journal_mode=WAL",
    "PRAGMA synchronous=NORMAL",
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE IF NOT EXISTS artifacts ("
    "  id INTEGER PRIMARY KEY,"
    "  type TEXT NOT NULL,"
    "  source TEXT,"
    "  key TEXT,"
    "  timestamp_utc TEXT,"
    "  path TEXT,"
    "  name TEXT,"
    "  size INTEGER,"
    "  sha256 TEXT,"
    "  data TEXT)",
    "CREATE INDEX IF NOT EXISTS idx_artifacts_type ON artifacts(type)",
    "CREATE INDEX IF NOT EXISTS idx_artifacts_time ON artifacts(timestamp_utc)",
    "CREATE INDEX IF NOT EXISTS idx_artifacts_source ON artifacts(source)",
    "CREATE TABLE IF NOT EXISTS timeline ("
    "  id INTEGER PRIMARY KEY,"
    "  timestamp_utc TEXT NOT NULL,"
    "  type TEXT NOT NULL,"
    "  source TEXT,"
    "  description TEXT,"
    "  artifact_id INTEGER,"
    "  data TEXT)",
    "CREATE INDEX IF NOT EXISTS idx_timeline_time ON timeline(timestamp_utc)",
    "CREATE TABLE IF NOT EXISTS schema_migrations ("
    "  version INTEGER PRIMARY KEY, applied_at_utc TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS cloud_authorization ("
    "  id TEXT PRIMARY KEY, case_id TEXT NOT NULL, legal_basis TEXT NOT NULL,"
    "  approved_by TEXT NOT NULL, approved_at_utc TEXT NOT NULL,"
    "  expires_at_utc TEXT, scope_note TEXT, created_at_utc TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS cloud_tenant ("
    "  id TEXT PRIMARY KEY, provider TEXT NOT NULL, tenant_id TEXT NOT NULL,"
    "  display_name TEXT, region TEXT, consent_status TEXT NOT NULL DEFAULT 'unknown',"
    "  created_at_utc TEXT NOT NULL, UNIQUE(provider, tenant_id))",
    "CREATE TABLE IF NOT EXISTS cloud_account ("
    "  id TEXT PRIMARY KEY, tenant_pk TEXT NOT NULL REFERENCES cloud_tenant(id) ON DELETE CASCADE,"
    "  account_id TEXT NOT NULL, user_principal_name TEXT, display_name TEXT,"
    "  account_type TEXT, collected_at_utc TEXT, UNIQUE(tenant_pk, account_id))",
    "CREATE TABLE IF NOT EXISTS cloud_job ("
    "  id TEXT PRIMARY KEY, case_id TEXT NOT NULL, provider TEXT NOT NULL,"
    "  tenant_pk TEXT REFERENCES cloud_tenant(id), account_pk TEXT REFERENCES cloud_account(id),"
    "  authorization_pk TEXT REFERENCES cloud_authorization(id), action TEXT NOT NULL,"
    "  scope_json TEXT, status TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0,"
    "  checkpoint_json TEXT, started_at_utc TEXT, finished_at_utc TEXT,"
    "  error_code TEXT, error_message TEXT)",
    "CREATE TABLE IF NOT EXISTS cloud_object ("
    "  id TEXT PRIMARY KEY, job_pk TEXT NOT NULL REFERENCES cloud_job(id) ON DELETE CASCADE,"
    "  account_pk TEXT REFERENCES cloud_account(id), parent_external_id TEXT,"
    "  object_type TEXT NOT NULL, external_id TEXT NOT NULL, name TEXT, path TEXT,"
    "  size_bytes INTEGER, content_type TEXT, etag TEXT, version TEXT,"
    "  created_at_utc TEXT, modified_at_utc TEXT, collected_at_utc TEXT NOT NULL,"
    "  metadata_json TEXT, UNIQUE(job_pk, object_type, external_id))",
    "CREATE TABLE IF NOT EXISTS cloud_artifact ("
    "  id TEXT PRIMARY KEY, object_pk TEXT NOT NULL REFERENCES cloud_object(id) ON DELETE CASCADE,"
    "  storage_path TEXT NOT NULL, sha256 TEXT NOT NULL, byte_size INTEGER NOT NULL,"
    "  content_type TEXT, collected_at_utc TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS cloud_permission ("
    "  id TEXT PRIMARY KEY, object_pk TEXT NOT NULL REFERENCES cloud_object(id) ON DELETE CASCADE,"
    "  grantee TEXT, role TEXT, permission_type TEXT, inherited INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS cloud_audit ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT, at_utc TEXT NOT NULL, actor TEXT NOT NULL,"
    "  action TEXT NOT NULL, case_id TEXT NOT NULL, provider TEXT, tenant_id TEXT,"
    "  job_id TEXT, result TEXT NOT NULL, error_code TEXT, previous_hash TEXT,"
    "  entry_hash TEXT NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_cloud_job_case ON cloud_job(case_id, status)",
    "DELETE FROM cloud_artifact WHERE rowid NOT IN (SELECT MIN(rowid) FROM cloud_artifact GROUP BY object_pk)",
    "DELETE FROM cloud_artifact WHERE length(sha256) != 64",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_cloud_artifact_object ON cloud_artifact(object_pk)",
    "CREATE TRIGGER IF NOT EXISTS cloud_artifact_sha_insert BEFORE INSERT ON cloud_artifact "
    "WHEN length(NEW.sha256) != 64 BEGIN SELECT RAISE(ABORT, 'invalid cloud artifact sha256'); END",
    "CREATE TRIGGER IF NOT EXISTS cloud_artifact_sha_update BEFORE UPDATE ON cloud_artifact "
    "WHEN length(NEW.sha256) != 64 BEGIN SELECT RAISE(ABORT, 'invalid cloud artifact sha256'); END",

    "CREATE INDEX IF NOT EXISTS idx_cloud_object_job ON cloud_object(job_pk)",
    "CREATE INDEX IF NOT EXISTS idx_cloud_object_external ON cloud_object(external_id)",
    "CREATE INDEX IF NOT EXISTS idx_cloud_audit_case ON cloud_audit(case_id, at_utc)",
    "CREATE INDEX IF NOT EXISTS idx_cloud_authorization_case ON cloud_authorization(case_id)",
    "INSERT OR IGNORE INTO schema_migrations(version, applied_at_utc) VALUES(1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))",
    "INSERT OR IGNORE INTO schema_migrations(version, applied_at_utc) VALUES(3, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))",
    "INSERT OR IGNORE INTO schema_migrations(version, applied_at_utc) VALUES(4, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))",    nullptr
};

struct QueryBuffer {
    std::string output;
    int rows = 0;
    bool first = true;
};

int query_callback(void* data, int columns, char** values, char** names) {
    QueryBuffer* buffer = static_cast<QueryBuffer*>(data);
    if (buffer->rows >= 50000) return 1;
    if (!buffer->first) buffer->output += ",";
    buffer->first = false;
    buffer->output += "{";
    for (int i = 0; i < columns; ++i) {
        if (i) buffer->output += ",";
        buffer->output += art::json_escape(names[i] ? names[i] : "");
        buffer->output += ":";
        if (values[i] == nullptr) {
            buffer->output += "null";
        } else {
            const std::string value(values[i]);
            bool numeric = !value.empty();
            for (char c : value) {
                if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' ||
                      c == '+' || c == '.' || c == 'e' || c == 'E')) {
                    numeric = false;
                    break;
                }
            }
            if (numeric) buffer->output += value;
            else buffer->output += art::json_escape(value);
        }
    }
    buffer->output += "}";
    ++buffer->rows;
    return 0;
}

} // namespace

CaseDatabase::~CaseDatabase() { close(); }

void CaseDatabase::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool CaseDatabase::exec(const std::string& sql, std::wstring& error) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = L"sqlite: " + art::from_utf8(message ? message : "exec failed");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool CaseDatabase::open(const std::wstring& path, std::wstring& error) {
    close();
    const std::string utf8 = art::to_utf8(path);
    const int rc = sqlite3_open_v2(utf8.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        error = L"sqlite open failed: " +
            art::from_utf8(db_ ? sqlite3_errmsg(db_) : "unknown");
        close();
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    for (const char** sql = kSchema; *sql != nullptr; ++sql) {
        if (!exec(*sql, error)) {
            close();
            return false;
        }
    }
    if (!set_meta("tool", "Olho de Deus deep engine", error) ||
        !set_meta("schema", "4", error) ||
        !set_meta("created_utc", art::now_iso(), error)) {
        close();
        return false;
    }
    return true;
}

bool CaseDatabase::begin(std::wstring& error) {
    return exec("BEGIN IMMEDIATE", error);
}

bool CaseDatabase::commit(std::wstring& error) {
    return exec("COMMIT", error);
}

bool CaseDatabase::rollback(std::wstring& error) {
    return exec("ROLLBACK", error);
}

bool CaseDatabase::set_meta(const std::string& key, const std::string& value,
                            std::wstring& error) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = L"sqlite prepare: " + art::from_utf8(sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) error = L"sqlite meta: " + art::from_utf8(sqlite3_errmsg(db_));
    sqlite3_finalize(statement);
    return ok;
}

bool CaseDatabase::insert(const ArtifactRow& row, std::wstring& error) {
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO artifacts(type, source, key, timestamp_utc, path, name,"
        " size, sha256, data) VALUES(?,?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = L"sqlite prepare: " + art::from_utf8(sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(statement, 1, row.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, row.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, row.key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, row.timestamp_utc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, row.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, row.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 7, static_cast<sqlite3_int64>(row.size));
    sqlite3_bind_text(statement, 8, row.sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 9, row.data.empty() ? "{}" : row.data.c_str(),
                      -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) error = L"sqlite insert: " + art::from_utf8(sqlite3_errmsg(db_));
    const sqlite3_int64 id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(statement);
    if (!ok) return false;

    if (row.timestamp_utc.empty()) return true;
    const char* timeline_sql =
        "INSERT INTO timeline(timestamp_utc, type, source, description,"
        " artifact_id, data) VALUES(?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db_, timeline_sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = L"sqlite prepare timeline: " +
            art::from_utf8(sqlite3_errmsg(db_));
        return false;
    }
    std::string description = row.name.empty() ? row.path : row.name;
    if (description.empty()) description = row.type;
    sqlite3_bind_text(statement, 1, row.timestamp_utc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, row.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, row.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 5, id);
    sqlite3_bind_text(statement, 6, row.data.empty() ? "{}" : row.data.c_str(),
                      -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) error = L"sqlite timeline: " + art::from_utf8(sqlite3_errmsg(db_));
    sqlite3_finalize(statement);
    return ok;
}

int CaseDatabase::count(const std::string& table, std::wstring& error) {
    for (char c : table) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            error = L"invalid table name";
            return -1;
        }
    }
    const std::string sql = "SELECT COUNT(*) FROM " + table;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        error = L"sqlite count: " + art::from_utf8(sqlite3_errmsg(db_));
        return -1;
    }
    int result = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        result = sqlite3_column_int(statement, 0);
    } else {
        error = L"sqlite count step failed";
    }
    sqlite3_finalize(statement);
    return result;
}

bool CaseDatabase::query_json(const std::string& sql, std::string& output,
                              std::wstring& error) {
    QueryBuffer buffer;
    buffer.output = "[";
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), query_callback, &buffer, &message);
    if (rc != SQLITE_OK) {
        error = L"sqlite query: " + art::from_utf8(message ? message : "failed");
        sqlite3_free(message);
        return false;
    }
    buffer.output += "]";
    output = buffer.output;
    return true;
}

} // namespace deep
