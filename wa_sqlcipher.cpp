// language: C++, file: wa_sqlcipher.cpp, target: Windows 11 (MSVC / MinGW g++)
// SQLCipher acquisition core. See wa_sqlcipher.hpp for the design rationale.
#include "wa_sqlcipher.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#ifndef BCRYPT_SUCCESS
#define BCRYPT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace olho::wa {
namespace {

constexpr std::uint8_t kSqliteMagic[16] = {
    'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f',
    'o', 'r', 'm', 'a', 't', ' ', '3', 0};

// SQLCipher derives the MAC key from the page salt with every byte XOR 0x3a.
// Built into the format, not a knob: getting it wrong breaks every MAC.
constexpr std::uint8_t kMacSaltXor = 0x3a;

// ---------------------------------------------------------------------------
// Evidence file access. Win32, not fstream: a msgstore.db is routinely held
// open by the live app during an acquisition, and CreateFileW with full sharing
// opens what fstream refuses. It also keeps 64-bit offsets honest past 2 GB.
// ---------------------------------------------------------------------------
class evidence_file {
public:
    enum class mode { read, write };

    bool open(const std::wstring& path, mode m) {
        close();
        if (m == mode::read) {
            // Share everything: the suspect process may still hold the file.
            handle_ = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        } else {
            handle_ = CreateFileW(path.c_str(), GENERIC_WRITE | GENERIC_READ, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        if (handle_ == INVALID_HANDLE_VALUE) { handle_ = nullptr; return false; }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle_, &size)) size.QuadPart = 0;
        size_ = static_cast<std::uint64_t>(size.QuadPart);
        return true;
    }

    ~evidence_file() { close(); }
    evidence_file() = default;
    evidence_file(const evidence_file&) = delete;
    evidence_file& operator=(const evidence_file&) = delete;

    void close() {
        if (handle_) { CloseHandle(handle_); handle_ = nullptr; }
        size_ = 0;
    }

    bool ok() const { return handle_ != nullptr; }
    std::uint64_t size() const { return size_; }

    bool read_at(std::uint64_t off, void* dst, std::size_t len) const {
        if (!handle_) return false;
        auto* p = static_cast<std::uint8_t*>(dst);
        std::size_t done = 0;
        while (done < len) {
            OVERLAPPED ov{};
            const std::uint64_t pos = off + done;
            ov.Offset = static_cast<DWORD>(pos & 0xffffffffu);
            ov.OffsetHigh = static_cast<DWORD>(pos >> 32);
            DWORD got = 0;
            if (!ReadFile(handle_, p + done, static_cast<DWORD>(len - done), &got, &ov)) {
                if (GetLastError() != ERROR_HANDLE_EOF) return false;
                break;
            }
            if (got == 0) break;
            done += got;
        }
        return done == len;
    }

    bool write_at(std::uint64_t off, const void* src, std::size_t len) {
        if (!handle_) return false;
        const auto* p = static_cast<const std::uint8_t*>(src);
        std::size_t done = 0;
        while (done < len) {
            OVERLAPPED ov{};
            const std::uint64_t pos = off + done;
            ov.Offset = static_cast<DWORD>(pos & 0xffffffffu);
            ov.OffsetHigh = static_cast<DWORD>(pos >> 32);
            DWORD put = 0;
            if (!WriteFile(handle_, p + done, static_cast<DWORD>(len - done), &put, &ov)) return false;
            if (put == 0) return false;
            done += put;
        }
        if (off + len > size_) size_ = off + len;
        return true;
    }

private:
    HANDLE handle_ = nullptr;
    std::uint64_t size_ = 0;
};

// ---------------------------------------------------------------------------
// CNG primitives.
// ---------------------------------------------------------------------------
const wchar_t* cng_name(digest d) {
    switch (d) {
        case digest::sha256: return BCRYPT_SHA256_ALGORITHM;
        case digest::sha512: return BCRYPT_SHA512_ALGORITHM;
        default: return BCRYPT_SHA1_ALGORITHM;
    }
}

std::uint32_t mac_size_of(digest d) {
    switch (d) {
        case digest::sha256: return 32;
        case digest::sha512: return 64;
        default: return 20;
    }
}

class cng_alg {
public:
    cng_alg() = default;
    ~cng_alg() { if (h_) BCryptCloseAlgorithmProvider(h_, 0); }
    cng_alg(const cng_alg&) = delete;
    cng_alg& operator=(const cng_alg&) = delete;

