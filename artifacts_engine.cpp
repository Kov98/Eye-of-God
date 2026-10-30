// language: C++, file: artifacts_engine.cpp, target: Windows 10/11
// Orchestrates artifact collectors into JSONL and a SQLite case database.

#include "artifacts.hpp"
#include "case_db.hpp"

#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace deep {
namespace {

std::string row_json(const ArtifactRow& row) {
    std::ostringstream out;
    out << "{\"type\":" << art::json_escape(row.type)
        << ",\"source\":" << art::json_escape(row.source)
        << ",\"key\":" << art::json_escape(row.key)
        << ",\"timestamp_utc\":" << art::json_escape(row.timestamp_utc)
        << ",\"path\":" << art::json_escape(row.path)
        << ",\"name\":" << art::json_escape(row.name)
        << ",\"size\":" << row.size
        << ",\"sha256\":" << art::json_escape(row.sha256)
        << ",\"data\":" << (row.data.empty() ? "{}" : row.data) << "}";
    return out.str();
}

bool append_line(HANDLE file, const std::string& line) {
    const std::string with_newline = line + "\r\n";
    DWORD written = 0;
    return WriteFile(file, with_newline.data(),
                     static_cast<DWORD>(with_newline.size()), &written, nullptr) &&
        written == with_newline.size();
}

std::wstring default_out_dir() {
    wchar_t cwd[32768] = {};
    if (GetCurrentDirectoryW(32768, cwd) == 0) return L".";
    return art::join_path(cwd, L"cases\\artifacts_" + art::from_utf8(art::utc_id()));
}

} // namespace

