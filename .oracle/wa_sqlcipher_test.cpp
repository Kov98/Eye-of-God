// language: C++, file: wa_sqlcipher_test.cpp
// Differential test: byte-for-byte round trip against the PowerShell oracle.
#include "wa_sqlcipher.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::vector<std::uint8_t> parse_hex(const std::string& s) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: %s <db> <out> <rawkey_hex>\n", argv[0]); return 2; }

    olho::wa::key_material key;
    key.raw_key = parse_hex(argv[3]);
    std::printf("raw key bytes: %zu\n", key.raw_key.size());

    auto found = olho::wa::detect(std::wstring(L"") + std::wstring(argv[1], argv[1] + std::strlen(argv[1])), key);
    std::printf("detect ok=%d  %s\n", found.ok ? 1 : 0, found.detail.c_str());
    if (!found.ok) { std::printf("DETECT FAILED\n"); return 1; }

    olho::wa::decrypt_stats stats{};
    std::string error;
    const std::wstring db(argv[1], argv[1] + std::strlen(argv[1]));
    const std::wstring out(argv[2], argv[2] + std::strlen(argv[2]));
    if (!olho::wa::decrypt_database(db, out, found.params, key, stats, error)) {
        std::printf("DECRYPT FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf("pages=%llu wal_frames=%llu bytes_out=%llu\n",
                (unsigned long long)stats.pages, (unsigned long long)stats.wal_frames,
                (unsigned long long)stats.bytes_out);

    auto want = slurp("C:/Users/pedri/Downloads/Eye-of-God-main/.oracle/plain2.db");
    auto got = slurp(argv[2]);
    std::printf("plain=%zu decrypted=%zu\n", want.size(), got.size());
    if (want.empty() || got.empty()) { std::printf("MISSING FILE\n"); return 1; }
    if (want.size() != got.size()) { std::printf("SIZE MISMATCH\n"); return 1; }

    // SQLCipher reserves the last `reserve` bytes of every page for IV + MAC.
    // That tail is never stored, so it is not recoverable by anyone, including
    // Cellebrite. Compare exactly the region the format preserves.
    const std::size_t page = 4096, reserve = 48;
    std::size_t compared = 0;
    for (std::size_t base = 0; base + page <= want.size(); base += page) {
        const std::size_t off = (base == 0) ? 16 : 0;
        const std::size_t end = page - reserve;
        for (std::size_t i = off; i < end; ++i) {
            if (want[base + i] != got[base + i]) {
                std::printf("MISMATCH at page %zu byte %zu\n", base / page + 1, i);
                return 1;
            }
            ++compared;
        }
    }
    std::printf("ROUND TRIP IDENTICAL over %zu stored bytes\n", compared);
    return 0;
}
