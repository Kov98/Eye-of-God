// Standalone regression test for cloud/db/cloud_db_import.cpp.
// It is not part of build_cloud.ps1. Build with MSYS2 UCRT64 g++:
//
//   g++ -std=c++17 -O2 -fms-extensions -I <repo> -I <repo>/third_party/sqlite
//       cloud/db/cloud_db_import.cpp case_db.cpp cloud/core/cloud_core.cpp
//       cloud/tests/cloud_db_import_test.cpp third_party/sqlite/sqlite3.o
//       -lbcrypt -o cloud_db_import_test.exe
//
// The test creates disposable case directories under the system temp folder.

#include "cloud/core/cloud_core.hpp"
#include "cloud/db/cloud_db_import.hpp"
#include "third_party/nlohmann/json.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << "\n";
    } else {
        std::cout << "ok: " << label << "\n";
    }
}

bool contains(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

void write_bytes(const std::filesystem::path& path, const std::string& content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
}

class Sqlite {
public:
    ~Sqlite() { close(); }

    bool open(const std::string& path) {
        close();
        return sqlite3_open_v2(path.c_str(), &db_,
            SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK;
    }

    bool exec(const std::string& sql) {
        return sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    void close() {
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3_int64 scalar(const std::string& sql) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
            std::cerr << "query prepare failed: " << sqlite3_errmsg(db_) << "\n";
            return -1;
        }
        sqlite3_int64 value = -1;
        if (sqlite3_step(statement) == SQLITE_ROW) value = sqlite3_column_int64(statement, 0);
        sqlite3_finalize(statement);
        return value;
    }

    std::string text(const std::string& sql) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
            std::cerr << "query prepare failed: " << sqlite3_errmsg(db_) << "\n";
            return {};
        }
        std::string value;
        if (sqlite3_step(statement) == SQLITE_ROW) {
            const unsigned char* raw = sqlite3_column_text(statement, 0);
            if (raw != nullptr) value.assign(reinterpret_cast<const char*>(raw));
        }
        sqlite3_finalize(statement);
        return value;
    }

private:
    sqlite3* db_ = nullptr;
};

nlohmann::json file_entry(const std::filesystem::path& directory,
                          const std::string& relative, const std::string& content,
                          const std::string& source) {
    write_bytes(directory / std::filesystem::path(relative), content);
    return {{"name", relative},
            {"size_bytes", content.size()},
            {"sha256", deep::cloud::sha256_hex(content)},
            {"source", source}};
}

nlohmann::json base_manifest(const std::string& job_id) {
    return {{"provider", "m365"},
            {"account", "analyst@example.com"},
            {"tenant_id", "tenant-1"},
            {"legal_basis", "court-order"},
            {"job_id", job_id},
            {"status", "complete"},
            {"sources", {{"messages", {{"status", "complete"}}}}},
            {"files", nlohmann::json::array()}};
}

