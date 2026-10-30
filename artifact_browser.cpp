// language: C++, file: artifact_browser.cpp, target: Windows 10/11 (UCRT64 g++ / MSVC)
//
// Browser artifact collector for Chrome, Edge and Firefox. History databases
// are snapshotted into %TEMP% (the live profile is never written) and opened
// with SQLITE_OPEN_READONLY through the SQLite C API. Every row is streamed to
// the shared ArtifactSink; no table is materialised in memory.

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

#include "artifacts.hpp"
#include "sqlite3.h"

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <vector>

namespace deep {
namespace {

constexpr std::uint64_t kMaxDatabaseBytes =
    2ull * 1024ull * 1024ull * 1024ull;
constexpr int kMaxRowsPerDatabase = 200000;
constexpr std::uint64_t kFirefoxEpochOffsetUs = 11644473600ull * 1000000ull;
constexpr std::uint64_t kUint64Max = ~static_cast<std::uint64_t>(0);

enum class BrowserKind { chrome, edge, firefox };

const char* browser_name(BrowserKind kind) {
    switch (kind) {
    case BrowserKind::chrome: return "chrome";
    case BrowserKind::edge: return "edge";
    case BrowserKind::firefox: return "firefox";
    }
    return "unknown";
}

struct DatabaseTask {
    std::wstring path;
    std::wstring profile;
    BrowserKind browser = BrowserKind::chrome;
};

struct DatabaseFailure {
    DatabaseTask task;
    std::wstring reason;
    std::string status;
};

struct DbBudget {
    int remaining = kMaxRowsPerDatabase;
    bool truncated = false;
};

struct DbContext {
    sqlite3* db = nullptr;
    BrowserKind browser = BrowserKind::chrome;
    std::wstring profile;
    std::string profile_utf8;
    std::string source_utf8;
    const ArtifactSink* sink = nullptr;
    int emitted = 0;
    DbBudget budget;
};

struct JsonObject {
    std::string body;
    bool first = true;

    void separator() {
        if (!first) body.push_back(',');
        first = false;
    }

    void string(const char* key, const std::string& value) {
        separator();
        body.push_back('"');
        body += key;
        body += "\":";
        body += art::json_escape(value);
    }

    void integer(const char* key, long long value) {
        separator();
        body.push_back('"');
        body += key;
        body += "\":";
        body += std::to_string(value);
    }

