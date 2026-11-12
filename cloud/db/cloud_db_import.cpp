#include "cloud_db_import.hpp"

#include "case_db.hpp"
#include "cloud/core/cloud_core.hpp"
#include "third_party/nlohmann/json.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace deep::cloud {
namespace {

constexpr const char* kAttachmentObjectType = "attachment";

// Stable row ids are derived from evidence identity, so a re-import must update
// the existing row instead of replacing it. Replacing a row in cloud_object or
// cloud_job deletes its children through ON DELETE CASCADE (the hashed
// cloud_artifact rows), which is what the importer did before.
constexpr const char* kObjectById = "SELECT 1 FROM cloud_object WHERE id = ?";
constexpr const char* kArtifactByObject =
    "SELECT 1 FROM cloud_artifact WHERE object_pk = ?";

struct DatabaseCloser {
    void operator()(sqlite3* database) const {
        if (database != nullptr) sqlite3_close(database);
    }
};

using DatabaseHandle = std::unique_ptr<sqlite3, DatabaseCloser>;

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
    gmtime_s(&value, &now);
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &value);
    return text;
}

std::string stable_id(const std::string& prefix, const std::string& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char text[24] = {};
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return prefix + text;
}

bool exec(sqlite3* db, const char* sql, std::string& error) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : "sqlite exec failed";
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) ==
        SQLITE_OK;
}

// Runs one statement and reports how many rows it actually inserted/updated.
// ON CONFLICT DO NOTHING reports 0 for a skipped duplicate, which keeps the
// importer counters tied to real writes instead of attempted writes.
bool run_statement(sqlite3* db, const char* sql,
                   const std::vector<std::string>& values, std::string& error,
                   int* changed = nullptr) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!bind_text(statement, static_cast<int>(i + 1), values[i])) {
            error = sqlite3_errmsg(db);
            sqlite3_finalize(statement);
            return false;
        }
    }
    const int rc = sqlite3_step(statement);
    const int row_changes = sqlite3_changes(db);
    sqlite3_finalize(statement);
    if (rc != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        return false;
    }
    if (changed != nullptr) *changed = row_changes;
    return true;
}

bool row_exists(sqlite3* db, const char* sql, const std::string& id, bool& exists,
                std::string& error) {
    exists = false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    bool ok = bind_text(statement, 1, id);
    if (ok) {
        const int rc = sqlite3_step(statement);
        if (rc == SQLITE_ROW) {
            exists = true;
        } else if (rc != SQLITE_DONE) {
            error = sqlite3_errmsg(db);
            ok = false;
        }
    } else {
        error = sqlite3_errmsg(db);
    }
    sqlite3_finalize(statement);
    return ok;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool is_sha256_hex(const std::string& value) {
    if (value.size() != 64) return false;
    for (const unsigned char c : value) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

// Reads a file exactly as stored (binary). Evidence files are hashed over the
// bytes on disk, so text-mode translation must not be used here.
bool read_file(const std::filesystem::path& path, std::string& content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    content.assign((std::istreambuf_iterator<char>(input)),
                   std::istreambuf_iterator<char>());
    return !input.bad();
}

bool is_within(const std::filesystem::path& base,
               const std::filesystem::path& candidate) {
    auto base_it = base.begin();
    auto candidate_it = candidate.begin();
    for (; base_it != base.end(); ++base_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *base_it != *candidate_it) return false;
    }
    return candidate_it != candidate.end();
}

// Resolves a manifest-relative evidence path. Absolute paths, drive/root
// components and ".." (or ".") components are rejected so a manifest cannot
// make the importer read outside its own directory.
bool resolve_evidence_path(const std::filesystem::path& base, const std::string& name,
                           std::filesystem::path& resolved) {
    const std::filesystem::path relative(name);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory()) {
        return false;
    }
    for (const auto& part : relative) {
        const std::string component = part.string();
        if (component.empty() || component == "." || component == ".." ||
            component.find('\0') != std::string::npos) {
            return false;
        }
    }
    const std::filesystem::path normalized_base = base.lexically_normal();
    const std::filesystem::path candidate =
        (normalized_base / relative).lexically_normal();
    if (!is_within(normalized_base, candidate)) return false;
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(candidate, file_error) || file_error) {
        return false;
    }
    const auto canonical_base = std::filesystem::weakly_canonical(normalized_base, file_error);
    if (file_error) return false;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, file_error);
    if (file_error || !is_within(canonical_base, canonical_candidate)) return false;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(candidate, file_error)) ||
        file_error) {
        return false;
    }
    resolved = canonical_candidate;
    return true;
}

