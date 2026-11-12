// language: C++, file: tsa_client.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "tsa_client.hpp"

#include "pkcs7_builder.hpp"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <cstring>
#include <ctime>

namespace olho::tsa {
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
            while (n > 0) { --n; body.push_back(static_cast<std::uint8_t>(tmp[n] | (n > 0 ? 0x80 : 0x00))); }
        }
    }
    return tlv(0x06, body);
}

bytes integer_from(std::uint64_t v) {
    bytes body;
    if (v == 0) { body.push_back(0); return tlv(0x02, body); }
    std::uint8_t tmp[9];
    int n = 0;
    while (v > 0) { tmp[n++] = static_cast<std::uint8_t>(v & 0xFF); v >>= 8; }
    if (tmp[n - 1] & 0x80) body.push_back(0);
    while (n > 0) body.push_back(tmp[--n]);
    return tlv(0x02, body);
}

bytes null_value() { bytes b; return tlv(0x05, b); }

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

bool sha256_into(const bytes& data, bytes& digest) {
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
        BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), hl, 0))) {
        ok = true;
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

}  // namespace

bytes sha256(const bytes& data) {
    bytes digest;
    if (!sha256_into(data, digest)) digest.clear();
    return digest;
}

bytes build_request(const bytes& imprint, std::uint64_t nonce, bool request_certificate) {
    const bytes alg = tlv(0x30, std::vector<std::uint8_t>{0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00});
    const bytes imprint_seq = tlv(0x30, [&] {
        bytes b = alg;
        const bytes hash = tlv(0x04, imprint);
        b.insert(b.end(), hash.begin(), hash.end());
        return b;
    }());
    bytes body = integer_from(1);
    {
        const bytes t = imprint_seq;
        body.insert(body.end(), t.begin(), t.end());
    }
    {
        const bytes t = integer_from(nonce);
        body.insert(body.end(), t.begin(), t.end());
    }
    if (request_certificate) {
        const bytes t = tlv(0x01, std::vector<std::uint8_t>{0xFF});   // BOOLEAN TRUE
        body.insert(body.end(), t.begin(), t.end());
    }
    return tlv(0x30, body);
}

status parse_response(const bytes& response, bytes& token, std::uint64_t expected_nonce) {
    status st;
    token.clear();
    std::size_t off = 0;
    std::uint8_t tag = 0;
    bytes resp_body;
    if (!read_tlv(response, off, tag, resp_body) || tag != 0x30) { st.text = "resposta nao e SEQUENCE"; return st; }

    std::size_t p = 0;
    bytes status_info;
    if (!read_tlv(resp_body, p, tag, status_info) || tag != 0x30) { st.text = "PKIStatusInfo ausente"; return st; }
    std::size_t q = 0;
    bytes code_bytes;
    if (!read_tlv(status_info, q, tag, code_bytes) || tag != 0x02) { st.text = "PKIStatus ausente"; return st; }
    int code = 0;
    for (auto c : code_bytes) code = (code << 8) | c;
    st.code = code;
    if (code != 0 && code != 1) { st.text = "TSA recusou a solicitacao (PKIStatus " + std::to_string(code) + ")"; return st; }

    bytes content_info;
    if (!read_tlv(resp_body, p, tag, content_info) || tag != 0x30) { st.text = "token ausente na resposta"; return st; }
    // Re-encode to keep the full ContentInfo TLV for verification.
    const bytes full = tlv(0x30, content_info);
    token = full;
    (void)expected_nonce;
    st.ok = true;
    st.text = "token recebido";
    return st;
}

status verify_token(const bytes& token, const bytes& imprint, std::string& gen_time,
                    std::string& signer_subject) {
    status st;
    gen_time.clear();
    signer_subject.clear();

    // CMS signature + embedded content, straight from the platform.
    CRYPT_VERIFY_MESSAGE_PARA vp{};
    vp.cbSize = sizeof(vp);
    vp.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    PCCERT_CONTEXT signer = nullptr;
    DWORD content_len = 0;
    if (!CryptVerifyMessageSignature(&vp, 0, token.data(), static_cast<DWORD>(token.size()),
                                     nullptr, &content_len, &signer)) {
        st.text = "assinatura do token invalida (erro " + std::to_string(GetLastError()) + ")";
        if (signer) CertFreeCertificateContext(signer);
        return st;
    }
    bytes content(content_len);
    if (!CryptVerifyMessageSignature(&vp, 0, token.data(), static_cast<DWORD>(token.size()),
                                     content.data(), &content_len, &signer)) {
        st.text = "nao consegui extrair o TSTInfo";
        if (signer) CertFreeCertificateContext(signer);
        return st;
    }
    content.resize(content_len);
    if (signer) {
        char name[512] = {0};
        CertGetNameStringA(signer, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, sizeof(name));
        signer_subject = name;
        CertFreeCertificateContext(signer);
    }

    // TSTInfo: version, policy, messageImprint, serialNumber, genTime, ...
    std::size_t off = 0;
    std::uint8_t tag = 0;
    bytes tst;
    if (!read_tlv(content, off, tag, tst) || tag != 0x30) { st.text = "TSTInfo nao e SEQUENCE"; return st; }

    std::size_t p = 0;
    bytes field;
    if (!read_tlv(tst, p, tag, field) || tag != 0x02) { st.text = "TSTInfo sem versao"; return st; }
    if (!read_tlv(tst, p, tag, field) || tag != 0x06) { st.text = "TSTInfo sem policy"; return st; }

    bytes imprint_seq;
    if (!read_tlv(tst, p, tag, imprint_seq) || tag != 0x30) { st.text = "TSTInfo sem messageImprint"; return st; }
    std::size_t i = 0;
    bytes alg;
    bytes hashed;
    if (!read_tlv(imprint_seq, i, tag, alg) || tag != 0x30) { st.text = "messageImprint sem algoritmo"; return st; }
    if (!read_tlv(imprint_seq, i, tag, hashed) || tag != 0x04) { st.text = "messageImprint sem digest"; return st; }
    if (hashed != imprint) {
        st.text = "o digest carimbado nao e o da assinatura";
        return st;
    }

    if (!read_tlv(tst, p, tag, field) || tag != 0x02) { st.text = "TSTInfo sem serial"; return st; }
    bytes when;
    if (!read_tlv(tst, p, tag, when) || tag != 0x18) { st.text = "TSTInfo sem genTime"; return st; }
    gen_time.assign(when.begin(), when.end());

    st.ok = true;
    st.text = "token verificado";
    return st;
}

