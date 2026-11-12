// language: C++, file: ios_tls.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "ios_tls.hpp"

#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <sspi.h>

#include <cstring>

#pragma comment(lib, "secur32.lib")

namespace olho::ios {
namespace {

constexpr std::uint32_t kTimeout = 5000;

void free_cred(void* p) {
    if (!p) return;
    auto* cred = static_cast<CredHandle*>(p);
    FreeCredentialsHandle(cred);
    delete cred;
}

void free_ctx(void* p) {
    if (!p) return;
    auto* ctx = static_cast<CtxtHandle*>(p);
    DeleteSecurityContext(ctx);
    delete ctx;
}

// Schannel reports many conditions as SEC_E_* status codes rather than Win32
// errors, so the human text has to come from the security subsystem.
std::string sspi_text(SECURITY_STATUS status) {
    char buffer[256] = {0};
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                       static_cast<DWORD>(status), 0, buffer, sizeof(buffer) - 1, nullptr)) {
        std::string text(buffer);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        return text;
    }
    char hex[32];
    std::snprintf(hex, sizeof(hex), "0x%08lX", static_cast<unsigned long>(status));
    return hex;
}

std::string cert_subject(PCCERT_CONTEXT cert) {
    if (!cert) return {};
    char name[512] = {0};
    CertGetNameStringA(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, sizeof(name));
    return std::string(name);
}

}  // namespace

tls_client::~tls_client() { close(); }

void tls_client::close() {
    ready_ = false;
    free_ctx(ctx_);
    ctx_ = nullptr;
    free_cred(cred_);
    cred_ = nullptr;
    cipher_buffer_.clear();
}

bool tls_client::is_ready() const { return ready_; }

