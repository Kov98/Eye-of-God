// language: C++, file: case_db_seal.cpp
#include "case_db_seal.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace deep {
namespace {

constexpr std::uint32_t kBlockSize = 4096;
constexpr char kMagic[8] = {'O', 'L', 'H', 'O', 'M', 'A', 'C', '1'};
constexpr std::size_t kTagSize = 32;

bool read_all(const std::wstring& path, std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir " + std::string(path.begin(), path.end());
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    out.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        if (!ReadFile(file, out.data() + done, static_cast<DWORD>(out.size() - done), &got,
                      nullptr) || got == 0) {
            CloseHandle(file);
            error = "falha de leitura";
            return false;
        }
        done += got;
    }
    CloseHandle(file);
    return true;
}

// ---- temporario + rename, compartilhado pelos tres selos -------------------
//
// A regra e sempre a mesma: escreve em <alvo>.tmp e publica com um rename. O
// alvo ou e o selo antigo inteiro ou o novo inteiro, nunca um pedaco.
std::wstring temp_path_for(const std::wstring& path) { return path + L".tmp"; }

HANDLE open_temp(const std::wstring& path, std::wstring& temp) {
    temp = temp_path_for(path);
    return CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool write_handle(HANDLE file, const std::uint8_t* data, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
        DWORD wrote = 0;
        if (!WriteFile(file, data + done, static_cast<DWORD>(size - done), &wrote, nullptr) ||
            wrote == 0) {
            return false;
        }
        done += wrote;
    }
    return true;
}

bool publish_temp(const std::wstring& temp, const std::wstring& final_path, std::string& error) {
    if (!MoveFileExW(temp.c_str(), final_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD code = GetLastError();
        // O temporario nao pode sobrar: ele nao e evidencia e nao e selo.
        DeleteFileW(temp.c_str());
        error = "nao foi possivel publicar o selo (rename falhou, erro " +
                std::to_string(code) + ")";
        return false;
    }
    return true;
}

// Grava em <path>.tmp e publica com MoveFileExW(REPLACE_EXISTING).
//
// POR QUE NAO ESCREVER DIRETO
//   Se o processo morrer no meio da gravacao, o arquivo fica pela metade e a
//   proxima verificacao le "selo truncado" - o conjunto parece corrompido por um
//   motivo que nao tem nada a ver com adulteracao. Com o rename, o destino ou e o
//   selo antigo inteiro ou o novo inteiro, nunca um pedaco.
//
//   Em Windows, MoveFileExW sobre um arquivo em uso falha com ERROR_ACCESS_DENIED
//   (por isso cai no DeleteFileW do temporario e reporta o motivo em vez de fingir
//   que selou).
bool write_all(const std::wstring& path, const std::vector<std::uint8_t>& data,
               std::string& error) {
    std::wstring temp;
    HANDLE file = open_temp(path, temp);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel criar o temporario do selo";
        return false;
    }
    if (!write_handle(file, data.data(), data.size())) {
        CloseHandle(file);
        DeleteFileW(temp.c_str());
        error = "falha ao gravar o temporario do selo";
        return false;
    }
    // Sem o flush, o rename pode publicar um arquivo cujo conteudo ainda esta no
    // cache: o selo existiria e nao conferiria depois de uma queda de energia.
    FlushFileBuffers(file);
    CloseHandle(file);
    return publish_temp(temp, path, error);
}

// HMAC-SHA256 via CNG. O projeto nao linka OpenSSL; este e o mesmo primitivo que
// a cadeia de custodia usa, aplicado bloco a bloco.
bool hmac_sha256(const std::uint8_t* key, std::size_t key_len, const std::uint8_t* data,
                 std::size_t data_len, std::uint8_t out[kTagSize]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) {
        return false;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, const_cast<PUCHAR>(key),
                               static_cast<ULONG>(key_len), 0) == 0;
    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(data_len), 0) == 0;
    if (ok) ok = BCryptFinishHash(hash, out, static_cast<ULONG>(kTagSize), 0) == 0;
    if (hash != nullptr) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// Chave do selo derivada da chave do caso: pega 32 bytes do keystream do CTR num
// deslocamento que nunca coincide com paginas reais. Assim a chave do banco nunca
// e exposta e o selo usa um segredo distinto do da cifra.
bool seal_key(const case_cipher& cipher, std::uint8_t key[kTagSize]) {
    std::memset(key, 0, kTagSize);
    constexpr std::uint64_t kDeriveOffset = 0xF0000000000ull;
    return cipher.apply(kDeriveOffset, key, kTagSize);
}

std::wstring seal_path(const std::wstring& db_path) { return db_path + L".seal"; }

}  // namespace

