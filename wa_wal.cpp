// language: C++, file: wa_wal.cpp
#include "wa_wal.hpp"

#include <windows.h>

#include <cstring>
#include <vector>

namespace olho::wa {
namespace {

std::uint32_t be32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

bool read_file(const std::wstring& path, std::vector<unsigned char>& out, std::string& error) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir o arquivo";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        error = "nao foi possivel medir o arquivo";
        return false;
    }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(out.size() - done);
        if (!ReadFile(file, out.data() + done, want, &got, nullptr) || got == 0) {
            CloseHandle(file);
            error = "falha de leitura";
            return false;
        }
        done += got;
    }
    CloseHandle(file);
    return true;
}

bool write_file(const std::wstring& path, const std::vector<unsigned char>& data,
                std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel criar o arquivo de saida";
        return false;
    }
    std::size_t done = 0;
    while (done < data.size()) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(data.size() - done);
        if (!WriteFile(file, data.data() + done, chunk, &wrote, nullptr) || wrote == 0) {
            CloseHandle(file);
            error = "falha de escrita";
            return false;
        }
        done += wrote;
    }
    CloseHandle(file);
    return true;
}

// Checksum do WAL (especificacao do formato): soma de 32 bits sobre pares de
// palavras, dependendo do byte de magic (0x...82 e little-endian, 0x...83 e
// big-endian). Recebe o valor anterior e devolve o novo.
std::uint64_t wal_checksum(bool big_endian, const unsigned char* data, std::size_t length,
                           std::uint32_t s1, std::uint32_t s2) {
    std::uint32_t a = s1;
    std::uint32_t b = s2;
    const std::size_t words = length / 4;
    for (std::size_t i = 0; i < words; i += 2) {
        const unsigned char* p0 = data + i * 4;
        const unsigned char* p1 = data + (i + 1) * 4;
        const auto read32 = [&](const unsigned char* p) {
            return big_endian ? be32(p)
                              : (static_cast<std::uint32_t>(p[0]) |
                                 (static_cast<std::uint32_t>(p[1]) << 8) |
                                 (static_cast<std::uint32_t>(p[2]) << 16) |
                                 (static_cast<std::uint32_t>(p[3]) << 24));
        };
        a += read32(p0) + b;
        b += read32(p1) + a;
    }
    return (static_cast<std::uint64_t>(b) << 32) | a;
}

}  // namespace


