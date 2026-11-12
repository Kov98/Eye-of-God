// language: C++, file: pkcs7_builder.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "pkcs7_builder.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <wincrypt.h>

#include <cstring>
#include <ctime>

namespace olho::pkcs7 {
namespace {

void put_len(bytes& out, std::size_t n) {
    if (n < 0x80) { out.push_back(static_cast<std::uint8_t>(n)); return; }
    std::uint8_t tmp[8];
    int count = 0;
    while (n > 0) { tmp[count++] = static_cast<std::uint8_t>(n & 0xFF); n >>= 8; }
    out.push_back(static_cast<std::uint8_t>(0x80 | count));
    while (count > 0) out.push_back(tmp[--count]);
}

bytes tlv(std::uint8_t tag, const bytes& content) {
    bytes out;
    out.push_back(tag);
    put_len(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

bytes cat(std::initializer_list<bytes> parts) {
    bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

bytes oid(const std::string& dotted) {
    std::vector<std::uint64_t> arcs;
    std::uint64_t cur = 0;
    bool any = false;
    for (char c : dotted) {
        if (c == 46) { arcs.push_back(cur); cur = 0; any = false; continue; }
        if (c < 48 || c > 57) continue;
        cur = cur * 10 + static_cast<std::uint64_t>(c - 48);
        any = true;
    }
    if (any) arcs.push_back(cur);
    bytes body;
    if (arcs.size() >= 2) {
        body.push_back(static_cast<std::uint8_t>(arcs[0] * 40 + arcs[1]));
        for (std::size_t i = 2; i < arcs.size(); ++i) {
            std::uint8_t tmp[10];
            int n = 0;
            std::uint64_t v = arcs[i];
            do { tmp[n++] = static_cast<std::uint8_t>(v & 0x7F); v >>= 7; } while (v > 0);
            while (n > 0) {
                --n;
                body.push_back(static_cast<std::uint8_t>(tmp[n] | (n > 0 ? 0x80 : 0x00)));
            }
        }
    }
    return tlv(0x06, body);
}

bytes integer_small(std::uint32_t v) {
    bytes body;
    if (v == 0) { body.push_back(0); return tlv(0x02, body); }
    std::uint8_t tmp[5];
    int n = 0;
    while (v > 0) { tmp[n++] = static_cast<std::uint8_t>(v & 0xFF); v >>= 8; }
    if (tmp[n - 1] & 0x80) body.push_back(0);
    while (n > 0) body.push_back(tmp[--n]);
    return tlv(0x02, body);
}

bytes octet_string(const bytes& v) { return tlv(0x04, v); }
bytes null_value() { bytes b; return tlv(0x05, b); }

bytes utc_time(std::time_t t) {
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%y%m%d%H%M%SZ", &tm);
    bytes body(buf, buf + std::strlen(buf));
    return tlv(0x17, body);
}

bool sha256(const bytes& data, bytes& digest) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    ULONG obj = 0, hl = 0, written = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj), sizeof(obj), &written, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hl), sizeof(hl), &written, 0);
    bytes object(obj);
    digest.assign(hl, 0);
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, object.data(), obj, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data.data()),
                                      static_cast<ULONG>(data.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), hl, 0))) {
        ok = true;
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

std::string to_hex(const bytes& v) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (auto b : v) { s.push_back(h[b >> 4]); s.push_back(h[b & 15]); }
    return s;
}

bool read_tlv(const bytes& d, std::size_t& off, std::uint8_t& tag, bytes& content) {
    if (off >= d.size()) return false;
    tag = d[off++];
    if (off >= d.size()) return false;
    std::size_t len = d[off++];
    if (len & 0x80) {
        const std::size_t n = len & 0x7F;
        if (n == 0 || n > 4 || off + n > d.size()) return false;
        len = 0;
        for (std::size_t i = 0; i < n; ++i) len = (len << 8) | d[off++];
    }
    if (off + len > d.size()) return false;
    content.assign(d.begin() + static_cast<std::ptrdiff_t>(off),
                   d.begin() + static_cast<std::ptrdiff_t>(off + len));
    off += len;
    return true;
}

