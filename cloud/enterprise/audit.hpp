#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace deep::cloud {

struct AuditEntry {
    std::string timestamp_utc;
    std::string actor;
    std::string action;
    std::string target;
    std::string result;
    std::string detail;
    std::string prev_hash;
    std::string hash;
};

struct AuditVerifyReport {
    bool ok = false;
    std::size_t entries = 0;
    std::vector<std::string> errors;
    std::string last_hash;
};

// Append-only JSONL audit log with a SHA-256 hash chain. It detects edits and
// reordering inside the file; it does not protect against an administrator
// deleting or replacing the whole file (that needs an external anchor/WORM).
class AuditLog {
public:
    explicit AuditLog(std::string path);

    bool append(const std::string& actor, const std::string& action,
                const std::string& target, const std::string& result,
                const std::string& detail, std::string& error);
    AuditVerifyReport verify() const;
    std::vector<AuditEntry> tail(std::size_t limit = 100) const;

    const std::string& path() const { return path_; }

private:
    std::string path_;
    mutable std::mutex mutex_;
};

} // namespace deep::cloud
