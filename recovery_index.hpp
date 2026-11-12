#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace deep::forensic {

struct RecoveredFile {
    std::string path;
    std::string relative;
    std::string kind;
    std::string extension;
    std::uint64_t size = 0;
    std::string sha256;
    std::string modified_utc;
    bool text_like = false;
};

struct DuplicateGroup {
    std::string sha256;
    std::vector<std::string> paths;
};

struct RecoveryIndex {
    std::string schema = "olho-de-deus/recovery-index/v1";
    std::string root;
    std::string generated_at_utc;
    std::vector<RecoveredFile> files;
    std::vector<DuplicateGroup> duplicate_groups;
};

RecoveryIndex scan_recovery_directory(const std::string& root, std::string& error);

bool write_recovery_index_json(const RecoveryIndex& index,
                               const std::string& path, std::string& error);

} // namespace deep::forensic