bool issuer_and_serial(const bytes& cert_der, bytes& issuer_tlv, bytes& serial_tlv) {
    std::size_t off = 0;
    std::uint8_t tag = 0;
    bytes cert_body;
    if (!read_tlv(cert_der, off, tag, cert_body) || tag != 0x30) return false;
    std::size_t p = 0;
    bytes tbs;
    if (!read_tlv(cert_body, p, tag, tbs) || tag != 0x30) return false;
    std::size_t q = 0;
    bytes field;
    const std::size_t save = q;
    if (!read_tlv(tbs, q, tag, field)) return false;
    if (tag != 0xA0) q = save;
    const std::size_t serial_start = q;
    if (!read_tlv(tbs, q, tag, field) || tag != 0x02) return false;
    serial_tlv.assign(tbs.begin() + static_cast<std::ptrdiff_t>(serial_start),
                      tbs.begin() + static_cast<std::ptrdiff_t>(q));
    if (!read_tlv(tbs, q, tag, field) || tag != 0x30) return false;
    const std::size_t issuer_start = q;
    if (!read_tlv(tbs, q, tag, field) || tag != 0x30) return false;
    issuer_tlv.assign(tbs.begin() + static_cast<std::ptrdiff_t>(issuer_start),
                      tbs.begin() + static_cast<std::ptrdiff_t>(q));
    return true;
}

bytes attribute(const char* type_oid, const bytes& value_tlv) {
    return tlv(0x30, cat({oid(type_oid), tlv(0x31, value_tlv)}));
}

// Content of an already-encoded SET, whatever its length encoding. Skipping a
// fixed 2-byte header breaks the moment the attributes exceed 127 bytes and the
// length goes long-form.
bytes set_content(const bytes& encoded_set) {
    std::size_t off = 0;
    std::uint8_t tag = 0;
    bytes content;
    if (!read_tlv(encoded_set, off, tag, content)) return bytes();
    return content;
}

bytes cert_der_of(void* cert_void) {
    PCCERT_CONTEXT cert = static_cast<PCCERT_CONTEXT>(cert_void);
    return bytes(cert->pbCertEncoded, cert->pbCertEncoded + cert->cbCertEncoded);
}

}  // namespace

bool pick_signing_certificate(const std::string& subject_filter, void** cert_out,
                              std::string& subject, std::string& thumbprint) {
    if (!cert_out) return false;
    *cert_out = nullptr;
    HCERTSTORE store = CertOpenSystemStoreW(0, L"MY");
    if (!store) return false;
    PCCERT_CONTEXT it = nullptr;
    while ((it = CertEnumCertificatesInStore(store, it)) != nullptr) {
        DWORD key_info_size = 0;
        if (!CertGetCertificateContextProperty(it, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &key_info_size)) continue;
        char name[512] = {0};
        CertGetNameStringA(it, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, sizeof(name));
        if (!subject_filter.empty() && std::string(name).find(subject_filter) == std::string::npos) continue;
        DWORD hash_size = 0;
        if (CertGetCertificateContextProperty(it, CERT_SHA1_HASH_PROP_ID, nullptr, &hash_size) && hash_size > 0) {
            bytes h(hash_size);
            if (CertGetCertificateContextProperty(it, CERT_SHA1_HASH_PROP_ID, h.data(), &hash_size)) {
                thumbprint = to_hex(h);
            }
        }
        subject = name;
        *cert_out = const_cast<CERT_CONTEXT*>(CertDuplicateCertificateContext(it));
        CertCloseStore(store, 0);
        return *cert_out != nullptr;
    }
    CertCloseStore(store, 0);
    return false;
}

void free_certificate(void* cert) {
    if (cert) CertFreeCertificateContext(static_cast<PCCERT_CONTEXT>(cert));
}