    std::string finish() const { return "{" + body + "}"; }
};

struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    ~StmtGuard() {
        if (stmt) sqlite3_finalize(stmt);
    }
};

struct SqliteGuard {
    sqlite3* db = nullptr;
    ~SqliteGuard() {
        if (db) sqlite3_close(db);
    }
};

struct TempCopy {
    std::wstring path;
    ~TempCopy() {
        if (path.empty()) return;
        DeleteFileW(path.c_str());
        DeleteFileW((path + L"-wal").c_str());
        DeleteFileW((path + L"-shm").c_str());
        DeleteFileW((path + L"-journal").c_str());
    }
};

std::wstring environment_value(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::wstring value(static_cast<std::size_t>(needed), L'\0');
    const DWORD written =
        GetEnvironmentVariableW(name, &value[0], needed);
    if (written == 0 || written >= needed) return {};
    value.resize(static_cast<std::size_t>(written));
    return value;
}

std::wstring temp_directory() {
    const DWORD needed = GetTempPathW(0, nullptr);
    if (needed == 0) return {};
    std::wstring value(static_cast<std::size_t>(needed), L'\0');
    const DWORD written =
        GetTempPathW(needed, &value[0]);
    if (written == 0 || written >= needed) return {};
    value.resize(static_cast<std::size_t>(written));
    return value;
}

bool is_regular_file(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool file_size(const std::wstring& path, std::uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
           static_cast<std::uint64_t>(data.nFileSizeLow);
    return true;
}

std::vector<std::wstring> child_directories(const std::wstring& root) {
    std::vector<std::wstring> result;
    if (root.empty()) return result;
    const std::wstring pattern = art::join_path(root, L"*");
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return result;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (std::wcscmp(data.cFileName, L".") == 0 ||
            std::wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        result.push_back(art::join_path(root, data.cFileName));
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(result.begin(), result.end(),
        [](const std::wstring& left, const std::wstring& right) {
            return _wcsicmp(left.c_str(), right.c_str()) < 0;
        });
    return result;
}

std::wstring base_name_wide(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return path;
    return path.substr(slash + 1);
}

bool is_chromium_profile(const std::wstring& name) {
    return _wcsicmp(name.c_str(), L"Default") == 0 ||
           _wcsnicmp(name.c_str(), L"Profile ", 8) == 0;
}

void add_chromium_tasks(const std::wstring& user_data_root,
                        BrowserKind browser,
                        std::vector<DatabaseTask>& tasks) {
    if (user_data_root.empty()) return;
    const std::vector<std::wstring> directories =
        child_directories(user_data_root);
    for (const std::wstring& directory : directories) {
        const std::wstring profile = base_name_wide(directory);
        if (!is_chromium_profile(profile)) continue;
        const std::wstring history = art::join_path(directory, L"History");
        if (!is_regular_file(history)) continue;
        DatabaseTask task;
        task.path = history;
        task.profile = profile;
        task.browser = browser;
        tasks.push_back(task);
    }
}

void add_firefox_tasks(const std::wstring& profiles_root,
                       std::vector<DatabaseTask>& tasks) {
    if (profiles_root.empty()) return;
    const std::vector<std::wstring> directories =
        child_directories(profiles_root);
    for (const std::wstring& directory : directories) {
        const std::wstring places = art::join_path(directory, L"places.sqlite");
        if (!is_regular_file(places)) continue;
        DatabaseTask task;
        task.path = places;
        task.profile = base_name_wide(directory);
        task.browser = BrowserKind::firefox;
        tasks.push_back(task);
    }
}

std::vector<DatabaseTask> discover_databases() {
    std::vector<DatabaseTask> tasks;
    const std::wstring local = environment_value(L"LOCALAPPDATA");
    if (!local.empty()) {
        add_chromium_tasks(
            art::join_path(local, L"Google\\Chrome\\User Data"),
            BrowserKind::chrome, tasks);
        add_chromium_tasks(
            art::join_path(local, L"Microsoft\\Edge\\User Data"),
            BrowserKind::edge, tasks);
    }
    const std::wstring roaming = environment_value(L"APPDATA");
    if (!roaming.empty()) {
        add_firefox_tasks(
            art::join_path(roaming, L"Mozilla\\Firefox\\Profiles"), tasks);
    }
    return tasks;
}

bool copy_read_share(const std::wstring& source, const std::wstring& target,
                     DWORD& error_code) {
    HANDLE input = CreateFileW(source.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        error_code = GetLastError();
        return false;
    }
    HANDLE output = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error_code = GetLastError();
        CloseHandle(input);
        return false;
    }

    bool ok = true;
    std::vector<char> buffer(1024 * 1024);
    for (;;) {
        DWORD received = 0;
        if (!ReadFile(input, buffer.data(),
                static_cast<DWORD>(buffer.size()), &received, nullptr)) {
            error_code = GetLastError();
            ok = false;
            break;
        }
        if (received == 0) break;
        DWORD offset = 0;
        while (offset < received) {
            DWORD written = 0;
            if (!WriteFile(output, buffer.data() + offset,
                    received - offset, &written, nullptr)) {
                error_code = GetLastError();
                ok = false;
                break;
            }
            if (written == 0) {
                error_code = ERROR_WRITE_FAULT;
                ok = false;
                break;
            }
            offset += written;
        }
        if (!ok) break;
    }
    CloseHandle(input);
    CloseHandle(output);
    if (!ok) DeleteFileW(target.c_str());
    return ok;
}

bool copy_snapshot(const std::wstring& source, const std::wstring& target,
                   std::wstring& reason) {
    if (CopyFileW(source.c_str(), target.c_str(), FALSE)) return true;
    const DWORD copy_error = GetLastError();

    DWORD share_error = 0;
    if (copy_read_share(source, target, share_error)) return true;

    reason = L"copy failed (CopyFileW error " + std::to_wstring(copy_error) +
             L", read-share error " + std::to_wstring(share_error) + L")";
    return false;
}

bool create_temp_copy(const std::wstring& source, TempCopy& temp,
                      std::wstring& reason) {
    const std::wstring directory = temp_directory();
    if (directory.empty()) {
        reason = L"GetTempPathW failed";
        return false;
    }
    wchar_t name[MAX_PATH + 1]{};
    if (GetTempFileNameW(directory.c_str(), L"eog", 0, name) == 0) {
        reason = L"GetTempFileNameW failed with error " +
                 std::to_wstring(GetLastError());
        return false;
    }
    temp.path = name;
    if (!copy_snapshot(source, temp.path, reason)) {
        DeleteFileW(temp.path.c_str());
        temp.path.clear();
        return false;
    }

    // Firefox Places commonly runs in WAL mode. Copying only the main file
    // would silently drop committed frames still held in -wal, so snapshot
    // the sidecar as well. A failed sidecar copy is not fatal: the main copy
    // remains a valid, older snapshot.
    const std::wstring wal_source = source + L"-wal";
    const std::wstring wal_target = temp.path + L"-wal";
    if (is_regular_file(wal_source)) {
        std::wstring ignored;
        if (!copy_snapshot(wal_source, wal_target, ignored)) {
            DeleteFileW(wal_target.c_str());
        }
    }
    return true;
}

bool prepare_statement(sqlite3* db, const std::string& sql,
                       sqlite3_stmt** statement, std::string& error) {
    const int rc = sqlite3_prepare_v2(db, sql.c_str(),
        static_cast<int>(sql.size()), statement, nullptr);
    if (rc != SQLITE_OK) {
        const char* message = sqlite3_errmsg(db);
        error = message ? message : "sqlite3_prepare_v2 failed";
        return false;
    }
    return true;
}

bool probe_database(sqlite3* db, std::string& error) {
    StmtGuard statement;
    if (!prepare_statement(db, "SELECT count(*) FROM sqlite_master",
                           &statement.stmt, error)) {
        return false;
    }
    const int rc = sqlite3_step(statement.stmt);
    if (rc != SQLITE_ROW) {
        const char* message = sqlite3_errmsg(db);
        error = message ? message : "database probe failed";
        return false;
    }
    return true;
}

bool table_exists(sqlite3* db, const char* table, bool& exists,
                  std::string& error) {
    exists = false;
    StmtGuard statement;
    if (!prepare_statement(db,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1",
            &statement.stmt, error)) {
        return false;
    }
    int rc = sqlite3_bind_text(statement.stmt, 1, table, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        const char* message = sqlite3_errmsg(db);
        error = message ? message : "sqlite3_bind_text failed";
        return false;
    }
    rc = sqlite3_step(statement.stmt);
    if (rc == SQLITE_ROW) {
        exists = true;
        return true;
    }
    if (rc != SQLITE_DONE) {
        const char* message = sqlite3_errmsg(db);
        error = message ? message : "sqlite_master query failed";
        return false;
    }
    return true;
}

bool read_columns(sqlite3* db, const char* table,
                  std::vector<std::string>& columns, std::string& error) {
    columns.clear();
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    StmtGuard statement;
    if (!prepare_statement(db, sql, &statement.stmt, error)) return false;
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.stmt)) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(statement.stmt, 1);
        if (name) {
            columns.push_back(reinterpret_cast<const char*>(name));
        }
    }
    if (rc != SQLITE_DONE) {
        const char* message = sqlite3_errmsg(db);
        error = message ? message : "PRAGMA table_info failed";
        return false;
    }
    return true;
}

