// language: C++, file: android_backup.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "android_backup.hpp"

#include <windows.h>
#include <bcrypt.h>

#include "wa_crypto.hpp"   // olho::wa::crypto::aes_cbc: the CNG AES-CBC wrapper already proven

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

namespace olho::android {
namespace {

constexpr std::size_t kWindow = 65536;        // 32K history + 32K new output
constexpr std::size_t kHistory = 32768;
constexpr std::size_t kCopyChunk = 65536;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr,
                        nullptr);
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string win_error(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string out;
    if (n != 0 && text != nullptr) {
        std::wstring w(text, n);
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ')) w.pop_back();
        out = narrow(w);
    }
    if (text) LocalFree(text);
    if (out.empty()) out = "Win32 error " + std::to_string(code);
    return out;
}

// Every filesystem call goes through here: an extracted WhatsApp tree routinely
// exceeds MAX_PATH, and CreateFileW without the extended prefix simply fails.
std::wstring long_path(const std::wstring& path) {
    if (path.empty()) return path;
    if (path.compare(0, 4, L"\\\\?\\") == 0) return path;
    std::wstring fixed;
    fixed.reserve(path.size() + 8);
    for (const wchar_t c : path) fixed.push_back(c == L'/' ? L'\\' : c);
    if (fixed.size() >= 2 && fixed[1] == L':') return L"\\\\?\\" + fixed;
    return fixed;
}

bool make_dir(const std::wstring& path) {
    const std::wstring full = long_path(path);
    if (CreateDirectoryW(full.c_str(), nullptr)) return true;
    const DWORD code = GetLastError();
    return code == ERROR_ALREADY_EXISTS;
}

bool make_dirs(const std::wstring& path) {
    if (path.empty()) return false;
    if (make_dir(path)) return true;
    const std::size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return false;
    if (!make_dirs(path.substr(0, cut))) return false;
    return make_dir(path);
}

bool is_dir(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(long_path(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::uint64_t file_size_of(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(long_path(path).c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hex_decode(const std::string& text, std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    if (text.empty()) {
        error = "campo hexadecimal vazio";
        return false;
    }
    if ((text.size() % 2) != 0) {
        error = "campo hexadecimal com numero impar de digitos (" + std::to_string(text.size()) + ")";
        return false;
    }
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = hex_nibble(text[i]);
        const int lo = hex_nibble(text[i + 1]);
        if (hi < 0 || lo < 0) {
            error = "caractere nao hexadecimal no cabecalho";
            out.clear();
            return false;
        }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

std::string hex_encode(const std::uint8_t* data, std::size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// CNG primitives
// ---------------------------------------------------------------------------

class AlgHandle {
public:
    AlgHandle() = default;
    ~AlgHandle() { if (handle_) BCryptCloseAlgorithmProvider(handle_, 0); }
    AlgHandle(const AlgHandle&) = delete;
    AlgHandle& operator=(const AlgHandle&) = delete;

    bool open(LPCWSTR algorithm, ULONG flags, std::string& error) {
        const NTSTATUS status = BCryptOpenAlgorithmProvider(&handle_, algorithm, nullptr, flags);
        if (!BCRYPT_SUCCESS(status)) {
            handle_ = nullptr;
            error = "BCryptOpenAlgorithmProvider falhou (" + std::to_string(status) + ")";
            return false;
        }
        return true;
    }
    BCRYPT_ALG_HANDLE get() const { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
};

bool pbkdf2_sha1(const std::string& password, const std::vector<std::uint8_t>& salt,
                 std::uint32_t rounds, std::uint8_t* out, std::size_t out_len, std::string& error) {
    AlgHandle alg;
    if (!alg.open(BCRYPT_SHA1_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG, error)) return false;
    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        alg.get(), reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        reinterpret_cast<PUCHAR>(const_cast<std::uint8_t*>(salt.data())),
        static_cast<ULONG>(salt.size()), static_cast<ULONGLONG>(rounds), out,
        static_cast<ULONG>(out_len), 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = "BCryptDeriveKeyPBKDF2 falhou (" + std::to_string(status) + ")";
        return false;
    }
    return true;
}

std::uint32_t crc32_bytes(const std::uint8_t* data, std::size_t len, std::uint32_t crc = 0) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    std::uint32_t value = crc ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) value = table[(value ^ data[i]) & 0xFF] ^ (value >> 8);
    return value ^ 0xFFFFFFFFu;
}

// Adler-32 is what zlib (RFC 1950) carries as its trailer; the .ab payload is a
// zlib stream, not a gzip one, so this is the check that actually runs.
std::uint32_t adler32_bytes(const std::uint8_t* data, std::size_t len, std::uint32_t adler = 1) {
    std::uint32_t a = adler & 0xFFFF;
    std::uint32_t b = (adler >> 16) & 0xFFFF;
    for (std::size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

class Sha256 {
public:
    bool init(std::string& error) {
        if (!alg_.open(BCRYPT_SHA256_ALGORITHM, 0, error)) return false;
        ULONG object_len = 0, written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(alg_.get(), BCRYPT_OBJECT_LENGTH,
                                             reinterpret_cast<PUCHAR>(&object_len), sizeof(object_len),
                                             &written, 0))) {
            error = "BCryptGetProperty(OBJECT_LENGTH) falhou";
            return false;
        }
        object_.resize(object_len);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(alg_.get(), &hash_, object_.data(), object_len, nullptr,
                                            0, 0))) {
            error = "BCryptCreateHash falhou";
            hash_ = nullptr;
            return false;
        }
        return true;
    }
    ~Sha256() { if (hash_) BCryptDestroyHash(hash_); }
    bool update(const void* data, std::size_t len) {
        if (!hash_ || len == 0) return true;
        return BCRYPT_SUCCESS(BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                                             static_cast<ULONG>(len), 0));
    }
    bool finish(std::string& hex, std::string& error) {
        if (!hash_) {
            error = "hash nao inicializado";
            return false;
        }
        std::uint8_t digest[32] = {0};
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash_, digest, sizeof(digest), 0))) {
            error = "BCryptFinishHash falhou";
            return false;
        }
        hex = hex_encode(digest, sizeof(digest));
        return true;
    }

private:
    AlgHandle alg_;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<std::uint8_t> object_;
};

// PKCS7 strip, as Java's AES/CBC/PKCS5Padding writes it.
bool strip_pkcs7(std::uint8_t* block, std::size_t len, std::size_t& out_len, std::string& error) {
    if (len == 0 || (len % 16) != 0) {
        error = "bloco cifrado com tamanho invalido";
        return false;
    }
    const std::uint8_t pad = block[len - 1];
    if (pad == 0 || pad > 16 || pad > len) {
        error = "padding PKCS7 invalido (" + std::to_string(static_cast<int>(pad)) + ")";
        return false;
    }
    for (std::size_t i = len - pad; i < len; ++i) {
        if (block[i] != pad) {
            error = "padding PKCS7 inconsistente";
            return false;
        }
    }
    out_len = len - pad;
    return true;
}

// ---------------------------------------------------------------------------
// byte readers
// ---------------------------------------------------------------------------

class Reader {
public:
    virtual ~Reader() = default;
    // Returns false on error. got == 0 means end of stream.
    virtual bool read(std::uint8_t* dst, std::size_t cap, std::size_t& got, std::string& error) = 0;
};

bool read_exact(Reader& reader, std::uint8_t* dst, std::size_t len, std::string& error) {
    std::size_t done = 0;
    while (done < len) {
        std::size_t got = 0;
        if (!reader.read(dst + done, len - done, got, error)) return false;
        if (got == 0) {
            error = "fluxo terminou antes do esperado (" + std::to_string(done) + " de " +
                    std::to_string(len) + " bytes)";
            return false;
        }
        done += got;
    }
    return true;
}

class FileReader : public Reader {
public:
    ~FileReader() override { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }

    bool open(const std::wstring& path, std::uint64_t offset, std::string& error) {
        file_ = CreateFileW(long_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            error = "nao foi possivel abrir " + narrow(path) + ": " + win_error(GetLastError());
            return false;
        }
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(file_, pos, nullptr, FILE_BEGIN)) {
            error = "nao foi possivel posicionar em " + std::to_string(offset);
            return false;
        }
        return true;
    }

    bool read(std::uint8_t* dst, std::size_t cap, std::size_t& got, std::string& error) override {
        got = 0;
        DWORD want = cap > kCopyChunk ? static_cast<DWORD>(kCopyChunk) : static_cast<DWORD>(cap);
        DWORD read_now = 0;
        if (!ReadFile(file_, dst, want, &read_now, nullptr)) {
            error = "falha de leitura: " + win_error(GetLastError());
            return false;
        }
        got = read_now;
        return true;
    }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
};

// AES-256-CBC decryptor that strips PKCS7 at the end of the stream. One block of
// ciphertext is always held back, because whether the final block is data or
// padding is only known at EOF.
class AesReader : public Reader {
public:
    bool init(std::unique_ptr<Reader> upstream, const std::vector<std::uint8_t>& key,
              const std::vector<std::uint8_t>& iv) {
        upstream_ = std::move(upstream);
        cipher_ = std::make_unique<olho::wa::crypto::aes_cbc>(key.data(), key.size());
        if (!cipher_->ok()) return false;
        iv_ = iv;
        iv_.resize(16, 0);
        return true;
    }

    bool read(std::uint8_t* dst, std::size_t cap, std::size_t& got, std::string& error) override {
        got = 0;
        while (got < cap) {
            if (!ready_.empty()) {
                const std::size_t take = (ready_.size() < cap - got) ? ready_.size() : (cap - got);
                std::memcpy(dst + got, ready_.data(), take);
                ready_.erase(ready_.begin(), ready_.begin() + static_cast<std::ptrdiff_t>(take));
                got += take;
                continue;
            }
            // The final fill() can set done_ with bytes still in ready_: they are
            // the last block of the tar and must be handed over before EOF.
            if (done_) break;
            if (!fill(error)) return false;
        }
        return true;
    }

private:
    bool decrypt_prefix(std::size_t usable, std::string& error) {
        if (usable == 0) return true;
        if (!cipher_->decrypt_update(carry_.data(), usable, iv_.data())) {
            error = "AES-CBC falhou ao decifrar o payload";
            return false;
        }
        ready_.append(reinterpret_cast<const char*>(carry_.data()), usable);
        carry_.erase(carry_.begin(), carry_.begin() + static_cast<std::ptrdiff_t>(usable));
        return true;
    }