    bool open(const wchar_t* name, ULONG flags = 0) {
        return BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h_, name, nullptr, flags));
    }
    bool ok() const { return h_ != nullptr; }
    BCRYPT_ALG_HANDLE get() const { return h_; }

    std::uint32_t property_u32(LPCWSTR prop) const {
        ULONG value = 0, written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(h_, prop, reinterpret_cast<PUCHAR>(&value),
                                              sizeof(value), &written, 0))) return 0;
        return value;
    }

private:
    BCRYPT_ALG_HANDLE h_ = nullptr;
};

// PBKDF2 through CNG. The HMAC provider doubles as the PRF.
bool pbkdf2(digest prf, const std::uint8_t* pass, std::size_t pass_len,
            const std::uint8_t* salt, std::size_t salt_len, std::uint32_t iterations,
            std::uint8_t* out, std::size_t out_len) {
    cng_alg alg;
    if (!alg.open(cng_name(prf), BCRYPT_ALG_HANDLE_HMAC_FLAG)) return false;
    return BCRYPT_SUCCESS(BCryptDeriveKeyPBKDF2(
        alg.get(),
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(pass)), static_cast<ULONG>(pass_len),
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(salt)), static_cast<ULONG>(salt_len),
        iterations, reinterpret_cast<PUCHAR>(out), static_cast<ULONG>(out_len), 0));
}

// HMAC over fragments, so a page is verified without concatenating it in RAM.
class hmac_t {
public:
    hmac_t(digest d, const std::uint8_t* key, std::size_t key_len) {
        if (!alg_.open(cng_name(d), BCRYPT_ALG_HANDLE_HMAC_FLAG)) return;
        object_.resize(alg_.property_u32(BCRYPT_OBJECT_LENGTH));
        const NTSTATUS st = BCryptCreateHash(
            alg_.get(), &hash_, object_.empty() ? nullptr : object_.data(),
            static_cast<ULONG>(object_.size()),
            const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key)),
            static_cast<ULONG>(key_len), 0);
        if (!BCRYPT_SUCCESS(st)) hash_ = nullptr;
    }
    ~hmac_t() { if (hash_) BCryptDestroyHash(hash_); }
    hmac_t(const hmac_t&) = delete;
    hmac_t& operator=(const hmac_t&) = delete;

    bool ok() const { return hash_ != nullptr; }

    bool feed(const void* data, std::size_t len) {
        if (!len) return true;
        return hash_ && BCRYPT_SUCCESS(BCryptHashData(
            hash_, const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
            static_cast<ULONG>(len), 0));
    }

    bool finish(std::uint8_t* out, std::size_t out_len) {
        return hash_ && BCRYPT_SUCCESS(BCryptFinishHash(
            hash_, reinterpret_cast<PUCHAR>(out), static_cast<ULONG>(out_len), 0));
    }

private:
    cng_alg alg_;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<std::uint8_t> object_;
};

class aes_cbc {
public:
    explicit aes_cbc(const std::uint8_t* key, std::size_t key_len) {
        if (!alg_.open(BCRYPT_AES_ALGORITHM)) return;
        const PUCHAR mode = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(BCRYPT_CHAIN_MODE_CBC));
        if (!BCRYPT_SUCCESS(BCryptSetProperty(alg_.get(), BCRYPT_CHAINING_MODE, mode,
                                              sizeof(BCRYPT_CHAIN_MODE_CBC), 0))) return;
        object_.resize(alg_.property_u32(BCRYPT_OBJECT_LENGTH));
        const NTSTATUS st = BCryptGenerateSymmetricKey(
            alg_.get(), &key_, object_.empty() ? nullptr : object_.data(),
            static_cast<ULONG>(object_.size()),
            const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key)),
            static_cast<ULONG>(key_len), 0);
        if (!BCRYPT_SUCCESS(st)) key_ = nullptr;
    }
    ~aes_cbc() { if (key_) BCryptDestroyKey(key_); }
    aes_cbc(const aes_cbc&) = delete;
    aes_cbc& operator=(const aes_cbc&) = delete;

    bool ok() const { return key_ != nullptr; }

    // In place. SQLCipher never pads: every encrypted region is a 16-byte multiple.
    bool decrypt(std::uint8_t* buf, std::size_t len, const std::uint8_t* iv) {
        if (!key_ || len == 0 || (len % 16) != 0) return false;
        ULONG written = 0;
        return BCRYPT_SUCCESS(BCryptDecrypt(
            key_, buf, static_cast<ULONG>(len), nullptr,
            const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(iv)), 16,
            buf, static_cast<ULONG>(len), &written, 0));
    }