bool seal_database(const std::wstring& db_path, const case_cipher& cipher, seal_report& report,
                   const seal_progress_fn& progress) {
    report = seal_report{};
    report.block_size = kBlockSize;
    // O temporario precisa de nome: iteradores de um wstring ja destruido sao
    // ponteiro pendurado (o crash aparecia aqui).
    const std::wstring seal_file = seal_path(db_path);
    report.path = std::string(seal_file.begin(), seal_file.end());
    if (!cipher.ready()) {
        report.error = "cifra do caso nao esta carregada";
        return false;
    }
    std::uint8_t key[kTagSize] = {};
    if (!seal_key(cipher, key)) {
        report.error = "nao foi possivel derivar a chave do selo";
        return false;
    }
    // STREAMING, nao "le tudo para a memoria": um case.db de 8 GB nao cabe, e ler
    // inteiro antes de gravar o primeiro byte deixa o console mudo por minutos.
    HANDLE db = CreateFileW(db_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (db == INVALID_HANDLE_VALUE) {
        report.error = "nao foi possivel abrir " + std::string(db_path.begin(), db_path.end());
        return false;
    }
    LARGE_INTEGER size_info{};
    if (!GetFileSizeEx(db, &size_info) || size_info.QuadPart < 0) {
        CloseHandle(db);
        report.error = "tamanho do banco ilegivel";
        return false;
    }
    const std::uint64_t db_bytes = static_cast<std::uint64_t>(size_info.QuadPart);
    const std::uint64_t blocks = (db_bytes + kBlockSize - 1) / kBlockSize;

    std::wstring temp;
    HANDLE out = open_temp(seal_file, temp);
    if (out == INVALID_HANDLE_VALUE) {
        CloseHandle(db);
        report.error = "nao foi possivel criar o temporario do selo";
        return false;
    }
    const auto abandon = [&](const std::string& why) {
        CloseHandle(db);
        CloseHandle(out);
        DeleteFileW(temp.c_str());
        report.error = why;
        return false;
    };

    std::vector<std::uint8_t> header(kMagic, kMagic + sizeof(kMagic));
    for (int shift = 0; shift < 8; ++shift) {
        header.push_back(static_cast<std::uint8_t>((blocks >> (shift * 8)) & 0xFF));
    }
    if (!write_handle(out, header.data(), header.size())) {
        return abandon("falha ao gravar o cabecalho do selo");
    }

    std::vector<std::uint8_t> block(8 + kBlockSize);
    std::uint64_t done = 0;
    for (std::uint64_t index = 0; index < blocks; ++index) {
        const std::size_t want = static_cast<std::size_t>(
            (db_bytes - done) < kBlockSize ? (db_bytes - done) : kBlockSize);
        DWORD got = 0;
        if (!ReadFile(db, block.data() + 8, static_cast<DWORD>(want), &got, nullptr) ||
            got != want) {
            return abandon("leitura curta no banco durante o selo (o arquivo mudou?)");
        }
        // O numero do bloco entra no MAC: sem isso, reordenar blocos passaria.
        for (int shift = 0; shift < 8; ++shift) {
            block[shift] = static_cast<std::uint8_t>((index >> (shift * 8)) & 0xFF);
        }
        std::uint8_t tag[kTagSize] = {};
        if (!hmac_sha256(key, sizeof(key), block.data(), 8 + want, tag)) {
            return abandon("falha no HMAC do selo");
        }
        if (!write_handle(out, tag, kTagSize)) {
            return abandon("falha ao gravar o selo");
        }
        done += want;
        if (progress) progress(index + 1, blocks, done, db_bytes);
    }

    // Banco vivo pode crescer durante a leitura; nesse caso o selo nao descreve
    // o que esta no disco e nao deve ser publicado.
    LARGE_INTEGER after{};
    const bool stable = GetFileSizeEx(db, &after) &&
                        static_cast<std::uint64_t>(after.QuadPart) == db_bytes;
    CloseHandle(db);
    if (!stable) {
        return abandon("o banco mudou de tamanho durante o selo; nada foi publicado");
    }
    FlushFileBuffers(out);
    CloseHandle(out);
    std::string error;
    if (!publish_temp(temp, seal_file, error)) {
        report.error = error;
        return false;
    }
    report.blocks = blocks;
    report.ok = true;
    return true;
}

bool verify_database(const std::wstring& db_path, const case_cipher& cipher, seal_report& report,
                     const seal_progress_fn& progress) {
    report = seal_report{};
    report.block_size = kBlockSize;
    const std::wstring seal_file = seal_path(db_path);
    report.path = std::string(seal_file.begin(), seal_file.end());
    if (!cipher.ready()) {
        report.error = "cifra do caso nao esta carregada";
        return false;
    }
    std::uint8_t key[kTagSize] = {};
    if (!seal_key(cipher, key)) {
        report.error = "nao foi possivel derivar a chave do selo";
        return false;
    }
    std::vector<std::uint8_t> seal;
    std::string error;
    if (!read_all(seal_file, seal, error)) {
        report.error = "selo ausente: " + error;
        return false;
    }
    if (seal.size() < sizeof(kMagic) + 8 || std::memcmp(seal.data(), kMagic, sizeof(kMagic)) != 0) {
        report.error = "selo invalido (cabecalho)";
        return false;
    }
    std::uint64_t declared = 0;
    for (int i = 0; i < 8; ++i) {
        declared |= static_cast<std::uint64_t>(seal[sizeof(kMagic) + i]) << (i * 8);
    }
    if (seal.size() != sizeof(kMagic) + 8 + declared * kTagSize) {
        report.error = "selo truncado ou de outro arquivo";
        return false;
    }
    // O banco e lido bloco a bloco: um case.db de 8 GB nao cabe na memoria, e a
    // verificacao tem que poder rodar na maquina do perito, nao so no servidor.
    HANDLE db = CreateFileW(db_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (db == INVALID_HANDLE_VALUE) {
        report.error = "nao foi possivel abrir " + std::string(db_path.begin(), db_path.end());
        return false;
    }
    LARGE_INTEGER size_info{};
    if (!GetFileSizeEx(db, &size_info) || size_info.QuadPart < 0) {
        CloseHandle(db);
        report.error = "tamanho do banco ilegivel";
        return false;
    }
    const std::uint64_t db_bytes = static_cast<std::uint64_t>(size_info.QuadPart);
    const std::uint64_t blocks = (db_bytes + kBlockSize - 1) / kBlockSize;
    report.blocks = blocks;
    if (blocks != declared) {
        CloseHandle(db);
        report.error = "tamanho do banco difere do selado";
        report.has_bad_block = true;
        report.bad_block = declared;
        return false;
    }
    const std::uint8_t* tags = seal.data() + sizeof(kMagic) + 8;
    std::vector<std::uint8_t> block(8 + kBlockSize);
    std::uint64_t done = 0;
    for (std::uint64_t index = 0; index < blocks; ++index) {
        const std::size_t want = static_cast<std::size_t>(
            (db_bytes - done) < kBlockSize ? (db_bytes - done) : kBlockSize);
        DWORD got = 0;
        if (!ReadFile(db, block.data() + 8, static_cast<DWORD>(want), &got, nullptr) ||
            got != want) {
            CloseHandle(db);
            report.error = "leitura curta no banco durante a verificacao";
            return false;
        }
        for (int shift = 0; shift < 8; ++shift) {
            block[shift] = static_cast<std::uint8_t>((index >> (shift * 8)) & 0xFF);
        }
        std::uint8_t tag[kTagSize] = {};
        if (!hmac_sha256(key, sizeof(key), block.data(), 8 + want, tag)) {
            CloseHandle(db);
            report.error = "falha no HMAC da verificacao";
            return false;
        }
        if (std::memcmp(tag, tags + index * kTagSize, kTagSize) != 0) {
            CloseHandle(db);
            report.has_bad_block = true;
            report.bad_block = index;
            report.error = "bloco " + std::to_string(index) + " difere do selo";
            return false;
        }
        done += want;
        if (progress) progress(index + 1, blocks, done, db_bytes);
    }
    CloseHandle(db);
    report.ok = true;
    return true;
}


namespace {

// Arquivos de um caso vivo que carregam dado. O .seal de cada um fica ao lado,
// dentro do diretorio do caso, para o laudo hashear o selo junto com a evidencia.
std::vector<std::wstring> case_files(const std::wstring& case_dir) {
    const std::wstring base = case_dir + L"/case.db";
    std::vector<std::wstring> files;
    for (const wchar_t* suffix : {L"", L"-journal", L"-wal"}) {
        const std::wstring candidate = base + suffix;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            files.push_back(candidate);
        }
    }
    return files;
}

std::string narrow(const std::wstring& value) { return std::string(value.begin(), value.end()); }

}  // namespace

seal_set_report seal_case_files(const std::wstring& case_dir, const case_cipher& cipher,
                                const seal_progress_fn& progress) {
    seal_set_report result;
    for (const std::wstring& file : case_files(case_dir)) {
        seal_report one;
        if (seal_database(file, cipher, one, progress) && one.ok) {
            ++result.files;
            result.sealed_files.push_back(narrow(file) + " (" + std::to_string(one.blocks) +
                                          " blocos)");
        } else {
            ++result.failures;
            result.errors.push_back(narrow(file) + ": " + one.error);
        }
    }
    if (result.files == 0) {
        result.error = "nenhum arquivo de caso encontrado em " + narrow(case_dir);
        return result;
    }
    result.ok = result.failures == 0;
    if (!result.ok) result.error = result.errors.front();
    return result;
}

seal_set_report verify_case_files(const std::wstring& case_dir, const case_cipher& cipher,
                                  const seal_progress_fn& progress) {
    seal_set_report result;
    for (const std::wstring& file : case_files(case_dir)) {
        seal_report one;
        if (verify_database(file, cipher, one, progress) && one.ok) {
            ++result.files;
            result.sealed_files.push_back(narrow(file) + " ok");
        } else {
            ++result.failures;
            result.errors.push_back(narrow(file) + ": " + one.error);
        }
    }
    if (result.files == 0 && result.failures == 0) {
        result.error = "nenhum arquivo de caso encontrado em " + narrow(case_dir);
        return result;
    }
    result.ok = result.failures == 0;
    if (!result.ok) result.error = result.errors.front();
    return result;
}

// ---------------------------------------------------------------------------
// Selo da midia extraida
// ---------------------------------------------------------------------------
namespace {

constexpr char kMediaMagic[8] = {'O', 'L', 'H', 'O', 'M', 'E', 'D', '1'};
constexpr std::uint32_t kMediaSealVersion = 1;
constexpr std::size_t kHashChunk = 1024 * 1024;

std::string utf8_of(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &out[0], size, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(size) - 1);
    return out;
}

bool ends_with(const std::wstring& value, const std::wstring& tail) {
    if (value.size() < tail.size()) return false;
    return value.compare(value.size() - tail.size(), tail.size(), tail) == 0;
}

// SHA-256 em fluxo via CNG. Um video de 4 GB nao pode ser lido para a memoria, e
// carregar o arquivo inteiro tambem quebraria o orcamento de um disco de caso.
bool sha256_file(const std::wstring& path, std::uint8_t out[kTagSize], std::uint64_t& bytes,
                 std::string& error,
                 const std::function<void(std::uint64_t, std::uint64_t)>& progress = {}) {
    bytes = 0;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir " + utf8_of(path);
        return false;
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        CloseHandle(file);
        error = "SHA-256 indisponivel";
        return false;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    // Tamanho vem do proprio arquivo aberto: o total do progresso tem que ser o
    // que Esta sendo lido, nao o que a varredura viu antes.
    LARGE_INTEGER size_info{};
    std::uint64_t total_bytes = 0;
    if (GetFileSizeEx(file, &size_info) && size_info.QuadPart > 0) {
        total_bytes = static_cast<std::uint64_t>(size_info.QuadPart);
    }
    std::vector<std::uint8_t> chunk(kHashChunk);
    while (ok) {
        DWORD got = 0;
        if (!ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr)) {
            error = "falha de leitura em " + utf8_of(path);
            ok = false;
            break;
        }
        if (got == 0) break;
        bytes += got;
        if (progress) progress(bytes, total_bytes);
        if (BCryptHashData(hash, chunk.data(), got, 0) != 0) {
            error = "falha no SHA-256 de " + utf8_of(path);
            ok = false;
            break;
        }
    }
    if (ok) ok = BCryptFinishHash(hash, out, static_cast<ULONG>(kTagSize), 0) == 0;
    if (!ok && error.empty()) error = "falha no SHA-256 de " + utf8_of(path);
    if (hash != nullptr) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    return ok;
}