bool prepare(void* cert, const bytes& content, partial& out, std::string& error) {
    if (!cert) { error = "certificado nulo"; return false; }
    if (!sha256(content, out.content_digest)) { error = "SHA-256 do conteudo falhou"; return false; }

    const bytes attr_content_type = attribute("1.2.840.113549.1.9.3", oid("1.2.840.113549.1.7.1"));
    const bytes attr_digest = attribute("1.2.840.113549.1.9.4", octet_string(out.content_digest));
    const bytes attr_time = attribute("1.2.840.113549.1.9.5", utc_time(std::time(nullptr)));
    out.attrs_der = tlv(0x31, cat({attr_content_type, attr_digest, attr_time}));
    if (!sha256(out.attrs_der, out.attrs_digest)) { error = "SHA-256 dos atributos falhou"; return false; }
    return true;
}

bool sign_digest(void* cert_void, const bytes& digest, bytes& signature, std::string& error) {
    PCCERT_CONTEXT cert = static_cast<PCCERT_CONTEXT>(cert_void);
    if (!cert) { error = "certificado nulo"; return false; }

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE prov = 0;
    DWORD key_spec = 0;
    BOOL free_key = FALSE;
    if (!CryptAcquireCertificatePrivateKey(cert, CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG, nullptr,
                                           &prov, &key_spec, &free_key)) {
        error = "chave privada indisponivel";
        return false;
    }
    if (key_spec != CERT_NCRYPT_KEY_SPEC) {
        if (free_key) CryptReleaseContext(prov, 0);
        error = "chave nao e CNG";
        return false;
    }

    NCRYPT_KEY_HANDLE key = reinterpret_cast<NCRYPT_KEY_HANDLE>(prov);
    BCRYPT_PKCS1_PADDING_INFO pad{};
    pad.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    DWORD sig_len = 0;
    SECURITY_STATUS st = NCryptSignHash(key, &pad, const_cast<BYTE*>(digest.data()),
                                        static_cast<DWORD>(digest.size()), nullptr, 0, &sig_len,
                                        BCRYPT_PAD_PKCS1);
    if (st != 0 || sig_len == 0) {
        if (free_key) NCryptFreeObject(key);
        error = "NCryptSignHash (tamanho) falhou: " + std::to_string(st);
        return false;
    }
    signature.assign(sig_len, 0);
    st = NCryptSignHash(key, &pad, const_cast<BYTE*>(digest.data()),
                        static_cast<DWORD>(digest.size()), signature.data(), sig_len, &sig_len,
                        BCRYPT_PAD_PKCS1);
    if (free_key) NCryptFreeObject(key);
    if (st != 0) { error = "NCryptSignHash falhou: " + std::to_string(st); return false; }
    signature.resize(sig_len);
    return true;
}

bool assemble(void* cert_void, const partial& p, const bytes& signature,
              const bytes& timestamp_token, bytes& der, std::string& error) {
    PCCERT_CONTEXT cert = static_cast<PCCERT_CONTEXT>(cert_void);
    if (!cert) { error = "certificado nulo"; return false; }

    const bytes alg_sha256 = tlv(0x30, cat({oid("2.16.840.1.101.3.4.2.1"), null_value()}));
    const bytes alg_sha256_rsa = tlv(0x30, cat({oid("1.2.840.113549.1.1.11"), null_value()}));
    const bytes cert_der = cert_der_of(cert_void);

    bytes issuer_tlv, serial_tlv;
    if (!issuer_and_serial(cert_der, issuer_tlv, serial_tlv)) {
        error = "nao consegui extrair issuer/serial do certificado";
        return false;
    }
    const bytes sid = tlv(0x30, cat({issuer_tlv, serial_tlv}));

    bytes signer_info_body = cat({
        integer_small(1),
        sid,
        alg_sha256,
        tlv(0xA0, set_content(p.attrs_der)),
        alg_sha256_rsa,
        octet_string(signature),
    });

    // Unsigned attribute: the timestamp token rides AFTER the signature, which is
    // why it can be added without touching what was signed.
    if (!timestamp_token.empty()) {
        const bytes attr = attribute("1.2.840.113549.1.9.16.2.14", timestamp_token);
        const bytes unsigned_attrs = tlv(0xA1, attr);
        signer_info_body = cat({signer_info_body, unsigned_attrs});
    }

    const bytes signer_info = tlv(0x30, signer_info_body);
    const bytes signed_data = tlv(0x30, cat({
        integer_small(1),
        tlv(0x31, alg_sha256),
        tlv(0x30, oid("1.2.840.113549.1.7.1")),
        tlv(0xA0, cert_der),
        tlv(0x31, signer_info),
    }));

    der = tlv(0x30, cat({oid("1.2.840.113549.1.7.2"), tlv(0xA0, signed_data)}));
    if (der.empty()) { error = "montagem do CMS produziu zero bytes"; return false; }
    return true;
}