std::filesystem::path account_directory(const std::filesystem::path& root,
                                        const std::string& case_name) {
    const std::filesystem::path directory =
        root / case_name / "cloud" / "m365" / "acct";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

void write_manifest(const std::filesystem::path& account_dir,
                    const nlohmann::json& manifest) {
    write_bytes(account_dir / "manifest.json", manifest.dump(2));
}

int cloud_object_count(const std::string& db_path) {
    Sqlite db;
    if (!db.open(db_path)) return -1;
    return static_cast<int>(db.scalar("SELECT COUNT(*) FROM cloud_object"));
}

int cloud_artifact_count(const std::string& db_path) {
    Sqlite db;
    if (!db.open(db_path)) return -1;
    return static_cast<int>(db.scalar("SELECT COUNT(*) FROM cloud_artifact"));
}

void test_roundtrip(const std::filesystem::path& root) {
    std::cout << "== import / re-import ==\n";
    const std::filesystem::path case_dir = root / "CASE-001";
    const std::filesystem::path account_dir = account_directory(root, "CASE-001");
    const std::string db_path = (case_dir / "case.db").string();
    const std::string attachment_content = "hello\n";
    const std::string attachment_sha =
        deep::cloud::sha256_hex(attachment_content);
    const std::string page =
        R"({"value":[{"id":"m1","attachments":[{"id":"att1","name":"note.txt",)"
        R"("size":6,"contentType":"text/plain"}]}]})";

    nlohmann::json manifest = base_manifest("job-1");
    manifest["files"].push_back(file_entry(account_dir, "raw/messages-1.json",
                                           page, "messages"));
    nlohmann::json attachment = file_entry(
        account_dir, "attachments/m1/att1_note.txt", attachment_content, "attachment");
    attachment["message_id"] = "m1";
    attachment["attachment_id"] = "att1";
    attachment["original_name"] = "note.txt";
    attachment["content_type"] = "text/plain";
    manifest["files"].push_back(attachment);
    write_manifest(account_dir, manifest);

    std::string timeline;
    timeline +=
        R"({"channel":"M365/Mail","event_id":"m1","timestamp_utc":"2026-01-01T00:00:00Z",)"
        R"("event_data":{"subject":"Hi","size":12}})" "\n";
    timeline +=
        R"({"channel":"M365/Drive","event_id":"d1","timestamp_utc":"2026-01-01T00:00:00Z",)"
        R"("event_data":{"name":"doc.txt","size":5}})" "\n";
    timeline +=
        R"({"channel":"M365/Attachment","event_id":"att1","timestamp_utc":"2026-01-01T00:00:00Z",)"
        R"("event_data":{"name":"note.txt","size":6,"sha256":")" + attachment_sha +
        R"(","path":"attachments/m1/att1_note.txt"}})" "\n";
    write_bytes(account_dir / "timeline.jsonl", timeline);

    deep::cloud::CloudDbImportReport report =
        deep::cloud::import_cloud_case(case_dir.string(), db_path);
    check(report.ok, "fresh import succeeds");
    check(report.jobs == 1, "fresh import counts one job");
    check(report.accounts == 1, "fresh import counts one account");
    check(report.objects == 4,
          "fresh import counts four distinct objects (raw page, attachment, "
          "message, drive item)");
    check(report.artifacts == 2, "fresh import counts two artifacts");

    {
        Sqlite db;
        check(db.open(db_path), "database opens for inspection");
        check(db.scalar("SELECT COUNT(*) FROM cloud_object") == 4,
              "database stores four objects");
        check(db.scalar("SELECT COUNT(*) FROM cloud_artifact") == 2,
              "database stores two artifacts");
        check(db.scalar("SELECT COUNT(*) FROM cloud_object WHERE object_type = "
                        "'attachment'") == 1,
              "attachment metadata and manifest row share one object");
        check(db.scalar("SELECT COUNT(*) FROM cloud_object WHERE object_type = 'event' "
                        "AND external_id = 'att1'") == 0,
              "M365/Attachment event is not imported as a generic event object");
        check(db.scalar("SELECT COUNT(*) FROM cloud_artifact a JOIN cloud_object o "
                        "ON o.id = a.object_pk WHERE o.object_type = 'attachment'") == 1,
              "attachment object keeps exactly one artifact");
        check(db.scalar("SELECT COUNT(*) FROM cloud_artifact a JOIN cloud_object o "
                        "ON o.id = a.object_pk WHERE o.object_type = 'attachment' "
                        "AND a.sha256 = '" + attachment_sha + "'") == 1,
              "attachment artifact stores the verified sha256");
        check(contains(db.text("SELECT metadata_json FROM cloud_object WHERE "
                               "object_type = 'attachment' LIMIT 1"),
                       attachment_sha),
              "hashed metadata survives the embedded/timeline metadata rows");
        check(db.text("SELECT case_id FROM cloud_job LIMIT 1") == "CASE-001",
              "case id is derived from the case directory name");
    }

    {
        Sqlite db;
        check(db.open(db_path), "database opens for mutation");
        // Simulate collector-owned job state that the importer must not touch.
        check(db.exec("UPDATE cloud_job SET attempts = 3,"
                      " started_at_utc = '2026-01-01T00:00:00Z',"
                      " error_code = 'E1', error_message = 'boom',"
                      " status = 'running', finished_at_utc = 'old'"),
              "job state seeded for the re-import check");
    }

    // A trailing separator must not turn the case id into an empty string, and a
    // second import must update rows instead of replacing them.
    report = deep::cloud::import_cloud_case(case_dir.string() + "\\", db_path);
    check(report.ok, "re-import with trailing separator succeeds");
    check(report.jobs == 1, "re-import reports the job");
    check(report.objects == 0, "re-import counts no new objects");
    check(report.artifacts == 0, "re-import counts no new artifacts");
    check(cloud_object_count(db_path) == 4, "re-import keeps four objects");
    check(cloud_artifact_count(db_path) == 2, "re-import keeps two artifacts");
    {
        Sqlite db;
        db.open(db_path);
        check(db.text("SELECT case_id FROM cloud_job LIMIT 1") == "CASE-001",
              "re-import keeps the normalized case id");
        check(db.scalar("SELECT attempts FROM cloud_job LIMIT 1") == 3,
              "re-import preserves attempts");
        check(db.text("SELECT started_at_utc FROM cloud_job LIMIT 1") ==
                  "2026-01-01T00:00:00Z",
              "re-import preserves started_at_utc");
        check(db.text("SELECT error_code FROM cloud_job LIMIT 1") == "E1",
              "re-import preserves error_code");
        check(db.text("SELECT status FROM cloud_job LIMIT 1") == "complete",
              "re-import refreshes status");
    }

    std::cout << "== tamper detection ==\n";
    write_bytes(account_dir / "attachments" / "m1" / "att1_note.txt",
                attachment_content + "tampered");
    report = deep::cloud::import_cloud_case(case_dir.string(), db_path);
    check(!report.ok, "tampered attachment aborts the import");
    check(contains(report.error, "mismatch"),
          "tampered attachment reports a size/hash mismatch");
    check(report.jobs == 0 && report.objects == 0 && report.artifacts == 0,
          "failed import zeroes the report counters");
    check(cloud_object_count(db_path) == 4 &&
          cloud_artifact_count(db_path) == 2,
          "failed import rolls the transaction back");
    write_bytes(account_dir / "attachments" / "m1" / "att1_note.txt",
                attachment_content);
}