std::string hex_of(const std::uint8_t* data, std::size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

struct media_entry {
    std::string rel;                       // relativo ao diretorio selado, com '/'
    std::uint64_t bytes = 0;
    std::uint8_t digest[kTagSize] = {};
};

void walk_media_dir(const std::wstring& current, std::vector<std::wstring>& out) {
    const std::wstring pattern = current + L"/*";
    WIN32_FIND_DATAW info{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &info);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = info.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring full = current + L"/" + name;
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            // Junction/symlink apontando para fora do caso viraria varredura
            // infinita; o selo cobre o que esta fisicamente na pasta.
            if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
            walk_media_dir(full, out);
            continue;
        }
        // O proprio selo nunca entra na lista (selar o selo e circular), e nem o
        // temporario de uma selagem interrompida: ele nao e evidencia, e sobra.
        if (ends_with(name, std::wstring(L".seal"))) continue;
        if (ends_with(name, std::wstring(L".tmp"))) continue;
        out.push_back(full);
    } while (FindNextFileW(find, &info));
    FindClose(find);
}

std::string relative_to(const std::wstring& root, const std::wstring& full) {
    std::wstring rel = full;
    if (rel.size() > root.size() && rel.compare(0, root.size(), root) == 0) {
        rel = rel.substr(root.size());
        while (!rel.empty() && (rel[0] == L'/' || rel[0] == L'\\')) rel.erase(rel.begin());
    }
    std::string text = utf8_of(rel);
    for (char& c : text) {
        if (c == '\\') c = '/';
    }
    return text;
}