bool tls_client::handshake(const tls_io& io, const std::string& server_name, bool allow_untrusted,
                           std::string& error) {
    close();
    if (!io.read || !io.write) { error = "tls_io incompleto"; return false; }

    // Credentials. Manual validation is the only way to accept a device
    // certificate that no machine store knows about.
    auto* cred = new CredHandle{};
    SCHANNEL_CRED settings{};
    settings.dwVersion = SCHANNEL_CRED_VERSION;
    // Pin TLS 1.2: it is what locked-down services actually negotiate, and TLS 1.3
    // on this stack rejects a self-signed RSA peer with SEC_E_ILLEGAL_MESSAGE even
    // under manual validation.
    settings.grbitEnabledProtocols = SP_PROT_TLS1_2;
    settings.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;
    if (allow_untrusted) settings.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION;
    settings.dwMinimumCipherStrength = 128;

    TimeStamp expiry{};
    SECURITY_STATUS status =
        AcquireCredentialsHandleA(nullptr, const_cast<SEC_CHAR*>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND,
                                  nullptr, &settings, nullptr, nullptr, cred, &expiry);
    if (status != SEC_E_OK) {
        error = "AcquireCredentialsHandle falhou: " + sspi_text(status);
        delete cred;
        return false;
    }
    cred_ = cred;

    auto* ctx = new CtxtHandle{};
    SecBuffer out_token{};
    out_token.BufferType = SECBUFFER_TOKEN;
    out_token.pvBuffer = nullptr;
    SecBufferDesc out_desc{};
    out_desc.ulVersion = SECBUFFER_VERSION;
    out_desc.cBuffers = 1;
    out_desc.pBuffers = &out_token;

    // Only context requirements belong here. ISC_REQ_MANUAL_CRED_VALIDATION and
    // ISC_REQ_USE_SUPPLIED_CREDS are credential flags; passing them as context
    // flags makes Schannel build a malformed handshake (SEC_E_INVALID_TOKEN).
    constexpr DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                            ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
                            ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

    SecBuffer in_token{};
    SecBufferDesc in_desc{};
    const std::string target = server_name;
    bytes pending;
    bool first = true;

    // Two input buffers: the token, and a slot Schannel fills with SECBUFFER_EXTRA
    // for bytes it did not consume. Re-feeding consumed bytes is exactly what
    // produces SEC_E_ILLEGAL_MESSAGE, so the leftover is carried forward instead.
    SecBuffer in_extra{};

    for (int round = 0; round < 16; ++round) {
        DWORD out_flags = 0;
        SecBuffer in_parts[2]{};
        if (!first && !pending.empty()) {
            in_parts[0].BufferType = SECBUFFER_TOKEN;
            in_parts[0].pvBuffer = pending.data();
            in_parts[0].cbBuffer = static_cast<unsigned long>(pending.size());
            in_parts[1].BufferType = SECBUFFER_EMPTY;
            in_parts[1].pvBuffer = nullptr;
            in_parts[1].cbBuffer = 0;
            in_desc.ulVersion = SECBUFFER_VERSION;
            in_desc.cBuffers = 2;
            in_desc.pBuffers = in_parts;
        }

        status = InitializeSecurityContextA(
            cred, first ? nullptr : ctx, const_cast<SEC_CHAR*>(target.c_str()), flags, 0,
            SECURITY_NATIVE_DREP, (!first && !pending.empty()) ? &in_desc : nullptr, 0,
            first ? ctx : nullptr, &out_desc, &out_flags, &expiry);

        if (out_token.cbBuffer > 0 && out_token.pvBuffer) {
            const bytes chunk(static_cast<std::uint8_t*>(out_token.pvBuffer),
                              static_cast<std::uint8_t*>(out_token.pvBuffer) + out_token.cbBuffer);
            if (!io.write(chunk, kTimeout, error)) {
                FreeContextBuffer(out_token.pvBuffer);
                delete ctx;
                return false;
            }
            FreeContextBuffer(out_token.pvBuffer);
            out_token.pvBuffer = nullptr;
            out_token.cbBuffer = 0;
        }

        // Keep only what Schannel did not consume.
        if (!first && !pending.empty()) {
            if (in_parts[1].BufferType == SECBUFFER_EXTRA && in_parts[1].cbBuffer > 0) {
                const std::size_t keep = in_parts[1].cbBuffer;
                pending.erase(pending.begin(),
                              pending.end() - static_cast<std::ptrdiff_t>(keep));
            } else {
                pending.clear();
            }
        }

        if (status == SEC_E_OK) {
            ctx_ = ctx;
            break;
        }
        if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_INCOMPLETE_MESSAGE) {
            error = "InitializeSecurityContext falhou: " + sspi_text(status);
            delete ctx;
            return false;
        }

        bytes record;
        bool timed_out = false;
        if (!io.read(record, kTimeout, timed_out, error)) {
            if (timed_out) error = "timeout no handshake TLS";
            delete ctx;
            return false;
        }
        pending.insert(pending.end(), record.begin(), record.end());
        first = false;
    }

    if (!ctx_) { error = "handshake nao convergiu"; return false; }

    SecPkgContext_StreamSizes sizes{};
    status = QueryContextAttributesA(static_cast<CtxtHandle*>(ctx_), SECPKG_ATTR_STREAM_SIZES, &sizes);
    if (status != SEC_E_OK) {
        error = "SECPKG_ATTR_STREAM_SIZES falhou: " + sspi_text(status);
        return false;
    }
    header_max_ = sizes.cbHeader;
    trailer_max_ = sizes.cbTrailer;
    block_max_ = sizes.cbMaximumMessage;

    PCCERT_CONTEXT remote = nullptr;
    if (QueryContextAttributesA(static_cast<CtxtHandle*>(ctx_), SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                &remote) == SEC_E_OK) {
        peer_subject_ = cert_subject(remote);
        if (remote) CertFreeCertificateContext(remote);
    }

    // Whatever the peer sent past the final handshake token is application data
    // already on the wire. Dropping it loses the first record — and if that record
    // straddles the boundary, the next decrypt sees a torn frame.
    if (!pending.empty()) cipher_buffer_ = pending;

    ready_ = true;
    return true;
}

bool tls_client::pull_record(const tls_io& io, std::string& error) {
    bytes record;
    bool timed_out = false;
    if (!io.read(record, kTimeout, timed_out, error)) {
        if (timed_out) error = "timeout lendo registro TLS";
        return false;
    }
    cipher_buffer_.insert(cipher_buffer_.end(), record.begin(), record.end());
    return true;
}