void test_unsafe_paths(const std::filesystem::path& root) {
    std::cout << "== path traversal ==\n";
    write_bytes(root / "outside.txt", "outside");
    const std::filesystem::path account_dir = account_directory(root, "CASE-TRAV");
    const std::string content = "outside";
    nlohmann::json manifest = base_manifest("job-trav");
    manifest["files"].push_back(
        {{"name", "../outside.txt"},
         {"size_bytes", content.size()},
         {"sha256", deep::cloud::sha256_hex(content)},
         {"source", "messages"}});
    write_manifest(account_dir, manifest);
    const std::string db_path = (root / "CASE-TRAV" / "case.db").string();
    const deep::cloud::CloudDbImportReport report =
        deep::cloud::import_cloud_case((root / "CASE-TRAV").string(), db_path);
    check(!report.ok, "parent-directory evidence path aborts the import");
    check(contains(report.error, "unsafe evidence path"),
          "parent-directory path is rejected explicitly");
    check(cloud_object_count(db_path) == 0, "traversal import wrote no objects");
    {
        Sqlite db;
        db.open(db_path);
        check(db.scalar("SELECT COUNT(*) FROM cloud_job") == 0,
              "traversal import rolled the job back");
    }
}

void test_absolute_path(const std::filesystem::path& root) {
    std::cout << "== absolute path ==\n";
    const std::filesystem::path account_dir = account_directory(root, "CASE-ABS");
    nlohmann::json manifest = base_manifest("job-abs");
    manifest["files"].push_back(
        {{"name", "C:\\Windows\\win.ini"},
         {"size_bytes", 1},
         {"sha256", deep::cloud::sha256_hex("x")},
         {"source", "messages"}});
    write_manifest(account_dir, manifest);
    const std::string db_path = (root / "CASE-ABS" / "case.db").string();
    const deep::cloud::CloudDbImportReport report =
        deep::cloud::import_cloud_case((root / "CASE-ABS").string(), db_path);
    check(!report.ok, "absolute evidence path aborts the import");
    check(contains(report.error, "unsafe evidence path"),
          "absolute path is rejected explicitly");
    check(cloud_object_count(db_path) == 0, "absolute-path import wrote no objects");
}

