// language: C++, file: artifact_amcache.cpp, target: Windows 10/11
// Amcache normalizer. Reuses the offline regf walker and relabels values under
// Root\InventoryApplication*, Root\File and Root\Programs as "amcache".

#include "artifacts.hpp"

#include <string>

namespace deep {
namespace {

bool is_amcache_path(const std::string& path) {
    return path.rfind("\\Root\\InventoryApplicationFile", 0) == 0 ||
        path.rfind("\\Root\\InventoryApplication", 0) == 0 ||
        path.rfind("\\Root\\File\\", 0) == 0 ||
        path.rfind("\\Root\\Programs\\", 0) == 0;
}

} // namespace

int collect_amcache(const std::wstring& hive_path, const ArtifactSink& sink,
                    std::wstring& error) {
    int emitted = 0;
    deep::ArtifactSink filter = [&](const deep::ArtifactRow& row) {
        if (row.type != "registry_offline" || !is_amcache_path(row.path))
            return;
        ArtifactRow out = row;
        out.type = "amcache";
        sink(out);
        ++emitted;
    };
    std::wstring parse_error;
    const int rows = collect_hive_file(hive_path, false, filter, parse_error);
    if (rows < 0) {
        error = parse_error;
        return -1;
    }
    if (emitted == 0) {
        error = L"Amcache hive parsed but no Amcache paths were found";
        return 0;
    }
    return emitted;
}

} // namespace deep