std::wstring media_seal_path(const std::wstring& dir) { return dir + L"/media.seal"; }

void push_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 4; ++shift) {
        out.push_back(static_cast<std::uint8_t>((value >> (shift * 8)) & 0xFF));
    }
}

void push_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 8; ++shift) {
        out.push_back(static_cast<std::uint8_t>((value >> (shift * 8)) & 0xFF));
    }
}

bool take_u32(const std::vector<std::uint8_t>& data, std::size_t& at, std::uint32_t& value) {
    if (at + 4 > data.size()) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[at + i]) << (i * 8);
    at += 4;
    return true;
}

bool take_u64(const std::vector<std::uint8_t>& data, std::size_t& at, std::uint64_t& value) {
    if (at + 8 > data.size()) return false;
    value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[at + i]) << (i * 8);
    at += 8;
    return true;
}

// Monta cabecalho + entradas + HMAC sobre tudo o que veio antes dele.
bool build_seal_blob(const std::vector<media_entry>& entries, const std::uint8_t key[kTagSize],
                     std::vector<std::uint8_t>& blob, std::string& error) {
    blob.clear();
    blob.insert(blob.end(), kMediaMagic, kMediaMagic + sizeof(kMediaMagic));
    push_u32(blob, kMediaSealVersion);
    push_u64(blob, static_cast<std::uint64_t>(entries.size()));
    for (const media_entry& entry : entries) {
        if (entry.rel.size() > 0xFFFFFFFFull) {
            error = "caminho grande demais para o selo";
            return false;
        }
        push_u32(blob, static_cast<std::uint32_t>(entry.rel.size()));
        blob.insert(blob.end(), entry.rel.begin(), entry.rel.end());
        push_u64(blob, entry.bytes);
        blob.insert(blob.end(), entry.digest, entry.digest + kTagSize);
    }
    std::uint8_t tag[kTagSize] = {};
    if (!hmac_sha256(key, kTagSize, blob.data(), blob.size(), tag)) {
        error = "falha no HMAC do selo de midia";
        return false;
    }
    blob.insert(blob.end(), tag, tag + kTagSize);
    return true;
}

