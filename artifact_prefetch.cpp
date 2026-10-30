// language: C++, file: artifact_prefetch.cpp, target: Windows 10/11
// Prefetch parser for raw SCCA files and MAM-compressed Windows 10/11 files.
// Header layout follows libscca's PF format documentation (v17, v23, v26,
// v30 variant 1/2, v31).

#include "artifacts.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace deep {
namespace {

using RtlGetCompressionWorkSpaceSizeFn = NTSTATUS(NTAPI*)(USHORT, PULONG, PULONG);
using RtlDecompressBufferExFn = NTSTATUS(NTAPI*)(USHORT, PUCHAR, ULONG,
    PUCHAR, ULONG, PULONG, PVOID);

constexpr USHORT kCompressionXpressHuff = 0x0004;
constexpr std::uint32_t kMaxFile = 64u * 1024u * 1024u;

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

bool read_file(const std::wstring& path, std::vector<std::uint8_t>& data,
               std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"open failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > kMaxFile) {
        error = L"size invalid";
        CloseHandle(file);
        return false;
    }
    data.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()),
                             &read, nullptr) && read == data.size();
    CloseHandle(file);
    if (!ok) error = L"read failed";
    return ok;
}

bool decompress_mam(const std::vector<std::uint8_t>& input,
                    std::vector<std::uint8_t>& output, std::wstring& error) {
    if (input.size() < 12 || std::memcmp(input.data(), "MAM", 3) != 0) {
        error = L"not a MAM stream";
        return false;
    }
    const std::uint32_t uncompressed_size = read_u32(input.data() + 4);
    if (uncompressed_size == 0 || uncompressed_size > kMaxFile) {
        error = L"MAM size invalid";
        return false;
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        error = L"ntdll unavailable";
        return false;
    }
    auto get_workspace = reinterpret_cast<RtlGetCompressionWorkSpaceSizeFn>(
        GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize"));
    auto decompress = reinterpret_cast<RtlDecompressBufferExFn>(
        GetProcAddress(ntdll, "RtlDecompressBufferEx"));
    if (get_workspace == nullptr || decompress == nullptr) {
        error = L"compression API unavailable";
        return false;
    }
    ULONG workspace_size = 0;
    ULONG fragment_size = 0;
    if (get_workspace(kCompressionXpressHuff, &workspace_size, &fragment_size) < 0) {
        error = L"workspace query failed";
        return false;
    }
    std::vector<std::uint8_t> workspace(workspace_size);
    output.resize(uncompressed_size);
    ULONG final_size = 0;
    const NTSTATUS status = decompress(kCompressionXpressHuff, output.data(),
        uncompressed_size, const_cast<PUCHAR>(input.data() + 8),
        static_cast<ULONG>(input.size() - 8), &final_size, workspace.data());
    if (status < 0) {
        error = L"XPRESS Huffman decompression failed, status " +
            std::to_wstring(static_cast<unsigned long>(status));
        return false;
    }
    output.resize(final_size);
    return true;
}

std::wstring utf16_string(const std::uint8_t* data, std::size_t bytes,
                          std::size_t chars) {
    if (data == nullptr || chars == 0) return {};
    if (chars * 2 > bytes) chars = bytes / 2;
    std::wstring out(chars, L'\0');
    std::memcpy(&out[0], data, chars * 2);
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring filename_at(const std::vector<std::uint8_t>& data,
                         std::uint32_t strings_offset, std::uint32_t string_offset,
                         std::uint32_t chars) {
    const std::uint64_t start = static_cast<std::uint64_t>(strings_offset) +
        string_offset;
    if (chars == 0 || start + static_cast<std::uint64_t>(chars) * 2 > data.size())
        return {};
    return utf16_string(data.data() + start, data.size() - static_cast<std::size_t>(start),
                        chars);
}

struct FileInfoLayout {
    std::uint32_t metrics_offset = 0;
    std::uint32_t metrics_count = 0;
    std::uint32_t trace_offset = 0;
    std::uint32_t trace_count = 0;
    std::uint32_t strings_offset = 0;
    std::uint32_t strings_size = 0;
    std::uint32_t volumes_offset = 0;
    std::uint32_t volumes_count = 0;
    std::uint32_t volumes_size = 0;
    std::uint32_t run_count_offset = 0;
    std::uint32_t last_run_offset = 0;
    std::uint32_t last_run_count = 0;
};

bool parse_layout(std::uint32_t version, const std::vector<std::uint8_t>& data,
                  FileInfoLayout& layout) {
    const std::uint32_t base = 0x54;
    if (base + 36 > data.size()) return false;
    layout.metrics_offset = read_u32(data.data() + base);
    layout.metrics_count = read_u32(data.data() + base + 4);
    layout.trace_offset = read_u32(data.data() + base + 8);
    layout.trace_count = read_u32(data.data() + base + 12);
    layout.strings_offset = read_u32(data.data() + base + 16);
    layout.strings_size = read_u32(data.data() + base + 20);
    layout.volumes_offset = read_u32(data.data() + base + 24);
    layout.volumes_count = read_u32(data.data() + base + 28);
    layout.volumes_size = read_u32(data.data() + base + 32);
    if (version == 17) {
        layout.last_run_offset = base + 36;
        layout.last_run_count = 1;
        layout.run_count_offset = base + 60;
    } else if (version == 23) {
        layout.last_run_offset = base + 44;
        layout.last_run_count = 1;
        layout.run_count_offset = base + 68;
    } else if (version == 26) {
        layout.last_run_offset = base + 44;
        layout.last_run_count = 8;
        layout.run_count_offset = base + 124;
    } else if (version == 30) {
        layout.last_run_offset = base + 44;
        layout.last_run_count = 8;
        if (layout.metrics_offset == 0x130) {
            layout.run_count_offset = base + 124;
        } else {
            layout.run_count_offset = base + 116;
        }
    } else if (version == 31) {
        layout.last_run_offset = base + 44;
        layout.last_run_count = 8;
        layout.run_count_offset = base + 116;
    } else {
        return false;
    }
    if (layout.run_count_offset + 4 > data.size()) return false;
    return true;
}

std::string build_row_data(const std::vector<std::uint8_t>& data,
                           std::uint32_t version, const FileInfoLayout& layout,
                           const std::wstring& executable,
                           std::uint32_t hash, std::uint32_t flags) {
    std::ostringstream out;
    out << "{\"version\":" << version
        << ",\"executable\":" << art::json_escape_w(executable)
        << ",\"prefetch_hash\":\"0x" << std::hex << hash << std::dec << "\""
        << ",\"flags\":" << flags;
    std::uint32_t run_count = 0;
    if (layout.run_count_offset + 4 <= data.size())
        run_count = read_u32(data.data() + layout.run_count_offset);
    out << ",\"run_count\":" << run_count << ",\"last_run_times\":[";
    bool first = true;
    for (std::uint32_t i = 0; i < layout.last_run_count; ++i) {
        const std::uint64_t offset = layout.last_run_offset + i * 8ull;
        if (offset + 8 > data.size()) break;
        const std::uint64_t ft = read_u64(data.data() + offset);
        if (ft == 0) continue;
        if (!first) out << ",";
        first = false;
        out << art::json_escape(art::filetime_iso(ft));
    }
    out << "],\"volumes\":[";
    const std::uint32_t volume_entry_size = (version <= 17) ? 40 : (version <= 26 ? 104 : 96);
    for (std::uint32_t i = 0; i < layout.volumes_count; ++i) {
        const std::uint64_t entry = static_cast<std::uint64_t>(layout.volumes_offset) +
            static_cast<std::uint64_t>(i) * volume_entry_size;
        if (entry + 40 > data.size()) break;
        const std::uint32_t path_offset = read_u32(data.data() + entry);
        const std::uint32_t path_chars = read_u32(data.data() + entry + 4);
        const std::uint64_t creation = read_u64(data.data() + entry + 8);
        const std::uint32_t serial = read_u32(data.data() + entry + 16);
        const std::uint64_t path_start = static_cast<std::uint64_t>(layout.volumes_offset) +
            path_offset;
        if (path_chars == 0 || path_start + static_cast<std::uint64_t>(path_chars) * 2 >
            data.size()) continue;
        const std::wstring device = utf16_string(
            data.data() + path_start, data.size() - static_cast<std::size_t>(path_start),
            path_chars);
        if (i) out << ",";
        out << "{\"device_path\":" << art::json_escape_w(device)
            << ",\"serial\":\"0x" << std::hex << serial << std::dec << "\""
            << ",\"creation_utc\":" << art::json_escape(art::filetime_iso(creation))
            << "}";
    }
    out << "],\"files\":[";
    const std::uint32_t entry_size = (version <= 17) ? 20 : 32;
    std::uint32_t emitted = 0;
    for (std::uint32_t i = 0; i < layout.metrics_count && i < 10000; ++i) {
        const std::uint64_t entry = static_cast<std::uint64_t>(layout.metrics_offset) +
            static_cast<std::uint64_t>(i) * entry_size;
        if (entry + entry_size > data.size()) break;
        const std::uint32_t trace_index = read_u32(data.data() + entry);
        const std::uint32_t blocks = read_u32(data.data() + entry + 4);
        const std::uint32_t filename_offset = read_u32(data.data() +
            entry + (version <= 17 ? 8 : 12));
        const std::uint32_t filename_chars = read_u32(data.data() +
            entry + (version <= 17 ? 12 : 16));
        const std::uint32_t file_flags = read_u32(data.data() +
            entry + (version <= 17 ? 16 : 20));
        const std::wstring file = filename_at(data, layout.strings_offset,
            filename_offset, filename_chars);
        if (file.empty()) continue;
        if (emitted) out << ",";
        ++emitted;
        out << "{\"path\":" << art::json_escape_w(file)
            << ",\"trace_chain\":" << trace_index
            << ",\"blocks\":" << blocks
            << ",\"flags\":" << file_flags;
        if (version >= 23) {
            const std::uint64_t reference = read_u64(data.data() + entry + 24);
            out << ",\"file_reference\":\"0x" << std::hex << reference << std::dec << "\"";
        }
        out << "}";
        if (emitted >= 2000) break;
    }
    out << "]}";
    return out.str();
}

bool parse_pf(const std::vector<std::uint8_t>& data, ArtifactRow& row,
              std::wstring& error) {
    if (data.size() < 0x54 || std::memcmp(data.data() + 4, "SCCA", 4) != 0) {
        error = L"not an SCCA file";
        return false;
    }
    const std::uint32_t version = read_u32(data.data());
    FileInfoLayout layout;
    if (!parse_layout(version, data, layout)) {
        error = L"unsupported prefetch version";
        return false;
    }
    const std::wstring executable = utf16_string(data.data() + 16, 60, 30);
    const std::uint32_t hash = read_u32(data.data() + 0x4C);
    const std::uint32_t flags = read_u32(data.data() + 0x50);
    row.data = build_row_data(data, version, layout, executable, hash, flags);
    row.name = art::to_utf8(executable.empty() ? L"unknown" : executable);
    std::uint64_t last_run = 0;
    for (std::uint32_t i = 0; i < layout.last_run_count; ++i) {
        const std::uint64_t pos = layout.last_run_offset + i * 8ull;
        if (pos + 8 > data.size()) break;
        const std::uint64_t ft = read_u64(data.data() + pos);
        if (ft != 0) { last_run = ft; break; }
    }
    row.timestamp_utc = art::filetime_iso(last_run);
    return true;
}

} // namespace

int collect_prefetch_file(const std::wstring& path, const ArtifactSink& sink,
                          std::wstring& error) {
    std::vector<std::uint8_t> raw;
    if (!read_file(path, raw, error)) return -1;
    std::vector<std::uint8_t> data;
    if (raw.size() >= 4 && std::memcmp(raw.data(), "MAM", 3) == 0) {
        if (!decompress_mam(raw, data, error)) return -1;
    } else {
        data = std::move(raw);
    }
    ArtifactRow row;
    if (!parse_pf(data, row, error)) return -1;
    row.type = "prefetch";
    row.source = art::to_utf8(path);
    row.path = art::to_utf8(path);
    row.size = data.size();
    row.key = row.name;
    sink(row);
    return 1;
}

int collect_prefetch_directory(const std::wstring& directory,
                               const ArtifactSink& sink, std::wstring& error) {
    int rows = 0;
    std::vector<std::wstring> pending{directory};
    while (!pending.empty()) {
        const std::wstring current = pending.back();
        pending.pop_back();
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW(art::join_path(current, L"*").c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(entry.cFileName, L".") == 0 ||
                wcscmp(entry.cFileName, L"..") == 0) continue;
            const std::wstring path = art::join_path(current, entry.cFileName);
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                    pending.push_back(path);
                continue;
            }
            std::wstring lower = entry.cFileName;
            for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
            if (lower.size() < 3 || lower.substr(lower.size() - 3) != L".pf") continue;
            std::wstring file_error;
            if (collect_prefetch_file(path, sink, file_error) > 0) ++rows;
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    return rows;
}

} // namespace deep
