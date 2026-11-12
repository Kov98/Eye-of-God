// language: C++, file: .oracle/threads_test.cpp
// Prova de concorrencia: as mesmas entradas em N threads tem que produzir
// exatamente os mesmos resultados da execucao single-thread.
#include "custody_chain.hpp"
#include "evidence_verify.hpp"
#include "android_backup.hpp"
#include "pkcs7_builder.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static std::string sha256_hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string out;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return out;
    ULONG obj = 0, hl = 0, wr = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj, sizeof(obj), &wr, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hl, sizeof(hl), &wr, 0);
    std::vector<unsigned char> object(obj), digest(hl);
    static const char* hex = "0123456789abcdef";
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, object.data(), obj, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(h, (PUCHAR)const_cast<char*>(data.data()), (ULONG)data.size(), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(h, digest.data(), hl, 0))) {
        for (auto b : digest) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 15]); }
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : ".oracle/case_test/recovered";
    const int threads = 8;
    const int iterations = 40;

    olho::custody::chain base;
    std::string error;
    if (!olho::custody::build("GENESE-FIXA", dir, base, error)) {
        std::printf("build base falhou: %s\n", error.c_str());
        return 1;
    }
    const std::string base_sha = sha256_hex("conteudo fixo para o teste");
    const std::wstring probe = L".oracle/laudo.pdf";
    const std::string base_kind = olho::evidence::detect_kind(probe);
    std::printf("referencia: %zu elos, selo %s\n", base.entries.size(), base.head.substr(0, 16).c_str());

    std::atomic<int> mismatches{0};
    std::atomic<int> completed{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&]() {
            for (int i = 0; i < iterations; ++i) {
                if (sha256_hex("conteudo fixo para o teste") != base_sha) ++mismatches;
                if (olho::evidence::detect_kind(probe) != base_kind) ++mismatches;

                olho::custody::chain c;
                std::string e;
                if (!olho::custody::build("GENESE-FIXA", dir, c, e)) { ++mismatches; continue; }
                if (c.head != base.head) ++mismatches;
                std::size_t bad = 0;
                std::string badp, verr;
                if (!olho::custody::verify(c, bad, badp, verr)) ++mismatches;
                ++completed;
            }
        });
    }
    for (auto& th : pool) th.join();

    // Assinatura concorrente com a MESMA chave: e onde o handle de certificado, o
    // provedor CNG e o contexto por thread se cruzam.
    int signed_ok = 0;
    int signed_bad = 0;
    void* cert = nullptr;
    std::string subject, thumb;
    if (olho::pkcs7::pick_signing_certificate("", &cert, subject, thumb)) {
        std::vector<std::thread> signers;
        for (int t = 0; t < threads; ++t) {
            signers.emplace_back([&]() {
                for (int i = 0; i < 5; ++i) {
                    const std::string payload = "conteudo assinado em paralelo";
                    olho::pkcs7::bytes content(payload.begin(), payload.end());
                    olho::pkcs7::bytes der;
                    std::string serr;
                    if (!olho::pkcs7::sign_detached(cert, content, der, serr)) { ++signed_bad; continue; }
                    CRYPT_VERIFY_MESSAGE_PARA vp{};
                    vp.cbSize = sizeof(vp);
                    vp.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
                    PCCERT_CONTEXT signer = nullptr;
                    const BYTE* parts[1] = {content.data()};
                    DWORD sizes[1] = {(DWORD)content.size()};
                    BOOL ok = CryptVerifyDetachedMessageSignature(&vp, 0, der.data(), (DWORD)der.size(),
                                                                  1, parts, sizes, &signer);
                    if (signer) CertFreeCertificateContext(signer);
                    if (ok) ++signed_ok; else ++signed_bad;
                }
            });
        }
        for (auto& th : signers) th.join();
        olho::pkcs7::free_certificate(cert);
    }

    // extract_ab concorrente sobre o MESMO arquivo: exercita o crc32/adler que
    // antes era preenchido atras de um bool sem sincronizacao.
    int ab_ok = 0;
    int ab_bad = 0;
    {
        const std::wstring ab = L".oracle_android/fixtures/v2_none_zlib.ab";
        olho::android::AbStats probe_stats;
        const bool base_ok = olho::android::extract_ab(ab, L".oracle/ab_base", "", probe_stats);
        if (!base_ok) {
            std::printf("  [debug] baseline .ab falhou: %s", probe_stats.error.c_str());
            std::printf(" (integrity=%d bytes=%llu)", (int)probe_stats.integrity_ok,
                        (unsigned long long)probe_stats.tar.bytes_written);
            std::puts("");
        }
        if (base_ok) {
            std::vector<std::thread> workers;
            for (int t = 0; t < 6; ++t) {
                workers.emplace_back([&, t]() {
                    const std::wstring out = L".oracle/ab_t" + std::to_wstring(t);
                    olho::android::AbStats st;
                    if (olho::android::extract_ab(ab, out, "", st) && st.integrity_ok &&
                        st.tar.bytes_written == probe_stats.tar.bytes_written) {
                        ++ab_ok;
                    } else {
                        ++ab_bad;
                    }
                });
            }
            for (auto& w : workers) w.join();
        }
    }

    std::printf("threads=%d iteracoes=%d  operacoes=%d  divergencias=%d\n",
                threads, iterations, completed.load(), mismatches.load());
    std::printf("assinaturas concorrentes: %d verificadas, %d falharam\n", signed_ok, signed_bad);
    std::printf("extracao .ab concorrente: %d ok, %d falharam\n", ab_ok, ab_bad);
    // ab_ok > 0 is required on purpose: a stage that silently did no work must not
    // report success. Zero operations and zero failures is not a pass.
    return (mismatches.load() == 0 && signed_bad == 0 && signed_ok > 0 && ab_bad == 0 &&
            ab_ok > 0) ? 0 : 1;
}