bool parse_seal_blob(const std::vector<std::uint8_t>& blob, const std::uint8_t key[kTagSize],
                     std::vector<media_entry>& entries, std::string& error) {
    entries.clear();
    const std::size_t minimum = sizeof(kMediaMagic) + 4 + 8 + kTagSize;
    if (blob.size() < minimum) {
        error = "selo de midia truncado";
        return false;
    }
    const std::size_t body = blob.size() - kTagSize;
    std::uint8_t tag[kTagSize] = {};
    if (!hmac_sha256(key, kTagSize, blob.data(), body, tag)) {
        error = "falha no HMAC da verificacao do selo de midia";
        return false;
    }
    if (std::memcmp(tag, blob.data() + body, kTagSize) != 0) {
        // HMAC nao distingue chave errada de selo adulterado: as duas produzem
        // exatamente o mesmo silencio verificavel, e a mensagem tem que dizer isso.
        error = "selo de midia nao autentica (HMAC nao confere: chave errada ou selo adulterado)";
        return false;
    }
    if (std::memcmp(blob.data(), kMediaMagic, sizeof(kMediaMagic)) != 0) {
        error = "selo de midia invalido (cabecalho)";
        return false;
    }
    std::size_t at = sizeof(kMediaMagic);
    std::uint32_t version = 0;
    if (!take_u32(blob, at, version) || version != kMediaSealVersion) {
        error = "versao de selo de midia nao suportada";
        return false;
    }
    std::uint64_t count = 0;
    if (!take_u64(blob, at, count)) {
        error = "selo de midia truncado";
        return false;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint32_t path_len = 0;
        if (!take_u32(blob, at, path_len) || at + path_len > body) {
            error = "entrada de selo de midia truncada";
            return false;
        }
        media_entry entry;
        entry.rel.assign(reinterpret_cast<const char*>(blob.data() + at), path_len);
        at += path_len;
        if (!take_u64(blob, at, entry.bytes) || at + kTagSize > body) {
            error = "entrada de selo de midia truncada";
            return false;
        }
        std::memcpy(entry.digest, blob.data() + at, kTagSize);
        at += kTagSize;
        entries.push_back(std::move(entry));
    }
    if (at != body) {
        error = "selo de midia com bytes sobrando";
        return false;
    }
    return true;
}

// Le a pasta em ordem estavel (caminho completo ordenado), para o selo ser
// reproduzivel: a ordem que o NTFS devolve nao e contrato.
bool collect_media(const std::wstring& dir, std::vector<media_entry>& entries,
                   std::uint64_t& total, std::string& error,
                   const seal_progress_fn& progress) {
    entries.clear();
    total = 0;
    std::vector<std::wstring> files;
    walk_media_dir(dir, files);
    std::sort(files.begin(), files.end());
    const std::uint64_t count = static_cast<std::uint64_t>(files.size());

    // Total de bytes ANTES de comecar: um total que cresce enquanto trabalha nao
    // informa nada. GetFileAttributesExW da o tamanho sem abrir o arquivo.
    std::uint64_t bytes_total = 0;
    for (const std::wstring& file : files) {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info)) {
            bytes_total += (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
                           static_cast<std::uint64_t>(info.nFileSizeLow);
        }
    }

    std::uint64_t done = 0;
    std::uint64_t bytes_done = 0;
    for (const std::wstring& file : files) {
        media_entry entry;
        entry.rel = relative_to(dir, file);
        std::uint64_t bytes = 0;
        // Progresso DENTRO do arquivo: e o que salva um video de 8 GB de ficar
        // parado em "1/1" durante minutos.
        const std::uint64_t done_before = done;
        const std::uint64_t bytes_before = bytes_done;
        const std::function<void(std::uint64_t, std::uint64_t)> chunk_progress =
            [&progress, done_before, count, bytes_before, bytes_total](std::uint64_t chunk_done,
                                                                       std::uint64_t) {
                if (!progress) return;
                const std::uint64_t now = bytes_before + chunk_done;
                progress(done_before, count, now > bytes_total ? bytes_total : now, bytes_total);
            };
        if (!sha256_file(file, entry.digest, bytes, error, chunk_progress)) return false;
        entry.bytes = bytes;
        total += bytes;
        bytes_done += bytes;
        entries.push_back(std::move(entry));
        ++done;
        // O arquivo pode ter crescido entre a medicao e a leitura; nesse caso o
        // total passa a ser o que foi lido de verdade.
        if (bytes_done > bytes_total) bytes_total = bytes_done;
        if (progress) progress(done, count, bytes_done, bytes_total);
    }
    return true;
}

}  // namespace

