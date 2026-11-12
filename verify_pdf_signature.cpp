// language: C++, file: verify_pdf_signature.cpp, target: Windows 11
// Verifies the embedded PKCS#7 signature of a laudo.
//
//   olho_verify <file.pdf>
//
// Rebuilds the signed byte ranges from /ByteRange, extracts /Contents, and asks
// crypt32 to verify the detached signature. Prints who signed, when, and whether
// the document bytes still match. A non-zero exit means the signature does not
// hold — tampering, a truncated file, or a corrupt range table.
#include "tsa_client.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

long long parse_ll(const std::string& s, std::size_t at) {
    long long v = 0;
    while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
        v = v * 10 + (s[at] - '0');
        ++at;
    }
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: %s <arquivo.pdf>\n", argv[0]); return 2; }

    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::printf("nao abriu %s\n", argv[1]); return 2; }
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const std::size_t br_at = data.find("/ByteRange");
    const std::size_t ct_at = data.find("/Contents <");
    if (br_at == std::string::npos || ct_at == std::string::npos) {
        std::printf("sem assinatura embutida\n");
        return 1;
    }

    const std::size_t lb = data.find('[', br_at);
    const std::size_t rb = data.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos) { std::printf("ByteRange malformado\n"); return 1; }
    const std::string range = data.substr(lb + 1, rb - lb - 1);

    std::size_t p = 0;
    long long vals[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        while (p < range.size() && (range[p] == ' ' || range[p] == '\n' || range[p] == '\r')) ++p;
        vals[i] = parse_ll(range, p);
        while (p < range.size() && range[p] >= '0' && range[p] <= '9') ++p;
    }
    std::printf("ByteRange: [%lld %lld %lld %lld]\n", vals[0], vals[1], vals[2], vals[3]);

    const std::size_t off1 = static_cast<std::size_t>(vals[0]);
    const std::size_t len1 = static_cast<std::size_t>(vals[1]);
    const std::size_t off2 = static_cast<std::size_t>(vals[2]);
    const std::size_t len2 = static_cast<std::size_t>(vals[3]);
    if (off1 + len1 > data.size() || off2 + len2 > data.size()) {
        std::printf("ByteRange aponta fora do arquivo\n");
        return 1;
    }

    std::vector<BYTE> signed_bytes;
    signed_bytes.insert(signed_bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(off1),
                        data.begin() + static_cast<std::ptrdiff_t>(off1 + len1));
    signed_bytes.insert(signed_bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(off2),
                        data.begin() + static_cast<std::ptrdiff_t>(off2 + len2));

    // /Contents <hex> — decode until the closing angle bracket.
    const std::size_t hex_at = ct_at + std::strlen("/Contents <");
    const std::size_t hex_end = data.find('>', hex_at);
    std::vector<BYTE> pkcs7;
    for (std::size_t i = hex_at; i + 1 < hex_end; i += 2) {
        const char hi = data[i], lo = data[i + 1];
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        // Every byte is data: DER contains 0x00 legitimately. The slot is padded
        // with zeros at the END, so only trailing zeros are stripped.
        pkcs7.push_back(static_cast<BYTE>((nib(hi) << 4) | nib(lo)));
    }
    // DER encodes its own length, so slice exactly that: trimming trailing zeros
    // is wrong when a legitimate value ends in 0x00.
    if (pkcs7.size() > 4 && pkcs7[0] == 0x30) {
        std::size_t header = 2;
        std::size_t body = pkcs7[1];
        if (pkcs7[1] & 0x80) {
            const std::size_t n = pkcs7[1] & 0x7f;
            body = 0;
            for (std::size_t i = 0; i < n && 2 + i < pkcs7.size(); ++i) body = (body << 8) | pkcs7[2 + i];
            header = 2 + n;
        }
        const std::size_t total = header + body;
        if (total <= pkcs7.size()) pkcs7.resize(total);
    }
    if (pkcs7.empty()) { std::printf("bloco de assinatura vazio\n"); return 1; }
    std::printf("PKCS#7: %zu bytes | conteudo assinado: %zu bytes\n", pkcs7.size(), signed_bytes.size());

    CRYPT_VERIFY_MESSAGE_PARA para{};
    para.cbSize = sizeof(para);
    para.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;

    PCCERT_CONTEXT signer = nullptr;
    const BYTE* parts[1] = {signed_bytes.data()};
    DWORD sizes[1] = {static_cast<DWORD>(signed_bytes.size())};
    const BOOL ok = CryptVerifyDetachedMessageSignature(
        &para, 0, pkcs7.data(), static_cast<DWORD>(pkcs7.size()), 1, parts, sizes, &signer);
    if (!ok) {
        std::printf("ASSINATURA INVALIDA (erro %lu)\n", (unsigned long)GetLastError());
        return 1;
    }

    char subject[512] = {0};
    CertGetNameStringA(signer, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, subject, sizeof(subject));
    std::printf("ASSINATURA VALIDA\n  signatario: %s\n", subject);
    CertFreeCertificateContext(signer);

    // A timestamp is only worth the imprint it carries: "there is a token" is not
    // the same as "this token covers the signature in hand".
    const unsigned char kTsOid[] = {0x06, 0x0B, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D,
                                    0x01, 0x09, 0x10, 0x02, 0x0E};
    std::size_t oid_at = std::string::npos;
    for (std::size_t i = 0; i + sizeof(kTsOid) <= pkcs7.size(); ++i) {
        if (std::memcmp(pkcs7.data() + i, kTsOid, sizeof(kTsOid)) == 0) { oid_at = i; break; }
    }
    if (oid_at == std::string::npos) {
        std::printf("  carimbo de tempo: ausente\n");
        return 0;
    }

    // The OCTET STRING after the OID holds the token ContentInfo.
    const std::size_t tok_at = oid_at + sizeof(kTsOid);
    olho::tsa::bytes token;
    if (tok_at < pkcs7.size() && pkcs7[tok_at] == 0x31) {
        std::size_t len_at = tok_at + 1;
        std::size_t len = pkcs7[len_at++];
        if (len & 0x80) {
            const std::size_t n = len & 0x7F;
            len = 0;
            for (std::size_t i = 0; i < n; ++i) len = (len << 8) | pkcs7[len_at++];
        }
        const std::size_t end = len_at + len;
        if (end > pkcs7.size() || len < 2) { std::printf("  carimbo: malformado\n"); return 0; }
        // Inside the SET: SEQUENCE (ContentInfo) then OCTET STRING (signature).
        std::size_t q = len_at;
        if (pkcs7[q] == 0x30) {
            std::size_t l2 = pkcs7[q + 1];
            std::size_t hdr = 2;
            if (l2 & 0x80) {
                const std::size_t n = l2 & 0x7F;
                l2 = 0;
                for (std::size_t i = 0; i < n; ++i) l2 = (l2 << 8) | pkcs7[q + 2 + i];
                hdr = 2 + n;
            }
            token.assign(pkcs7.begin() + static_cast<std::ptrdiff_t>(q),
                         pkcs7.begin() + static_cast<std::ptrdiff_t>(q + hdr + l2));
        }
    }
    if (token.empty()) { std::printf("  carimbo: nao consegui extrair\n"); return 0; }

    // The imprint is SHA-256 of the signature value, which is the OCTET STRING
    // that precedes the unsigned attributes in the SignerInfo.
    std::size_t sig_octet = std::string::npos;
    for (std::size_t i = 0; i + 2 < oid_at; ++i) {
        if (pkcs7[i] == 0x04) {
            std::size_t len2 = pkcs7[i + 1];
            std::size_t hdr2 = 2;
            if (len2 & 0x80) {
                const std::size_t n = len2 & 0x7F;
                len2 = 0;
                for (std::size_t k = 0; k < n; ++k) len2 = (len2 << 8) | pkcs7[i + 2 + k];
                hdr2 = 2 + n;
            }
            if (len2 >= 128 && i + hdr2 + len2 <= oid_at) {
                sig_octet = i;
                break;
            }
        }
    }
    if (sig_octet == std::string::npos) {
        std::printf("  carimbo de tempo: presente, mas nao localizei a assinatura\n");
        return 0;
    }
    std::size_t hdr3 = 2;
    std::size_t sig_len2 = pkcs7[sig_octet + 1];
    if (sig_len2 & 0x80) {
        const std::size_t n = sig_len2 & 0x7F;
        sig_len2 = 0;
        for (std::size_t k = 0; k < n; ++k) sig_len2 = (sig_len2 << 8) | pkcs7[sig_octet + 2 + k];
        hdr3 = 2 + n;
    }
    olho::tsa::bytes signature(pkcs7.begin() + static_cast<std::ptrdiff_t>(sig_octet + hdr3),
                               pkcs7.begin() + static_cast<std::ptrdiff_t>(sig_octet + hdr3 + sig_len2));
    const olho::tsa::bytes imprint = olho::tsa::sha256(signature);

    std::string gen_time, tsa_subject;
    const auto ts = olho::tsa::verify_token(token, imprint, gen_time, tsa_subject);
    if (ts.ok) {
        std::printf("  carimbo de tempo: VALIDO\n    emitido em: %s\n    TSA: %s\n",
                    gen_time.c_str(), tsa_subject.c_str());
    } else {
        std::printf("  carimbo de tempo: INVALIDO (%s)\n", ts.text.c_str());
        return 1;
    }
    return 0;
}
