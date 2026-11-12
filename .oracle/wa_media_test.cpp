// language: C++, file: .oracle/wa_media_test.cpp
// Discovery + decryption round trip against fixtures encrypted by .NET AES.
#include "wa_media.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> slurp(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(n));
    if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    std::fclose(f);
    return out;
}

std::string base_name(const std::string& p) {
    const std::size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

}  // namespace

int main(int argc, char** argv) {
    // O diretorio das fixtures vem do runner: um caminho absoluto gravado
    // dentro do .cpp so funciona na maquina de quem escreveu o teste.
    const std::string dir = argc > 1 ? std::string(argv[1]) : std::string(".oracle");
    const std::string oracle = dir + "/";
    const std::string db_narrow = oracle + "mediadb.db";
    const std::wstring db(db_narrow.begin(), db_narrow.end());

    auto found = olho::wa::discover_media(db);
    std::printf("discover ok=%d  %s\n", found.ok ? 1 : 0, found.detail.c_str());
    for (const auto& t : found.scanned_tables) std::printf("  tabela: %s\n", t.c_str());
    if (!found.ok || found.rows.empty()) { std::printf("NO ROWS\n"); return 1; }

    // Midia efemera: uma linha marcada e a tabela dedicada presente.
    std::printf("  view_once_rows=%d view_once_table=%d\n", found.view_once_rows,
                found.view_once_table ? 1 : 0);
    if (found.view_once_rows != 1 || !found.view_once_table) {
        std::printf("VIEW_ONCE FAIL\n");
        return 1;
    }

    int index = 0, failures = 0;
    for (const auto& row : found.rows) {
        ++index;
        const std::string enc = oracle + base_name(row.path_hint);
        const std::string out = oracle + "recovered_media" + std::to_string(index) + ".jpg";
        const std::wstring wenc(enc.begin(), enc.end());
        const std::wstring wout(out.begin(), out.end());

        auto r = olho::wa::decrypt_media(wenc, wout, row.key);
        std::printf("  [%d] %s -> ok=%d type=%s scheme=%s (%s)\n", index, base_name(enc).c_str(),
                    r.ok ? 1 : 0, r.media_type.c_str(), r.scheme.c_str(), r.detail.c_str());
        if (!r.ok) { ++failures; continue; }

        auto want = slurp(oracle + "orig" + std::to_string(index) + ".jpg");
        auto got = slurp(out);
        if (want.empty() || got.size() != want.size() || std::memcmp(want.data(), got.data(), want.size()) != 0) {
            std::printf("      BYTES DIFFER (want=%zu got=%zu)\n", want.size(), got.size());
            ++failures;
            continue;
        }
        std::printf("      bytes identicos: %zu\n", want.size());
    }

    std::printf(failures == 0 ? "MEDIA OK\n" : "MEDIA FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