media_seal_report seal_media_dir(const std::wstring& dir, const case_cipher& cipher,
                                 const seal_progress_fn& progress) {
    media_seal_report report;
    report.seal_path = utf8_of(media_seal_path(dir));
    if (!cipher.ready()) {
        report.error = "cifra do caso nao esta carregada";
        return report;
    }
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        report.error = "diretorio de midia inexistente: " + utf8_of(dir);
        return report;
    }
    std::uint8_t key[kTagSize] = {};
    if (!seal_key(cipher, key)) {
        report.error = "nao foi possivel derivar a chave do selo";
        return report;
    }
    std::vector<media_entry> entries;
    std::uint64_t total = 0;
    std::string error;
    if (!collect_media(dir, entries, total, error, progress)) {
        report.error = error;
        return report;
    }
    std::vector<std::uint8_t> blob;
    if (!build_seal_blob(entries, key, blob, error)) {
        report.error = error;
        return report;
    }
    const std::wstring seal_file = media_seal_path(dir);
    if (!write_all(seal_file, blob, error)) {
        report.error = error;
        return report;
    }
    report.files = entries.size();
    report.bytes = total;
    report.ok = true;
    return report;
}

media_seal_report verify_media_dir(const std::wstring& dir, const case_cipher& cipher,
                                   const seal_progress_fn& progress) {
    media_seal_report report;
    report.verified = true;
    report.seal_path = utf8_of(media_seal_path(dir));
    if (!cipher.ready()) {
        report.error = "cifra do caso nao esta carregada";
        return report;
    }
    std::uint8_t key[kTagSize] = {};
    if (!seal_key(cipher, key)) {
        report.error = "nao foi possivel derivar a chave do selo";
        return report;
    }
    std::vector<std::uint8_t> blob;
    std::string error;
    if (!read_all(media_seal_path(dir), blob, error)) {
        report.error = "selo de midia ausente: " + error;
        return report;
    }
    std::vector<media_entry> sealed_entries;
    if (!parse_seal_blob(blob, key, sealed_entries, error)) {
        report.error = error;
        return report;
    }
    std::vector<media_entry> disk_entries;
    std::uint64_t total = 0;
    if (!collect_media(dir, disk_entries, total, error, progress)) {
        report.error = error;
        return report;
    }
    report.files = disk_entries.size();
    report.bytes = total;

    // Comparacao por caminho relativo. O selo e uma lista ordenada, entao o
    // caminho e a chave natural e os dois lados ja vem ordenados.
    std::size_t sealed_at = 0;
    std::size_t disk_at = 0;
    while (sealed_at < sealed_entries.size() || disk_at < disk_entries.size()) {
        if (sealed_at >= sealed_entries.size()) {
            report.added.push_back(disk_entries[disk_at].rel);
            ++disk_at;
            continue;
        }
        if (disk_at >= disk_entries.size()) {
            report.missing.push_back(sealed_entries[sealed_at].rel);
            ++sealed_at;
            continue;
        }
        const media_entry& sealed_entry = sealed_entries[sealed_at];
        const media_entry& disk_entry = disk_entries[disk_at];
        if (sealed_entry.rel == disk_entry.rel) {
            if (sealed_entry.bytes != disk_entry.bytes) {
                report.problems.push_back(sealed_entry.rel + ": tamanho " +
                                          std::to_string(disk_entry.bytes) + " bytes, selado com " +
                                          std::to_string(sealed_entry.bytes));
            } else if (std::memcmp(sealed_entry.digest, disk_entry.digest, kTagSize) != 0) {
                report.problems.push_back(sealed_entry.rel + ": conteudo difere do selo (sha256 " +
                                          hex_of(disk_entry.digest, kTagSize) + ", selado " +
                                          hex_of(sealed_entry.digest, kTagSize) + ")");
            }
            ++sealed_at;
            ++disk_at;
            continue;
        }
        if (sealed_entry.rel < disk_entry.rel) {
            report.missing.push_back(sealed_entry.rel);
            ++sealed_at;
        } else {
            report.added.push_back(disk_entry.rel);
            ++disk_at;
        }
    }

    report.ok = report.problems.empty() && report.missing.empty() && report.added.empty();
    if (!report.ok) {
        if (!report.problems.empty()) report.error = report.problems.front();
        else if (!report.missing.empty()) report.error = "arquivo selado ausente: " + report.missing.front();
        else report.error = "arquivo fora do selo: " + report.added.front();
    }
    return report;
}