    bool fill(std::string& error) {
        std::uint8_t chunk[kCopyChunk];
        for (;;) {
            std::size_t received = 0;
            if (!upstream_->read(chunk, sizeof(chunk), received, error)) return false;
            if (received == 0) {
                // EOF: what is held back is the final block, and it is padding.
                if (carry_.empty()) {
                    done_ = true;
                    return true;
                }
                if ((carry_.size() % 16) != 0) {
                    error = "payload cifrado nao e multiplo de 16 bytes";
                    return false;
                }
                if (!cipher_->decrypt_update(carry_.data(), carry_.size(), iv_.data())) {
                    error = "AES-CBC falhou no bloco final";
                    return false;
                }
                std::size_t plain = 0;
                if (!strip_pkcs7(carry_.data(), carry_.size(), plain, error)) return false;
                ready_.append(reinterpret_cast<const char*>(carry_.data()), plain);
                carry_.clear();
                done_ = true;
                return true;
            }
            carry_.insert(carry_.end(), chunk, chunk + received);
            if (carry_.size() > 16) {
                const std::size_t usable = ((carry_.size() - 16) / 16) * 16;
                if (!decrypt_prefix(usable, error)) return false;
            }
            if (!ready_.empty()) return true;
        }
    }

    std::unique_ptr<Reader> upstream_;
    std::unique_ptr<olho::wa::crypto::aes_cbc> cipher_;
    std::vector<std::uint8_t> iv_;
    std::vector<std::uint8_t> carry_;
    std::string ready_;
    bool done_ = false;
};

// ---------------------------------------------------------------------------
// RFC 1951 inflate (raw deflate). Self-contained because the project links no
// zlib: CNG provides AES and PBKDF2 but nothing for gzip.
// ---------------------------------------------------------------------------

struct Huffman {
    short count[16] = {0};
    short symbol[288] = {0};
};

class Inflater {
public:
    enum class Status { need_input, output_ready, done, failed };

    void reset() {
        in_.clear();
        in_pos_ = 0;
        bitbuf_ = 0;
        bitcount_ = 0;
        hist_ = 0;
        produced_ = 0;
        stage_ = Stage::block_header;
        last_block_ = false;
        block_type_ = 0;
        stored_remaining_ = 0;
        copy_len_ = 0;
        copy_dist_ = 0;
        for (auto& table : lengths_) table = Huffman{};
        dyn_index_ = 0;
        dyn_total_ = 0;
    }

    void feed(const std::uint8_t* data, std::size_t len) {
        if (len == 0) return;
        if (in_pos_ > 0 && in_pos_ == in_.size()) {
            in_.clear();
            in_pos_ = 0;
        }
        in_.insert(in_.end(), data, data + len);
    }

    bool has_input() const { return in_pos_ < in_.size(); }
    const std::uint8_t* leftover() const { return in_.data() + in_pos_; }
    std::size_t leftover_len() const { return in_.size() - in_pos_; }
    void drop_leftover(std::size_t count) {
        in_pos_ += count;
        if (in_pos_ > in_.size()) in_pos_ = in_.size();
    }
    // Deflate ends mid-byte, so the bytes that follow the stream may already have
    // been pulled into the bit buffer by the lookahead. A container trailer has to
    // read those first, or the last one to four bytes of a valid stream look
    // missing.
    // The low (bitcount % 8) bits belong to the partially read byte that closed
    // the deflate stream: they are padding, not the start of the trailer. Only the
    // whole bytes above them are container data.
    void peek_pending(std::uint8_t* dst, std::size_t& count) const {
        count = 0;
        const int pad = bitcount_ & 7;
        std::uint64_t value = bitbuf_ >> pad;
        int bits = bitcount_ - pad;
        while (bits >= 8) {
            dst[count++] = static_cast<std::uint8_t>(value & 0xFF);
            value >>= 8;
            bits -= 8;
        }
    }
    void drop_pending(std::size_t count) {
        const int pad = bitcount_ & 7;
        bitbuf_ >>= pad;
        bitcount_ -= pad;
        const int bits = static_cast<int>(count) * 8;
        if (bits > 0) {
            bitbuf_ >>= bits;
            bitcount_ -= bits;
            if (bitcount_ < 0) bitcount_ = 0;
        }
    }
    // Tells the decoder that no further input will arrive, so the final (short)
    // code may be decoded from the bits that are actually left.
    void set_input_eof(bool value) { input_eof_ = value; }

    const std::uint8_t* out_ptr() const { return window_ + hist_; }
    std::size_t out_len() const { return produced_; }

    // Consumes only what the caller actually took: the inflater can finish with
    // its whole output still in the window, and dropping it there would lose the
    // tail of the archive.
    void consume(std::size_t count) {
        if (count > produced_) count = produced_;
        hist_ += count;
        produced_ -= count;
        if (hist_ > kHistory) {
            const std::size_t drop = hist_ - kHistory;
            std::memmove(window_, window_ + drop, hist_ + produced_ - drop);
            hist_ -= drop;
        }
    }

    Status run(std::string& error);

private:
    enum class Stage {
        block_header,
        stored_length,
        stored_bytes,
        dynamic_counts,
        dynamic_lengths,
        codes,
        copy,
        finished
    };

    bool ensure_bits(int need) {
        while (bitcount_ < need) {
            if (in_pos_ >= in_.size()) return false;
            bitbuf_ |= static_cast<std::uint64_t>(in_[in_pos_++]) << bitcount_;
            bitcount_ += 8;
        }
        return true;
    }
    int take_bits(int need) {
        const std::uint64_t mask = (need == 64) ? ~0ull : ((1ull << need) - 1);
        const int value = static_cast<int>(bitbuf_ & mask);
        bitbuf_ >>= need;
        bitcount_ -= need;
        return value;
    }
    void align_byte() {
        const int drop = bitcount_ & 7;
        bitbuf_ >>= drop;
        bitcount_ -= drop;
    }
    bool build(Huffman& table, const short* lengths, int symbols, std::string& error);
    enum class Decode { code, need_input, corrupt };
    // Never consumes bits unless it returns code or corrupt: a 'need_input' here
    // has to be resumable, or a symbol split across two reads decodes wrongly.
    Decode decode_symbol(const Huffman& table, int& symbol);

    bool emit(std::uint8_t byte) {
        if (hist_ + produced_ >= kWindow) return false;
        window_[hist_ + produced_++] = byte;
        return true;
    }

    std::vector<std::uint8_t> in_;
    std::size_t in_pos_ = 0;
    std::uint64_t bitbuf_ = 0;
    int bitcount_ = 0;
    bool input_eof_ = false;
    std::uint8_t window_[kWindow] = {0};
    std::size_t hist_ = 0;
    std::size_t produced_ = 0;
    Stage stage_ = Stage::block_header;
    bool last_block_ = false;
    int block_type_ = 0;
    std::uint32_t stored_remaining_ = 0;
    std::size_t copy_len_ = 0;
    std::size_t copy_dist_ = 0;
    Huffman lengths_[2];   // 0 = literal/length, 1 = distance
    Huffman cl_lengths_;
    short dyn_[320] = {0};
    int dyn_index_ = 0;
    int dyn_total_ = 0;
    int dyn_lit_ = 0;
    int dyn_dist_ = 0;
};

bool Inflater::build(Huffman& table, const short* lengths, int symbols, std::string& error) {
    for (int i = 0; i < 16; ++i) table.count[i] = 0;
    for (int i = 0; i < symbols; ++i) table.count[lengths[i]]++;
    if (table.count[0] == symbols) return true;   // no codes at all: legal but unusable

    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= table.count[len];
        if (left < 0) {
            error = "conjunto de codigos Huffman sobre-assinado";
            return false;
        }
    }
    short offsets[16] = {0};
    for (int len = 1; len < 15; ++len) offsets[len + 1] = static_cast<short>(offsets[len] + table.count[len]);
    for (int i = 0; i < symbols; ++i) {
        if (lengths[i] != 0) table.symbol[offsets[lengths[i]]++] = static_cast<short>(i);
    }
    return true;
}

