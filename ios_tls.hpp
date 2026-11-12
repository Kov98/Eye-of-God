// language: C++, file: ios_tls.hpp, target: Windows 11 (MSVC / MinGW g++)
// TLS over a byte pipe, using Schannel.
//
// WHY SCHANNEL AND NOT A TLS LIBRARY
//   Same policy as the rest of the project: no third-party crypto. Schannel is the
//   Windows stack, already audited, and it is a credential handle plus a state
//   machine — the application owns the record IO. That last part is what makes it
//   usable here: lockdown runs TLS over a usbmux byte pipe, not over a socket.
//
// THE VALIDATION DECISION, STATED PLAINLY
//   An iPhone presents a certificate issued by Apple's device CA. Normal chain
//   validation fails unless that root is in the machine store, and a forensic
//   workstation is often offline. allow_untrusted switches to manual validation:
//   the chain is NOT checked, and the peer subject is exposed so the operator can
//   record it. That flag is a deliberate weakening, named in the API so nobody can
//   enable it by accident.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace olho::ios {

using bytes = std::vector<std::uint8_t>;

// The pipe TLS rides on. Callbacks instead of an interface so the USB connection
// (and the loopback used by tests) do not have to share a base class.
struct tls_io {
    std::function<bool(bytes& out, std::uint32_t timeout_ms, bool& timed_out, std::string& error)> read;
    std::function<bool(const bytes& data, std::uint32_t timeout_ms, std::string& error)> write;
};

class tls_client {
public:
    tls_client() = default;
    ~tls_client();
    tls_client(const tls_client&) = delete;
    tls_client& operator=(const tls_client&) = delete;

    // Performs the handshake. Returns false with a diagnostic; never leaves a
    // half-open context behind.
    bool handshake(const tls_io& io, const std::string& server_name, bool allow_untrusted,
                   std::string& error);

    // Application data, encrypted on the way out and decrypted on the way in.
    bool send(const tls_io& io, const bytes& plain, std::string& error);
    bool receive(const tls_io& io, bytes& plain, std::string& error);

    void close();
    bool is_ready() const;

    // Subject of the certificate the peer presented, whatever the validation mode.
    const std::string& peer_subject() const { return peer_subject_; }

private:
    bool pull_record(const tls_io& io, std::string& error);
    bool decrypt_buffered(bytes& out, std::string& error);

    void* cred_ = nullptr;          // CredHandle*
    void* ctx_ = nullptr;           // CtxtHandle*
    bool ready_ = false;
    std::uint32_t header_max_ = 0;
    std::uint32_t trailer_max_ = 0;
    std::uint32_t block_max_ = 0;
    bytes cipher_buffer_;           // bytes read but not yet consumed by Schannel
    std::string peer_subject_;
};

}  // namespace olho::ios
