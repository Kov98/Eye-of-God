// language: C++, file: artifact_bam.cpp, target: Windows 10/11
// BAM/DAM offline parser. Values under Services\bam\State\UserSettings\<SID>
// are executable paths with an 8-byte FILETIME and flags; Version and
// SequenceNumber are metadata.

#include "artifacts.hpp"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace deep {
namespace {

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

std::wstring basename(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::string hex(const std::vector<std::uint8_t>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::uint8_t byte : data) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

} // namespace

int collect_bam(const std::wstring& hive_path, const ArtifactSink& sink,
                std::wstring& error) {
    int rows = 0;
    const int result = enumerate_hive_values(hive_path,
        [&](const HiveValueRecord& record) {
            const bool bam = record.key_path.find(
                L"\\Services\\bam\\State\\UserSettings\\") != std::wstring::npos;
            const bool dam = record.key_path.find(
                L"\\Services\\dam\\State\\UserSettings\\") != std::wstring::npos;
            if (!bam && !dam) return;
            if (record.value_name == L"Version" ||
                record.value_name == L"SequenceNumber")
                return;
            if (record.data.size() < 8) return;
            std::wstring sid;
            const std::wstring marker = L"UserSettings\\";
            const std::size_t at = record.key_path.find(marker);
            if (at != std::wstring::npos) {
                const std::size_t start = at + marker.size();
                const std::size_t end = record.key_path.find(L'\\', start);
                sid = record.key_path.substr(start, end == std::wstring::npos
                    ? std::wstring::npos : end - start);
            }
            const std::uint64_t modified = read_u64(record.data.data());
            const std::uint32_t flags = record.data.size() >= 12
                ? read_u32(record.data.data() + 8) : 0;
            ArtifactRow row;
            row.type = bam ? "bam" : "dam";
            row.source = art::to_utf8(hive_path);
            row.key = art::to_utf8(sid) + "|" + art::to_utf8(record.value_name);
            row.timestamp_utc = art::filetime_iso(modified);
            row.path = art::to_utf8(record.value_name);
            row.name = art::to_utf8(basename(record.value_name));
            row.size = record.data.size();
            row.data = "{\"sid\":" + art::json_escape_w(sid) +
                ",\"executable\":" + art::json_escape_w(record.value_name) +
                ",\"last_execution_utc\":" +
                art::json_escape(art::filetime_iso(modified)) +
                ",\"flags\":" + std::to_string(flags) +
                ",\"raw_hex\":" + art::json_escape(hex(record.data)) + "}";
            sink(row);
            ++rows;
        }, error);
    if (result < 0) return -1;
    if (rows == 0) {
        error = L"no BAM/DAM values found";
        return 0;
    }
    return rows;
}

} // namespace deep
