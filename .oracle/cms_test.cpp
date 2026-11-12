#include "pkcs7_builder.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <string>
#include <vector>

int main() {
    void* cert = nullptr;
    std::string subject, thumb, error;
    if (!olho::pkcs7::pick_signing_certificate("", &cert, subject, thumb)) {
        std::printf("sem certificado\n");
        return 1;
    }
    std::printf("cert: %s\n", subject.c_str());

    const std::string msg = "bytes exatos que o laudo assina";
    std::vector<std::uint8_t> content(msg.begin(), msg.end());
    std::vector<std::uint8_t> der;
    if (!olho::pkcs7::sign_detached(cert, content, der, error)) {
        std::printf("assinatura falhou: %s\n", error.c_str());
        return 1;
    }
    std::printf("CMS: %zu bytes\n", der.size());

    CRYPT_VERIFY_MESSAGE_PARA vp{};
    vp.cbSize = sizeof(vp);
    vp.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    PCCERT_CONTEXT signer = nullptr;
    const BYTE* parts[1] = {content.data()};
    DWORD sizes[1] = {static_cast<DWORD>(content.size())};
    BOOL ok = CryptVerifyDetachedMessageSignature(&vp, 0, der.data(), static_cast<DWORD>(der.size()),
                                                  1, parts, sizes, &signer);
    std::printf("verify(conteudo) ok=%d err=%lu\n", (int)ok, (unsigned long)GetLastError());
    if (signer) CertFreeCertificateContext(signer);

    std::vector<std::uint8_t> tampered = content;
    tampered[0] ^= 0x01;
    const BYTE* parts2[1] = {tampered.data()};
    DWORD sizes2[1] = {static_cast<DWORD>(tampered.size())};
    PCCERT_CONTEXT s2 = nullptr;
    BOOL ok2 = CryptVerifyDetachedMessageSignature(&vp, 0, der.data(), static_cast<DWORD>(der.size()),
                                                   1, parts2, sizes2, &s2);
    std::printf("verify(adulterado) ok=%d err=%lu\n", (int)ok2, (unsigned long)GetLastError());
    if (s2) CertFreeCertificateContext(s2);
    olho::pkcs7::free_certificate(cert);
    return ok ? 0 : 1;
}