Inflater::Decode Inflater::decode_symbol(const Huffman& table, int& symbol) {
    // Fast path: with a full 15-bit window the decode cannot run dry half way.
    if (bitcount_ < 15 && !input_eof_) {
        if (!ensure_bits(15)) return Decode::need_input;
    }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        if (bitcount_ < 1 && !ensure_bits(1)) {
            // Only reachable at end of stream: the code is longer than the bits
            // that remain, which no encoder produces.
            return input_eof_ ? Decode::corrupt : Decode::need_input;
        }
        code |= take_bits(1);
        const int count = table.count[len];
        if (code - first < count) {
            symbol = table.symbol[index + (code - first)];
            return Decode::code;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return Decode::corrupt;
}

namespace {

const short kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                             33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                             1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const short kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

}  // namespace

Inflater::Status Inflater::run(std::string& error) {
    for (;;) {
        if (stage_ == Stage::finished) return Status::done;
        if (hist_ + produced_ >= kWindow) return Status::output_ready;

        switch (stage_) {
            case Stage::block_header: {
                if (!ensure_bits(3)) return Status::need_input;
                last_block_ = take_bits(1) != 0;
                block_type_ = take_bits(2);
                if (block_type_ == 0) {
                    stage_ = Stage::stored_length;
                } else if (block_type_ == 1) {
                    short lit[288];
                    for (int i = 0; i < 144; ++i) lit[i] = 8;
                    for (int i = 144; i < 256; ++i) lit[i] = 9;
                    for (int i = 256; i < 280; ++i) lit[i] = 7;
                    for (int i = 280; i < 288; ++i) lit[i] = 8;
                    if (!build(lengths_[0], lit, 288, error)) return Status::failed;
                    short dist[30];
                    for (int i = 0; i < 30; ++i) dist[i] = 5;
                    if (!build(lengths_[1], dist, 30, error)) return Status::failed;
                    stage_ = Stage::codes;
                } else if (block_type_ == 2) {
                    stage_ = Stage::dynamic_counts;
                } else {
                    error = "tipo de bloco deflate reservado (3)";
                    return Status::failed;
                }
                break;
            }
            case Stage::stored_length: {
                align_byte();
                if (!ensure_bits(32)) return Status::need_input;
                const int len = take_bits(16);
                const int nlen = take_bits(16);
                if (((len ^ 0xFFFF) & 0xFFFF) != nlen) {
                    error = "bloco deflate armazenado com LEN/NLEN inconsistente";
                    return Status::failed;
                }
                stored_remaining_ = static_cast<std::uint32_t>(len);
                stage_ = Stage::stored_bytes;
                break;
            }
            case Stage::stored_bytes: {
                while (stored_remaining_ > 0) {
                    if (hist_ + produced_ >= kWindow) return Status::output_ready;
                    if (bitcount_ >= 8) {
                        if (!emit(static_cast<std::uint8_t>(take_bits(8)))) return Status::output_ready;
                        --stored_remaining_;
                    } else if (in_pos_ < in_.size()) {
                        if (!emit(in_[in_pos_++])) return Status::output_ready;
                        --stored_remaining_;
                    } else {
                        return Status::need_input;
                    }
                }
                stage_ = last_block_ ? Stage::finished : Stage::block_header;
                break;
            }
            case Stage::dynamic_counts: {
                if (!ensure_bits(14)) return Status::need_input;
                dyn_lit_ = take_bits(5) + 257;
                dyn_dist_ = take_bits(5) + 1;
                const int clen = take_bits(4) + 4;
                if (dyn_lit_ > 286 || dyn_dist_ > 30) {
                    error = "cabecalho Huffman dinamico fora de faixa";
                    return Status::failed;
                }
                for (int i = 0; i < 19; ++i) dyn_[i] = 0;
                for (int i = 0; i < clen; ++i) {
                    if (!ensure_bits(3)) return Status::need_input;
                    dyn_[kClOrder[i]] = static_cast<short>(take_bits(3));
                }
                if (!build(cl_lengths_, dyn_, 19, error)) return Status::failed;
                dyn_index_ = 0;
                dyn_total_ = dyn_lit_ + dyn_dist_;
                for (int i = 0; i < dyn_total_; ++i) dyn_[i] = 0;
                stage_ = Stage::dynamic_lengths;
                break;
            }
            case Stage::dynamic_lengths: {
                while (dyn_index_ < dyn_total_) {
                    int symbol = 0;
                    const Decode decoded = decode_symbol(cl_lengths_, symbol);
                    if (decoded == Decode::need_input) return Status::need_input;
                    if (decoded == Decode::corrupt) {
                        error = "codigo de comprimento invalido";
                        return Status::failed;
                    }
                    if (symbol < 16) {
                        dyn_[dyn_index_++] = static_cast<short>(symbol);
                    } else if (symbol == 16) {
                        if (dyn_index_ == 0) {
                            error = "repeticao de comprimento sem codigo anterior";
                            return Status::failed;
                        }
                        if (!ensure_bits(2)) return Status::need_input;
                        const short prev = dyn_[dyn_index_ - 1];
                        const int repeat = 3 + take_bits(2);
                        for (int i = 0; i < repeat && dyn_index_ < dyn_total_; ++i) dyn_[dyn_index_++] = prev;
                    } else if (symbol == 17) {
                        if (!ensure_bits(3)) return Status::need_input;
                        const int repeat = 3 + take_bits(3);
                        for (int i = 0; i < repeat && dyn_index_ < dyn_total_; ++i) dyn_[dyn_index_++] = 0;
                    } else {
                        if (!ensure_bits(7)) return Status::need_input;
                        const int repeat = 11 + take_bits(7);
                        for (int i = 0; i < repeat && dyn_index_ < dyn_total_; ++i) dyn_[dyn_index_++] = 0;
                    }
                }
                if (!build(lengths_[0], dyn_, dyn_lit_, error)) return Status::failed;
                if (!build(lengths_[1], dyn_ + dyn_lit_, dyn_dist_, error)) return Status::failed;
                stage_ = Stage::codes;
                break;
            }
            case Stage::codes: {
                int symbol = 0;
                const Decode decoded = decode_symbol(lengths_[0], symbol);
                if (decoded == Decode::need_input) return Status::need_input;
                if (decoded == Decode::corrupt) {
                    error = "simbolo Huffman invalido no bloco deflate";
                    return Status::failed;
                }
                if (symbol < 256) {
                    if (!emit(static_cast<std::uint8_t>(symbol))) return Status::output_ready;
                } else if (symbol == 256) {
                    stage_ = last_block_ ? Stage::finished : Stage::block_header;
                } else {
                    const int index = symbol - 257;
                    if (index >= 29) {
                        error = "codigo de comprimento reservado";
                        return Status::failed;
                    }
                    if (!ensure_bits(kLengthExtra[index])) return Status::need_input;
                    copy_len_ = static_cast<std::size_t>(kLengthBase[index] + take_bits(kLengthExtra[index]));
                    int dsym = 0;
                    const Decode dist_decoded = decode_symbol(lengths_[1], dsym);
                    if (dist_decoded == Decode::need_input) return Status::need_input;
                    if (dist_decoded == Decode::corrupt) {
                        error = "codigo de distancia invalido";
                        return Status::failed;
                    }
                    if (dsym >= 30) {
                        error = "codigo de distancia reservado";
                        return Status::failed;
                    }
                    if (!ensure_bits(kDistExtra[dsym])) return Status::need_input;
                    copy_dist_ =
                        static_cast<std::size_t>(kDistBase[dsym] + take_bits(kDistExtra[dsym]));
                    if (copy_dist_ > hist_ + produced_) {
                        error = "distancia LZ77 aponta antes do inicio do fluxo";
                        return Status::failed;
                    }
                    stage_ = Stage::copy;
                }
                break;
            }
            case Stage::copy: {
                while (copy_len_ > 0) {
                    if (hist_ + produced_ >= kWindow) return Status::output_ready;
                    const std::uint8_t byte = window_[hist_ + produced_ - copy_dist_];
                    window_[hist_ + produced_++] = byte;
                    --copy_len_;
                }
                stage_ = Stage::codes;
                break;
            }
            case Stage::finished: {
                return Status::done;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// gzip container (RFC 1952) around the deflate stream
// ---------------------------------------------------------------------------

// The payload of a real .ab is zlib (RFC 1950): AOSP writes it with
// java.util.zip.DeflaterOutputStream and reads it back with InflaterInputStream.
// gzip (RFC 1952) is accepted as well, because tools that re-pack archives emit
// it; the two are told apart by their headers, not by a guess.
class CompressedReader : public Reader {
public:
    enum class Container { unknown, zlib, gzip };

    const char* container_name() const {
        if (container_ == Container::zlib) return "zlib";
        if (container_ == Container::gzip) return "gzip";
        return "desconhecido";
    }

    bool init(std::unique_ptr<Reader> upstream) {
        upstream_ = std::move(upstream);
        inflater_.reset();
        return true;
    }

    bool read(std::uint8_t* dst, std::size_t cap, std::size_t& got, std::string& error) override {
        got = 0;
        while (got < cap) {
            if (!fill(error)) return false;
            if (mode_ == Mode::finished) return true;
            std::size_t take = inflater_.out_len();
            if (take > cap - got) take = cap - got;
            if (take == 0) continue;
            std::memcpy(dst + got, inflater_.out_ptr(), take);
            if (container_ == Container::gzip) {
                checksum_computed_ = crc32_bytes(inflater_.out_ptr(), take, checksum_computed_);
            } else {
                checksum_computed_ = adler32_bytes(inflater_.out_ptr(), take, checksum_computed_);
            }
            size_ += take;
            produced_ += take;
            inflater_.consume(take);
            got += take;
        }
        return true;
    }

    bool integrity_ok() const { return integrity_checked_ && checksum_expected_ == checksum_computed_; }
    std::uint32_t checksum_expected() const { return checksum_expected_; }
    std::uint32_t checksum_computed() const { return checksum_computed_; }
    std::uint64_t uncompressed_size() const { return size_; }
    std::uint64_t produced() const { return produced_; }
    // Bytes after the container trailer (a concatenated member, or padding): they
    // are reported, never decoded.
    bool has_trailing_data() const { return extra_; }

private:
    enum class Mode { header, body, trailer, finished };

    bool need_bytes(std::size_t count, std::string& error) {
        while (pending_.size() - pending_pos_ < count) {
            std::uint8_t chunk[kCopyChunk];
            std::size_t received = 0;
            if (!upstream_->read(chunk, sizeof(chunk), received, error)) return false;
            if (received == 0) {
                eof_ = true;
                return false;
            }
            if (pending_pos_ > 0 && pending_pos_ == pending_.size()) {
                pending_.clear();
                pending_pos_ = 0;
            }
            pending_.insert(pending_.end(), chunk, chunk + received);
        }
        return true;
    }

    bool read_header(std::string& error) {
        if (!need_bytes(2, error)) {
            if (error.empty()) error = "cabecalho de compressao truncado";
            return false;
        }
        const std::uint8_t* head = pending_.data() + pending_pos_;
        if (head[0] == 0x1F && head[1] == 0x8B) {
            container_ = Container::gzip;
            checksum_computed_ = 0;   // CRC-32 starts at zero
            return read_gzip_header(error);
        }
        const std::uint32_t cmf = head[0];
        const std::uint32_t flg = head[1];
        if ((cmf & 0x0F) == 8 && ((cmf << 8) + flg) % 31 == 0) {
            container_ = Container::zlib;
            if ((flg & 0x20) != 0) {
                error = "zlib: FDICT (dicionario predefinido) nao suportado";
                return false;
            }
            checksum_computed_ = 1;   // Adler-32 starts at one
            pending_pos_ += 2;
            if (pending_pos_ < pending_.size()) {
                inflater_.feed(pending_.data() + pending_pos_, pending_.size() - pending_pos_);
            }
            pending_.clear();
            pending_pos_ = 0;
            mode_ = Mode::body;
            return true;
        }
        error = "compressao nao reconhecida: nao e zlib (RFC 1950) nem gzip (RFC 1952)";
        return false;
    }

    bool read_gzip_header(std::string& error) {
        if (!need_bytes(10, error)) {
            if (error.empty()) error = "gzip: cabecalho truncado";
            return false;
        }
        const std::uint8_t* p = pending_.data() + pending_pos_;
        if (p[2] != 8) {
            error = "gzip: metodo de compressao diferente de deflate";
            return false;
        }
        const std::uint8_t flags = p[3];
        pending_pos_ += 10;

        if ((flags & 0x04) != 0) {   // FEXTRA
            if (!need_bytes(2, error)) {
                if (error.empty()) error = "gzip: FEXTRA truncado";
                return false;
            }
            const std::size_t len = pending_[pending_pos_] | (pending_[pending_pos_ + 1] << 8);
            pending_pos_ += 2;
            if (!need_bytes(len, error)) {
                if (error.empty()) error = "gzip: FEXTRA truncado";
                return false;
            }
            pending_pos_ += len;
        }
        for (int which = 0; which < 2; ++which) {   // FNAME, FCOMMENT
            const std::uint8_t mask = which == 0 ? 0x08 : 0x10;
            if ((flags & mask) == 0) continue;
            for (;;) {
                if (!need_bytes(1, error)) {
                    if (error.empty()) error = "gzip: campo de texto truncado";
                    return false;
                }
                const char c = static_cast<char>(pending_[pending_pos_++]);
                if (c == '\0') break;
            }
        }
        if ((flags & 0x02) != 0) {   // FHCRC
            if (!need_bytes(2, error)) {
                if (error.empty()) error = "gzip: FHCRC truncado";
                return false;
            }
            pending_pos_ += 2;
        }

        if (pending_pos_ < pending_.size()) {
            inflater_.feed(pending_.data() + pending_pos_, pending_.size() - pending_pos_);
        }
        pending_.clear();
        pending_pos_ = 0;
        mode_ = Mode::body;
        return true;
    }

    bool read_trailer(std::string& error) {
        const std::size_t needed = container_ == Container::gzip ? 8 : 4;
        std::uint8_t trailer[8] = {0};
        std::size_t filled = 0;

        // 1. Bytes already inside the inflater's bit buffer (the stream ends
        //    mid-byte, so the trailer usually starts there).
        std::uint8_t carried[8] = {0};
        std::size_t carried_len = 0;
        inflater_.peek_pending(carried, carried_len);
        if (carried_len > needed) carried_len = needed;
        if (carried_len > 0) {
            std::memcpy(trailer, carried, carried_len);
            inflater_.drop_pending(carried_len);
            filled = carried_len;
        } else {
            inflater_.drop_pending(0);   // discard the trailing padding bits
        }
        // 2. Bytes fed to the inflater but never consumed.
        if (filled < needed) {
            const std::size_t left = inflater_.leftover_len();
            if (left > 0) {
                const std::size_t take = left < (needed - filled) ? left : (needed - filled);
                std::memcpy(trailer + filled, inflater_.leftover(), take);
                inflater_.drop_leftover(take);
                filled += take;
            }
        }
        // 3. Bytes still upstream.
        while (filled < needed) {
            if (!need_bytes(1, error)) {
                if (error.empty()) {
                    error = container_ == Container::gzip
                                ? "gzip: trailer truncado (CRC/ISIZE ausentes)"
                                : "zlib: Adler-32 ausente (fluxo truncado)";
                }
                return false;
            }
            trailer[filled++] = pending_[pending_pos_++];
        }

        if (container_ == Container::gzip) {
            checksum_expected_ = static_cast<std::uint32_t>(trailer[0]) |
                                 (static_cast<std::uint32_t>(trailer[1]) << 8) |
                                 (static_cast<std::uint32_t>(trailer[2]) << 16) |
                                 (static_cast<std::uint32_t>(trailer[3]) << 24);
            const std::uint32_t expected_isize = static_cast<std::uint32_t>(trailer[4]) |
                                                 (static_cast<std::uint32_t>(trailer[5]) << 8) |
                                                 (static_cast<std::uint32_t>(trailer[6]) << 16) |
                                                 (static_cast<std::uint32_t>(trailer[7]) << 24);
            if (expected_isize != static_cast<std::uint32_t>(size_)) {
                error = "gzip: ISIZE nao confere (esperado " + std::to_string(expected_isize) +
                        ", obtido " + std::to_string(size_) + ")";
                return false;
            }
        } else {
            checksum_expected_ = (static_cast<std::uint32_t>(trailer[0]) << 24) |
                                 (static_cast<std::uint32_t>(trailer[1]) << 16) |
                                 (static_cast<std::uint32_t>(trailer[2]) << 8) |
                                 static_cast<std::uint32_t>(trailer[3]);
        }
        integrity_checked_ = true;
        if (checksum_expected_ != checksum_computed_) {
            error = std::string(container_name()) + ": checksum nao confere (esperado " +
                    std::to_string(checksum_expected_) + ", obtido " +
                    std::to_string(checksum_computed_) + ")";
            return false;
        }
        std::uint8_t tail[8] = {0};
        std::size_t tail_len = 0;
        inflater_.peek_pending(tail, tail_len);
        extra_ = tail_len > 0 || inflater_.leftover_len() > 0 || pending_pos_ < pending_.size();
        mode_ = Mode::finished;
        return true;
    }

    bool fill(std::string& error) {
        for (;;) {
            if (mode_ == Mode::finished) return true;
            if (mode_ == Mode::header) {
                if (!read_header(error)) return false;
                continue;
            }
            // Decoded bytes are delivered before the trailer is read: the inflater
            // can report 'done' with its whole output still buffered, and dropping
            // it there would cost the archive its last window.
            if (inflater_.out_len() > 0) return true;
            if (mode_ == Mode::trailer) {
                if (!read_trailer(error)) return false;
                continue;
            }
            const Inflater::Status status = inflater_.run(error);
            if (status == Inflater::Status::failed) return false;
            if (status == Inflater::Status::done) {
                mode_ = Mode::trailer;
                continue;
            }
            if (status == Inflater::Status::output_ready) continue;

            if (eof_) {
                error = "gzip: fluxo deflate truncado";
                return false;
            }
            std::uint8_t chunk[kCopyChunk];
            std::size_t received = 0;
            if (!upstream_->read(chunk, sizeof(chunk), received, error)) return false;
            if (received == 0) {
                eof_ = true;
                inflater_.set_input_eof(true);
            } else {
                inflater_.feed(chunk, received);
            }
        }
    }

    std::unique_ptr<Reader> upstream_;
    Inflater inflater_;
    std::vector<std::uint8_t> pending_;
    std::size_t pending_pos_ = 0;
    Mode mode_ = Mode::header;
    Container container_ = Container::unknown;
    bool eof_ = false;
    bool extra_ = false;
    bool integrity_checked_ = false;
    std::uint32_t checksum_expected_ = 0;
    std::uint32_t checksum_computed_ = 0;
    std::uint64_t size_ = 0;
    std::uint64_t produced_ = 0;
};

// ---------------------------------------------------------------------------
// tar
// ---------------------------------------------------------------------------

bool parse_octal(const char* field, std::size_t len, std::uint64_t& out) {
    out = 0;
    std::size_t i = 0;
    while (i < len && (field[i] == ' ' || field[i] == '\0')) ++i;
    if (i < len && (static_cast<unsigned char>(field[i]) & 0x80) != 0) {
        // GNU base-256: the high bit marks a binary value, rest is big-endian.
        out = static_cast<std::uint64_t>(static_cast<unsigned char>(field[i]) & 0x7F);
        for (++i; i < len; ++i) out = (out << 8) | static_cast<unsigned char>(field[i]);
        return true;
    }
    bool any = false;
    for (; i < len; ++i) {
        const char c = field[i];
        if (c == '\0' || c == ' ') break;
        if (c < '0' || c > '7') return false;
        out = out * 8 + static_cast<std::uint64_t>(c - '0');
        any = true;
    }
    return any;
}

std::string field_string(const char* field, std::size_t len) {
    std::size_t n = 0;
    while (n < len && field[n] != '\0') ++n;
    return std::string(field, n);
}

// Rejects anything that would land outside the extraction root: absolute paths,
// drive prefixes and '..' segments. Rejected names are reported, never written.
bool safe_relative(const std::string& raw, std::string& cleaned) {
    cleaned.clear();
    if (raw.empty()) return false;
    std::string path;
    for (const char c : raw) path.push_back(c == '\\' ? '/' : c);
    if (path.size() >= 2 && path[1] == ':') return false;
    if (path[0] == '/') return false;

    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i <= path.size()) {
        const std::size_t slash = path.find('/', i);
        const std::string part = path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty() && part != ".") {
            if (part == "..") return false;
            parts.push_back(part);
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    if (parts.empty()) return false;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (k) cleaned.push_back('\\');
        cleaned += parts[k];
    }
    return true;
}

std::wstring parent_of(const std::wstring& path) {
    const std::size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : path.substr(0, cut);
}

void apply_unix_mtime(HANDLE file, std::uint64_t mtime) {
    if (mtime == 0) return;
    const std::uint64_t ticks = (mtime + 11644473600ull) * 10000000ull;
    FILETIME ft{};
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFull);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    SetFileTime(file, nullptr, nullptr, &ft);
}

class TarExtractor {
public:
    bool run(Reader& in, const std::wstring& out_dir, TarStats& stats, std::string& error) {
        std::vector<std::pair<std::wstring, unsigned>> dir_modes;
        std::string long_name;
        std::string pax_path;
        std::uint64_t pax_size = 0;
        bool pax_has_size = false;
        std::uint8_t header[512];

        for (;;) {
            std::size_t got = 0;
            if (!in.read(header, sizeof(header), got, error)) return false;
            if (got == 0) break;   // clean EOF without the usual trailer
            if (got < sizeof(header)) {
                if (!read_exact(in, header + got, sizeof(header) - got, error)) return false;
            }
            bool all_zero = true;
            for (std::size_t i = 0; i < sizeof(header); ++i) {
                if (header[i] != 0) {
                    all_zero = false;
                    break;
                }
            }
            if (all_zero) break;   // end-of-archive marker

            const std::string stored =
                field_string(reinterpret_cast<const char*>(header) + 148, 8);
            std::uint64_t stored_sum = 0;
            if (!parse_octal(stored.c_str(), stored.size(), stored_sum)) {
                error = "tar: campo de checksum ilegivel";
                return false;
            }
            std::uint64_t unsigned_sum = 0;
            std::int64_t signed_sum = 0;
            for (std::size_t i = 0; i < sizeof(header); ++i) {
                const unsigned char byte =
                    (i >= 148 && i < 156) ? static_cast<unsigned char>(' ') : header[i];
                unsigned_sum += byte;
                signed_sum += static_cast<signed char>(byte);
            }
            if (stored_sum != unsigned_sum && static_cast<std::int64_t>(stored_sum) != signed_sum) {
                ++stats.bad_headers;
                error = "tar: checksum do cabecalho nao confere (bloco corrompido)";
                return false;
            }
            ++stats.entries;

            std::string name = field_string(reinterpret_cast<const char*>(header), 100);
            const std::string prefix = field_string(reinterpret_cast<const char*>(header) + 345, 155);
            if (!prefix.empty()) name = prefix + "/" + name;
            std::uint64_t size = 0;
            const std::string size_field(reinterpret_cast<const char*>(header) + 124, 12);
            if (!parse_octal(size_field.c_str(), size_field.size(), size)) {
                error = "tar: campo de tamanho ilegivel em '" + name + "'";
                return false;
            }
            std::uint64_t mode = 0;
            const std::string mode_field(reinterpret_cast<const char*>(header) + 100, 8);
            parse_octal(mode_field.c_str(), mode_field.size(), mode);
            std::uint64_t mtime = 0;
            const std::string mtime_field(reinterpret_cast<const char*>(header) + 136, 12);
            parse_octal(mtime_field.c_str(), mtime_field.size(), mtime);
            const char type = static_cast<char>(header[156]);
            const std::string link = field_string(reinterpret_cast<const char*>(header) + 157, 100);

            if (type == 'L' || type == 'K') {
                std::string text;
                if (!read_blob(in, size, text, error)) return false;
                if (type == 'L') long_name = text;
                if (!skip_padding(in, size, error)) return false;
                continue;
            }
            if (type == 'x' || type == 'g') {
                std::string text;
                if (!read_blob(in, size, text, error)) return false;
                parse_pax(text, pax_path, pax_size, pax_has_size);
                if (!skip_padding(in, size, error)) return false;
                continue;
            }

            if (!long_name.empty()) name = long_name;
            else if (!pax_path.empty()) name = pax_path;
            if (pax_has_size) size = pax_size;
            long_name.clear();
            pax_path.clear();
            pax_has_size = false;

            std::string relative;
            if (!safe_relative(name, relative)) {
                stats.rejected.push_back(name);
                ++stats.skipped;
                if (!skip_data(in, size, error)) return false;
                continue;
            }

            const std::wstring target = out_dir + L"\\" + widen(relative);
            if (type == '5') {
                if (!make_dirs(target)) {
                    error = "tar: nao foi possivel criar o diretorio " + narrow(target);
                    return false;
                }
                ++stats.dirs;
                dir_modes.emplace_back(target, static_cast<unsigned>(mode));
                continue;
            }
            if (type == '1' || type == '2') {
                ++stats.links;
                if (!skip_data(in, size, error)) return false;
                continue;
            }
            if (type != '0' && type != '\0' && type != '7') {
                ++stats.skipped;
                if (!skip_data(in, size, error)) return false;
                continue;
            }

            if (!make_dirs(parent_of(target))) {
                error = "tar: nao foi possivel criar " + narrow(parent_of(target));
                return false;
            }
            if (!write_file(in, target, size, mode, mtime, stats, error)) return false;
            if (!skip_padding(in, size, error)) return false;
        }

        // Directory modes are applied last: a read-only directory would block the
        // very writes that have to happen inside it.
        for (const auto& entry : dir_modes) {
            if ((entry.second & 0200) == 0) {
                SetFileAttributesW(long_path(entry.first).c_str(),
                                   GetFileAttributesW(long_path(entry.first).c_str()) |
                                       FILE_ATTRIBUTE_READONLY);
            }
        }
        return true;
    }

private:
    static void parse_pax(const std::string& text, std::string& path, std::uint64_t& size,
                          bool& has_size) {
        std::size_t i = 0;
        while (i < text.size()) {
            const std::size_t space = text.find(' ', i);
            if (space == std::string::npos) break;
            std::uint64_t length = 0;
            for (std::size_t k = i; k < space; ++k) {
                if (text[k] < '0' || text[k] > '9') break;
                length = length * 10 + static_cast<std::uint64_t>(text[k] - '0');
            }
            if (length == 0 || i + length > text.size()) break;
            const std::string record = text.substr(space + 1, i + length - space - 2);
            if (record.compare(0, 5, "path=") == 0) {
                path = record.substr(5);
            } else if (record.compare(0, 5, "size=") == 0) {
                std::uint64_t value = 0;
                for (const char c : record.substr(5)) {
                    if (c < '0' || c > '9') break;
                    value = value * 10 + static_cast<std::uint64_t>(c - '0');
                }
                size = value;
                has_size = true;
            }
            i += length;
        }
    }

    static bool read_blob(Reader& in, std::uint64_t size, std::string& out, std::string& error) {
        out.clear();
        out.reserve(static_cast<std::size_t>(size));
        std::uint8_t buffer[4096];
        std::uint64_t remaining = size;
        while (remaining > 0) {
            const std::size_t want =
                remaining < sizeof(buffer) ? static_cast<std::size_t>(remaining) : sizeof(buffer);
            if (!read_exact(in, buffer, want, error)) return false;
            out.append(reinterpret_cast<const char*>(buffer), want);
            remaining -= want;
        }
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return true;
    }

    static bool skip_padding(Reader& in, std::uint64_t size, std::string& error) {
        const std::uint64_t pad = (512 - (size % 512)) % 512;
        if (pad == 0) return true;
        std::uint8_t buffer[512];
        return read_exact(in, buffer, static_cast<std::size_t>(pad), error);
    }

    static bool skip_data(Reader& in, std::uint64_t size, std::string& error) {
        std::uint8_t buffer[8192];
        std::uint64_t remaining = size;
        while (remaining > 0) {
            const std::size_t want =
                remaining < sizeof(buffer) ? static_cast<std::size_t>(remaining) : sizeof(buffer);
            if (!read_exact(in, buffer, want, error)) return false;
            remaining -= want;
        }
        return skip_padding(in, size, error);
    }

    static bool write_file(Reader& in, const std::wstring& path, std::uint64_t size, unsigned mode,
                           std::uint64_t mtime, TarStats& stats, std::string& error) {
        HANDLE file = CreateFileW(long_path(path).c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES, 0,
                                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = "tar: nao foi possivel criar " + narrow(path) + ": " + win_error(GetLastError());
            return false;
        }
        std::uint8_t buffer[kCopyChunk];
        std::uint64_t remaining = size;
        std::uint64_t written = 0;
        bool ok = true;
        while (remaining > 0) {
            const std::size_t want =
                remaining < sizeof(buffer) ? static_cast<std::size_t>(remaining) : sizeof(buffer);
            if (!read_exact(in, buffer, want, error)) {
                ok = false;
                break;
            }
            DWORD put = 0;
            if (!WriteFile(file, buffer, static_cast<DWORD>(want), &put, nullptr) || put != want) {
                error = "tar: falha de escrita em " + narrow(path) + ": " + win_error(GetLastError());
                ok = false;
                break;
            }
            written += want;
            remaining -= want;
        }
        apply_unix_mtime(file, mtime);
        CloseHandle(file);
        if (!ok) return false;
        stats.bytes_written += written;
        ++stats.files;
        if ((mode & 0200) == 0) {
            SetFileAttributesW(long_path(path).c_str(),
                               GetFileAttributesW(long_path(path).c_str()) | FILE_ATTRIBUTE_READONLY);
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// .ab header
// ---------------------------------------------------------------------------

class LineReader {
public:
    ~LineReader() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }

    bool open(const std::wstring& path, std::string& error) {
        file_ = CreateFileW(long_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            error = "nao foi possivel abrir " + narrow(path) + ": " + win_error(GetLastError());
            return false;
        }
        LARGE_INTEGER size{};
        if (GetFileSizeEx(file_, &size)) total_ = static_cast<std::uint64_t>(size.QuadPart);
        return true;
    }

    bool read_line(std::string& line, std::string& error) {
        line.clear();
        for (;;) {
            for (; pos_ < len_; ++pos_) {
                const char c = static_cast<char>(buffer_[pos_]);
                ++consumed_;
                if (c == '\n') {
                    ++pos_;
                    return true;
                }
                line.push_back(c);
                if (line.size() > 8192) {
                    error = "linha de cabecalho com mais de 8192 bytes";
                    return false;
                }
            }
            DWORD got = 0;
            if (!ReadFile(file_, buffer_, static_cast<DWORD>(sizeof(buffer_)), &got, nullptr)) {
                error = "falha de leitura do cabecalho: " + win_error(GetLastError());
                return false;
            }
            if (got == 0) {
                error = "arquivo termina dentro do cabecalho";
                return false;
            }
            len_ = got;
            pos_ = 0;
        }
    }

    std::uint64_t consumed() const { return consumed_; }
    std::uint64_t total() const { return total_; }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::uint8_t buffer_[4096] = {0};
    std::size_t pos_ = 0;
    std::size_t len_ = 0;
    std::uint64_t consumed_ = 0;
    std::uint64_t total_ = 0;
};

bool parse_decimal(const std::string& text, long& value) {
    if (text.empty()) return false;
    long parsed = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        parsed = parsed * 10 + (c - '0');
        if (parsed > 100000000L) return false;
    }
    value = parsed;
    return true;
}

// Java's PBEKeySpec takes a char[]; the JDK encodes it either as UTF-8 (the
// modern PBKDF2WithHmacSHA1) or by truncating each char to 8 bits (the
// PBKDF2WithHmacSHA1And8bit fallback AOSP still carries for old backups).
std::vector<std::uint32_t> utf8_codepoints(const std::string& text) {
    std::vector<std::uint32_t> points;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t value = 0;
        if (c < 0x80) {
            points.push_back(c);
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            value = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            value = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            value = c & 0x07;
        } else {
            points.push_back(c);
            ++i;
            continue;
        }
        if (i + extra >= text.size()) {
            points.push_back(c);
            ++i;
            continue;
        }
        bool good = true;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) {
                good = false;
                break;
            }
            value = (value << 6) | (next & 0x3F);
        }
        if (!good) {
            points.push_back(c);
            ++i;
            continue;
        }
        points.push_back(value);
        i += extra + 1;
    }
    return points;
}

std::string utf8_from_codepoints(const std::vector<std::uint32_t>& points) {
    std::string out;
    for (const std::uint32_t cp : points) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string password_utf8(const std::string& utf8) { return utf8_from_codepoints(utf8_codepoints(utf8)); }

std::string password_8bit(const std::string& utf8) {
    std::string out;
    for (const std::uint32_t cp : utf8_codepoints(utf8)) out.push_back(static_cast<char>(cp & 0xFF));
    return out;
}

// 'char c = (char) byte' sign-extends in Java, so a byte >= 0x80 becomes a code
// point in U+FF80..U+FFFF, not U+0080..U+00FF. The checksum depends on that.
std::string java_chars_utf8(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint32_t> points;
    points.reserve(bytes.size());
    for (const std::uint8_t byte : bytes) {
        points.push_back(byte < 0x80 ? byte : (0xFF00u | byte));
    }
    return utf8_from_codepoints(points);
}

std::string java_chars_8bit(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const std::uint8_t byte : bytes) out.push_back(static_cast<char>(byte));
    return out;
}

bool blob_unwrap(const std::vector<std::uint8_t>& user_key, const std::vector<std::uint8_t>& iv,
                 const std::vector<std::uint8_t>& encrypted, std::vector<std::uint8_t>& plain,
                 std::string& error) {
    olho::wa::crypto::aes_cbc cipher(user_key.data(), user_key.size());
    if (!cipher.ok()) {
        error = "CNG recusou a chave AES-256 do usuario";
        return false;
    }
    plain = encrypted;
    std::vector<std::uint8_t> chain = iv;
    chain.resize(16, 0);
    if (!cipher.decrypt_update(plain.data(), plain.size(), chain.data())) {
        error = "AES-CBC falhou ao decifrar o blob da chave mestra";
        return false;
    }
    std::size_t plain_len = 0;
    if (!strip_pkcs7(plain.data(), plain.size(), plain_len, error)) return false;
    plain.resize(plain_len);
    return true;
}

}  // namespace

