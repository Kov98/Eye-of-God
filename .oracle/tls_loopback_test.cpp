// language: C++, file: .oracle/tls_loopback_test.cpp
// Prova o cliente TLS sem iPhone: um servidor Schannel no mesmo processo, ligado
// ao cliente por dois pipes em memoria. Handshake real, dados reais.
#include "ios_tls.hpp"
#include "pkcs7_builder.hpp"

#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#include <wincrypt.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using olho::ios::bytes;

// ---------------------------------------------------------------- byte pipe
class pipe {
public:
    void push(const bytes& d) {
        std::lock_guard<std::mutex> lock(m_);
        buffer_.insert(buffer_.end(), d.begin(), d.end());
        cv_.notify_all();
    }
    bool pop(bytes& out, std::uint32_t timeout_ms, bool& timed_out) {
        std::unique_lock<std::mutex> lock(m_);
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [&] { return !buffer_.empty() || closed_; })) {
            timed_out = true;
            return false;
        }
        timed_out = false;
        if (buffer_.empty()) return false;
        out = buffer_;
        buffer_.clear();
        return true;
    }
    void close() {
        std::lock_guard<std::mutex> lock(m_);
        closed_ = true;
        cv_.notify_all();
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    bytes buffer_;
    bool closed_ = false;
};

