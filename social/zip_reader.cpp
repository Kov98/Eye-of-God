// language: C++, file: social/zip_reader.cpp
#include "social/zip_reader.hpp"

#include "android_backup.hpp"   // olho::android::inflate_deflate (unico inflate do projeto)

#include <windows.h>

#include <array>
#include <cstring>

namespace olho::social {
namespace {

std::uint16_t le16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t le64(const unsigned char* p) {
    return static_cast<std::uint64_t>(le32(p)) |
           (static_cast<std::uint64_t>(le32(p + 4)) << 32);
}

constexpr std::uint32_t kEocdSignature = 0x06054b50u;
constexpr std::uint32_t kCentralSignature = 0x02014b50u;
constexpr std::uint32_t kLocalSignature = 0x04034b50u;
constexpr std::uint32_t kEocd64Signature = 0x06064b50u;
constexpr std::uint32_t kEocd64LocatorSignature = 0x07064b50u;

// Valores de 8 bytes de uma entrada ficam no campo extra 0x0001, na ordem
// tamanho, comprimido, deslocamento e disco - e so os que estouraram o campo de
// 32 bits aparecem, sempre nessa ordem.
bool zip64_extra(const unsigned char* extra, std::uint16_t length,
                 std::uint64_t& uncompressed, std::uint64_t& compressed,
                 std::uint64_t& offset) {
    std::uint16_t cursor = 0;
    while (cursor + 4 <= length) {
        const std::uint16_t id = le16(extra + cursor);
        const std::uint16_t size = le16(extra + cursor + 2);
        if (cursor + 4 + size > length) return false;
        if (id == 0x0001) {
            const unsigned char* p = extra + cursor + 4;
            std::uint16_t used = 0;
            if (uncompressed == 0xFFFFFFFFu && used + 8 <= size) {
                uncompressed = le64(p + used);
                used += 8;
            }
            if (compressed == 0xFFFFFFFFu && used + 8 <= size) {
                compressed = le64(p + used);
                used += 8;
            }
            if (offset == 0xFFFFFFFFu && used + 8 <= size) {
                offset = le64(p + used);
                used += 8;
            }
            return true;
        }
        cursor = static_cast<std::uint16_t>(cursor + 4 + size);
    }
    return false;
}

}  // namespace

std::uint32_t crc32_of(const void* data, std::size_t length) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            values[i] = c;
        }
        return values;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

bool zip_path_is_safe(const std::string& name, std::string& reason) {
    reason.clear();
    if (name.empty()) {
        reason = "nome vazio";
        return false;
    }
    if (name.front() == '/' || name.front() == '\\') {
        reason = "caminho absoluto";
        return false;
    }
    if (name.size() >= 2 && name[1] == ':') {
        reason = "letra de unidade";
        return false;
    }
    for (unsigned char c : name) {
        if (c < 0x20) {
            reason = "caractere de controle";
            return false;
        }
    }
    // Alguns produtores (o Compress-Archive do Windows, por exemplo) gravam a
    // barra invertida como separador. Tratar as duas evita que "..\\..\\x" passe
    // pela checagem so porque o componente pai nao esta separado por '/'.
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find('/', start);
        const std::size_t alternate = name.find('\\', start);
        if (alternate != std::string::npos && (end == std::string::npos || alternate < end)) {
            end = alternate;
        }
        const std::string part = name.substr(start, end == std::string::npos
                                                        ? std::string::npos
                                                        : end - start);
        if (part == "..") {
            reason = "componente pai (..)";
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool zip_archive::read_at(std::uint64_t offset, void* buffer, std::size_t length,
                          std::string& error) const {
    if (handle_ == nullptr) {
        error = "arquivo nao aberto";
        return false;
    }
    if (offset > file_size_ || length > file_size_ - offset) {
        error = "leitura alem do fim do arquivo";
        return false;
    }
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(static_cast<HANDLE>(handle_), position, nullptr, FILE_BEGIN)) {
        error = "nao foi possivel posicionar no arquivo";
        return false;
    }
    std::size_t done = 0;
    auto* target = static_cast<unsigned char*>(buffer);
    while (done < length) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(
            (length - done) > 0x10000000u ? 0x10000000u : (length - done));
        if (!ReadFile(static_cast<HANDLE>(handle_), target + done, want, &got, nullptr)) {
            error = "falha de leitura";
            return false;
        }
        if (got == 0) {
            error = "fim inesperado do arquivo";
            return false;
        }
        done += got;
    }
    return true;
}