std::string AbHeader::describe() const {
    std::string text = "v" + std::to_string(version) + " compressao=" + (compressed ? "1" : "0") +
                       " algoritmo=" + algorithm;
    if (encrypted) {
        text += " rounds=" + std::to_string(rounds);
        text += " salt=" + std::to_string(user_salt.size()) + "B";
        text += " cksalt=" + std::to_string(checksum_salt.size()) + "B";
        text += " iv=" + std::to_string(user_iv.size()) + "B";
        text += " blob=" + std::to_string(master_blob.size()) + "B";
    }
    text += " payload_em=" + std::to_string(header_bytes);
    return text;
}

bool read_ab_header(const std::wstring& ab_path, AbHeader& header, std::string& error) {
    header = AbHeader{};
    error.clear();

    LineReader reader;
    if (!reader.open(ab_path, error)) return false;

    std::string line;
    if (!reader.read_line(line, error)) return false;
    if (line != "ANDROID BACKUP") {
        error = "magica invalida: esperado 'ANDROID BACKUP', encontrado '" + line + "'";
        return false;
    }
    if (!reader.read_line(line, error)) return false;
    long version = 0;
    if (!parse_decimal(line, version)) {
        error = "versao ilegivel: '" + line + "'";
        return false;
    }
    if (version < 1 || version > 5) {
        error = "versao " + std::to_string(version) + " nao suportada (1..5)";
        return false;
    }
    header.version = static_cast<int>(version);

    if (!reader.read_line(line, error)) return false;
    if (line == "1") header.compressed = true;
    else if (line == "0") header.compressed = false;
    else {
        error = "flag de compressao invalida: '" + line + "' (esperado '0' ou '1')";
        return false;
    }

    if (!reader.read_line(line, error)) return false;
    header.algorithm = line;
    if (line == "none") {
        header.encrypted = false;
        header.header_bytes = reader.consumed();
        return true;
    }
    if (line != "AES-256") {
        error = "algoritmo desconhecido na linha 4: '" + line + "'";
        if (line == "1" || line == "0") {
            error +=
                " - a linha 4 do .ab e o nome do algoritmo, nao uma flag booleana; "
                "esse layout de 4 linhas nao existe em nenhuma versao do AOSP";
        }
        return false;
    }
    header.encrypted = true;

    if (!reader.read_line(line, error)) return false;
    if (!hex_decode(line, header.user_salt, error)) {
        error = "salt do usuario: " + error;
        return false;
    }
    if (!reader.read_line(line, error)) return false;
    if (!hex_decode(line, header.checksum_salt, error)) {
        error = "salt do checksum: " + error;
        return false;
    }
    if (!reader.read_line(line, error)) return false;
    long rounds = 0;
    if (!parse_decimal(line, rounds) || rounds < 1) {
        error = "numero de rounds ilegivel: '" + line + "'";
        return false;
    }
    if (rounds > 10000000L) {
        error = "numero de rounds absurdo: " + std::to_string(rounds);
        return false;
    }
    header.rounds = static_cast<int>(rounds);

    if (!reader.read_line(line, error)) return false;
    if (!hex_decode(line, header.user_iv, error)) {
        error = "IV do usuario: " + error;
        return false;
    }
    if (header.user_iv.size() != 16) {
        error = "IV do usuario com " + std::to_string(header.user_iv.size()) + " bytes (esperado 16)";
        return false;
    }
    if (!reader.read_line(line, error)) return false;
    if (!hex_decode(line, header.master_blob, error)) {
        error = "blob da chave mestra: " + error;
        return false;
    }
    if (header.master_blob.size() < 16 || (header.master_blob.size() % 16) != 0) {
        error = "blob da chave mestra com tamanho invalido (" +
                std::to_string(header.master_blob.size()) + " bytes)";
        return false;
    }
    header.header_bytes = reader.consumed();
    return true;
}