bool has_column(const std::vector<std::string>& columns, const char* name) {
    return std::find(columns.begin(), columns.end(), name) != columns.end();
}

const char* pick_column(const std::vector<std::string>& columns,
                        std::initializer_list<const char*> candidates) {
    for (const char* candidate : candidates) {
        if (has_column(columns, candidate)) return candidate;
    }
    return nullptr;
}

std::string text_column(sqlite3_stmt* statement, int index) {
    if (sqlite3_column_type(statement, index) == SQLITE_NULL) return {};
    const unsigned char* text = sqlite3_column_text(statement, index);
    if (!text) return {};
    const int bytes = sqlite3_column_bytes(statement, index);
    return std::string(reinterpret_cast<const char*>(text),
                       static_cast<std::size_t>(bytes));
}

long long integer_column(sqlite3_stmt* statement, int index) {
    return static_cast<long long>(sqlite3_column_int64(statement, index));
}

std::string chrome_time_iso(long long microseconds) {
    if (microseconds <= 0) return {};
    const std::uint64_t value = static_cast<std::uint64_t>(microseconds);
    if (value > kUint64Max / 10ull) return {};
    return art::filetime_iso(value * 10ull);
}

std::string firefox_time_iso(long long microseconds) {
    if (microseconds <= 0) return {};
    const std::uint64_t value = static_cast<std::uint64_t>(microseconds);
    if (value > kUint64Max - kFirefoxEpochOffsetUs) return {};
    const std::uint64_t shifted = value + kFirefoxEpochOffsetUs;
    if (shifted > kUint64Max / 10ull) return {};
    return art::filetime_iso(shifted * 10ull);
}