status stamp_local_selftest(const bytes& request, bytes& response, std::string& issuer) {
    status st;
    response.clear();

    // Parse TimeStampReq: version, messageImprint{ algId, hashedMessage }.
    std::size_t off = 0;
    std::uint8_t tag = 0;
    bytes req_body;
    if (!read_tlv(request, off, tag, req_body) || tag != 0x30) { st.text = "requisicao invalida"; return st; }
    std::size_t p = 0;
    bytes field;
    if (!read_tlv(req_body, p, tag, field) || tag != 0x02) { st.text = "requisicao sem versao"; return st; }
    bytes imprint_seq;
    if (!read_tlv(req_body, p, tag, imprint_seq) || tag != 0x30) { st.text = "requisicao sem messageImprint"; return st; }
    std::size_t i = 0;
    bytes alg, hashed;
    if (!read_tlv(imprint_seq, i, tag, alg) || tag != 0x30) { st.text = "imprint sem algoritmo"; return st; }
    if (!read_tlv(imprint_seq, i, tag, hashed) || tag != 0x04) { st.text = "imprint sem digest"; return st; }

    // TSTInfo with the same imprint and the current time.
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d%H%M%SZ", &tm);
    const bytes generalized(stamp, stamp + std::strlen(stamp));

    bytes tst_body;
    auto append = [&tst_body](const bytes& t) { tst_body.insert(tst_body.end(), t.begin(), t.end()); };
    append(integer_from(1));
    append(oid("1.2.3.4.1"));
    {
        // read_tlv hands back the CONTENT, not the TLV: re-encode it or the
        // messageImprint loses its AlgorithmIdentifier wrapper.
        bytes imprint_inner;
        const bytes alg_tlv = tlv(0x30, alg);
        imprint_inner.insert(imprint_inner.end(), alg_tlv.begin(), alg_tlv.end());
        const bytes h = tlv(0x04, hashed);
        imprint_inner.insert(imprint_inner.end(), h.begin(), h.end());
        append(tlv(0x30, imprint_inner));
    }
    append(integer_from(1));
    append(tlv(0x18, generalized));
    const bytes tstinfo = tlv(0x30, tst_body);

    void* cert = nullptr;
    std::string subject, thumb;
    if (!olho::pkcs7::pick_signing_certificate("", &cert, subject, thumb)) {
        st.text = "sem certificado para o TSA local";
        return st;
    }
    bytes token;
    std::string error;
    const bool built = olho::pkcs7::build_timestamp_token(cert, tstinfo, token, error);
    olho::pkcs7::free_certificate(cert);
    if (!built) { st.text = "token local falhou: " + error; return st; }

    // TimeStampResp: status granted (0) + token.
    bytes status_info;
    status_info.push_back(0x02);
    status_info.push_back(0x01);
    status_info.push_back(0x00);
    const bytes status_seq = tlv(0x30, status_info);
    bytes resp_body = status_seq;
    resp_body.insert(resp_body.end(), token.begin(), token.end());
    response = tlv(0x30, resp_body);

    issuer = subject;
    st.ok = true;
    st.code = 0;
    st.text = "carimbo local emitido (NAO e uma TSA confiavel)";
    return st;
}

status stamp_remote(const std::string& url, const bytes& request, bytes& response) {
    status st;
    response.clear();
    // RFC 3161 over HTTP: POST application/timestamp-query.
    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[1024] = {0};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1023;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { st.text = "URL invalida"; return st; }

    HINTERNET session = WinHttpOpen(L"OlhoDeDeus/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { st.text = "WinHttpOpen falhou"; return st; }
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); st.text = "conexao falhou"; return st; }
    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); st.text = "requisicao falhou"; return st; }

    const wchar_t* headers = L"Content-Type: application/timestamp-query\r\n";
    BOOL ok = WinHttpSendRequest(req, headers, static_cast<DWORD>(-1),
                                 const_cast<bytes*>(&request)->data(),
                                 static_cast<DWORD>(request.size()),
                                 static_cast<DWORD>(request.size()), 0);
    ok = ok && WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        st.text = "sem resposta da TSA (erro " + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return st;
    }
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        bytes chunk(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
        response.insert(response.end(), chunk.begin(), chunk.begin() + read);
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
    if (response.empty()) { st.text = "resposta vazia"; return st; }
    st.ok = true;
    st.text = "resposta recebida";
    return st;
}

}  // namespace olho::tsa