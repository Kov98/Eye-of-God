#include "audit.hpp"

#include <windows.h>

#include "cloud/core/cloud_core.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace deep::cloud {
namespace {

class AuditFileLock {
public:
    explicit AuditFileLock(const std::string& path, std::string& error) {
        try {
            const std::filesystem::path lock_path = std::filesystem::path(path + ".lock");
            if (lock_path.has_parent_path())
                std::filesystem::create_directories(lock_path.parent_path());
            handle_ = CreateFileW(lock_path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_HIDDEN, nullptr);
            if (handle_ == INVALID_HANDLE_VALUE) {
                error = "cannot open audit lock";
                return;
            }
            OVERLAPPED overlap{};
            if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap)) {
                error = "cannot lock audit";
                CloseHandle(handle_);
                handle_ = INVALID_HANDLE_VALUE;
                return;
            }
            locked_ = true;
        } catch (const std::exception&) {
            error = "cannot lock audit";
        }
    }
    ~AuditFileLock() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                OVERLAPPED overlap{};
                UnlockFileEx(handle_, 0, 1, 0, &overlap);
            }
            CloseHandle(handle_);
        }
    }
    AuditFileLock(const AuditFileLock&) = delete;
    AuditFileLock& operator=(const AuditFileLock&) = delete;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool locked_ = false;
};
constexpr const char* kGenesis = "GENESIS";

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
    gmtime_s(&value, &now);
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &value);
    return text;
}

std::string canonical(const AuditEntry& entry) {
    return entry.prev_hash + "\n" + entry.timestamp_utc + "\n" + entry.actor + "\n" +
           entry.action + "\n" + entry.target + "\n" + entry.result + "\n" + entry.detail;
}

AuditEntry from_json(const nlohmann::json& document) {
    AuditEntry entry;
    entry.timestamp_utc = document.value("timestamp_utc", "");
    entry.actor = document.value("actor", "");
    entry.action = document.value("action", "");
    entry.target = document.value("target", "");
    entry.result = document.value("result", "");
    entry.detail = document.value("detail", "");
    entry.prev_hash = document.value("prev_hash", "");
    entry.hash = document.value("hash", "");
    return entry;
}

nlohmann::json to_json(const AuditEntry& entry) {
    return {
        {"timestamp_utc", entry.timestamp_utc},
        {"actor", entry.actor},
        {"action", entry.action},
        {"target", entry.target},
        {"result", entry.result},
        {"detail", entry.detail},
        {"prev_hash", entry.prev_hash},
        {"hash", entry.hash}
    };
}

bool read_last_hash(const std::string& path, std::string& hash) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        hash = kGenesis;
        return true;
    }
    std::string line;
    std::string last;
    while (std::getline(input, line)) {
        if (!line.empty()) last = line;
    }
    if (last.empty()) {
        hash = kGenesis;
        return true;
    }
    try {
        const nlohmann::json document = nlohmann::json::parse(last);
        hash = document.value("hash", "");
        return !hash.empty();
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

AuditLog::AuditLog(std::string path) : path_(std::move(path)) {}

bool AuditLog::append(const std::string& actor, const std::string& action,
                      const std::string& target, const std::string& result,
                      const std::string& detail, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lock_error;
    AuditFileLock file_lock(path_, lock_error);
    if (!lock_error.empty()) {
        error = lock_error;
        return false;
    }
    error.clear();
    if (actor.empty() || action.empty()) {
        error = "audit actor/action is required";
        return false;
    }
    std::string previous;
    if (!read_last_hash(path_, previous)) {
        error = "audit log is corrupt; verify it before appending";
        return false;
    }
    AuditEntry entry;
    entry.timestamp_utc = utc_now();
    entry.actor = actor;
    entry.action = action;
    entry.target = target;
    entry.result = result;
    entry.detail = detail;
    entry.prev_hash = previous;
    entry.hash = sha256_hex(canonical(entry));
    try {
        const std::filesystem::path path(path_);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path_, std::ios::binary | std::ios::app);
        if (!output) {
            error = "cannot open audit log";
            return false;
        }
        output << to_json(entry).dump() << "\n";
        output.close();
        if (!output) {
            error = "cannot write audit log";
            return false;
        }
        return true;
    } catch (const std::exception&) {
        error = "cannot write audit log";
        return false;
    }
}

AuditVerifyReport AuditLog::verify() const {
    std::lock_guard<std::mutex> lock(mutex_);
    AuditVerifyReport report;
    std::string lock_error;
    AuditFileLock file_lock(path_, lock_error);
    if (!lock_error.empty()) {
        report.errors.push_back(lock_error);
        return report;
    }
    report.last_hash = kGenesis;
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        report.ok = true;
        return report;
    }
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;
        nlohmann::json document;
        try {
            document = nlohmann::json::parse(line);
        } catch (const std::exception&) {
            report.errors.push_back("line " + std::to_string(line_number) +
                                    ": invalid JSON");
            break;
        }
        if (!document.is_object()) {
            report.errors.push_back("line " + std::to_string(line_number) +
                                    ": not an object");
            break;
        }
        const AuditEntry entry = from_json(document);
        if (entry.timestamp_utc.empty() || entry.hash.empty() ||
            entry.actor.empty() || entry.action.empty()) {
            report.errors.push_back("line " + std::to_string(line_number) +
                                    ": missing audit fields");
            break;
        }
        if (entry.prev_hash != report.last_hash) {
            report.errors.push_back("line " + std::to_string(line_number) +
                                    ": broken hash chain");
            break;
        }
        if (sha256_hex(canonical(entry)) != entry.hash) {
            report.errors.push_back("line " + std::to_string(line_number) +
                                    ": hash mismatch");
            break;
        }
        report.last_hash = entry.hash;
        ++report.entries;
    }
    report.ok = report.errors.empty();
    return report;
}

std::vector<AuditEntry> AuditLog::tail(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AuditEntry> entries;
    std::string lock_error;
    AuditFileLock file_lock(path_, lock_error);
    if (!lock_error.empty()) return entries;
    std::ifstream input(path_, std::ios::binary);
    if (!input) return entries;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        try {
            const nlohmann::json document = nlohmann::json::parse(line);
            if (document.is_object()) entries.push_back(from_json(document));
        } catch (const std::exception&) {
            // A corrupt log is surfaced by verify(), not silently repaired.
            break;
        }
        if (entries.size() > limit) entries.erase(entries.begin());
    }
    return entries;
}

} // namespace deep::cloud