int run_artifacts(const ArtifactOptions& options) {
    SetConsoleOutputCP(CP_UTF8);
    const std::wstring out = options.out_dir.empty() ? default_out_dir()
                                                      : options.out_dir;
    if (!art::ensure_directory(out)) {
        std::cerr << "artifacts: cannot create output directory\n";
        return 1;
    }
    HANDLE jsonl = CreateFileW(art::extend_path(art::join_path(out,
        L"artifacts.jsonl")).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (jsonl == INVALID_HANDLE_VALUE) {
        std::cerr << "artifacts: cannot create JSONL\n";
        return 1;
    }

    const std::wstring db_path = options.db_path.empty()
        ? art::join_path(out, L"case.db") : options.db_path;
    CaseDatabase database;
    std::wstring error;
    if (!db_path.empty()) {
        if (!database.open(db_path, error)) {
            std::cerr << "artifacts: " << art::to_utf8(error) << "\n";
            CloseHandle(jsonl);
            return 1;
        }
        if (!database.begin(error)) {
            std::cerr << "artifacts: " << art::to_utf8(error) << "\n";
            CloseHandle(jsonl);
            return 1;
        }
    }

    std::map<std::string, int> counts;
    int total = 0;
    bool sink_failed = false;
    auto sink = [&](const ArtifactRow& row) {
        if (sink_failed) return;
        if (!append_line(jsonl, row_json(row))) sink_failed = true;
        if (database.is_open() && !database.insert(row, error)) sink_failed = true;
        ++counts[row.type];
        ++total;
        if (total % 1000 == 0) {
            std::cout << "  artifacts: " << total << "\r";
            std::cout.flush();
        }
    };

    if (options.registry) {
        std::wstring collector_error;
        const int rows = collect_registry(sink, collector_error);
        if (rows < 0) {
            std::cerr << "artifacts: registry: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (options.browser) {
        std::wstring collector_error;
        const int rows = collect_browser(sink, collector_error);
        if (rows < 0) {
            std::cerr << "artifacts: browser: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (options.browser_secrets) {
        std::wstring collector_error;
        if (collect_browser_secrets(options.browser_profile, sink,
                                    collector_error) < 0) {
            std::cerr << "artifacts: browser secrets: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (options.recycle) {
        std::wstring collector_error;
        if (collect_recycle(sink, collector_error) < 0) {
            std::cerr << "artifacts: recycle: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (options.lnk) {
        std::wstring collector_error;
        if (collect_lnk(sink, collector_error) < 0) {
            std::cerr << "artifacts: lnk: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (options.jumplist) {
        std::wstring collector_error;
        if (collect_jumplist(sink, collector_error) < 0) {
            std::cerr << "artifacts: jumplist: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (options.prefetch) {
        std::wstring prefetch_dir = options.prefetch_dir;
        if (prefetch_dir.empty()) {
            wchar_t windows[MAX_PATH] = {};
            if (GetWindowsDirectoryW(windows, MAX_PATH) != 0)
                prefetch_dir = art::join_path(windows, L"Prefetch");
        }
        if (!prefetch_dir.empty() &&
            GetFileAttributesW(prefetch_dir.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring collector_error;
            collect_prefetch_directory(prefetch_dir, sink, collector_error);
        }
    }
    if (options.shellbags) {
        std::wstring collector_error;
        if (collect_shellbags(sink, collector_error) < 0) {
            std::cerr << "artifacts: shellbags: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.hive_file.empty()) {
        std::wstring collector_error;
        if (collect_hive_file(options.hive_file, options.hive_deleted, sink,
                              collector_error) < 0) {
            std::cerr << "artifacts: hive: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.hive_dir.empty()) {
        std::wstring collector_error;
        if (collect_hive_directory(options.hive_dir, options.hive_deleted, sink,
                                   collector_error) < 0) {
            std::cerr << "artifacts: hive dir: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.amcache_file.empty()) {
        std::wstring collector_error;
        if (collect_amcache(options.amcache_file, sink, collector_error) < 0) {
            std::cerr << "artifacts: amcache: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.shimcache_file.empty()) {
        std::wstring collector_error;
        if (collect_shimcache_file(options.shimcache_file, sink,
                                   collector_error) < 0) {
            std::cerr << "artifacts: shimcache: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.shimcache_hive.empty()) {
        std::wstring collector_error;
        if (collect_shimcache_hive(options.shimcache_hive, sink,
                                   collector_error) < 0) {
            std::cerr << "artifacts: shimcache hive: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.srum_db.empty()) {
        std::wstring collector_error;
        if (collect_srum(options.srum_db, sink, collector_error) < 0) {
            std::cerr << "artifacts: srum: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.bam_hive.empty()) {
        std::wstring collector_error;
        if (collect_bam(options.bam_hive, sink, collector_error) < 0) {
            std::cerr << "artifacts: bam: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.evtx_file.empty()) {
        std::wstring collector_error;
        if (collect_evtx_file(options.evtx_file, sink, collector_error) < 0) {
            std::cerr << "artifacts: evtx: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }
    if (!options.evtx_dir.empty()) {
        std::wstring collector_error;
        collect_evtx_directory(options.evtx_dir, sink, collector_error);
        if (!collector_error.empty()) {
            std::cerr << "artifacts: evtx dir: "
                      << art::to_utf8(collector_error) << "\n";
        }
    }

    if (database.is_open()) {
        database.set_meta("artifacts_total", std::to_string(total), error);
        database.set_meta("completed_utc", art::now_iso(), error);
        if (sink_failed) database.rollback(error);
        else database.commit(error);
    }
    FlushFileBuffers(jsonl);
    CloseHandle(jsonl);

    std::ostringstream summary;
    summary << "{\n  \"total\": " << total << ",\n  \"counts\": {";
    bool first = true;
    for (const auto& entry : counts) {
        if (!first) summary << ",";
        first = false;
        summary << "\n    " << art::json_escape(entry.first) << ": " << entry.second;
    }
    summary << "\n  },\n  \"output\": " << art::json_escape_w(out);
    summary << ",\n  \"database\": " << art::json_escape_w(db_path);
    summary << "\n}\n";
    HANDLE summary_file = CreateFileW(art::extend_path(art::join_path(out,
        L"artifact_summary.json")).c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (summary_file != INVALID_HANDLE_VALUE) {
        const std::string text = summary.str();
        DWORD written = 0;
        WriteFile(summary_file, text.data(), static_cast<DWORD>(text.size()),
                  &written, nullptr);
        CloseHandle(summary_file);
    }

    std::cout << "artifacts: total=" << total << "\n";
    for (const auto& entry : counts)
        std::cout << "  " << entry.first << ": " << entry.second << "\n";
    std::cout << "artifacts: output=" << art::to_utf8(out) << "\n";
    if (database.is_open()) {
        std::cout << "artifacts: db=" << art::to_utf8(db_path) << "\n";
    }
    database.close();
    return sink_failed ? 1 : 0;
}

} // namespace deep