bool unwrap_master_key(const AbHeader& header, const std::string& password, MasterKey& out,
                       std::string& error) {
    out = MasterKey{};
    error.clear();
    if (!header.encrypted) {
        error = "arquivo nao cifrado: nao ha chave mestra a extrair";
        return false;
    }
    if (password.empty()) {
        error = "arquivo cifrado: a senha do backup e obrigatoria";
        return false;
    }

    struct variant {
        const char* name;
        std::string password_bytes;
    };
    const variant variants[2] = {{"utf8", password_utf8(password)}, {"8bit", password_8bit(password)}};

    std::string last_reason;
    for (const auto& candidate : variants) {
        std::vector<std::uint8_t> user_key(32, 0);
        if (!pbkdf2_sha1(candidate.password_bytes, header.user_salt,
                         static_cast<std::uint32_t>(header.rounds), user_key.data(), user_key.size(),
                         error)) {
            return false;
        }
        std::vector<std::uint8_t> blob;
        std::string blob_error;
        if (!blob_unwrap(user_key, header.user_iv, header.master_blob, blob, blob_error)) {
            last_reason = blob_error;
            continue;
        }
        std::size_t offset = 0;
        if (blob.size() < 3) {
            last_reason = "blob da chave mestra menor que o minimo";
            continue;
        }
        const std::size_t iv_len = blob[offset++];
        if (offset + iv_len > blob.size()) {
            last_reason = "blob truncado no IV";
            continue;
        }
        std::vector<std::uint8_t> payload_iv(blob.begin() + static_cast<std::ptrdiff_t>(offset),
                                             blob.begin() + static_cast<std::ptrdiff_t>(offset + iv_len));
        offset += iv_len;
        if (offset >= blob.size()) {
            last_reason = "blob truncado no tamanho da chave";
            continue;
        }
        const std::size_t key_len = blob[offset++];
        if (offset + key_len > blob.size()) {
            last_reason = "blob truncado na chave mestra";
            continue;
        }
        std::vector<std::uint8_t> master_key(blob.begin() + static_cast<std::ptrdiff_t>(offset),
                                             blob.begin() + static_cast<std::ptrdiff_t>(offset + key_len));
        offset += key_len;
        if (offset >= blob.size()) {
            last_reason = "blob truncado no tamanho do checksum";
            continue;
        }
        const std::size_t checksum_len = blob[offset++];
        if (offset + checksum_len > blob.size()) {
            last_reason = "blob truncado no checksum";
            continue;
        }
        std::vector<std::uint8_t> stored_checksum(
            blob.begin() + static_cast<std::ptrdiff_t>(offset),
            blob.begin() + static_cast<std::ptrdiff_t>(offset + checksum_len));

        if (master_key.size() != 32) {
            last_reason = "chave mestra com " + std::to_string(master_key.size()) + " bytes (esperado 32)";
            continue;
        }
        if (payload_iv.size() != 16) {
            last_reason = "IV do payload com " + std::to_string(payload_iv.size()) + " bytes";
            continue;
        }

        const std::string checksum_password = std::string(candidate.name) == "utf8"
                                                  ? java_chars_utf8(master_key)
                                                  : java_chars_8bit(master_key);
        std::vector<std::uint8_t> computed(stored_checksum.size() == 0 ? 32 : stored_checksum.size(), 0);
        if (!pbkdf2_sha1(checksum_password, header.checksum_salt,
                         static_cast<std::uint32_t>(header.rounds), computed.data(), computed.size(),
                         error)) {
            return false;
        }
        if (computed.size() == stored_checksum.size() &&
            std::memcmp(computed.data(), stored_checksum.data(), computed.size()) == 0) {
            out.key = master_key;
            out.iv = payload_iv;
            out.verified = true;
            out.encoding = candidate.name;
            return true;
        }
        last_reason = "checksum da chave mestra nao confere com a codificacao " +
                      std::string(candidate.name);
    }

    error = "senha incorreta: " + (last_reason.empty() ? std::string("checksum nao confere") : last_reason);
    return false;
}