bool sign_detached(void* cert, const std::vector<std::uint8_t>& content,
                   std::vector<std::uint8_t>& der, std::string& error) {
    partial p;
    if (!prepare(cert, content, p, error)) return false;
    bytes signature;
    if (!sign_digest(cert, p.attrs_digest, signature, error)) return false;
    return assemble(cert, p, signature, bytes(), der, error);
}

bool build_timestamp_token(void* cert_void, const bytes& tstinfo_der, bytes& token,
                           std::string& error) {
    PCCERT_CONTEXT cert = static_cast<PCCERT_CONTEXT>(cert_void);
    if (!cert) { error = "certificado nulo"; return false; }

    bytes econtent_digest;
    if (!sha256(tstinfo_der, econtent_digest)) { error = "SHA-256 do TSTInfo falhou"; return false; }

    // signature-time-stamp attributes: contentType id-ct-TSTInfo + messageDigest.
    const bytes attr_content_type = attribute("1.2.840.113549.1.9.3", oid("1.2.840.113549.1.9.16.1.4"));
    const bytes attr_digest = attribute("1.2.840.113549.1.9.4", octet_string(econtent_digest));
    const bytes attr_time = attribute("1.2.840.113549.1.9.5", utc_time(std::time(nullptr)));
    const bytes attrs = tlv(0x31, cat({attr_content_type, attr_digest, attr_time}));

    bytes attrs_digest;
    if (!sha256(attrs, attrs_digest)) { error = "SHA-256 dos atributos falhou"; return false; }
    bytes signature;
    if (!sign_digest(cert_void, attrs_digest, signature, error)) return false;

    const bytes alg_sha256 = tlv(0x30, cat({oid("2.16.840.1.101.3.4.2.1"), null_value()}));
    const bytes alg_sha256_rsa = tlv(0x30, cat({oid("1.2.840.113549.1.1.11"), null_value()}));
    const bytes cert_der = cert_der_of(cert_void);
    bytes issuer_tlv, serial_tlv;
    if (!issuer_and_serial(cert_der, issuer_tlv, serial_tlv)) {
        error = "issuer/serial indisponivel";
        return false;
    }
    const bytes sid = tlv(0x30, cat({issuer_tlv, serial_tlv}));
    const bytes signer_info = tlv(0x30, cat({
        integer_small(1),
        sid,
        alg_sha256,
        tlv(0xA0, set_content(attrs)),
        alg_sha256_rsa,
        octet_string(signature),
    }));

    // eContent present: a timestamp token is NOT detached.
    const bytes encap = tlv(0x30, cat({
        oid("1.2.840.113549.1.9.16.1.4"),
        tlv(0xA0, octet_string(tstinfo_der)),
    }));

    const bytes signed_data = tlv(0x30, cat({
        integer_small(3),
        tlv(0x31, alg_sha256),
        encap,
        tlv(0xA0, cert_der),
        tlv(0x31, signer_info),
    }));

    token = tlv(0x30, cat({oid("1.2.840.113549.1.7.2"), tlv(0xA0, signed_data)}));
    if (token.empty()) { error = "token vazio"; return false; }
    return true;
}

}  // namespace olho::pkcs7