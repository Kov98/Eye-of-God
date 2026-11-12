// language: C++, file: mem_acq.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "mem_acq.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <cstring>
#include <set>

namespace olho::mem {
namespace {

constexpr std::size_t kChunk = 4u * 1024u * 1024u;   // read in 4 MiB slices
constexpr std::size_t kMaxCarve = 64u * 1024u * 1024u;

// Uma leitura grande pode falhar por inteiro quando UMA pagina do intervalo nao
// e legivel (ERROR_PARTIAL_COPY) - e numa maquina carregada isso acontece. Aqui
// a falha degrada para pedacos de 64 KiB: o que nao da para ler vira zero, e o
// mapa de deslocamentos continua valendo, entao o endereco reportado e o real.
// Devolve quantos bytes foram REALMENTE lidos (0 = nada legivel). O que nao pode
// ser lido fica zero no buffer, para os deslocamentos continuarem validos.
std::size_t read_resilient(HANDLE process, std::uint64_t base, std::size_t size, bytes& out) {
    out.assign(size, 0);
    SIZE_T got = 0;
    if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base), out.data(), size, &got)) {
        if (got == size) return got;
    }
    constexpr std::size_t kPiece = 64u * 1024u;
    std::size_t any = 0;
    for (std::size_t offset = 0; offset < size; offset += kPiece) {
        const std::size_t want = std::min(kPiece, size - offset);
        SIZE_T piece_got = 0;
        if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base + offset),
                              out.data() + offset, want, &piece_got) &&
            piece_got > 0) {
            any += piece_got;
        }
    }
    return any;
}


// A container signature plus where it starts and how the end is recognised.
struct signature {
    const char* kind;
    bytes head;
};

const std::vector<signature>& signatures() {
    static const std::vector<signature> table = {
        {"jpeg", {0xFF, 0xD8, 0xFF}},
        {"png", {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}},
        {"gif", {'G', 'I', 'F', '8'}},
        {"mp4", {'f', 't', 'y', 'p'}},   // matched at offset 4 below, not 0
    };
    return table;
}

bool starts_with(const bytes& buffer, std::size_t at, const bytes& head) {
    if (at + head.size() > buffer.size()) return false;
    return std::memcmp(buffer.data() + at, head.data(), head.size()) == 0;
}

// Finds the end of a JPEG by its EOI marker.
std::size_t jpeg_end(const bytes& buffer, std::size_t start) {
    for (std::size_t i = start + 2; i + 1 < buffer.size(); ++i) {
        if (buffer[i] == 0xFF && buffer[i + 1] == 0xD9) return i + 2 - start;
    }
    return 0;
}

// A marker byte that must follow FF D8 FF for the hit to be a real JPEG start:
// APPn, DQT, SOFn or DHT. Random heap rarely produces this and then a plausible
// EOI; requiring it removes the bulk of the false positives.
// PNG is a chunk list ending in IEND. Walking the chunks gives an exact length
// instead of a truncated guess.
std::size_t png_end(const bytes& buffer, std::size_t start) {
    std::size_t pos = start + 8;
    while (pos + 8 <= buffer.size()) {
        const std::uint32_t len = (static_cast<std::uint32_t>(buffer[pos]) << 24) |
                                  (static_cast<std::uint32_t>(buffer[pos + 1]) << 16) |
                                  (static_cast<std::uint32_t>(buffer[pos + 2]) << 8) |
                                  static_cast<std::uint32_t>(buffer[pos + 3]);
        const char* type = reinterpret_cast<const char*>(buffer.data() + pos + 4);
        if (len > 16u * 1024u * 1024u) return 0;                 // absurd chunk
        if (pos + 12 + len > buffer.size()) return 0;            // chunk runs off
        pos += 12 + len;
        if (std::memcmp(type, "IEND", 4) == 0) return pos - start;
    }
    return 0;
}