bool inflate_deflate(const std::uint8_t* data, std::size_t length, std::string& out,
                     std::string& error) {
    out.clear();
    error.clear();
    if (data == nullptr || length == 0) {
        error = "deflate vazio";
        return false;
    }
    Inflater inflater;
    inflater.reset();
    inflater.feed(data, length);
    // Nao havera mais entrada: sem isto o ultimo codigo curto nao decodifica.
    inflater.set_input_eof(true);
    for (;;) {
        const Inflater::Status status = inflater.run(error);
        if (inflater.out_len() > 0) {
            out.append(reinterpret_cast<const char*>(inflater.out_ptr()), inflater.out_len());
            inflater.consume(inflater.out_len());
        }
        if (status == Inflater::Status::done) return true;
        if (status == Inflater::Status::failed) {
            if (error.empty()) error = "deflate invalido";
            return false;
        }
        if (status == Inflater::Status::need_input) {
            error = "deflate truncado";
            return false;
        }
        // output_ready: ha mais saida a drenar.
    }
}

std::string sha256_hex(const void* data, std::size_t len) {
    Sha256 hash;
    std::string error;
    if (!hash.init(error)) return {};
    if (!hash.update(data, len)) return {};
    std::string hex;
    if (!hash.finish(hex, error)) return {};
    return hex;
}