void test_malformed_types(const std::filesystem::path& root) {
    std::cout << "== malformed JSON types ==\n";
    const std::filesystem::path account_dir = account_directory(root, "CASE-BAD");
    const std::string content = "{}";
    write_bytes(account_dir / "raw" / "page-1.json", content);
    nlohmann::json manifest = base_manifest("job-bad");
    manifest["files"].push_back(
        {{"name", "raw/page-1.json"},
         {"size_bytes", "2"},  // wrong JSON type on purpose
         {"sha256", deep::cloud::sha256_hex(content)},
         {"source", "messages"}});
    write_manifest(account_dir, manifest);
    const std::string db_path = (root / "CASE-BAD" / "case.db").string();
    deep::cloud::CloudDbImportReport report =
        deep::cloud::import_cloud_case((root / "CASE-BAD").string(), db_path);
    check(!report.ok, "string size_bytes fails cleanly");
    check(contains(report.error, "invalid size_bytes"),
          "string size_bytes reports a type error");
    check(cloud_object_count(db_path) == 0, "type error import wrote no objects");

    // Non-object event_data is accepted but must not throw.
    const std::filesystem::path event_dir = account_directory(root, "CASE-EVT");
    nlohmann::json event_manifest = base_manifest("job-evt");
    write_manifest(event_dir, event_manifest);
    write_bytes(event_dir / "timeline.jsonl",
        R"({"channel":"M365/Mail","event_id":"m2","event_data":["not-an-object"]})"
        "\n");
    const std::string event_db =
        (root / "CASE-EVT" / "case.db").string();
    report = deep::cloud::import_cloud_case((root / "CASE-EVT").string(), event_db);
    check(report.ok, "non-object event_data does not abort the import");
    check(report.objects == 1, "non-object event_data still imports the event");
    {
        Sqlite db;
        db.open(event_db);
        check(db.scalar("SELECT COUNT(*) FROM cloud_object WHERE object_type = "
                        "'message'") == 1,
              "non-object event_data keeps the message object");
    }
}

void test_provider_without_tenant(const std::filesystem::path& root) {
    std::cout << "== dropbox: provedor sem tenant ==\n";
    const std::filesystem::path case_dir = root / "CASE-DBX";
    const std::filesystem::path account_dir = case_dir / "cloud" / "dropbox" / "acct";
    std::error_code create_error;
    std::filesystem::create_directories(account_dir, create_error);
    const std::string db_path = (case_dir / "case.db").string();

    const std::string page =
        R"({"entries":[{".tag":"file","name":"contrato.pdf","path_lower":"/caso/contrato.pdf"})"
        R"(,"cursor":"C2","has_more":false})";
    const std::string payload = "%PDF-1.4\n";

    nlohmann::json manifest = {
        {"provider", "dropbox"},
        {"account", "kai@exemplo.com"},
        {"legal_basis", "OFICIO-9/2026"},
        {"job_id", "job-dbx"},
        {"root_path", "/caso"},
        {"status", "complete"},
        {"files", nlohmann::json::array()},
        {"downloads", nlohmann::json::array()}};
    manifest["files"].push_back(
        file_entry(account_dir, "raw/list-1.json", page, "list_folder"));
    nlohmann::json download =
        file_entry(account_dir, "files/1-contrato.pdf", payload, "download");
    download["id"] = "id:1";
    download["original_name"] = "contrato.pdf";
    download["path_lower"] = "/caso/contrato.pdf";
    download["content_hash"] = "ch1";
    manifest["files"].push_back(download);
    write_manifest(account_dir, manifest);
    write_bytes(account_dir / "timeline.jsonl",
                R"({"channel":"Dropbox/Files","event_id":"id:1","timestamp_utc":"2026-10-01T12:00:00Z",)"
                R"("event_data":{"name":"contrato.pdf","size":9}})" "\n");

    const deep::cloud::CloudDbImportReport report =
        deep::cloud::import_cloud_case(case_dir.string(), db_path);
    check(report.ok, "dropbox import succeeds");
    if (!report.ok) std::cerr << "  import error: " << report.error << "\n";
    check(report.jobs == 1, "dropbox job recorded");

    Sqlite db;
    check(db.open(db_path), "case db opens");
    check(db.text("SELECT provider FROM cloud_tenant") == "dropbox",
          "tenant row carries the provider");
    check(db.text("SELECT object_type FROM cloud_object WHERE external_id = 'id:1'") == "file",
          "downloaded file stored as a file object");
    check(db.text("SELECT name FROM cloud_object WHERE external_id = 'id:1'") == "contrato.pdf",
          "original name kept");
    check(cloud_object_count(db_path) == 2,
          "timeline event merged onto the downloaded object, not duplicated");
    check(db.text("SELECT storage_path FROM cloud_artifact WHERE storage_path LIKE "
                  "'cloud/dropbox/acct/files/%'") == "cloud/dropbox/acct/files/1-contrato.pdf",
          "artifact stored under the provider tree");
    check(cloud_artifact_count(db_path) == 2, "both artifacts recorded");
    check(db.scalar("SELECT COUNT(*) FROM cloud_object WHERE object_type = 'event'") == 0,
          "dropbox channel maps to file, not a generic event");

    const deep::cloud::CloudDbImportReport again =
        deep::cloud::import_cloud_case(case_dir.string(), db_path);
    check(again.ok, "dropbox re-import succeeds");
    check(cloud_artifact_count(db_path) == 2, "re-import does not duplicate artifacts");
}