std::string case_name_from_path(const std::filesystem::path& root) {
    std::filesystem::path path = root.lexically_normal();
    // "C:\cases\alpha\" normalizes with a trailing separator, which leaves
    // filename() empty; walk up until the directory name is found.
    while (path.filename().empty() && path.has_parent_path() &&
           path.parent_path() != path) {
        path = path.parent_path();
    }
    return path.filename().string();
}

std::string dump_json(const nlohmann::json& value) {
    // Replace invalid UTF-8 instead of throwing while serializing metadata.
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string json_string(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) return {};
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

std::string json_string_or(const nlohmann::json& object, const char* key,
                           const std::string& fallback) {
    const std::string value = json_string(object, key);
    return value.empty() ? fallback : value;
}

bool json_int64(const nlohmann::json& object, const char* key, std::int64_t& value) {
    if (!object.is_object()) return false;
    const auto it = object.find(key);
    if (it == object.end()) return false;
    if (it->is_number_unsigned()) {
        const auto raw = it->get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(
                      (std::numeric_limits<std::int64_t>::max)())) {
            return false;
        }
        value = static_cast<std::int64_t>(raw);
        return true;
    }
    if (it->is_number_integer()) {
        value = it->get<std::int64_t>();
        return true;
    }
    return false;
}

std::int64_t json_int64_or(const nlohmann::json& object, const char* key,
                           std::int64_t fallback) {
    std::int64_t value = 0;
    return json_int64(object, key, value) ? value : fallback;
}

// Inserts one artifact for an object. An object only ever owns a single
// artifact: a previously imported hash is never replaced, and ON CONFLICT
// DO NOTHING protects the artifact id itself.
bool insert_artifact(sqlite3* db, const std::string& object_id,
                     const std::string& storage_path, const std::string& sha256,
                     std::int64_t size, const std::string& content_type,
                     const std::string& now, CloudDbImportReport& report,
                     std::string& error) {
    bool exists = false;
    if (!row_exists(db, kArtifactByObject, object_id, exists, error)) return false;
    if (exists) {
        ++report.artifacts_existing;
        return true;
    }
    int changes = 0;
    if (!run_statement(db,
        "INSERT INTO cloud_artifact"
        "(id, object_pk, storage_path, sha256, byte_size, content_type,"
        " collected_at_utc) VALUES(?,?,?,?,?,?,?)"
        " ON CONFLICT(id) DO NOTHING",
        {stable_id("art_", object_id + "|" + sha256), object_id, storage_path,
         sha256, std::to_string(size), content_type, now}, error, &changes)) {
        return false;
    }
    if (changes > 0) ++report.artifacts;
    return true;
}

