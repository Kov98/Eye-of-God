#include <windows.h>
#include <wincrypt.h>
#include <cstdio>
int main() {
    HCERTSTORE store = CertOpenSystemStoreW(0, L"MY");
    std::printf("MY store: %s\n", store ? "aberto" : "falhou");
    if (store) {
        int n = 0;
        PCCERT_CONTEXT c = nullptr;
        while ((c = CertEnumCertificatesInStore(store, c)) != nullptr) {
            char name[256] = {0};
            CertGetNameStringA(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, sizeof(name));
            std::printf("  cert %d: %s\n", ++n, name);
        }
        std::printf("total no MY: %d\n", n);
        CertCloseStore(store, 0);
    }
    std::printf("sizeof(PROV_ENUMALGS)=%zu\n", sizeof(PROV_ENUMALGS));
    return 0;
}