void test_social_export(const std::filesystem::path& root) {
    std::cout << "== exportacao do titular (social/) ==\n";
    const std::filesystem::path case_dir = root / "CASE-SOC";
    const std::filesystem::path account_dir = case_dir / "social" / "facebook" / "acct";
    std::error_code create_error;
    std::filesystem::create_directories(account_dir, create_error);
    const std::string db_path = (case_dir / "case.db").string();

    const std::string thread =
        R"({"participants":[{"name":"Ana"}],"messages":[{"sender_name":"Ana",)"
        R"("timestamp_ms":1696000000000,"content":"oi"}]})";
    nlohmann::json manifest = {
        {"provider", "facebook"},
        {"account", "kai@exemplo.com"},
        {"legal_basis", "OFICIO-31/2026"},
        {"job_id", "job-social"},
        {"status", "complete"},
        {"threads", 1},
        {"messages", 1},
        {"files", nlohmann::json::array()}};
    manifest["files"].push_back(file_entry(account_dir, "raw/1-message_1.json", thread, "messages"));
    write_manifest(account_dir, manifest);
    write_bytes(account_dir / "timeline.jsonl",
                R"({"channel":"Facebook/Messages","event_id":"messages/inbox/a/message_1.json#1",)"
                R"("timestamp_utc":"2023-09-29T15:06:40Z","event_data":{"sender":"Ana","text":"oi"}})" "\n");

    const deep::cloud::CloudDbImportReport report =
        deep::cloud::import_cloud_case(case_dir.string(), db_path);
    check(report.ok, "social import succeeds");
    if (!report.ok) std::cerr << "  import error: " << report.error << "\n";

    Sqlite db;
    check(db.open(db_path), "case db opens");
    check(db.text("SELECT provider FROM cloud_tenant") == "facebook",
          "social provider recorded");
    check(db.text("SELECT storage_path FROM cloud_artifact") ==
              "social/facebook/acct/raw/1-message_1.json",
          "artifact path follows the social root");
    check(db.scalar("SELECT COUNT(*) FROM cloud_object WHERE object_type = 'message'") == 1,
          "timeline event stored as a message");
}

} // namespace

int main() {
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(error) / "eye_cloud_import_test";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    if (error) {
        std::cerr << "cannot create temp directory: " << error.message() << "\n";
        return 1;
    }
    try {
        test_roundtrip(root);
        test_unsafe_paths(root);
        test_absolute_path(root);
        test_malformed_types(root);
        test_provider_without_tenant(root);
        test_social_export(root);
    } catch (const std::exception& exception) {
        std::cerr << "unexpected exception: " << exception.what() << "\n";
        ++failures;
    }
    std::filesystem::remove_all(root, error);
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "cloud db import tests: PASS\n";
    return 0;
}