bool import_manifest_rows(sqlite3* db, const std::filesystem::path& manifest_path,
                          const std::string& case_id, const std::string& root_name,
                          CloudDbImportReport& report, std::string& error) {
    std::string text;
    if (!read_file(manifest_path, text) || text.empty()) {
        error = "cannot read " + manifest_path.string();
        return false;
    }
    const nlohmann::json manifest = nlohmann::json::parse(text, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        error = "invalid manifest " + manifest_path.string();
        return false;
    }
    const std::string account = json_string(manifest, "account");
    std::string tenant = json_string(manifest, "tenant_id");
    const std::string legal = json_string(manifest, "legal_basis");
    const std::string job_id = json_string(manifest, "job_id");
    // The provider comes from the manifest so a connector that has no tenant
    // concept (Dropbox) imports without a fake one. Older manifests predate the
    // field and are all M365.
    std::string provider = json_string(manifest, "provider");
    if (provider.empty()) provider = "m365";
    if (tenant.empty()) tenant = provider;
    if (account.empty() || legal.empty() || job_id.empty()) {
        error = "manifest missing account/legal/job";
        return false;
    }
    if (manifest.contains("files") && !manifest["files"].is_array()) {
        error = "manifest files is not an array: " + manifest_path.string();
        return false;
    }
    const nlohmann::json files = manifest.contains("files")
        ? manifest["files"] : nlohmann::json::array();

    const std::filesystem::path manifest_dir = manifest_path.parent_path();
    const std::string now = utc_now();
    const std::string tenant_pk = stable_id("t_", provider + "|" + tenant);
    const std::string account_pk = stable_id("a_", tenant_pk + "|" + account);
    const std::string auth_pk = stable_id("auth_", case_id + "|" + legal);
    const std::string job_pk = stable_id("j_", case_id + "|" + job_id + "|" + account);
    const std::string account_dir = manifest_dir.filename().string();
    std::string status = json_string(manifest, "status");
    if (status.empty()) status = "unknown";
    const std::string checkpoint =
        (manifest.contains("sources") && manifest["sources"].is_object())
        ? dump_json(manifest["sources"]) : "{}";

    if (!run_statement(db,
        "INSERT OR IGNORE INTO cloud_authorization"
        "(id, case_id, legal_basis, approved_by, approved_at_utc, created_at_utc)"
        " VALUES(?,?,?,?,?,?)",
        {auth_pk, case_id, legal, "import", now, now}, error)) return false;
    if (!run_statement(db,
        "INSERT OR IGNORE INTO cloud_tenant"
        "(id, provider, tenant_id, consent_status, created_at_utc) VALUES(?,?,?,?,?)",
        {tenant_pk, provider, tenant, "unknown", now}, error)) return false;
    if (!run_statement(db,
        "INSERT OR IGNORE INTO cloud_account"
        "(id, tenant_pk, account_id, user_principal_name, collected_at_utc)"
        " VALUES(?,?,?,?,?)",
        {account_pk, tenant_pk, account, account, now}, error)) return false;
    // Update status/finished only: attempts, started_at_utc and the error fields
    // belong to the collector and must survive a re-import. ON DELETE CASCADE
    // from cloud_job would otherwise delete every object/artifact of the job.
    int job_changes = 0;
    if (!run_statement(db,
        "INSERT INTO cloud_job"
        "(id, case_id, provider, tenant_pk, account_pk, authorization_pk, action,"
        " scope_json, status, checkpoint_json, finished_at_utc)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET"
        " status = excluded.status,"
        " finished_at_utc = excluded.finished_at_utc",
        {job_pk, case_id, provider, tenant_pk, account_pk, auth_pk, "collect",
         nlohmann::json{{"account", account}}.dump(), status, checkpoint, now},
        error, &job_changes)) return false;
    if (job_changes > 0) ++report.jobs;

    for (const auto& file : files) {
        if (!file.is_object()) {
            error = "manifest file entry is not an object: " + manifest_path.string();
            return false;
        }
        const std::string name = json_string(file, "name");
        const std::string sha256 = lower_ascii(json_string(file, "sha256"));
        std::int64_t size = 0;
        if (name.empty()) {
            error = "manifest file entry has no name: " + manifest_path.string();
            return false;
        }
        if (!is_sha256_hex(sha256)) {
            error = "manifest file entry has no valid sha256: " + name;
            return false;
        }
        if (!json_int64(file, "size_bytes", size) || size < 0) {
            error = "manifest file entry has invalid size_bytes: " + name;
            return false;
        }
        // Never trust the manifest: resolve the path inside the manifest
        // directory and verify the bytes that are actually on disk.
        std::filesystem::path evidence_path;
        if (!resolve_evidence_path(manifest_dir, name, evidence_path)) {
            error = "unsafe evidence path in manifest: " + name;
            return false;
        }
        std::string content;
        if (!read_file(evidence_path, content)) {
            error = "missing evidence file: " + name;
            return false;
        }
        if (static_cast<std::int64_t>(content.size()) != size) {
            error = "size mismatch for evidence file: " + name;
            return false;
        }
        if (sha256_hex(content) != sha256) {
            error = "sha256 mismatch for evidence file: " + name;
            return false;
        }

        const std::string source = json_string(file, "source");
        // O caminho guardado segue a raiz real da evidencia: cloud/ para os
        // conectores de API, social/ para a exportacao do titular.
        const std::string storage_path =
            root_name + "/" + provider + "/" + account_dir + "/" + name;
        if (source == "attachment") {
            const std::string attachment_id = json_string_or(file, "attachment_id", name);
            const std::string original_name = json_string_or(file, "original_name", name);
            const std::string message_id = json_string(file, "message_id");
            const std::string content_type = json_string(file, "content_type");
            const std::string object_id =
                stable_id("o_", job_pk + "|attachment|" + attachment_id);
            bool existed = false;
            if (!row_exists(db, kObjectById, object_id, existed, error)) return false;
            if (!run_statement(db,
                "INSERT INTO cloud_object"
                "(id, job_pk, account_pk, parent_external_id, object_type, external_id,"
                " name, size_bytes, content_type, collected_at_utc, metadata_json)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?)"
                " ON CONFLICT(id) DO UPDATE SET"
                " parent_external_id = excluded.parent_external_id,"
                " name = excluded.name,"
                " size_bytes = excluded.size_bytes,"
                " content_type = excluded.content_type,"
                " collected_at_utc = excluded.collected_at_utc,"
                " metadata_json = excluded.metadata_json",
                {object_id, job_pk, account_pk, message_id, kAttachmentObjectType,
                 attachment_id, original_name, std::to_string(size), content_type, now,
                 nlohmann::json{{"sha256", sha256}, {"path", name}}.dump()}, error)) {
                return false;
            }
            if (!existed) ++report.objects;
            else ++report.objects_existing;
            if (!insert_artifact(db, object_id, storage_path, sha256, size,
                                 content_type, now, report, error)) return false;
            continue;
        }

        // A downloaded payload is evidence of its own: it keeps the remote path
        // as identity so a re-import enriches the same row instead of creating a
        // second copy under a new local filename.
        if (source == "download") {
            const std::string original_name = json_string_or(file, "original_name", name);
            const std::string remote_path = json_string(file, "path_lower");
            // Identity is the provider id when present, so the timeline event for
            // the same file lands on this object instead of creating a twin.
            std::string identity = json_string(file, "id");
            if (identity.empty()) identity = remote_path.empty() ? name : remote_path;
            const std::string object_id = stable_id("o_", job_pk + "|file|" + identity);
            bool existed = false;
            if (!row_exists(db, kObjectById, object_id, existed, error)) return false;
            if (!run_statement(db,
                "INSERT INTO cloud_object"
                "(id, job_pk, account_pk, object_type, external_id, name, path,"
                " size_bytes, content_type, collected_at_utc, metadata_json)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?)"
                " ON CONFLICT(id) DO UPDATE SET"
                " name = excluded.name,"
                " path = excluded.path,"
                " size_bytes = excluded.size_bytes,"
                " content_type = excluded.content_type,"
                " collected_at_utc = excluded.collected_at_utc,"
                " metadata_json = excluded.metadata_json",
                {object_id, job_pk, account_pk, "file", identity, original_name, storage_path,
                 std::to_string(size), "application/octet-stream", now,
                 nlohmann::json{{"sha256", sha256},
                                {"path", name},
                                {"remote_path", remote_path},
                                {"content_hash", json_string(file, "content_hash")}}
                     .dump()},
                error)) {
                return false;
            }
            if (!existed) ++report.objects;
            else ++report.objects_existing;
            if (!insert_artifact(db, object_id, storage_path, sha256, size,
                                 "application/octet-stream", now, report, error)) return false;
            continue;
        }

        const std::string object_id = stable_id("o_", job_pk + "|raw_page|" + name);
        bool existed = false;
        if (!row_exists(db, kObjectById, object_id, existed, error)) return false;
        if (!run_statement(db,
            "INSERT INTO cloud_object"
            "(id, job_pk, account_pk, object_type, external_id, name, path,"
            " size_bytes, content_type, collected_at_utc, metadata_json)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id) DO UPDATE SET"
            " name = excluded.name,"
            " path = excluded.path,"
            " size_bytes = excluded.size_bytes,"
            " content_type = excluded.content_type,"
            " collected_at_utc = excluded.collected_at_utc,"
            " metadata_json = excluded.metadata_json",
            {object_id, job_pk, account_pk, "raw_page", name, name, storage_path,
             std::to_string(size), "application/json", now,
             nlohmann::json{{"sha256", sha256}, {"source", source}}.dump()}, error)) {
            return false;
        }
        if (!existed) ++report.objects;
        else ++report.objects_existing;
        if (!insert_artifact(db, object_id, storage_path, sha256, size,
                             "application/json", now, report, error)) return false;

        // Attachment metadata embedded in a raw page is optional and has no
        // hash of its own. It may only create an object that the manifest has
        // not already recorded with a verified file; an existing row (and its
        // artifact) is left untouched.
        const nlohmann::json page = nlohmann::json::parse(content, nullptr, false);
        if (page.is_discarded() || !page.is_object()) continue;
        if (!page.contains("value") || !page["value"].is_array()) continue;
        for (const auto& item : page["value"]) {
            if (!item.is_object()) continue;
            if (!item.contains("attachments") || !item["attachments"].is_array()) {
                continue;
            }
            const std::string parent_id = json_string(item, "id");
            for (const auto& attachment : item["attachments"]) {
                if (!attachment.is_object()) continue;
                const std::string attachment_id = json_string(attachment, "id");
                if (attachment_id.empty()) continue;
                const std::string metadata_object =
                    stable_id("o_", job_pk + "|attachment|" + attachment_id);
                bool metadata_exists = false;
                if (!row_exists(db, kObjectById, metadata_object, metadata_exists, error))
                    return false;
                if (metadata_exists) continue;
                int changes = 0;
                if (!run_statement(db,
                    "INSERT INTO cloud_object"
                    "(id, job_pk, account_pk, parent_external_id, object_type,"
                    " external_id, name, size_bytes, content_type, collected_at_utc,"
                    " metadata_json) VALUES(?,?,?,?,?,?,?,?,?,?,?)"
                    " ON CONFLICT DO NOTHING",
                    {metadata_object, job_pk, account_pk, parent_id,
                     kAttachmentObjectType, attachment_id,
                     json_string(attachment, "name"),
                     std::to_string(json_int64_or(attachment, "size", 0)),
                     json_string(attachment, "contentType"), now,
                     dump_json(attachment)}, error, &changes)) {
                    return false;
                }
                if (changes > 0) ++report.objects;
            }
        }
    }

    const std::filesystem::path timeline_path = manifest_dir / "timeline.jsonl";
    std::ifstream timeline(timeline_path, std::ios::binary);
    std::string line;
    while (std::getline(timeline, line)) {
        if (line.empty()) continue;
        const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
        if (event.is_discarded() || !event.is_object()) continue;
        const std::string event_id = json_string(event, "event_id");
        if (event_id.empty()) continue;
        const std::string channel = json_string(event, "channel");
        std::string type = "event";
        if (channel == "M365/Mail") {
            type = "message";
        } else if (channel == "M365/Drive") {
            type = "file";
        } else if (channel == "Dropbox/Files") {
            type = "file";
        } else if (channel == "Google/Photos") {
            // Foto do Takeout e arquivo de evidencia, com carimbo do sidecar.
            type = "file";
        } else if (channel == "Google/Contacts") {
            type = "contact";
        } else if (channel == "Facebook/Messages" || channel == "Instagram/Messages" ||
                   channel == "Google/Mail" || channel == "Telegram/Messages") {
            // Export do titular (Meta) e Takeout (Gmail) entregam mensagem por
            // mensagem; o tipo casa com o do M365, que ja existe no esquema.
            type = "message";
        } else if (channel == "M365/Attachment") {
            // Share the manifest attachment object type/id so the event enriches
            // the same object instead of creating a parallel "event" row.
            type = kAttachmentObjectType;
        }
        nlohmann::json data = nlohmann::json::object();
        if (event.contains("event_data") && event["event_data"].is_object()) {
            data = event["event_data"];
        }
        const std::string name =
            json_string_or(data, "subject", json_string_or(data, "name", ""));
        const std::string timestamp = json_string(event, "timestamp_utc");
        const std::string object_id =
            stable_id("o_", job_pk + "|" + type + "|" + event_id);
        int changes = 0;
        if (!run_statement(db,
            "INSERT INTO cloud_object"
            "(id, job_pk, account_pk, object_type, external_id, name, path,"
            " size_bytes, collected_at_utc, modified_at_utc, metadata_json)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT DO NOTHING",
            {object_id, job_pk, account_pk, type, event_id, name, channel,
             std::to_string(json_int64_or(data, "size", 0)), now,
             timestamp.empty() ? now : timestamp, dump_json(data)},
            error, &changes)) {
            return false;
        }
        if (changes > 0) ++report.objects;
    }
    ++report.accounts;
    return true;
}