bool wal_scan(const std::vector<unsigned char>& wal, std::vector<wal_frame_info>& frames,
              wal_stats& stats) {
    frames.clear();
    if (wal.size() < 32) {
        stats.error = "wal menor que o cabecalho";
        return false;
    }
    const std::uint32_t magic = be32(wal.data());
    if (magic != 0x377f0682u && magic != 0x377f0683u) {
        stats.error = "magic do WAL desconhecido";
        return false;
    }
    const bool big_endian = (magic & 1u) != 0;
    const std::uint32_t page_size = be32(wal.data() + 8);
    if (page_size < 512 || page_size > 65536 || (page_size & (page_size - 1)) != 0) {
        stats.error = "tamanho de pagina invalido no WAL";
        return false;
    }
    stats.page_size = page_size;
    const std::uint32_t salt1 = be32(wal.data() + 16);
    const std::uint32_t salt2 = be32(wal.data() + 20);
    std::uint32_t sum1 = 0;
    std::uint32_t sum2 = 0;
    {
        const std::uint64_t header_sum = wal_checksum(big_endian, wal.data(), 24, 0, 0);
        sum1 = static_cast<std::uint32_t>(header_sum & 0xFFFFFFFFu);
        sum2 = static_cast<std::uint32_t>(header_sum >> 32);
    }
    const std::size_t frame_size = 24 + page_size;
    std::size_t offset = 32;
    while (offset + frame_size <= wal.size()) {
        const unsigned char* frame = wal.data() + offset;
        const std::uint32_t pgno = be32(frame);
        const std::uint32_t db_size = be32(frame + 4);
        const std::uint32_t frame_salt1 = be32(frame + 8);
        const std::uint32_t frame_salt2 = be32(frame + 12);
        const std::uint32_t frame_sum1 = be32(frame + 16);
        const std::uint32_t frame_sum2 = be32(frame + 20);
        ++stats.frames;
        if (frame_salt1 != salt1 || frame_salt2 != salt2 || pgno == 0) {
            ++stats.discarded;
            break;   // salt diferente encerra o WAL
        }
        const std::uint64_t header_sum = wal_checksum(big_endian, frame, 8, sum1, sum2);
        const std::uint64_t full_sum =
            wal_checksum(big_endian, frame + 24, page_size,
                         static_cast<std::uint32_t>(header_sum & 0xFFFFFFFFu),
                         static_cast<std::uint32_t>(header_sum >> 32));
        if (static_cast<std::uint32_t>(full_sum & 0xFFFFFFFFu) != frame_sum1 ||
            static_cast<std::uint32_t>(full_sum >> 32) != frame_sum2) {
            ++stats.discarded;
            break;   // checksum invalido: daqui para frente nao e confiavel
        }
        sum1 = static_cast<std::uint32_t>(full_sum & 0xFFFFFFFFu);
        sum2 = static_cast<std::uint32_t>(full_sum >> 32);
        wal_frame_info info;
        info.pgno = pgno;
        info.db_size = db_size;
        info.offset = offset;
        frames.push_back(info);
        offset += frame_size;
    }
    // Marca quem veio depois do ultimo commit. Para o SQLite esses frames nao
    // existem; para a pericia podem ser a ultima escrita do aparelho.
    std::size_t last_commit = frames.size();
    for (std::size_t i = frames.size(); i > 0; --i) {
        if (frames[i - 1].db_size != 0) {
            last_commit = i;
            break;
        }
    }
    for (std::size_t i = last_commit; i < frames.size(); ++i) frames[i].after_last_commit = true;
    return true;
}

bool wal_apply(const std::wstring& db_path, const std::wstring& wal_path,
               const std::wstring& out_path, wal_stats& stats) {
    stats = wal_stats{};
    std::vector<unsigned char> db;
    std::vector<unsigned char> wal;
    std::string error;
    if (!read_file(db_path, db, error)) {
        stats.error = "db: " + error;
        return false;
    }
    if (!read_file(wal_path, wal, error)) {
        stats.error = "wal: " + error;
        return false;
    }
    std::vector<wal_frame_info> frames;
    if (!wal_scan(wal, frames, stats)) return false;
    const std::uint32_t page_size = stats.page_size;

    // O banco reconstruido comeca como o arquivo original; frames commitados
    // sobrescrevem as paginas correspondentes.
    std::vector<unsigned char> image = db;
    const auto ensure_pages = [&](std::uint64_t pages) {
        const std::size_t needed = static_cast<std::size_t>(pages) * page_size;
        if (image.size() < needed) image.resize(needed, 0);
    };

    std::uint64_t db_pages_after = 0;
    // Aplica o prefixo commitado. Numero de pagina do SQLite comeca em 1: a
    // pagina N fica no deslocamento (N-1)*page_size; errar isso desloca o banco
    // inteiro e o resultado deixa de abrir.
    for (const wal_frame_info& frame : frames) {
        if (frame.after_last_commit) {
            ++stats.discarded;
            continue;
        }
        ensure_pages(std::max<std::uint64_t>(frame.pgno, frame.db_size));
        std::memcpy(image.data() + static_cast<std::size_t>(frame.pgno - 1) * page_size,
                    wal.data() + frame.offset + 24, page_size);
        ++stats.committed;
        if (frame.db_size != 0) {
            db_pages_after = frame.db_size;
            stats.db_pages_after = frame.db_size;
        }
    }

    if (stats.committed == 0) {
        stats.error = "nenhum frame commitado no WAL";
        return false;
    }
    // O tamanho final e o declarado pelo ultimo commit.
    if (db_pages_after > 0) {
        image.resize(static_cast<std::size_t>(db_pages_after) * page_size);
    }
    if (!write_file(out_path, image, error)) {
        stats.error = error;
        return false;
    }
    stats.ok = true;
    return true;
}

}  // namespace olho::wa