std::string base_name_utf8(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::string text_select(const char* column, bool present) {
    if (!present) return "''";
    return "IFNULL(" + std::string(column) + ",'')";
}

std::string integer_select(const char* column, bool present) {
    if (!present) return "0";
    return "IFNULL(" + std::string(column) + ",0)";
}

void add_status(DbContext& context, const std::string& table,
                const std::string& status, const std::string& message) {
    ArtifactRow row;
    row.type = "browser_status";
    row.source = context.source_utf8;
    const std::string table_key = table.empty() ? "database" : table;
    row.key = context.source_utf8 + "|" + table_key + "|" + status;
    row.path = context.source_utf8;
    row.name = std::string(browser_name(context.browser)) + " " +
               context.profile_utf8 + " " + table_key;
    row.timestamp_utc = art::now_iso();

    JsonObject data;
    data.string("browser", browser_name(context.browser));
    data.string("profile", context.profile_utf8);
    data.string("database", context.source_utf8);
    if (!table.empty()) data.string("table", table);
    data.string("status", status);
    data.string("message", message);
    row.data = data.finish();

    (*context.sink)(row);
    ++context.emitted;
}

void emit_row(DbContext& context, const ArtifactRow& row) {
    (*context.sink)(row);
    --context.budget.remaining;
    ++context.emitted;
}

void finish_database(DbContext& context) {
    if (!context.budget.truncated) return;
    add_status(context, "database", "truncated",
        "stopped after 200000 emitted rows for this database");
}

void collect_chrome_urls(DbContext& context,
                         const std::vector<std::string>& columns) {
    const bool has_title = has_column(columns, "title");
    const bool has_visits = has_column(columns, "visit_count");
    const bool has_typed = has_column(columns, "typed_count");
    const bool has_time = has_column(columns, "last_visit_time");

    std::string sql = "SELECT " + text_select("url", true) + ", " +
        text_select("title", has_title) + ", " +
        integer_select("visit_count", has_visits) + ", " +
        integer_select("typed_count", has_typed) + ", " +
        integer_select("last_visit_time", has_time) + " FROM urls";
    if (has_time) sql += " ORDER BY last_visit_time DESC";

    StmtGuard statement;
    std::string error;
    if (!prepare_statement(context.db, sql, &statement.stmt, error)) {
        add_status(context, "urls", "error",
                   "urls query failed: " + error);
        return;
    }

    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.stmt)) == SQLITE_ROW) {
        if (context.budget.remaining <= 0) {
            context.budget.truncated = true;
            break;
        }
        const std::string url = text_column(statement.stmt, 0);
        const std::string title = text_column(statement.stmt, 1);
        const long long visit_count = integer_column(statement.stmt, 2);
        const long long typed_count = integer_column(statement.stmt, 3);
        const long long last_visit = integer_column(statement.stmt, 4);

        ArtifactRow row;
        row.type = "browser_history";
        row.source = context.source_utf8;
        row.key = url;
        row.timestamp_utc = chrome_time_iso(last_visit);
        row.path = url;
        row.name = title;

        JsonObject data;
        data.string("browser", browser_name(context.browser));
        data.string("profile", context.profile_utf8);
        data.string("url", url);
        data.string("title", title);
        data.integer("visit_count", visit_count);
        data.integer("typed_count", typed_count);
        row.data = data.finish();
        emit_row(context, row);
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const char* message = sqlite3_errmsg(context.db);
        add_status(context, "urls", "error",
            std::string("urls query failed: ") +
            (message ? message : "sqlite3_step failed"));
    }
}