bool sha256_file(const std::wstring& path, std::string& hex, std::string& error) {
    hex.clear();
    error.clear();
    HANDLE file = CreateFileW(long_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir " + narrow(path) + ": " + win_error(GetLastError());
        return false;
    }
    Sha256 hash;
    if (!hash.init(error)) {
        CloseHandle(file);
        return false;
    }
    std::vector<std::uint8_t> buffer(kCopyChunk);
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
            error = "falha de leitura em " + narrow(path) + ": " + win_error(GetLastError());
            CloseHandle(file);
            return false;
        }
        if (got == 0) break;
        if (!hash.update(buffer.data(), got)) {
            error = "falha ao atualizar o SHA-256";
            CloseHandle(file);
            return false;
        }
    }
    CloseHandle(file);
    return hash.finish(hex, error);
}

namespace {

bool drain_reader(Reader& reader, std::string& error) {
    std::uint8_t buffer[kCopyChunk];
    for (;;) {
        std::size_t got = 0;
        if (!reader.read(buffer, sizeof(buffer), got, error)) return false;
        if (got == 0) return true;
    }
}

}  // namespace

bool extract_ab(const std::wstring& ab_path, const std::wstring& out_dir, const std::string& password,
                AbStats& stats) {
    stats = AbStats{};
    if (!read_ab_header(ab_path, stats.header, stats.error)) return false;

    const std::uint64_t total = file_size_of(ab_path);
    if (stats.header.header_bytes > total) {
        stats.error = "cabecalho maior que o arquivo";
        return false;
    }
    stats.payload_bytes = total - stats.header.header_bytes;
    if (stats.payload_bytes == 0) {
        stats.error = "arquivo sem payload";
        return false;
    }
    if (!make_dirs(out_dir)) {
        stats.error = "nao foi possivel criar o diretorio de extracao " + narrow(out_dir);
        return false;
    }

    auto file_reader = std::make_unique<FileReader>();
    if (!file_reader->open(ab_path, stats.header.header_bytes, stats.error)) return false;
    std::unique_ptr<Reader> stream = std::move(file_reader);

    if (stats.header.encrypted) {
        if (!unwrap_master_key(stats.header, password, stats.master, stats.error)) return false;
        auto aes = std::make_unique<AesReader>();
        if (!aes->init(std::move(stream), stats.master.key, stats.master.iv)) {
            stats.error = "NAO foi possivel inicializar o AES-256-CBC (bcrypt)";
            return false;
        }
        stream = std::move(aes);
    }

    if (stats.header.compressed) {
        auto compressed = std::make_unique<CompressedReader>();
        if (!compressed->init(std::move(stream))) {
            stats.error = "falha ao inicializar o leitor comprimido";
            return false;
        }
        TarExtractor tar;
        const bool ok = tar.run(*compressed, out_dir, stats.tar, stats.error);
        if (ok) {
            // Draining is what reaches the container trailer: without it the
            // Adler-32 is never checked and a truncated archive could pass as
            // complete evidence.
            if (!drain_reader(*compressed, stats.error)) return false;
        }
        stats.container = compressed->container_name();
        stats.checksum_expected = compressed->checksum_expected();
        stats.checksum_computed = compressed->checksum_computed();
        stats.integrity_ok = compressed->integrity_ok();
        stats.uncompressed_bytes = compressed->uncompressed_size();
        stats.trailing_data = compressed->has_trailing_data();
        if (!ok) return false;
        if (!stats.integrity_ok) {
            stats.error = "checksum do fluxo comprimido nao verificado";
            return false;
        }
    } else {
        TarExtractor tar;
        if (!tar.run(*stream, out_dir, stats.tar, stats.error)) return false;
    }

    stats.ok = true;
    return true;
}

