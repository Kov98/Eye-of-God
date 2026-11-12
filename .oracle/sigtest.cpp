#include <windows.h>
#include <wincrypt.h>
#include <cstdio>
#include <string>
#include <vector>

static PCCERT_CONTEXT find_cert() {
    HCERTSTORE s = CertOpenSystemStoreW(0, L"MY");
    if (!s) return nullptr;
    PCCERT_CONTEXT it = nullptr, chosen = nullptr;
    while ((it = CertEnumCertificatesInStore(s, it)) != nullptr) {
        DWORD n = 0;
        if (!CertGetCertificateContextProperty(it, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &n)) continue;
        chosen = CertDuplicateCertificateContext(it);
        break;
    }
    CertCloseStore(s, 0);
    return chosen;
}

static bool roundtrip(PCCERT_CONTEXT cert, const char* oid, const char* label,
                      const std::vector<BYTE>& data, bool tamper) {
    CRYPT_SIGN_MESSAGE_PARA sp{};
    sp.cbSize = sizeof(sp);
    sp.dwMsgEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    sp.pSigningCert = cert;
    sp.HashAlgorithm.pszObjId = const_cast<char*>(oid);

    std::vector<BYTE> msg = data;
    if (tamper) msg[0] ^= 0x01;

    const BYTE* parts[1] = {msg.data()};
    DWORD sizes[1] = {(DWORD)msg.size()};
    DWORD len = 0;
    if (!CryptSignMessage(&sp, TRUE, 1, parts, sizes, nullptr, &len)) {
        std::printf("%-10s %s: sign-size falhou (%lu)\n", label, tamper ? "adulterado" : "ok", (unsigned long)GetLastError());
        return false;
    }
    std::vector<BYTE> blob(len);
    if (!CryptSignMessage(&sp, TRUE, 1, parts, sizes, blob.data(), &len)) {
        std::printf("%-10s %s: sign falhou (%lu)\n", label, tamper ? "adulterado" : "ok", (unsigned long)GetLastError());
        return false;
    }

    // Verify against the ORIGINAL content; tampered run must fail.
    CRYPT_VERIFY_MESSAGE_PARA vp{};
    vp.cbSize = sizeof(vp);
    vp.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    PCCERT_CONTEXT signer = nullptr;
    const BYTE* vparts[1] = {data.data()};
    DWORD vsizes[1] = {(DWORD)data.size()};
    const BOOL ok = CryptVerifyDetachedMessageSignature(&vp, 0, blob.data(), len, 1, vparts, vsizes, &signer);
    std::printf("%-10s %-11s sign=ok len=%lu verify=%d err=%lu\n", label,
                tamper ? "adulterado" : "conteudo", (unsigned long)len, (int)ok,
                (unsigned long)GetLastError());
    if (signer) CertFreeCertificateContext(signer);
    return ok != 0;
}

int main() {
    const std::string msg = "conteudo de teste do laudo";
    std::vector<BYTE> data(msg.begin(), msg.end());
    PCCERT_CONTEXT cert = find_cert();
    if (!cert) { std::printf("sem certificado\n"); return 1; }

    // A valid signature must verify; the same signature against modified content must not.
    roundtrip(cert, szOID_NIST_sha256, "sha256", data, false);
    roundtrip(cert, szOID_NIST_sha256, "sha256", data, true);
    roundtrip(cert, "1.3.14.3.2.26", "sha1", data, false);
    roundtrip(cert, szOID_RSA_SHA1RSA, "sha1rsa", data, false);
    return 0;
}