void collect_chrome_visits(DbContext& context,
                           const std::vector<std::string>& url_columns,
                           const std::vector<std::string>& visit_columns) {
    const bool has_title = has_column(url_columns, "title");
    const bool has_transition = has_column(visit_columns, "transition");

    std::string sql = "SELECT " + text_select("u.url", true) + ", " +
        text_select("u.title", has_title) + ", " +
        integer_select("v.visit_time", true) + ", " +
        integer_select("v.transition", has_transition) +
        " FROM visits v LEFT JOIN urls u ON u.id = v.url" +
        std::string(" ORDER BY v.visit_time DESC");

    StmtGuard statement;
    std::string error;
    if (!prepare_statement(context.db, sql, &statement.stmt, error)) {
        add_status(context, "visits", "error",
                   "visits query failed: " + error);
        return;
    }

    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.stmt)) == SQLITE_ROW) {
        if (context.budget.remaining <= 0) {
            context.budget.truncated = true;
            break;
        }
        const std::string url = text_column(statement.stmt, 0);
        const std::string title = text_column(statement.stmt, 1);
        const long long visit_time = integer_column(statement.stmt, 2);
        const long long transition = integer_column(statement.stmt, 3);

        ArtifactRow row;
        row.type = "browser_visit";
        row.source = context.source_utf8;
        row.key = url + "|" + std::to_string(visit_time);
        row.timestamp_utc = chrome_time_iso(visit_time);
        row.path = url;
        row.name = title;

        JsonObject data;
        data.string("browser", browser_name(context.browser));
        data.string("profile", context.profile_utf8);
        data.string("url", url);
        data.string("title", title);
        data.integer("transition", transition);
        row.data = data.finish();
        emit_row(context, row);
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const char* message = sqlite3_errmsg(context.db);
        add_status(context, "visits", "error",
            std::string("visits query failed: ") +
            (message ? message : "sqlite3_step failed"));
    }
}

void collect_chrome_downloads(DbContext& context) {
    bool exists = false;
    std::string error;
    if (!table_exists(context.db, "downloads", exists, error)) {
        add_status(context, "downloads", "error",
                   "downloads schema probe failed: " + error);
        return;
    }
    if (!exists) return;

    std::vector<std::string> columns;
    if (!read_columns(context.db, "downloads", columns, error)) {
        add_status(context, "downloads", "error",
                   "downloads schema probe failed: " + error);
        return;
    }

    const char* target = pick_column(columns,
        {"target_path", "full_path", "current_path", "path"});
    const char* url = pick_column(columns,
        {"tab_url", "url", "site_url", "referrer"});
    const bool has_total = has_column(columns, "total_bytes");
    const bool has_start = has_column(columns, "start_time");
    const bool has_end = has_column(columns, "end_time");
    const bool has_state = has_column(columns, "state");
    const bool has_danger = has_column(columns, "danger_type");
    const bool has_mime = has_column(columns, "mime_type");

    std::string sql = "SELECT " +
        text_select(target, target != nullptr) + ", " +
        text_select(url, url != nullptr) + ", " +
        integer_select("total_bytes", has_total) + ", " +
        integer_select("start_time", has_start) + ", " +
        integer_select("end_time", has_end) + ", " +
        integer_select("state", has_state) + ", " +
        integer_select("danger_type", has_danger) + ", " +
        text_select("mime_type", has_mime) + " FROM downloads";
    if (has_start) sql += " ORDER BY start_time DESC";

    StmtGuard statement;
    if (!prepare_statement(context.db, sql, &statement.stmt, error)) {
        add_status(context, "downloads", "error",
                   "downloads query failed: " + error);
        return;
    }

    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.stmt)) == SQLITE_ROW) {
        if (context.budget.remaining <= 0) {
            context.budget.truncated = true;
            break;
        }
        const std::string target_path = text_column(statement.stmt, 0);
        const std::string tab_url = text_column(statement.stmt, 1);
        const long long total_bytes = integer_column(statement.stmt, 2);
        const long long start_time = integer_column(statement.stmt, 3);
        const long long end_time = integer_column(statement.stmt, 4);
        const long long state = integer_column(statement.stmt, 5);
        const long long danger_type = integer_column(statement.stmt, 6);
        const std::string mime_type = text_column(statement.stmt, 7);

        const std::string identity = !target_path.empty() ? target_path
                                     : (!tab_url.empty() ? tab_url : "download");
        const std::string display = !target_path.empty() ? target_path : tab_url;

        ArtifactRow row;
        row.type = "browser_download";
        row.source = context.source_utf8;
        row.key = identity + "|" + std::to_string(start_time);
        if (total_bytes > 0) row.size = static_cast<std::uint64_t>(total_bytes);
        row.timestamp_utc = chrome_time_iso(start_time);
        row.path = display;
        row.name = base_name_utf8(display);

        JsonObject data;
        data.string("browser", browser_name(context.browser));
        data.string("profile", context.profile_utf8);
        if (target) data.string("target_path", target_path);
        if (url) data.string("tab_url", tab_url);
        if (has_total) data.integer("total_bytes", total_bytes);
        if (has_start) data.integer("start_time", start_time);
        if (has_end) data.integer("end_time", end_time);
        if (has_state) data.integer("state", state);
        if (has_danger) data.integer("danger_type", danger_type);
        if (has_mime) data.string("mime_type", mime_type);
        row.data = data.finish();
        emit_row(context, row);
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const char* message = sqlite3_errmsg(context.db);
        add_status(context, "downloads", "error",
            std::string("downloads query failed: ") +
            (message ? message : "sqlite3_step failed"));
    }
}

