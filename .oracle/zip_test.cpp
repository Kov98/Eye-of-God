// language: C++, file: .oracle/zip_test.cpp
#include "social/zip_reader.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}
static std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static void write_all(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}
static std::wstring widen(const std::string& value) { return std::wstring(value.begin(), value.end()); }
static void put16(std::string& out, unsigned v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
static void put32(std::string& out, unsigned long v) {
    put16(out, v & 0xFFFF);
    put16(out, (v >> 16) & 0xFFFF);
}

// ZIP minimo com uma entrada STORED (metodo 0). O Compress-Archive do .NET nao
// produz stored de verdade (NoCompression ainda usa deflate), entao o caso
// metodo 0 precisa de um fixture montado aqui.
static void build_stored_zip(const std::string& path, const std::string& name,
                             const std::string& content) {
    std::string local;
    put32(local, 0x04034b50u);
    put16(local, 20); put16(local, 0); put16(local, 0); put16(local, 0); put16(local, 0);
    put32(local, olho::social::crc32_of(content.data(), content.size()));
    put32(local, static_cast<unsigned long>(content.size()));
    put32(local, static_cast<unsigned long>(content.size()));
    put16(local, static_cast<unsigned>(name.size()));
    put16(local, 0);
    local += name;
    local += content;

    std::string central;
    put32(central, 0x02014b50u);
    put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
    put16(central, 0); put16(central, 0);
    put32(central, olho::social::crc32_of(content.data(), content.size()));
    put32(central, static_cast<unsigned long>(content.size()));
    put32(central, static_cast<unsigned long>(content.size()));
    put16(central, static_cast<unsigned>(name.size()));
    put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0);
    put32(central, 0); put32(central, 0);
    central += name;

    std::string eocd;
    put32(eocd, 0x06054b50u);
    put16(eocd, 0); put16(eocd, 0); put16(eocd, 1); put16(eocd, 1);
    put32(eocd, static_cast<unsigned long>(central.size()));
    put32(eocd, static_cast<unsigned long>(local.size()));
    put16(eocd, 0);
    write_all(path, local + central + eocd);
}

int main() {
    {
        const char* text = "123456789";
        check("crc32 vetor 0xCBF43926", olho::social::crc32_of(text, 9) == 0xCBF43926u);
    }
    {
        std::string reason;
        check("recusa pai", !olho::social::zip_path_is_safe("../etc/passwd", reason));
        check("recusa pai no meio", !olho::social::zip_path_is_safe("a/../../b", reason));
        check("recusa absoluto", !olho::social::zip_path_is_safe("/etc/passwd", reason));
        check("recusa unidade", !olho::social::zip_path_is_safe("C:/x", reason));
        check("recusa controle", !olho::social::zip_path_is_safe(std::string("a\1b"), reason));
        check("recusa pai com barra invertida",
              !olho::social::zip_path_is_safe("..\\..\\evil.txt", reason));
        check("aceita caminho normal",
              olho::social::zip_path_is_safe("messages/inbox/abc/message_1.json", reason));
    }
    {
        olho::social::zip_archive archive;
        std::string error;
        check("abre zip deflate", archive.open(widen(".oracle\\zip_fixture.zip"), error));
        if (!error.empty()) std::printf("    erro: %s\n", error.c_str());
        int files = 0;
        bool has_big = false, has_json = false, has_small = false, has_accent = false;
        for (const auto& entry : archive.entries()) {
            if (!entry.name.empty() && entry.name.back() == '/') continue;
            ++files;
            if (entry.name.find("big.txt") != std::string::npos) has_big = true;
            if (entry.name.find("message_1.json") != std::string::npos) has_json = true;
            if (entry.name.find("small.txt") != std::string::npos) has_small = true;
            if (entry.name.find("acentuacao.txt") != std::string::npos) has_accent = true;
        }
        check("quatro arquivos no indice", files == 4);
        check("todos os nomes presentes", has_big && has_json && has_small && has_accent);

        std::string extracted;
        bool all_equal = true;
        for (const auto& entry : archive.entries()) {
            if (!entry.name.empty() && entry.name.back() == '/') continue;
            if (!archive.extract(entry, extracted, error)) {
                std::printf("    falha em %s: %s\n", entry.name.c_str(), error.c_str());
                all_equal = false;
                continue;
            }
            const std::string expected = read_all(".oracle\\zip_src\\" + entry.name);
            if (extracted != expected) {
                std::printf("    conteudo difere em %s\n", entry.name.c_str());
                all_equal = false;
            }
        }
        check("conteudo identico ao original", all_equal);
        bool separators_canonical = true;
        bool files_deflated = true;
        for (const auto& entry : archive.entries()) {
            if (entry.name.find('\\') != std::string::npos) separators_canonical = false;
            if (!entry.name.empty() && entry.name.back() != '/' && entry.method != 8) {
                files_deflated = false;
            }
        }
        check("separador canonico em todo nome", separators_canonical);
        check("arquivos usam metodo 8", files_deflated);
    }
    {
        const std::string name = "stored.txt";
        const std::string content = "conteudo armazenado sem compressao";
        build_stored_zip(".oracle\\zip_stored_manual.zip", name, content);
        olho::social::zip_archive archive;
        std::string error;
        check("abre zip stored montado a mao", archive.open(widen(".oracle\\zip_stored_manual.zip"), error));
        check("uma entrada", archive.entries().size() == 1);
        if (archive.entries().size() == 1) {
            check("metodo 0", archive.entries()[0].method == 0);
            std::string extracted;
            check("extrai stored", archive.extract(archive.entries()[0], extracted, error));
            check("conteudo stored confere", extracted == content);
        }
    }
    {
        write_all(".oracle\\not_a_zip.bin", "isto nao e um zip, e apenas texto para o teste");
        olho::social::zip_archive archive;
        std::string error;
        check("recusa arquivo que nao e ZIP", !archive.open(widen(".oracle\\not_a_zip.bin"), error));
        check("motivo claro", error.find("ZIP") != std::string::npos);
    }
    {
        // Corrompe um byte dentro dos dados comprimidos da maior entrada.
        olho::social::zip_archive archive;
        std::string error;
        if (!archive.open(widen(".oracle\\zip_fixture.zip"), error)) {
            check("abre fixture para corromper", false);
        } else {
            std::size_t target = 0;
            std::uint64_t best = 0;
            for (const auto& entry : archive.entries()) {
                if (entry.compressed_size > best) { best = entry.compressed_size; target = entry.local_offset; }
            }
            // cabecalho local (30) + nome + extra: le o nome real do arquivo
            std::string bytes = read_all(".oracle\\zip_fixture.zip");
            std::uint16_t name_len = 0, extra_len = 0;
            std::memcpy(&name_len, bytes.data() + target + 26, 2);
            std::memcpy(&extra_len, bytes.data() + target + 28, 2);
            const std::size_t data_at = static_cast<std::size_t>(target) + 30 + name_len + extra_len;
            bytes[data_at + best / 2] = static_cast<char>(bytes[data_at + best / 2] ^ 0x5A);
            write_all(".oracle\\zip_corrupt.zip", bytes);
            olho::social::zip_archive broken;
            bool rejected = false;
            std::string ignored;
            if (broken.open(widen(".oracle\\zip_corrupt.zip"), ignored)) {
                std::string extracted;
                for (const auto& entry : broken.entries()) {
                    if (!broken.extract(entry, extracted, ignored)) { rejected = true; break; }
                }
            } else {
                rejected = true;
            }
            check("corrupcao detectada (CRC ou deflate)", rejected);
        }
    }
    std::printf(failures == 0 ? "ZIP OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