bool tls_client::decrypt_buffered(bytes& out, std::string& error) {
    for (;;) {
        if (cipher_buffer_.empty()) return true;
        SecBuffer parts[4]{};
        parts[0].BufferType = SECBUFFER_DATA;
        parts[0].pvBuffer = cipher_buffer_.data();
        parts[0].cbBuffer = static_cast<unsigned long>(cipher_buffer_.size());
        parts[1].BufferType = SECBUFFER_EMPTY;
        parts[2].BufferType = SECBUFFER_EMPTY;
        parts[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc desc{};
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = parts;

        const SECURITY_STATUS status =
            DecryptMessage(static_cast<CtxtHandle*>(ctx_), &desc, 0, nullptr);
        if (status == SEC_E_INCOMPLETE_MESSAGE) return true;   // ask for more bytes
        if (status == SEC_I_CONTEXT_EXPIRED) return true;
        if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE) {
            error = "DecryptMessage falhou: " + sspi_text(status);
            return false;
        }
        // Schannel RELABELS the buffers: after the call the plaintext is in
        // whichever one is marked SECBUFFER_DATA, not necessarily the first.
        // Reading parts[0] blindly returns a slice of header.
        bytes leftover;
        for (int i = 0; i < 4; ++i) {
            if (parts[i].BufferType == SECBUFFER_DATA && parts[i].cbBuffer > 0 && parts[i].pvBuffer) {
                const auto* p = static_cast<std::uint8_t*>(parts[i].pvBuffer);
                out.insert(out.end(), p, p + parts[i].cbBuffer);
            } else if (parts[i].BufferType == SECBUFFER_EXTRA && parts[i].cbBuffer > 0) {
                const auto* p = static_cast<std::uint8_t*>(parts[i].pvBuffer);
                leftover.assign(p, p + parts[i].cbBuffer);
            }
        }
        cipher_buffer_.swap(leftover);
        if (cipher_buffer_.empty()) return true;
    }
}

bool tls_client::receive(const tls_io& io, bytes& plain, std::string& error) {
    plain.clear();
    if (!ready_) { error = "TLS nao iniciado"; return false; }
    if (!decrypt_buffered(plain, error)) return false;
    if (!plain.empty()) return true;
    if (!pull_record(io, error)) return false;
    return decrypt_buffered(plain, error);
}

bool tls_client::send(const tls_io& io, const bytes& plain, std::string& error) {
    if (!ready_) { error = "TLS nao iniciado"; return false; }
    if (plain.empty()) return true;

    std::size_t offset = 0;
    while (offset < plain.size()) {
        const std::size_t chunk = (plain.size() - offset > block_max_) ? block_max_ : plain.size() - offset;
        bytes record(header_max_ + chunk + trailer_max_);

        SecBuffer parts[3]{};
        parts[0].BufferType = SECBUFFER_STREAM_HEADER;
        parts[0].pvBuffer = record.data();
        parts[0].cbBuffer = header_max_;
        parts[1].BufferType = SECBUFFER_DATA;
        parts[1].pvBuffer = record.data() + header_max_;
        parts[1].cbBuffer = static_cast<unsigned long>(chunk);
        parts[2].BufferType = SECBUFFER_STREAM_TRAILER;
        parts[2].pvBuffer = record.data() + header_max_ + chunk;
        parts[2].cbBuffer = trailer_max_;
        std::memcpy(parts[1].pvBuffer, plain.data() + offset, chunk);

        SecBufferDesc desc{};
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 3;
        desc.pBuffers = parts;

        const SECURITY_STATUS status =
            EncryptMessage(static_cast<CtxtHandle*>(ctx_), 0, &desc, 0);
        if (status != SEC_E_OK) {
            error = "EncryptMessage falhou: " + sspi_text(status);
            return false;
        }
        record.resize(parts[0].cbBuffer + parts[1].cbBuffer + parts[2].cbBuffer);
        if (!io.write(record, kTimeout, error)) return false;
        offset += chunk;
    }
    return true;
}

}  // namespace olho::ios