namespace {

// Internal alias: discovery needs the public record and nothing more.
using Found = WaEntry;

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

std::wstring lower_w(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return out;
}

bool contains_ci(const std::wstring& haystack, const wchar_t* needle) {
    return lower_w(haystack).find(needle) != std::wstring::npos;
}

// Ranking is explicit so the choice is reproducible and explainable: an entry
// under a 'whatsapp' path wins, then a 'db'/'databases' parent, then the newest
// write. Never "the first one the walk happened to return".
int wa_score(const std::wstring& path, bool database_parent) {
    int value = 0;
    if (contains_ci(path, L"whatsapp")) value += 100;
    if (contains_ci(path, L"com.whatsapp")) value += 50;
    if (database_parent && (contains_ci(path, L"\\db\\") || contains_ci(path, L"\\databases\\"))) {
        value += 20;
    }
    return value;
}

std::uint64_t filetime_to_unix(const FILETIME& ft) {
    const std::uint64_t ticks =
        (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    if (ticks < 116444736000000000ull) return 0;
    return (ticks - 116444736000000000ull) / 10000000ull;
}

using visit_fn = std::function<void(const std::wstring&, const WIN32_FIND_DATAW&)>;

bool walk_tree(const std::wstring& root, const visit_fn& visit, std::size_t limit,
               std::string& error) {
    std::vector<std::wstring> stack;
    stack.push_back(root);
    std::size_t visited = 0;
    while (!stack.empty()) {
        const std::wstring dir = stack.back();
        stack.pop_back();
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(long_path(dir + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) continue;
            error = "falha ao listar " + narrow(dir) + ": " + win_error(code);
            return false;
        }
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            if (++visited > limit) {
                FindClose(find);
                error = "arvore maior que o limite de " + std::to_string(limit) + " entradas";
                return false;
            }
            const std::wstring full = dir + L"\\" + name;
            const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool reparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (directory) {
                if (!reparse) stack.push_back(full);
            } else {
                visit(full, data);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return true;
}

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

std::string now_utc_iso() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::string base_name(const std::wstring& path) {
    const std::size_t cut = path.find_last_of(L"\\/");
    return narrow(cut == std::wstring::npos ? path : path.substr(cut + 1));
}

bool copy_verified(const std::wstring& source, const std::wstring& target, std::string& source_hash,
                   std::uint64_t& copied, std::string& error) {
    copied = 0;
    source_hash.clear();
    HANDLE in = CreateFileW(long_path(source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (in == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir " + narrow(source) + ": " + win_error(GetLastError());
        return false;
    }
    if (!make_dirs(parent_of(target))) {
        CloseHandle(in);
        error = "nao foi possivel criar o diretorio de destino de " + narrow(target);
        return false;
    }
    HANDLE out = CreateFileW(long_path(target).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel criar " + narrow(target) + ": " + win_error(GetLastError());
        CloseHandle(in);
        return false;
    }
    Sha256 hash;
    if (!hash.init(error)) {
        CloseHandle(in);
        CloseHandle(out);
        return false;
    }
    std::vector<std::uint8_t> buffer(kCopyChunk);
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(in, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
            error = "falha de leitura em " + narrow(source) + ": " + win_error(GetLastError());
            ok = false;
            break;
        }
        if (got == 0) break;
        if (!hash.update(buffer.data(), got)) {
            error = "falha ao calcular SHA-256 da origem";
            ok = false;
            break;
        }
        DWORD put = 0;
        if (!WriteFile(out, buffer.data(), got, &put, nullptr) || put != got) {
            error = "falha de escrita em " + narrow(target) + ": " + win_error(GetLastError());
            ok = false;
            break;
        }
        copied += got;
    }
    CloseHandle(in);
    CloseHandle(out);
    if (!ok) return false;
    if (!hash.finish(source_hash, error)) return false;
    return true;
}

}  // namespace

bool locate_whatsapp(const std::wstring& root, WaCase& out, std::string& error) {
    out = WaCase{};
    error.clear();
    if (root.empty()) {
        error = "diretorio de origem vazio";
        return false;
    }
    if (!is_dir(root)) {
        error = "diretorio de origem inexistente: " + narrow(root);
        return false;
    }
    out.root = root;

    std::vector<Found> databases;
    std::vector<Found> wals;
    std::vector<Found> shms;
    std::vector<Found> keys;
    std::vector<std::wstring> media_dirs;

    std::string walk_error;
    const bool walked = walk_tree(
        root,
        [&](const std::wstring& full, const WIN32_FIND_DATAW& data) {
            const std::string name = lower_ascii(base_name(full));
            Found found;
            found.path = full;
            found.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            found.mtime = filetime_to_unix(data.ftLastWriteTime);
            if (name == "msgstore.db") databases.push_back(found);
            else if (name == "msgstore.db-wal") wals.push_back(found);
            else if (name == "msgstore.db-shm") shms.push_back(found);
            else if (name == "key" && found.size >= 32) keys.push_back(found);
        },
        2000000, walk_error);
    if (!walked) {
        error = walk_error;
        return false;
    }

    // Media is a directory, not a file: the backup tree of a package has none
    // (external storage is not backed up), so a pulled WhatsApp folder is where
    // it appears.
    {
        std::vector<std::wstring> stack;
        stack.push_back(root);
        std::size_t guard = 0;
        while (!stack.empty()) {
            const std::wstring dir = stack.back();
            stack.pop_back();
            WIN32_FIND_DATAW data{};
            HANDLE find = FindFirstFileW(long_path(dir + L"\\*").c_str(), &data);
            if (find == INVALID_HANDLE_VALUE) continue;
            do {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..") continue;
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
                const std::wstring full = dir + L"\\" + name;
                if (lower_w(name) == L"media") media_dirs.push_back(full);
                if (++guard > 200000) break;
                stack.push_back(full);
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }

    const auto pick = [](std::vector<Found>& items, bool database_parent) {
        if (items.empty()) return Found{};
        std::sort(items.begin(), items.end(), [&](const Found& a, const Found& b) {
            const int sa = wa_score(a.path, database_parent);
            const int sb = wa_score(b.path, database_parent);
            if (sa != sb) return sa > sb;
            if (a.mtime != b.mtime) return a.mtime > b.mtime;
            return a.path < b.path;
        });
        return items.front();
    };

    out.candidates = databases;
    const Found best_db = pick(databases, true);
    out.msgstore = best_db;
    if (best_db.path.empty()) {
        error = "msgstore.db nao encontrado em " + narrow(root);
        return false;
    }

    // WAL and SHM belong to the chosen database: same directory, same base name.
    const std::wstring db_dir = parent_of(best_db.path);
    for (const auto& entry : wals) {
        if (parent_of(entry.path) == db_dir) {
            out.wal = entry;
            break;
        }
    }
    if (out.wal.path.empty()) out.wal = pick(wals, true);
    for (const auto& entry : shms) {
        if (parent_of(entry.path) == db_dir) {
            out.shm = entry;
            break;
        }
    }
    if (out.shm.path.empty()) out.shm = pick(shms, true);

    if (!keys.empty()) {
        std::sort(keys.begin(), keys.end(), [&](const Found& a, const Found& b) {
            const int sa = wa_score(a.path, false);
            const int sb = wa_score(b.path, false);
            if (sa != sb) return sa > sb;
            if (a.size != b.size) return a.size < b.size;   // the key is the smallest match
            return a.path < b.path;
        });
        out.key_file = keys.front();
    }

    if (!media_dirs.empty()) {
        std::sort(media_dirs.begin(), media_dirs.end(), [&](const std::wstring& a, const std::wstring& b) {
            const int sa = wa_score(a, false);
            const int sb = wa_score(b, false);
            if (sa != sb) return sa > sb;
            return a < b;
        });
        out.media_dir = media_dirs.front();
        std::string media_error;
        walk_tree(
            out.media_dir,
            [&](const std::wstring&, const WIN32_FIND_DATAW& data) {
                ++out.media_files;
                out.media_bytes += (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
                                   data.nFileSizeLow;
            },
            2000000, media_error);
    }

    out.ok = true;
    std::string detail = "msgstore.db=" + base_name(out.msgstore.path) + " (" +
                         std::to_string(out.msgstore.size) + " B)";
    detail += out.wal.path.empty() ? "" : " + wal (" + std::to_string(out.wal.size) + " B)";
    detail += out.shm.path.empty() ? "" : " + shm";
    detail += out.key_file.path.empty() ? "; key AUSENTE (o SQLCipher nao podera ser aberto)"
                                        : "; key=" + std::to_string(out.key_file.size) + " B";
    detail += out.media_dir.empty() ? "; Media ausente (backup de pacote nao inclui /sdcard)"
                                    : "; Media=" + std::to_string(out.media_files) + " arquivos";
    if (out.candidates.size() > 1) {
        detail += "; " + std::to_string(out.candidates.size()) + " copias de msgstore.db";
    }
    out.detail = detail;
    return true;
}

bool build_whatsapp_case(const std::wstring& source_root, const std::wstring& out_dir,
                         const std::string& label, CaseStats& stats) {
    stats = CaseStats{};
    WaCase found;
    if (!locate_whatsapp(source_root, found, stats.error)) return false;

    stats.whatsapp_dir = out_dir + L"\\whatsapp";
    if (!make_dirs(stats.whatsapp_dir)) {
        stats.error = "nao foi possivel criar " + narrow(stats.whatsapp_dir);
        return false;
    }

    struct placed {
        std::string role;
        std::wstring source;
        std::string relative;
        std::uint64_t size = 0;
        std::string sha256;
    };
    std::vector<placed> entries;

    const auto place = [&](const std::string& role, const Found& item, const std::wstring& relative) {
        if (item.path.empty()) return true;
        const std::wstring target = stats.whatsapp_dir + L"\\" + relative;
        std::string source_hash;
        std::uint64_t copied = 0;
        if (!copy_verified(item.path, target, source_hash, copied, stats.error)) return false;

        std::string target_hash;
        if (!sha256_file(target, target_hash, stats.error)) return false;
        if (target_hash != source_hash) {
            ++stats.hash_mismatches;
            stats.error = "copia nao identica para " + narrow(relative) + ": origem " + source_hash +
                          ", destino " + target_hash;
            return false;
        }
        placed row;
        row.role = role;
        row.source = item.path;
        row.relative = narrow(relative);
        row.size = copied;
        row.sha256 = source_hash;
        entries.push_back(row);
        stats.hashes.push_back(source_hash + "  " + row.relative);
        ++stats.files;
        stats.bytes += copied;
        return true;
    };

    if (!place("msgstore", found.msgstore, L"msgstore.db")) return false;
    if (!place("msgstore-wal", found.wal, L"msgstore.db-wal")) return false;
    if (!place("msgstore-shm", found.shm, L"msgstore.db-shm")) return false;
    if (!place("key", found.key_file, L"key")) return false;

    std::vector<std::pair<std::string, std::uint64_t>> media_rows;
    if (!found.media_dir.empty()) {
        const std::wstring prefix = found.media_dir + L"\\";
        std::string media_error;
        bool ok = true;
        walk_tree(
            found.media_dir,
            [&](const std::wstring& full, const WIN32_FIND_DATAW&) {
                if (!ok) return;
                if (full.size() <= prefix.size()) return;
                const std::wstring relative = full.substr(prefix.size());
                Found item;
                item.path = full;
                item.size = file_size_of(full);
                ok = place("media", item, L"Media\\" + relative);
                if (ok) {
                    ++stats.media_files;
                    stats.media_bytes += item.size;
                }
            },
            2000000, media_error);
        if (!ok) return false;
        if (!media_error.empty()) {
            stats.error = media_error;
            return false;
        }
    }

    // The exact invocation, so the case can be reproduced without reading code.
    const std::wstring recover_out = out_dir + L"\\whatsapp_recover";
    stats.command = "wa_recover.exe \"" + narrow(stats.whatsapp_dir + L"\\msgstore.db") +
                    "\" --key \"" + narrow(stats.whatsapp_dir + L"\\key") + "\" --out \"" +
                    narrow(recover_out) + "\"";
    if (!found.media_dir.empty()) stats.command += " --media \"" + narrow(stats.whatsapp_dir) + "\"";

    // Manifest: provenance is part of the evidence, so the source of every copy
    // is recorded next to its hash.
    std::string json = "{\n";
    json += "  \"tool\": \"olho-de-deus/android_backup\",\n";
    json += "  \"label\": \"" + json_escape(label) + "\",\n";
    json += "  \"generated_utc\": \"" + now_utc_iso() + "\",\n";
    json += "  \"source_root\": \"" + json_escape(narrow(source_root)) + "\",\n";
    json += "  \"whatsapp_dir\": \"" + json_escape(narrow(stats.whatsapp_dir)) + "\",\n";
    json += "  \"discovery\": \"" + json_escape(found.detail) + "\",\n";
    json += "  \"msgstore_copies_seen\": " + std::to_string(found.candidates.size()) + ",\n";
    json += "  \"media\": {\"files\": " + std::to_string(stats.media_files) +
            ", \"bytes\": " + std::to_string(stats.media_bytes) + "},\n";
    json += "  \"files\": [\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& row = entries[i];
        json += "    {\"role\": \"" + json_escape(row.role) + "\", \"relative\": \"" +
                json_escape(row.relative) + "\", \"size\": " + std::to_string(row.size) +
                ", \"sha256\": \"" + row.sha256 + "\", \"source\": \"" +
                json_escape(narrow(row.source)) + "\"}";
        json += (i + 1 == entries.size()) ? "\n" : ",\n";
    }
    json += "  ],\n";
    json += "  \"wa_recover_command\": \"" + json_escape(stats.command) + "\"\n";
    json += "}\n";

    stats.manifest_path = out_dir + L"\\case_manifest.json";
    HANDLE manifest = CreateFileW(long_path(stats.manifest_path).c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (manifest == INVALID_HANDLE_VALUE) {
        stats.error = "nao foi possivel gravar o manifesto: " + win_error(GetLastError());
        return false;
    }
    DWORD put = 0;
    WriteFile(manifest, json.data(), static_cast<DWORD>(json.size()), &put, nullptr);
    CloseHandle(manifest);

    stats.command_path = out_dir + L"\\wa_recover.txt";
    HANDLE script = CreateFileW(long_path(stats.command_path).c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (script != INVALID_HANDLE_VALUE) {
        const std::string body = stats.command + "\r\n";
        WriteFile(script, body.data(), static_cast<DWORD>(body.size()), &put, nullptr);
        CloseHandle(script);
    }

    stats.ok = (stats.hash_mismatches == 0);
    if (!stats.ok && stats.error.empty()) stats.error = "houve copias nao identicas";
    return stats.ok;
}

bool load_wa_key(const std::wstring& key_file, olho::wa::key_material& out, std::string& error) {
    out = olho::wa::key_material{};
    error.clear();
    HANDLE file = CreateFileW(long_path(key_file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir a chave " + narrow(key_file) + ": " + win_error(GetLastError());
        return false;
    }
    std::uint8_t buffer[64] = {0};
    DWORD got = 0;
    const BOOL read_ok = ReadFile(file, buffer, sizeof(buffer), &got, nullptr);
    CloseHandle(file);
    if (!read_ok) {
        error = "falha de leitura da chave: " + win_error(GetLastError());
        return false;
    }
    if (got < 32) {
        error = "arquivo de chave com " + std::to_string(got) + " bytes (o WhatsApp grava 32)";
        return false;
    }
    out.raw_key.assign(buffer, buffer + 32);
    return true;
}

}  // namespace olho::android