// The manifest is untrusted input. Any exception while reading or interpreting
// it (or while writing rows) becomes an import error so the caller can roll the
// whole transaction back and close the database.
bool import_manifest(sqlite3* db, const std::filesystem::path& manifest_path,
                     const std::string& case_id, const std::string& root_name,
                     CloudDbImportReport& report, std::string& error) {
    try {
        return import_manifest_rows(db, manifest_path, case_id, root_name, report, error);
    } catch (const std::exception& exception) {
        error = "manifest import failed for " + manifest_path.string() + ": " +
            exception.what();
        return false;
    } catch (...) {
        error = "manifest import failed for " + manifest_path.string();
        return false;
    }
}

void import_cloud_case_rows(const std::string& case_dir,
                            const std::string& database_path,
                            CloudDbImportReport& report) {
    const std::filesystem::path case_root(case_dir);
    const std::string case_id = case_name_from_path(case_root);
    if (case_id.empty()) {
        report.error = "cannot determine case id from case_dir";
        return;
    }
    report.database_path = database_path.empty()
        ? (case_root / "case.db").string() : database_path;
    std::wstring schema_error;
    deep::CaseDatabase schema;
    if (!schema.open(art::from_utf8(report.database_path), schema_error)) {
        report.error = art::to_utf8(schema_error);
        return;
    }
    schema.close();

    sqlite3* opened = nullptr;
    if (sqlite3_open_v2(report.database_path.c_str(), &opened,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK) {
        report.error = opened != nullptr ? sqlite3_errmsg(opened) : "cannot open case.db";
        if (opened != nullptr) sqlite3_close(opened);
        return;
    }
    const DatabaseHandle db(opened);
    sqlite3_busy_timeout(db.get(), 5000);

    std::string error;
    const auto rollback = [&](const std::string& message) {
        report.error = message;
        report.ok = false;
        report.jobs = report.accounts = report.objects = report.artifacts = 0;
        std::string ignored;
        exec(db.get(), "ROLLBACK", ignored);
    };
    exec(db.get(), "PRAGMA foreign_keys=ON", error);
    if (!exec(db.get(), "BEGIN IMMEDIATE", error)) {
        rollback(error);
        return;
    }

    // Every provider writes <case>/cloud/<provider>/<account>/manifest.json, so
    // the scan walks both levels instead of naming one provider. Results are
    // sorted so the same tree always imports in the same order. Directories such
    // as cloud/jobs hold no manifest and are skipped by the file check.
    // cloud/ = conectores de API; social/ = exportacao entregue pelo titular.
    // As duas arvores tem a mesma forma <raiz>/<provedor>/<conta>/manifest.json.
    // O par guarda de qual raiz o manifesto veio: o caminho da evidencia
    // registrada no banco tem de refletir a pasta real (cloud/ ou social/).
    std::vector<std::pair<std::filesystem::path, std::string>> manifests;
    std::error_code directory_error;
    for (const char* root_name : {"cloud", "social"}) {
        const std::filesystem::path cloud_root = case_root / root_name;
        if (!std::filesystem::is_directory(cloud_root, directory_error)) continue;
        directory_error.clear();
        std::filesystem::directory_iterator provider(cloud_root, directory_error);
        const std::filesystem::directory_iterator end;
        for (; !directory_error && provider != end; provider.increment(directory_error)) {
            std::error_code entry_error;
            if (!provider->is_directory(entry_error) || entry_error) continue;
            std::filesystem::directory_iterator account(provider->path(), entry_error);
            for (; !entry_error && account != end; account.increment(entry_error)) {
                std::error_code manifest_error;
                if (!account->is_directory(manifest_error) || manifest_error) continue;
                const std::filesystem::path candidate = account->path() / "manifest.json";
                if (std::filesystem::is_regular_file(candidate, manifest_error) &&
                    !manifest_error) {
                    manifests.emplace_back(candidate, root_name);
                }
            }
            if (entry_error) {
                rollback("cannot enumerate cloud manifests: " + entry_error.message());
                return;
            }
        }
        if (directory_error) {
            rollback("cannot enumerate cloud manifests: " + directory_error.message());
            return;
        }
    }
    std::sort(manifests.begin(), manifests.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& manifest : manifests) {
        if (!import_manifest(db.get(), manifest.first, case_id, manifest.second, report,
                             error)) {
            rollback(error);
            return;
        }
    }
    if (!exec(db.get(), "COMMIT", error)) {
        rollback(error);
        return;
    }
    report.ok = report.error.empty() && report.jobs > 0;
    if (report.jobs == 0 && report.error.empty()) {
        report.error = "no cloud manifest found";
    }
}

} // namespace

CloudDbImportReport import_cloud_case(const std::string& case_dir,
                                      const std::string& database_path) {
    CloudDbImportReport report;
    if (case_dir.empty()) {
        report.error = "case_dir is required";
        return report;
    }
    try {
        import_cloud_case_rows(case_dir, database_path, report);
    } catch (const std::exception& exception) {
        report.error = std::string("cloud import failed: ") + exception.what();
        report.ok = false;
        report.jobs = report.accounts = report.objects = report.artifacts = 0;
    } catch (...) {
        report.error = "cloud import failed";
        report.ok = false;
        report.jobs = report.accounts = report.objects = report.artifacts = 0;
    }
    return report;
}

} // namespace deep::cloud