// GIF ends with the trailer byte and the block structure is a union of shapes; the
// trailer is the only reliable terminator.
std::size_t gif_end(const bytes& buffer, std::size_t start) {
    for (std::size_t i = start + 6; i < buffer.size(); ++i) {
        if (buffer[i] == 0x3B && i - start > 32) return i + 1 - start;
    }
    return 0;
}

bool plausible_jpeg_head(const bytes& buffer, std::size_t at) {
    if (at + 3 >= buffer.size()) return false;
    const std::uint8_t marker = buffer[at + 3];
    if (marker >= 0xE0 && marker <= 0xEF) return true;   // APPn
    if (marker == 0xDB || marker == 0xC4) return true;   // DQT / DHT
    if (marker >= 0xC0 && marker <= 0xCF) return true;   // SOFn
    return false;
}

// Dedupe key: the same image is read many times across chunks and regions, and a
// report that lists it four thousand times is a report nobody can use.
struct seen_key {
    std::size_t size;
    std::uint32_t head_crc;
    bool operator<(const seen_key& o) const {
        return size < o.size || (size == o.size && head_crc < o.head_crc);
    }
};

void carve_from(const bytes& buffer, std::uint64_t base_address,
                std::set<seen_key>& seen, std::vector<carve_result>& out) {
    for (std::size_t i = 0; i + 8 < buffer.size(); ++i) {
        for (const auto& sig : signatures()) {
            const bool is_mp4 = (std::strcmp(sig.kind, "mp4") == 0);
            const std::size_t at = is_mp4 ? i : i;
            if (is_mp4) {
                // ISO-BMFF: size then "ftyp" at offset 4.
                if (i + 12 >= buffer.size()) break;
                if (std::memcmp(buffer.data() + i + 4, "ftyp", 4) != 0) continue;
                continue;   // structure only; payload extraction handled elsewhere
            }
            if (!starts_with(buffer, at, sig.head)) continue;

            carve_result result;
            result.kind = sig.kind;
            result.address = base_address + at;
            if (std::strcmp(sig.kind, "jpeg") == 0) {
                if (!plausible_jpeg_head(buffer, at)) continue;
                const std::size_t len = jpeg_end(buffer, at);
                // A real photo is bounded; an unbounded match is a coincidence in
                // the heap, not an image.
                if (len < 128 || len > 16u * 1024u * 1024u) continue;
                result.data.assign(buffer.begin() + static_cast<std::ptrdiff_t>(at),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(at + len));
            } else if (std::strcmp(sig.kind, "png") == 0) {
                const std::size_t len = png_end(buffer, at);
                if (len < 128) continue;
                result.data.assign(buffer.begin() + static_cast<std::ptrdiff_t>(at),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(at + len));
            } else if (std::strcmp(sig.kind, "gif") == 0) {
                const std::size_t len = gif_end(buffer, at);
                if (len < 128) continue;
                result.data.assign(buffer.begin() + static_cast<std::ptrdiff_t>(at),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(at + len));
            } else {
                continue;
            }
            std::uint32_t crc = 2166136261u;
            for (std::size_t k = 0; k < std::min<std::size_t>(64, result.data.size()); ++k) {
                crc = (crc ^ result.data[k]) * 16777619u;
            }
            const seen_key key{result.data.size(), crc};
            if (!seen.insert(key).second) break;   // already reported

            result.ok = true;
            out.push_back(std::move(result));
            break;
        }
    }
}

}  // namespace

std::vector<process_entry> list_processes() {
    std::vector<process_entry> out;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            process_entry item;
            item.pid = entry.th32ProcessID;
            const int n = WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                std::string name(static_cast<std::size_t>(n), '\0');
                WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, &name[0], n, nullptr, nullptr);
                while (!name.empty() && name.back() == '\0') name.pop_back();
                item.name = name;
            }
            item.working_set = entry.cntThreads;   // overwritten below when readable
            out.push_back(std::move(item));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return out;
}