private:
    cng_alg alg_;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    std::vector<std::uint8_t> object_;
};

// Key schedule for one candidate configuration.
struct schedule {
    std::vector<std::uint8_t> aes_key;
    std::vector<std::uint8_t> mac_key;
};

bool build_schedule(const cipher_params& p, const key_material& key,
                    const std::uint8_t* salt, schedule& out) {
    std::uint8_t mac_salt[16];
    for (int i = 0; i < 16; ++i) mac_salt[i] = static_cast<std::uint8_t>(salt[i] ^ kMacSaltXor);

    out.mac_key.assign(mac_size_of(p.hmac_alg), 0);

    if (!key.raw_key.empty()) {
        // Raw key path: WhatsApp keeps the 32-byte database key in
        // /data/data/com.whatsapp/files/key. AES key is the blob verbatim; the
        // MAC key is derived from it with two PBKDF2 rounds.
        if (key.raw_key.size() != 32) return false;
        out.aes_key = key.raw_key;
        return pbkdf2(p.kdf_alg, key.raw_key.data(), key.raw_key.size(),
                      mac_salt, sizeof(mac_salt), 2, out.mac_key.data(), out.mac_key.size());
    }

    if (key.passphrase.empty()) return false;
    const auto* pass = reinterpret_cast<const std::uint8_t*>(key.passphrase.data());
    out.aes_key.assign(32, 0);
    if (!pbkdf2(p.kdf_alg, pass, key.passphrase.size(), salt, 16, p.kdf_iter,
                out.aes_key.data(), out.aes_key.size())) return false;
    return pbkdf2(p.kdf_alg, pass, key.passphrase.size(), mac_salt, sizeof(mac_salt),
                  p.kdf_iter, out.mac_key.data(), out.mac_key.size());
}

// One page: verify the tag, then decrypt. Page numbering is 1-based because
// SQLCipher feeds the page number itself into the MAC.
bool decrypt_page(const cipher_params& p, const schedule& sch,
                  std::uint32_t pgno, const std::uint8_t* in, std::uint8_t* out) {
    const std::uint32_t ps = p.page_size;
    const std::uint32_t reserve = p.reserve;
    if (reserve > ps || p.iv_size() + mac_size_of(p.hmac_alg) > reserve) return false;

    const std::uint32_t body_end = ps - reserve;   // end of ciphertext
    const std::uint32_t iv_off = body_end;         // IV starts the reserve
    const std::uint32_t mac_off = iv_off + p.iv_size();

    // Page 1 keeps its salt in the header slot; every other page is ciphertext
    // from byte 0.
    const std::uint32_t data_off = (pgno == 1) ? 16u : 0u;
    if (body_end <= data_off) return false;

    // Deterministic output: the reserve area is never stored by SQLCipher, so
    // whatever we leave there is ours to define. Zero it, or two runs over the
    // same evidence produce different hashes and the chain of custody breaks.
    std::memset(out, 0, ps);

    if (p.salt_as_iv_page1 && pgno == 1) {
        // Legacy layout: no IV stored for page 1, the salt doubles as IV.
        std::memcpy(out + data_off, in + data_off, body_end - data_off);
        aes_cbc aes(sch.aes_key.data(), sch.aes_key.size());
        if (!aes.ok() || !aes.decrypt(out + data_off, body_end - data_off, in)) return false;
    } else {
        hmac_t mac(p.hmac_alg, sch.mac_key.data(), sch.mac_key.size());
        if (!mac.ok()) return false;
        if (!mac.feed(in + data_off, body_end - data_off)) return false;
        if (!mac.feed(in + iv_off, p.iv_size())) return false;
        const std::uint8_t pgno_le[4] = {
            static_cast<std::uint8_t>(pgno & 0xff),
            static_cast<std::uint8_t>((pgno >> 8) & 0xff),
            static_cast<std::uint8_t>((pgno >> 16) & 0xff),
            static_cast<std::uint8_t>((pgno >> 24) & 0xff)};
        if (!mac.feed(pgno_le, 4)) return false;

        const std::uint32_t n = mac_size_of(p.hmac_alg);
        std::uint8_t want[64] = {0};
        if (!mac.finish(want, n)) return false;
        if (std::memcmp(want, in + mac_off, n) != 0) return false;

        std::memcpy(out + data_off, in + data_off, body_end - data_off);
        aes_cbc aes(sch.aes_key.data(), sch.aes_key.size());
        if (!aes.ok() || !aes.decrypt(out + data_off, body_end - data_off, in + iv_off)) return false;
    }

    if (pgno == 1) {
        std::memcpy(out, kSqliteMagic, 16);
        // SQLCipher zeroes the header's reserved-bytes field before encrypting
        // page 1 and restores it on read, because the codec owns that number.
        // Without this restore the recovered image lies about its own geometry.
        out[20] = static_cast<std::uint8_t>(reserve & 0xff);
    }
    return true;
}

}  // namespace