void collect_firefox_places(DbContext& context,
                            const std::vector<std::string>& columns) {
    const bool has_title = has_column(columns, "title");
    const bool has_count = has_column(columns, "visit_count");
    const bool has_last = has_column(columns, "last_visit_date");

    std::string sql = "SELECT " + text_select("url", true) + ", " +
        text_select("title", has_title) + ", " +
        integer_select("visit_count", has_count) + ", " +
        integer_select("last_visit_date", has_last) + " FROM moz_places";
    if (has_last) sql += " ORDER BY last_visit_date DESC";

    StmtGuard statement;
    std::string error;
    if (!prepare_statement(context.db, sql, &statement.stmt, error)) {
        add_status(context, "moz_places", "error",
                   "moz_places query failed: " + error);
        return;
    }

    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.stmt)) == SQLITE_ROW) {
        if (context.budget.remaining <= 0) {
            context.budget.truncated = true;
            break;
        }
        const std::string url = text_column(statement.stmt, 0);
        const std::string title = text_column(statement.stmt, 1);
        const long long visit_count = integer_column(statement.stmt, 2);
        const long long last_visit = integer_column(statement.stmt, 3);

        ArtifactRow row;
        row.type = "browser_history";
        row.source = context.source_utf8;
        row.key = url;
        row.timestamp_utc = firefox_time_iso(last_visit);
        row.path = url;
        row.name = title;

        JsonObject data;
        data.string("browser", "firefox");
        data.string("profile", context.profile_utf8);
        data.string("url", url);
        data.string("title", title);
        data.integer("visit_count", visit_count);
        row.data = data.finish();
        emit_row(context, row);
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const char* message = sqlite3_errmsg(context.db);
        add_status(context, "moz_places", "error",
            std::string("moz_places query failed: ") +
            (message ? message : "sqlite3_step failed"));
    }
}

void collect_firefox_visits(DbContext& context,
                            const std::vector<std::string>& place_columns,
                            const std::vector<std::string>& visit_columns) {
    const bool has_title = has_column(place_columns, "title");
    const bool has_transition = has_column(visit_columns, "transition");
    const bool has_visit_type = has_column(visit_columns, "visit_type");

    std::string transition_expr = "0";
    if (has_transition) {
        transition_expr = "IFNULL(h.transition,0)";
    } else if (has_visit_type) {
        transition_expr = "IFNULL(h.visit_type,0)";
    }

    const std::string sql = "SELECT " + text_select("p.url", true) + ", " +
        text_select("p.title", has_title) + ", " +
        integer_select("h.visit_date", true) + ", " + transition_expr +
        " FROM moz_historyvisits h LEFT JOIN moz_places p" +
        " ON p.id = h.place_id ORDER BY h.visit_date DESC";

    StmtGuard statement;
    std::string error;
    if (!prepare_statement(context.db, sql, &statement.stmt, error)) {
        add_status(context, "moz_historyvisits", "error",
                   "moz_historyvisits query failed: " + error);
        return;
    }

    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.stmt)) == SQLITE_ROW) {
        if (context.budget.remaining <= 0) {
            context.budget.truncated = true;
            break;
        }
        const std::string url = text_column(statement.stmt, 0);
        const std::string title = text_column(statement.stmt, 1);
        const long long visit_date = integer_column(statement.stmt, 2);
        const long long transition = integer_column(statement.stmt, 3);

        ArtifactRow row;
        row.type = "browser_visit";
        row.source = context.source_utf8;
        row.key = url + "|" + std::to_string(visit_date);
        row.timestamp_utc = firefox_time_iso(visit_date);
        row.path = url;
        row.name = title;

        JsonObject data;
        data.string("browser", "firefox");
        data.string("profile", context.profile_utf8);
        data.string("url", url);
        data.string("title", title);
        data.integer("transition", transition);
        row.data = data.finish();
        emit_row(context, row);
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const char* message = sqlite3_errmsg(context.db);
        add_status(context, "moz_historyvisits", "error",
            std::string("moz_historyvisits query failed: ") +
            (message ? message : "sqlite3_step failed"));
    }
}