bool acquire(std::uint32_t pid, std::uint64_t max_bytes, region_stats& stats,
             std::vector<carve_result>& found, std::string& error) {
    stats = region_stats{};
    found.clear();

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        error = "OpenProcess falhou (" + std::to_string(GetLastError()) +
                "); sem PROCESS_VM_READ nao ha leitura";
        return false;
    }

    bytes buffer;
    std::set<seen_key> seen;
    for (std::uint64_t address = 0;;) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) break;
        const std::uint64_t base = reinterpret_cast<std::uint64_t>(info.BaseAddress);
        const std::uint64_t size = info.RegionSize;
        address = base + size;
        ++stats.regions_seen;

        const bool readable = (info.State == MEM_COMMIT) &&
                              (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                               PAGE_EXECUTE_WRITECOPY)) &&
                              !(info.Protect & PAGE_GUARD) && !(info.Protect & PAGE_NOACCESS);
        if (!readable || size == 0) continue;

        for (std::uint64_t offset = 0; offset < size; offset += kChunk) {
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, size - offset));
            // Uma leitura negada custa UM pedaco, nao a regiao inteira: antes,
            // uma unica pagina ilegivel abandonava o resto (as vezes centenas de
            // MiB) e o carving reportava "nada encontrado" com conviccao.
            const std::size_t got = read_resilient(process, base + offset, want, buffer);
            if (got == 0) {
                ++stats.regions_denied;
                continue;
            }
            ++stats.regions_read;
            stats.bytes_read += got;
            carve_from(buffer, base + offset, seen, found);
            if (max_bytes != 0 && stats.bytes_read >= max_bytes) {
                CloseHandle(process);
                return true;
            }
        }
    }
    CloseHandle(process);
    return true;
}

bool find_bytes(std::uint32_t pid, const bytes& needle, std::size_t window,
                std::uint64_t& address, bytes& hit, std::string& error) {
    address = 0;
    hit.clear();
    if (needle.empty()) { error = "padrao vazio"; return false; }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        error = "OpenProcess falhou (" + std::to_string(GetLastError()) + ")";
        return false;
    }

    bytes buffer;
    // O padrao pode cair exatamente na fronteira entre dois pedacos. Guardar o
    // rabo do anterior e o que impede um casamento de sumir por 1 byte de azar.
    bytes carry;
    for (std::uint64_t at = 0;;) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(at), &info, sizeof(info)) == 0) break;
        const std::uint64_t base = reinterpret_cast<std::uint64_t>(info.BaseAddress);
        const std::uint64_t size = info.RegionSize;
        at = base + size;
        if (info.State != MEM_COMMIT) continue;
        if (!(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) continue;

        carry.clear();
        for (std::uint64_t offset = 0; offset < size; offset += kChunk) {
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, size - offset));
            if (read_resilient(process, base + offset, want, buffer) == 0) {
                carry.clear();
                continue;
            }
            bytes scan;
            scan.reserve(carry.size() + buffer.size());
            scan.insert(scan.end(), carry.begin(), carry.end());
            scan.insert(scan.end(), buffer.begin(), buffer.end());
            const auto it = std::search(scan.begin(), scan.end(), needle.begin(), needle.end());
            if (it != scan.end()) {
                const std::size_t pos = static_cast<std::size_t>(it - scan.begin());
                const std::size_t start = (pos > window) ? pos - window : 0;
                const std::size_t end = std::min(scan.size(), pos + needle.size() + window);
                hit.assign(scan.begin() + static_cast<std::ptrdiff_t>(start),
                           scan.begin() + static_cast<std::ptrdiff_t>(end));
                // pos inclui o carry; o endereco absoluto e o do inicio do pedaco
                // menos o que veio do pedaco anterior.
                address = base + offset + pos - carry.size();
                CloseHandle(process);
                return true;
            }
            if (!needle.empty()) {
                const std::size_t keep = std::min(needle.size() - 1, buffer.size());
                carry.assign(buffer.end() - static_cast<std::ptrdiff_t>(keep), buffer.end());
            }
        }
    }
    CloseHandle(process);
    error = "padrao nao encontrado";
    return false;
}

}  // namespace olho::mem
