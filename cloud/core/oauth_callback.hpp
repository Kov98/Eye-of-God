#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace deep::cloud {

struct CallbackResult {
    std::string code;
    std::string state;
    std::string error;
    bool timed_out = false;
    bool ok() const { return !code.empty() && error.empty() && !timed_out; }
};

// Loopback-only OAuth redirect receiver. It binds 127.0.0.1 on an ephemeral
// port, accepts one GET, returns a fixed HTML page, and never logs the code or
// query string. The caller must validate `state` before exchanging the code.
class OAuthCallbackServer {
public:
    OAuthCallbackServer();
    ~OAuthCallbackServer();
    OAuthCallbackServer(const OAuthCallbackServer&) = delete;
    OAuthCallbackServer& operator=(const OAuthCallbackServer&) = delete;

    bool start(std::string& error);
    void stop();
    std::uint16_t port() const { return port_; }
    std::string redirect_uri() const;

    CallbackResult wait(std::chrono::seconds timeout);

private:
    std::uintptr_t socket_ = static_cast<std::uintptr_t>(~0ull);
    std::uint16_t port_ = 0;
    bool started_ = false;
};

} // namespace deep::cloud