std::uint32_t cipher_params::mac_size() const { return mac_size_of(hmac_alg); }

std::string cipher_params::describe() const {
    static const char* names[] = {"sha1", "sha256", "sha512"};
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "page=%u reserve=%u iter=%u hmac=%s kdf=%s iv_salt=%s",
                  page_size, reserve, kdf_iter,
                  names[static_cast<int>(hmac_alg)], names[static_cast<int>(kdf_alg)],
                  salt_as_iv_page1 ? "on" : "off");
    return std::string(buf);
}

open_result detect(const std::wstring& db_path, const key_material& key) {
    open_result result;
    if (key.empty()) { result.detail = "sem material de chave (raw key ou passphrase)"; return result; }

    evidence_file f;
    if (!f.open(db_path, evidence_file::mode::read)) { result.detail = "nao abriu o banco"; return result; }
    const std::uint64_t size = f.size();
    if (size < 512) { result.detail = "arquivo pequeno demais"; return result; }

    std::uint8_t head[16] = {0};
    if (!f.read_at(0, head, sizeof(head))) { result.detail = "cabecalho ilegivel"; return result; }

    // Candidate space. Order affects only speed; correctness is the MAC.
    const std::uint32_t page_sizes[] = {4096, 1024, 2048, 8192, 16384, 512};
    const std::uint32_t reserves[] = {48, 80, 40, 32, 36, 24, 20, 16, 64, 56};
    const std::uint32_t iters[] = {64000, 256000, 1, 4000, 5000, 10000, 20000, 128000};
    const digest digests[] = {digest::sha1, digest::sha256, digest::sha512};

    // A database is whole pages; use that to skip impossible page sizes.
    std::vector<std::uint32_t> viable;
    for (std::uint32_t ps : page_sizes) {
        if (size >= ps && (size % ps) == 0) viable.push_back(ps);
    }
    if (viable.empty()) viable.assign(std::begin(page_sizes), std::end(page_sizes));

    std::unique_ptr<std::uint8_t[]> page_in(new std::uint8_t[65536]);
    std::unique_ptr<std::uint8_t[]> plain(new std::uint8_t[65536]);

    for (std::uint32_t ps : viable) {
        for (std::uint32_t rs : reserves) {
            if (ps <= rs + 16) continue;
            for (digest mac : digests) {
                if (16 + mac_size_of(mac) > rs) continue;
                for (digest kdf : digests) {
                    for (std::uint32_t iter : iters) {
                        if (key.raw_key.empty() && iter < 1000) continue;
                        for (int legacy = 0; legacy < 2; ++legacy) {
                            cipher_params p;
                            p.page_size = ps; p.reserve = rs; p.kdf_iter = iter;
                            p.hmac_alg = mac; p.kdf_alg = kdf;
                            p.salt_as_iv_page1 = legacy != 0;

                            schedule sch;
                            if (!build_schedule(p, key, head, sch)) continue;
                            if (!f.read_at(0, page_in.get(), ps)) continue;
                            if (!decrypt_page(p, sch, 1, page_in.get(), plain.get())) continue;
                            if (std::memcmp(plain.get(), kSqliteMagic, 16) != 0) continue;

                            // Page 2 confirms it: page 1 alone could in principle
                            // collide, a second verified MAC cannot.
                            if (size >= static_cast<std::uint64_t>(ps) * 2) {
                                if (!f.read_at(ps, page_in.get(), ps)) continue;
                                if (!decrypt_page(p, sch, 2, page_in.get(), plain.get())) continue;
                            }

                            result.ok = true;
                            result.params = p;
                            result.detail = "verificado por HMAC: " + p.describe();
                            return result;
                        }
                    }
                }
            }
        }
    }
    result.detail = "nenhuma combinacao verificou o HMAC da pagina 1";
    return result;
}

