// language: C++, file: .oracle_ios/plist_regress.cpp
// Runs the C++ plist parser over a directory tree and reports, per file, whether
// it parsed. A crash here is the finding: the process dies and the runner says so.
#include "ios_backup.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: %s <dir> [dir2 ...]\n", argv[0]); return 2; }

    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) {
        std::error_code ec;
        if (fs::is_regular_file(argv[i], ec)) { files.push_back(argv[i]); continue; }
        for (auto it = fs::recursive_directory_iterator(argv[i], ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file()) files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());

    std::size_t ok = 0, failed = 0;
    for (const auto& p : files) {
        olho::ios::PlistValue value;
        std::string error;
        if (olho::ios::plist_load(p.string(), value, error)) ++ok;
        else ++failed;
    }
    std::printf("arquivos=%zu aceitos=%zu recusados=%zu\n", files.size(), ok, failed);
    return 0;
}