void process_chrome(DbContext& context) {
    bool exists = false;
    std::string error;
    std::vector<std::string> url_columns;
    bool urls_ready = false;

    if (!table_exists(context.db, "urls", exists, error)) {
        add_status(context, "urls", "error",
                   "urls schema probe failed: " + error);
    } else if (!exists) {
        add_status(context, "urls", "error", "urls table is missing");
    } else if (!read_columns(context.db, "urls", url_columns, error)) {
        add_status(context, "urls", "error",
                   "urls schema probe failed: " + error);
    } else if (!has_column(url_columns, "url") ||
               !has_column(url_columns, "id")) {
        add_status(context, "urls", "error",
                   "urls table lacks url/id columns");
    } else {
        urls_ready = true;
        collect_chrome_urls(context, url_columns);
    }

    if (context.budget.remaining <= 0) {
        context.budget.truncated = true;
        finish_database(context);
        return;
    }

    bool visits_exist = false;
    std::vector<std::string> visit_columns;
    if (!table_exists(context.db, "visits", visits_exist, error)) {
        add_status(context, "visits", "error",
                   "visits schema probe failed: " + error);
    } else if (!visits_exist) {
        add_status(context, "visits", "error", "visits table is missing");
    } else if (!read_columns(context.db, "visits", visit_columns, error)) {
        add_status(context, "visits", "error",
                   "visits schema probe failed: " + error);
    } else if (!urls_ready) {
        add_status(context, "visits", "error",
                   "visits skipped: urls table unavailable");
    } else if (!has_column(visit_columns, "url") ||
               !has_column(visit_columns, "visit_time")) {
        add_status(context, "visits", "error",
                   "visits table lacks url/visit_time columns");
    } else {
        collect_chrome_visits(context, url_columns, visit_columns);
    }

    if (context.budget.remaining <= 0) {
        context.budget.truncated = true;
        finish_database(context);
        return;
    }

    collect_chrome_downloads(context);
    finish_database(context);
}

void process_firefox(DbContext& context) {
    bool places_exist = false;
    std::string error;
    std::vector<std::string> place_columns;
    bool places_ready = false;

    if (!table_exists(context.db, "moz_places", places_exist, error)) {
        add_status(context, "moz_places", "error",
                   "moz_places schema probe failed: " + error);
    } else if (!places_exist) {
        add_status(context, "moz_places", "error",
                   "moz_places table is missing");
    } else if (!read_columns(context.db, "moz_places", place_columns, error)) {
        add_status(context, "moz_places", "error",
                   "moz_places schema probe failed: " + error);
    } else if (!has_column(place_columns, "url") ||
               !has_column(place_columns, "id")) {
        add_status(context, "moz_places", "error",
                   "moz_places table lacks url/id columns");
    } else {
        places_ready = true;
        collect_firefox_places(context, place_columns);
    }

    if (context.budget.remaining <= 0) {
        context.budget.truncated = true;
        finish_database(context);
        return;
    }

    bool visits_exist = false;
    std::vector<std::string> visit_columns;
    if (!table_exists(context.db, "moz_historyvisits", visits_exist, error)) {
        add_status(context, "moz_historyvisits", "error",
                   "moz_historyvisits schema probe failed: " + error);
    } else if (!visits_exist) {
        add_status(context, "moz_historyvisits", "error",
                   "moz_historyvisits table is missing");
    } else if (!read_columns(context.db, "moz_historyvisits",
                             visit_columns, error)) {
        add_status(context, "moz_historyvisits", "error",
                   "moz_historyvisits schema probe failed: " + error);
    } else if (!places_ready) {
        add_status(context, "moz_historyvisits", "error",
                   "moz_historyvisits skipped: moz_places unavailable");
    } else if (!has_column(visit_columns, "place_id") ||
               !has_column(visit_columns, "visit_date")) {
        add_status(context, "moz_historyvisits", "error",
                   "moz_historyvisits lacks place_id/visit_date columns");
    } else {
        collect_firefox_visits(context, place_columns, visit_columns);
    }

    finish_database(context);
}

} // namespace