// ---------------------------------------------------------------------------
// Indice do caso
// ---------------------------------------------------------------------------
namespace {

constexpr char kIndexMagic[8] = {'O', 'L', 'H', 'O', 'C', 'A', 'S', '1'};
constexpr std::uint32_t kIndexVersion = 1;

struct index_entry {
    std::string name;                          // objeto selado, relativo ao caso
    std::uint64_t seal_bytes = 0;
    std::uint8_t seal_digest[kTagSize] = {};
};

std::wstring case_index_path(const std::wstring& case_dir) { return case_dir + L"/case.seal"; }

// Nome do objeto a partir do selo: "recovered/media.seal" -> "recovered/media".
std::string object_name(const std::wstring& case_dir, const std::wstring& seal_file) {
    std::string rel = relative_to(case_dir, seal_file);
    const std::string suffix = ".seal";
    if (rel.size() > suffix.size() &&
        rel.compare(rel.size() - suffix.size(), suffix.size(), suffix) == 0) {
        rel.erase(rel.size() - suffix.size());
    }
    return rel;
}

// Varre o caso atras de selos. O proprio indice fica de fora: um indice que se
// inclui nunca fecha.
void walk_seal_files(const std::wstring& current, std::vector<std::wstring>& out) {
    const std::wstring pattern = current + L"/*";
    WIN32_FIND_DATAW info{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &info);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = info.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring full = current + L"/" + name;
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
            walk_seal_files(full, out);
            continue;
        }
        if (name == L"case.seal") continue;
        if (ends_with(name, std::wstring(L".tmp"))) continue;
        if (ends_with(name, std::wstring(L".seal"))) out.push_back(full);
    } while (FindNextFileW(find, &info));
    FindClose(find);
}

bool digest_files(const std::wstring& case_dir, const std::vector<std::wstring>& files,
                  std::vector<index_entry>& entries, std::string& error,
                  const seal_progress_fn& progress) {
    entries.clear();
    const std::uint64_t count = static_cast<std::uint64_t>(files.size());
    std::uint64_t bytes_total = 0;
    for (const std::wstring& file : files) {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info)) {
            bytes_total += (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
                           static_cast<std::uint64_t>(info.nFileSizeLow);
        }
    }
    std::uint64_t done = 0;
    std::uint64_t bytes_done = 0;
    for (const std::wstring& file : files) {
        index_entry entry;
        entry.name = object_name(case_dir, file);
        std::uint64_t bytes = 0;
        const std::uint64_t done_before = done;
        const std::uint64_t bytes_before = bytes_done;
        const std::function<void(std::uint64_t, std::uint64_t)> chunk_progress =
            [&progress, done_before, count, bytes_before, bytes_total](std::uint64_t chunk_done,
                                                                       std::uint64_t) {
                if (!progress) return;
                const std::uint64_t now = bytes_before + chunk_done;
                progress(done_before, count, now > bytes_total ? bytes_total : now, bytes_total);
            };
        if (!sha256_file(file, entry.seal_digest, bytes, error, chunk_progress)) return false;
        entry.seal_bytes = bytes;
        bytes_done += bytes;
        entries.push_back(std::move(entry));
        ++done;
        if (bytes_done > bytes_total) bytes_total = bytes_done;
        if (progress) progress(done, count, bytes_done, bytes_total);
    }
    std::sort(entries.begin(), entries.end(),
              [](const index_entry& a, const index_entry& b) { return a.name < b.name; });
    return true;
}

bool build_index_blob(const std::vector<index_entry>& entries, const std::uint8_t key[kTagSize],
                      std::vector<std::uint8_t>& blob, std::string& error) {
    blob.clear();
    blob.insert(blob.end(), kIndexMagic, kIndexMagic + sizeof(kIndexMagic));
    push_u32(blob, kIndexVersion);
    push_u64(blob, static_cast<std::uint64_t>(entries.size()));
    for (const index_entry& entry : entries) {
        if (entry.name.size() > 0xFFFFFFFFull) {
            error = "nome grande demais para o indice";
            return false;
        }
        push_u32(blob, static_cast<std::uint32_t>(entry.name.size()));
        blob.insert(blob.end(), entry.name.begin(), entry.name.end());
        push_u64(blob, entry.seal_bytes);
        blob.insert(blob.end(), entry.seal_digest, entry.seal_digest + kTagSize);
    }
    std::uint8_t tag[kTagSize] = {};
    if (!hmac_sha256(key, kTagSize, blob.data(), blob.size(), tag)) {
        error = "falha no HMAC do indice";
        return false;
    }
    blob.insert(blob.end(), tag, tag + kTagSize);
    return true;
}

bool parse_index_blob(const std::vector<std::uint8_t>& blob, const std::uint8_t key[kTagSize],
                      std::vector<index_entry>& entries, std::string& error) {
    entries.clear();
    const std::size_t minimum = sizeof(kIndexMagic) + 4 + 8 + kTagSize;
    if (blob.size() < minimum) {
        error = "indice truncado";
        return false;
    }
    const std::size_t body = blob.size() - kTagSize;
    std::uint8_t tag[kTagSize] = {};
    if (!hmac_sha256(key, kTagSize, blob.data(), body, tag)) {
        error = "falha no HMAC da verificacao do indice";
        return false;
    }
    if (std::memcmp(tag, blob.data() + body, kTagSize) != 0) {
        error = "indice do caso nao autentica (chave errada ou indice adulterado)";
        return false;
    }
    if (std::memcmp(blob.data(), kIndexMagic, sizeof(kIndexMagic)) != 0) {
        error = "indice do caso invalido (cabecalho)";
        return false;
    }
    std::size_t at = sizeof(kIndexMagic);
    std::uint32_t version = 0;
    if (!take_u32(blob, at, version) || version != kIndexVersion) {
        error = "versao de indice nao suportada";
        return false;
    }
    std::uint64_t count = 0;
    if (!take_u64(blob, at, count)) {
        error = "indice truncado";
        return false;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint32_t name_len = 0;
        if (!take_u32(blob, at, name_len) || at + name_len > body) {
            error = "entrada do indice truncada";
            return false;
        }
        index_entry entry;
        entry.name.assign(reinterpret_cast<const char*>(blob.data() + at), name_len);
        at += name_len;
        if (!take_u64(blob, at, entry.seal_bytes) || at + kTagSize > body) {
            error = "entrada do indice truncada";
            return false;
        }
        std::memcpy(entry.seal_digest, blob.data() + at, kTagSize);
        at += kTagSize;
        entries.push_back(std::move(entry));
    }
    if (at != body) {
        error = "indice com bytes sobrando";
        return false;
    }
    return true;
}

}  // namespace