void zip_archive::close() {
    if (handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    entries_.clear();
    file_size_ = 0;
}

bool zip_archive::open(const std::wstring& path, std::string& error) {
    close();
    error.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir o arquivo";
        return false;
    }
    handle_ = file;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        error = "arquivo vazio ou sem tamanho";
        close();
        return false;
    }
    file_size_ = static_cast<std::uint64_t>(size.QuadPart);
    if (file_size_ < 22) {
        error = "arquivo pequeno demais para ser ZIP";
        close();
        return false;
    }

    // O EOCD fica no fim, depois de um comentario de ate 65535 bytes.
    const std::uint64_t tail_length = file_size_ < 65557 ? file_size_ : 65557;
    std::vector<unsigned char> tail(static_cast<std::size_t>(tail_length));
    if (!read_at(file_size_ - tail_length, tail.data(), tail.size(), error)) {
        close();
        return false;
    }
    std::size_t eocd = static_cast<std::size_t>(-1);
    for (std::size_t i = tail.size() - 21; ; --i) {
        if (le32(tail.data() + i) == kEocdSignature) {
            eocd = i;
            break;
        }
        if (i == 0) break;
    }
    if (eocd == static_cast<std::size_t>(-1)) {
        error = "diretorio central nao encontrado (arquivo nao e ZIP?)";
        close();
        return false;
    }
    const unsigned char* e = tail.data() + eocd;
    std::uint64_t entries_total = le16(e + 10);
    std::uint64_t central_size = le32(e + 12);
    std::uint64_t central_offset = le32(e + 16);
    if (entries_total == 0xFFFFu || central_size == 0xFFFFFFFFu ||
        central_offset == 0xFFFFFFFFu) {
        // ZIP64: o EOCD de 32 bits saturou. O localizador fica imediatamente
        // antes do EOCD e aponta para o EOCD64, que carrega os campos de 8 bytes.
        const std::uint64_t eocd_absolute = file_size_ - tail_length + eocd;
        if (eocd_absolute < 20) {
            error = "ZIP64: localizador ausente";
            close();
            return false;
        }
        unsigned char locator[20] = {};
        if (!read_at(eocd_absolute - 20, locator, sizeof(locator), error)) {
            close();
            return false;
        }
        if (le32(locator) != kEocd64LocatorSignature) {
            error = "ZIP64: localizador nao encontrado";
            close();
            return false;
        }
        const std::uint64_t eocd64_offset = le64(locator + 8);
        unsigned char record[56] = {};
        if (!read_at(eocd64_offset, record, sizeof(record), error)) {
            close();
            return false;
        }
        if (le32(record) != kEocd64Signature) {
            error = "ZIP64: EOCD64 invalido";
            close();
            return false;
        }
        entries_total = le64(record + 32);
        central_size = le64(record + 40);
        central_offset = le64(record + 48);
    }
    if (central_size == 0 || entries_total == 0) {
        error = "ZIP sem entradas";
        close();
        return false;
    }
    std::vector<unsigned char> central(central_size);
    if (!read_at(central_offset, central.data(), central.size(), error)) {
        close();
        return false;
    }

    std::size_t cursor = 0;
    // uint64: com ZIP64 a contagem passa de 65535 e um contador de 16 bits
    // daria a volta, lendo o diretorio central outra vez ate estourar.
    for (std::uint64_t i = 0; i < entries_total; ++i) {
        if (cursor + 46 > central.size() || le32(central.data() + cursor) != kCentralSignature) {
            error = "diretorio central corrompido";
            close();
            return false;
        }
        const unsigned char* header = central.data() + cursor;
        zip_entry entry;
        entry.method = le16(header + 10);
        entry.crc32 = le32(header + 16);
        entry.compressed_size = le32(header + 20);
        entry.size = le32(header + 24);
        const std::uint16_t name_length = le16(header + 28);
        const std::uint16_t extra_length = le16(header + 30);
        const std::uint16_t comment_length = le16(header + 32);
        entry.local_offset = le32(header + 42);
        if (cursor + 46 + name_length > central.size()) {
            error = "nome de entrada fora do diretorio central";
            close();
            return false;
        }
        if (entry.compressed_size == 0xFFFFFFFFu || entry.size == 0xFFFFFFFFu ||
            entry.local_offset == 0xFFFFFFFFu) {
            zip64_extra(header + 46 + name_length, extra_length, entry.size,
                        entry.compressed_size, entry.local_offset);
            if (entry.compressed_size == 0xFFFFFFFFu || entry.size == 0xFFFFFFFFu ||
                entry.local_offset == 0xFFFFFFFFu) {
                error = "ZIP64: campo extra incompleto na entrada";
                close();
                return false;
            }
        }
        entry.name.assign(reinterpret_cast<const char*>(header + 46), name_length);
        // Separador canonico: o resto do codigo compara e grava com '/'.
        for (char& c : entry.name) {
            if (c == '\\') c = '/';
        }
        entries_.push_back(std::move(entry));
        cursor += 46u + name_length + extra_length + comment_length;
    }
    return true;
}

bool zip_archive::extract(const zip_entry& entry, std::string& out, std::string& error) const {
    out.clear();
    error.clear();
    std::string path_reason;
    if (!zip_path_is_safe(entry.name, path_reason)) {
        error = "caminho inseguro no ZIP: " + path_reason;
        return false;
    }
    unsigned char local[30] = {};
    if (!read_at(entry.local_offset, local, sizeof(local), error)) return false;
    if (le32(local) != kLocalSignature) {
        error = "cabecalho local invalido";
        return false;
    }
    const std::uint16_t name_length = le16(local + 26);
    const std::uint16_t extra_length = le16(local + 28);
    const std::uint64_t data_offset = entry.local_offset + 30u + name_length + extra_length;
    if (entry.compressed_size > (16ull << 20) * 64) {   // 1 GiB por entrada
        error = "entrada grande demais para leitura em memoria";
        return false;
    }
    std::vector<unsigned char> compressed(static_cast<std::size_t>(entry.compressed_size));
    if (!compressed.empty() &&
        !read_at(data_offset, compressed.data(), compressed.size(), error)) {
        return false;
    }

    if (entry.method == 0) {
        out.assign(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    } else if (entry.method == 8) {
        if (!olho::android::inflate_deflate(compressed.data(), compressed.size(), out, error)) {
            error = "deflate: " + error;
            return false;
        }
    } else {
        error = "metodo de compressao nao suportado: " + std::to_string(entry.method);
        return false;
    }

    if (out.size() != entry.size) {
        error = "tamanho descomprimido difere do diretorio central";
        return false;
    }
    const std::uint32_t crc = crc32_of(out.data(), out.size());
    if (crc != entry.crc32) {
        error = "CRC-32 nao confere";
        return false;
    }
    return true;
}

}  // namespace olho::social