int collect_browser(const ArtifactSink& sink, std::wstring& error) {
    error.clear();
    if (!sink) {
        error = L"collect_browser received an empty sink";
        return -1;
    }

    const std::vector<DatabaseTask> tasks = discover_databases();
    if (tasks.empty()) return 0;

    std::vector<DatabaseFailure> failures;
    int total_rows = 0;
    int opened = 0;

    for (const DatabaseTask& task : tasks) {
        std::uint64_t size = 0;
        if (!file_size(task.path, size)) {
            failures.push_back({task, L"cannot read database metadata",
                                "skipped"});
            continue;
        }
        if (size > kMaxDatabaseBytes) {
            failures.push_back({task,
                L"database exceeds 2 GiB limit (" +
                    std::to_wstring(size) + L" bytes)",
                "skipped"});
            continue;
        }

        TempCopy temp;
        std::wstring reason;
        if (!create_temp_copy(task.path, temp, reason)) {
            failures.push_back({task, reason, "copy_failed"});
            continue;
        }

        SqliteGuard database;
        const std::string utf8_path = art::to_utf8(temp.path);
        const int rc = sqlite3_open_v2(utf8_path.c_str(), &database.db,
            SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            std::string message;
            if (database.db) {
                const char* text = sqlite3_errmsg(database.db);
                message = text ? text : "sqlite3_open_v2 failed";
            } else {
                const char* text = sqlite3_errstr(rc);
                message = text ? text : "sqlite3_open_v2 failed";
            }
            failures.push_back({task,
                L"sqlite open failed: " + art::from_utf8(message),
                "open_failed"});
            continue;
        }

        std::string probe_error;
        if (!probe_database(database.db, probe_error)) {
            failures.push_back({task,
                L"database is not usable SQLite: " +
                    art::from_utf8(probe_error),
                "open_failed"});
            continue;
        }

        ++opened;
        sqlite3_busy_timeout(database.db, 2000);
        sqlite3_exec(database.db, "PRAGMA query_only=ON", nullptr, nullptr,
                     nullptr);
        sqlite3_exec(database.db, "PRAGMA cache_size=-8192", nullptr, nullptr,
                     nullptr);
        sqlite3_exec(database.db, "PRAGMA temp_store=1", nullptr, nullptr,
                     nullptr);

        DbContext context;
        context.db = database.db;
        context.browser = task.browser;
        context.profile = task.profile;
        context.profile_utf8 = art::to_utf8(task.profile);
        context.source_utf8 = art::to_utf8(task.path);
        context.sink = &sink;

        if (task.browser == BrowserKind::firefox) {
            process_firefox(context);
        } else {
            process_chrome(context);
        }
        total_rows += context.emitted;
    }

    if (opened == 0) {
        error = L"no browser database could be opened";
        int listed = 0;
        for (const DatabaseFailure& failure : failures) {
            if (listed >= 3) {
                error += L"; ...";
                break;
            }
            error += L"; " + failure.task.path + L": " + failure.reason;
            ++listed;
        }
        return -1;
    }

    for (const DatabaseFailure& failure : failures) {
        DbContext context;
        context.browser = failure.task.browser;
        context.profile = failure.task.profile;
        context.profile_utf8 = art::to_utf8(failure.task.profile);
        context.source_utf8 = art::to_utf8(failure.task.path);
        context.sink = &sink;
        add_status(context, "", failure.status,
                   art::to_utf8(failure.reason));
        total_rows += context.emitted;
    }

    return total_rows;
}

} // namespace deep