case_index_report seal_case_index(const std::wstring& case_dir, const case_cipher& cipher,
                                 const seal_progress_fn& progress) {
    case_index_report report;
    report.index_path = utf8_of(case_index_path(case_dir));
    if (!cipher.ready()) {
        report.error = "cifra do caso nao esta carregada";
        return report;
    }
    std::uint8_t key[kTagSize] = {};
    if (!seal_key(cipher, key)) {
        report.error = "nao foi possivel derivar a chave do selo";
        return report;
    }
    std::vector<std::wstring> seal_files;
    walk_seal_files(case_dir, seal_files);
    std::sort(seal_files.begin(), seal_files.end());
    if (seal_files.empty()) {
        // Indice de conjunto vazio nao protege nada e ainda daria a impressao de
        // que o caso esta coberto.
        report.error = "nenhum selo encontrado no caso: sele o banco e as pastas antes";
        return report;
    }
    std::vector<index_entry> entries;
    std::string error;
    if (!digest_files(case_dir, seal_files, entries, error, progress)) {
        report.error = error;
        return report;
    }
    std::vector<std::uint8_t> blob;
    if (!build_index_blob(entries, key, blob, error)) {
        report.error = error;
        return report;
    }
    if (!write_all(case_index_path(case_dir), blob, error)) {
        report.error = error;
        return report;
    }
    report.entries = entries.size();
    report.ok = true;
    return report;
}

case_index_report verify_case_index(const std::wstring& case_dir, const case_cipher& cipher,
                                    const seal_progress_fn& progress) {
    case_index_report report;
    report.verified = true;
    report.index_path = utf8_of(case_index_path(case_dir));
    if (!cipher.ready()) {
        report.error = "cifra do caso nao esta carregada";
        return report;
    }
    std::uint8_t key[kTagSize] = {};
    if (!seal_key(cipher, key)) {
        report.error = "nao foi possivel derivar a chave do selo";
        return report;
    }
    std::vector<std::uint8_t> blob;
    std::string error;
    if (!read_all(case_index_path(case_dir), blob, error)) {
        report.error = "indice do caso ausente: " + error;
        return report;
    }
    std::vector<index_entry> expected;
    if (!parse_index_blob(blob, key, expected, error)) {
        report.error = error;
        return report;
    }
    std::vector<std::wstring> seal_files;
    walk_seal_files(case_dir, seal_files);
    std::sort(seal_files.begin(), seal_files.end());
    std::vector<index_entry> disk;
    if (!digest_files(case_dir, seal_files, disk, error, progress)) {
        report.error = error;
        return report;
    }
    report.entries = disk.size();

    std::size_t expected_at = 0;
    std::size_t disk_at = 0;
    while (expected_at < expected.size() || disk_at < disk.size()) {
        if (expected_at >= expected.size()) {
            report.added.push_back(disk[disk_at].name + ".seal");
            ++disk_at;
            continue;
        }
        if (disk_at >= disk.size()) {
            report.missing.push_back(expected[expected_at].name + ".seal");
            ++expected_at;
            continue;
        }
        const index_entry& want = expected[expected_at];
        const index_entry& have = disk[disk_at];
        if (want.name == have.name) {
            if (want.seal_bytes != have.seal_bytes ||
                std::memcmp(want.seal_digest, have.seal_digest, kTagSize) != 0) {
                report.problems.push_back(have.name + ".seal: digest " +
                                          hex_of(have.seal_digest, kTagSize) + ", registrado " +
                                          hex_of(want.seal_digest, kTagSize));
            }
            ++expected_at;
            ++disk_at;
            continue;
        }
        if (want.name < have.name) {
            report.missing.push_back(want.name + ".seal");
            ++expected_at;
        } else {
            report.added.push_back(have.name + ".seal");
            ++disk_at;
        }
    }

    report.ok = report.problems.empty() && report.missing.empty() && report.added.empty();
    if (!report.ok) {
        if (!report.missing.empty()) report.error = "selo do indice sumiu: " + report.missing.front();
        else if (!report.added.empty()) report.error = "selo fora do indice: " + report.added.front();
        else report.error = report.problems.front();
    }
    return report;
}

}  // namespace deep
