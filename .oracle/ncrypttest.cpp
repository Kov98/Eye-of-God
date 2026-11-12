#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <cstdio>
#include <vector>

int main() {
    HCERTSTORE s = CertOpenSystemStoreW(0, L"MY");
    PCCERT_CONTEXT c = nullptr, cert = nullptr;
    while (s && (c = CertEnumCertificatesInStore(s, c)) != nullptr) {
        DWORD n = 0;
        if (!CertGetCertificateContextProperty(c, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &n)) continue;
        cert = CertDuplicateCertificateContext(c);
        break;
    }
    if (!cert) { std::printf("sem cert\n"); return 1; }

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE prov = 0;
    DWORD spec = 0;
    BOOL free_key = FALSE;
    if (!CryptAcquireCertificatePrivateKey(cert, CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG, nullptr,
                                           &prov, &spec, &free_key)) { std::printf("acquire falhou\n"); return 1; }

    NCRYPT_KEY_HANDLE key = (NCRYPT_KEY_HANDLE)prov;
    std::vector<BYTE> digest(32, 0xAB);
    BCRYPT_PKCS1_PADDING_INFO pad{};
    pad.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    DWORD sig_len = 0;
    SECURITY_STATUS st = NCryptSignHash(key, &pad, digest.data(), (DWORD)digest.size(),
                                        nullptr, 0, &sig_len, BCRYPT_PAD_PKCS1);
    std::printf("size st=0x%08lx len=%lu\n", (unsigned long)st, (unsigned long)sig_len);
    std::vector<BYTE> sig(sig_len ? sig_len : 1);
    st = NCryptSignHash(key, &pad, digest.data(), (DWORD)digest.size(),
                        sig.data(), (DWORD)sig.size(), &sig_len, BCRYPT_PAD_PKCS1);
    std::printf("sign st=0x%08lx len=%lu\n", (unsigned long)st, (unsigned long)sig_len);
    if (st == 0) {
        std::printf("primeiros bytes: ");
        for (int i = 0; i < 8 && i < (int)sig_len; ++i) std::printf("%02x", sig[i]);
        std::printf("\n");
    }
    if (free_key) NCryptFreeObject(key);
    return 0;
}
