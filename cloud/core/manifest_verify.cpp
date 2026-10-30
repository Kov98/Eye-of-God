#include "manifest_verify.hpp"

#include "cloud_core.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace deep::cloud {
namespace {

std::string sanitize_component(std::string_view value) {
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.')
            result.push_back(static_cast<char>(c));
        else
            result.push_back('_');
    }
    return result.empty() ? "unknown" : result;
}

bool safe_relative(const std::string& name) {
    if (name.empty()) return false;
    const std::filesystem::path path(name);
    if (path.is_absolute()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

std::string read_file(const std::filesystem::path& path, bool& ok) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ok = false;
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    ok = true;
    return content;
}

void verify_manifest(const std::filesystem::path& manifest_path,
                     ManifestVerifyReport& report) {
    bool read_ok = false;
    const std::string text = read_file(manifest_path, read_ok);
    if (!read_ok) {
        report.errors.push_back("cannot read " + manifest_path.string());
        return;
    }
    ++report.manifests;
    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(text);
    } catch (const std::exception&) {
        report.errors.push_back("invalid JSON: " + manifest_path.string());
        return;
    }
    if (!manifest.is_object()) {
        report.errors.push_back("manifest is not an object");
        return;
    }
    if (manifest.value("legal_basis", "").empty())
        report.errors.push_back("manifest has no legal_basis");
    if (manifest.value("job_id", "").empty())
        report.errors.push_back("manifest has no job_id");
    report.objects += manifest.value("objects", 0);

    const std::filesystem::path base = manifest_path.parent_path();
    const std::string job_file = manifest.value("job_status_file", "");
    const std::filesystem::path case_root =
        base.parent_path().parent_path().parent_path();
    if (!job_file.empty() && !std::filesystem::exists(case_root / job_file)) {
        report.errors.push_back("job journal missing: " + job_file);
    }
    if (!std::filesystem::exists(base / "timeline.jsonl"))
        report.errors.push_back("timeline.jsonl missing for " + base.string());

    if (!manifest.contains("files") || !manifest["files"].is_array()) {
        report.errors.push_back("manifest files[] missing");
        return;
    }
    for (const auto& entry : manifest["files"]) {
        if (!entry.is_object()) {
            report.errors.push_back("manifest file entry is not an object");
            continue;
        }
        ++report.files;
        const std::string name = entry.value("name", "");
        const std::string expected = entry.value("sha256", "");
        if (!safe_relative(name)) {
            report.errors.push_back("unsafe evidence path: " + name);
            continue;
        }
        bool file_ok = false;
        const std::string content = read_file(base / std::filesystem::path(name), file_ok);
        if (!file_ok) {
            report.errors.push_back("missing evidence file: " + name);
            continue;
        }
        if (static_cast<int>(content.size()) != entry.value("size_bytes", -1)) {
            report.errors.push_back("size mismatch: " + name);
        }
        std::string expected_lower = expected;
        std::transform(expected_lower.begin(), expected_lower.end(),
                       expected_lower.begin(), [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (sha256_hex(content) != expected_lower) {
            report.errors.push_back("sha256 mismatch: " + name);
            continue;
        }
        ++report.verified;
    }
}

} // namespace

ManifestVerifyReport verify_cloud_manifests(const std::string& case_dir,
                                            const std::string& account) {
    ManifestVerifyReport report;
    const std::filesystem::path root =
        std::filesystem::path(case_dir) / "cloud" / "m365";
    if (!std::filesystem::exists(root)) {
        report.errors.push_back("cloud/m365 directory missing");
        return report;
    }
    std::vector<std::filesystem::path> manifests;
    if (!account.empty()) {
        const auto path = root / sanitize_component(account) / "manifest.json";
        if (std::filesystem::exists(path)) manifests.push_back(path);
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_directory()) continue;
            const auto path = entry.path() / "manifest.json";
            if (std::filesystem::exists(path)) manifests.push_back(path);
        }
    }
    if (manifests.empty()) {
        report.errors.push_back("no cloud manifest found");
        return report;
    }
    for (const auto& manifest : manifests) verify_manifest(manifest, report);
    report.ok = report.errors.empty() && report.manifests > 0;
    return report;
}

} // namespace deep::cloud