bool decrypt_database(const std::wstring& db_path,
                      const std::wstring& out_path,
                      const cipher_params& params,
                      const key_material& key,
                      decrypt_stats& stats,
                      std::string& error) {
    stats = decrypt_stats{};

    evidence_file in;
    if (!in.open(db_path, evidence_file::mode::read)) { error = "nao abriu o banco"; return false; }
    const std::uint64_t size = in.size();
    if (size < params.page_size) { error = "arquivo menor que uma pagina"; return false; }

    std::uint8_t salt[16] = {0};
    if (!in.read_at(0, salt, sizeof(salt))) { error = "cabecalho ilegivel"; return false; }

    schedule sch;
    if (!build_schedule(params, key, salt, sch)) { error = "derivacao de chave falhou"; return false; }

    const std::uint64_t pages = size / params.page_size;
    std::unique_ptr<std::uint8_t[]> enc(new std::uint8_t[params.page_size]);
    std::unique_ptr<std::uint8_t[]> dec(new std::uint8_t[params.page_size]);

    evidence_file out;
    if (!out.open(out_path, evidence_file::mode::write)) { error = "nao criou a saida"; return false; }

    for (std::uint64_t i = 0; i < pages; ++i) {
        if (!in.read_at(i * params.page_size, enc.get(), params.page_size)) break;
        if (!decrypt_page(params, sch, static_cast<std::uint32_t>(i + 1), enc.get(), dec.get())) {
            // A torn or half-overwritten tail is normal in a live acquisition.
            // Emit a zero page so byte offsets stay aligned with the evidence.
            std::memset(dec.get(), 0, params.page_size);
            dec[0] = 0x0d;
        }
        if (!out.write_at(i * params.page_size, dec.get(), params.page_size)) {
            error = "falha ao escrever a saida";
            return false;
        }
        ++stats.pages;
        stats.bytes_out += params.page_size;
    }
    stats.bytes_in = size;

    // WAL replay. In SQLCipher the frames carry full encrypted pages, so they
    // decrypt with the same schedule using the page number in the frame header.
    // Replaying every frame on purpose: for acquisition we want the newest state,
    // committed or not. A checkpoint would have discarded these already.
    evidence_file wal;
    if (wal.open(db_path + L"-wal", evidence_file::mode::read)) {
        stats.wal_present = true;
        std::uint8_t wal_header[32] = {0};
        if (wal.read_at(0, wal_header, sizeof(wal_header))) {
            std::uint32_t wal_page_size =
                (std::uint32_t(wal_header[8]) << 24) | (std::uint32_t(wal_header[9]) << 16) |
                (std::uint32_t(wal_header[10]) << 8) | std::uint32_t(wal_header[11]);
            if (wal_page_size == 1) wal_page_size = 65536;

            if (wal_page_size == params.page_size) {
                const std::uint64_t frame_size = 24 + params.page_size;
                std::uint64_t off = 32;
                std::uint8_t frame_header[24];
                std::unique_ptr<std::uint8_t[]> fbuf(new std::uint8_t[params.page_size]);
                while (wal.read_at(off, frame_header, sizeof(frame_header))) {
                    const std::uint32_t pgno = (std::uint32_t(frame_header[0]) << 24) |
                                               (std::uint32_t(frame_header[1]) << 16) |
                                               (std::uint32_t(frame_header[2]) << 8) |
                                               std::uint32_t(frame_header[3]);
                    if (pgno == 0) break;
                    if (!wal.read_at(off + 24, fbuf.get(), params.page_size)) break;
                    // Aplica todo frame decifrado, inclusive os de uma transacao
                    // que nao chegou a commitar: num aparelho apreendido no meio
                    // da escrita, essa e justamente a evidencia mais nova. O
                    // preco e que o banco pode ficar estruturalmente a meio
                    // caminho, entao o relatorio conta quantos frames ficaram
                    // depois do ultimo commit (wal_descartados) e o laudo sabe
                    // que aquele trecho nao estava confirmado pelo SQLite.
                    const std::uint32_t db_size = (std::uint32_t(frame_header[4]) << 24) |
                                                  (std::uint32_t(frame_header[5]) << 16) |
                                                  (std::uint32_t(frame_header[6]) << 8) |
                                                  std::uint32_t(frame_header[7]);
                    if (decrypt_page(params, sch, pgno, fbuf.get(), dec.get())) {
                        out.write_at(std::uint64_t(pgno - 1) * params.page_size, dec.get(),
                                     params.page_size);
                        ++stats.wal_frames;
                        if (db_size != 0) stats.wal_discarded = 0;
                        else ++stats.wal_discarded;
                    }
                    off += frame_size;
                }
                // wal_discarded ja carrega quantos frames vieram depois do ultimo
                // commit: eles foram aplicados, mas o SQLite nao os confirmou.
            }
        }
    }

    return true;
}

}  // namespace olho::wa