int main() {
    // Certificado do repositorio MY, o mesmo que o laudo usa.
    void* cert = nullptr;
    std::string subject, thumb;
    if (!olho::pkcs7::pick_signing_certificate("", &cert, subject, thumb)) {
        std::printf("sem certificado no repositorio MY\n");
        return 1;
    }
    std::printf("certificado: %s\n", subject.c_str());

    pipe to_server, to_client;
    bool server_ok = false;

    std::thread server([&]() {
        auto* cred = new CredHandle{};
        SCHANNEL_CRED settings{};
        settings.dwVersion = SCHANNEL_CRED_VERSION;
        // paCred is an ARRAY of certificate pointers. Casting the certificate
        // itself makes Schannel read the CERT_CONTEXT struct as a pointer table.
        PCCERT_CONTEXT cert_list[1] = {static_cast<PCCERT_CONTEXT>(cert)};
        settings.paCred = cert_list;
        settings.cCreds = 1;
        settings.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_CRED_NO_DEFAULT_CREDS;
        settings.grbitEnabledProtocols = SP_PROT_TLS1_2;
        TimeStamp expiry{};
        if (AcquireCredentialsHandleA(nullptr, const_cast<SEC_CHAR*>(UNISP_NAME_A),
                                      SECPKG_CRED_INBOUND, nullptr, &settings, nullptr, nullptr,
                                      cred, &expiry) != SEC_E_OK) {
            std::printf("  [server] AcquireCredentialsHandle falhou\n");
            return;
        }

        CtxtHandle ctx{};
        bool have = false;
        bytes pending;
        for (int round = 0; round < 16; ++round) {
            // The first AcceptSecurityContext must see a real token: a null input
            // is answered with SEC_E_INVALID_TOKEN by this stack.
            if (pending.empty()) {
                bytes record; bool timed_out = false;
                if (!to_server.pop(record, 5000, timed_out)) {
                    std::puts("  [server] sem bytes do cliente");
                    return;
                }
                pending = record;
            }
            SecBuffer in{};
            SecBufferDesc in_desc{};
            if (!pending.empty()) {
                in.BufferType = SECBUFFER_TOKEN;
                in.pvBuffer = pending.data();
                in.cbBuffer = static_cast<unsigned long>(pending.size());
                in_desc.ulVersion = SECBUFFER_VERSION;
                in_desc.cBuffers = 1;
                in_desc.pBuffers = &in;
            }
            SecBuffer out{};
            out.BufferType = SECBUFFER_TOKEN;
            SecBufferDesc out_desc{};
            out_desc.ulVersion = SECBUFFER_VERSION;
            out_desc.cBuffers = 1;
            out_desc.pBuffers = &out;
            DWORD flags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
                          ASC_REQ_STREAM | ASC_REQ_ALLOCATE_MEMORY;

            const SECURITY_STATUS st = AcceptSecurityContext(
                cred, have ? &ctx : nullptr, pending.empty() ? nullptr : &in_desc, flags,
                SECURITY_NATIVE_DREP, have ? nullptr : &ctx, &out_desc, &flags, &expiry);
            have = true;

            if (out.cbBuffer > 0 && out.pvBuffer) {
                to_client.push(bytes(static_cast<std::uint8_t*>(out.pvBuffer),
                                     static_cast<std::uint8_t*>(out.pvBuffer) + out.cbBuffer));
                FreeContextBuffer(out.pvBuffer);
            }
            if (st == SEC_E_OK) break;
            if (st != SEC_I_CONTINUE_NEEDED && st != SEC_E_INCOMPLETE_MESSAGE) {
                std::printf("  [server] AcceptSecurityContext parou em 0x%08lx\n",
                            static_cast<unsigned long>(st));
                return;
            }
            bytes record; bool timed_out = false;
            if (!to_server.pop(record, 5000, timed_out)) {
                std::printf("  [server] sem bytes do cliente\n");
                return;
            }
            pending = record;
        }

        // Le um registro, decifra, devolve o mesmo texto.
        bytes cipher; bool timed_out = false;
        if (to_server.pop(cipher, 5000, timed_out)) {
            SecBuffer parts[4]{};
            parts[0].BufferType = SECBUFFER_DATA;
            parts[0].pvBuffer = cipher.data();
            parts[0].cbBuffer = static_cast<unsigned long>(cipher.size());
            parts[1].BufferType = SECBUFFER_EMPTY;
            parts[2].BufferType = SECBUFFER_EMPTY;
            parts[3].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc desc{};
            desc.ulVersion = SECBUFFER_VERSION;
            desc.cBuffers = 4;
            desc.pBuffers = parts;
            if (DecryptMessage(&ctx, &desc, 0, nullptr) == SEC_E_OK) {
                SecPkgContext_StreamSizes sizes{};
                QueryContextAttributesA(&ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
                // Take the buffer Schannel marked as DATA: position is not guaranteed.
                bytes plain;
                for (int i = 0; i < 4; ++i) {
                    if (parts[i].BufferType == SECBUFFER_DATA && parts[i].cbBuffer > 0) {
                        const auto* p0 = static_cast<std::uint8_t*>(parts[i].pvBuffer);
                        plain.assign(p0, p0 + parts[i].cbBuffer);
                        break;
                    }
                }

                bytes record(sizes.cbHeader + plain.size() + sizes.cbTrailer);
                SecBuffer out_parts[3]{};
                out_parts[0].BufferType = SECBUFFER_STREAM_HEADER;
                out_parts[0].pvBuffer = record.data();
                out_parts[0].cbBuffer = sizes.cbHeader;
                out_parts[1].BufferType = SECBUFFER_DATA;
                out_parts[1].pvBuffer = record.data() + sizes.cbHeader;
                out_parts[1].cbBuffer = static_cast<unsigned long>(plain.size());
                out_parts[2].BufferType = SECBUFFER_STREAM_TRAILER;
                out_parts[2].pvBuffer = record.data() + sizes.cbHeader + plain.size();
                out_parts[2].cbBuffer = sizes.cbTrailer;
                std::memcpy(out_parts[1].pvBuffer, plain.data(), plain.size());
                SecBufferDesc out_desc{};
                out_desc.ulVersion = SECBUFFER_VERSION;
                out_desc.cBuffers = 3;
                out_desc.pBuffers = out_parts;
                if (EncryptMessage(&ctx, 0, &out_desc, 0) == SEC_E_OK) {
                    record.resize(out_parts[0].cbBuffer + out_parts[1].cbBuffer + out_parts[2].cbBuffer);
                    to_client.push(record);
                    server_ok = true;
                }
            }
        }
        DeleteSecurityContext(&ctx);
        FreeCredentialsHandle(cred);
        delete cred;
    });

    olho::ios::tls_io io;
    io.read = [&](bytes& out, std::uint32_t timeout, bool& timed_out, std::string&) {
        return to_client.pop(out, timeout, timed_out);
    };
    io.write = [&](const bytes& data, std::uint32_t, std::string&) {
        to_server.push(data);
        return true;
    };

    olho::ios::tls_client client;
    std::string error;
    // Target name matches the certificate subject: with manual validation the name
    // is not enforced, but keeping them aligned removes a variable while proving
    // the plumbing.
    const bool handshook = client.handshake(io, subject, true, error);
    std::printf("handshake do cliente: %s%s\n", handshook ? "OK" : "FALHOU",
                handshook ? "" : (" - " + error).c_str());

    int failures = 0;
    if (!handshook) { ++failures; }
    else {
        std::printf("sujeito do par: %s\n", client.peer_subject().c_str());
        const std::string payload = "requisicao de lockdown pela metade do caminho";
        bytes plain(payload.begin(), payload.end());
        if (!client.send(io, plain, error)) {
            std::printf("envio falhou: %s\n", error.c_str());
            ++failures;
        } else {
            bytes echo;
            if (!client.receive(io, echo, error)) {
                std::printf("recebimento falhou: %s\n", error.c_str());
                ++failures;
            } else {
                const std::string got(echo.begin(), echo.end());
                std::printf("eco recebido: '%s'\n", got.c_str());
                if (got != payload) { std::printf("  conteudo diferente!\n"); ++failures; }
            }
        }
    }

    to_server.close();
    to_client.close();
    server.join();
    olho::pkcs7::free_certificate(cert);

    std::printf("%s\n", (failures == 0 && server_ok) ? "TLS LOOPBACK OK" : "TLS LOOPBACK FALHOU");
    return (failures == 0 && server_ok) ? 0 : 1;
}