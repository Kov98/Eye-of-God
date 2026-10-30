#pragma once

#include "cloud/enterprise/audit.hpp"
#include "cloud/enterprise/session.hpp"
#include "cloud/core/oidc.hpp"
#include <functional>
#include <mutex>
#include <cstdint>
#include "job_queue.hpp"
#include "rate_limiter.hpp"

#include <string>

namespace deep::cloud {

struct ApiRequest {
    std::string method;
    std::string path;
    std::string authorization;
    std::string body;
};

struct ApiResponse {
    int status = 500;
    std::string content_type = "application/json";
    std::string body;
};

// Transport-independent API used by the loopback HTTP service. Every route
// except /health and POST /v1/session requires a bearer session token.
class CloudApiService {
public:
    CloudApiService(std::string case_dir, std::string identity_path,
                    std::string audit_path, int requests_per_minute = 120);

    ApiResponse handle(const ApiRequest& request);
    using OidcVerifyFn = std::function<JwtResult(
        std::string_view issuer, std::string_view audience, std::string_view token)>;
    void set_oidc_verifier(OidcVerifyFn verifier);
    void set_oidc_configuration(std::string issuer, std::string client_id);

private:
    ApiResponse unauthorized();
    ApiResponse error(int status, const std::string& message);
    ApiResponse health();
    ApiResponse config();
    ApiResponse login(const ApiRequest& request);
    ApiResponse oidc_login(const ApiRequest& request);
    ApiResponse logout(const ApiRequest& request);
    ApiResponse jobs(const ApiRequest& request);
    ApiResponse audit(const ApiRequest& request);
    ApiResponse verify(const ApiRequest& request);
    ApiResponse enqueue(const ApiRequest& request);
    ApiResponse queue(const ApiRequest& request);
    ApiResponse metrics(const ApiRequest& request);
    ApiResponse prometheus(const ApiRequest& request);
    ApiResponse cancel(const ApiRequest& request, const std::string& job_id);
    ApiResponse pause(const ApiRequest& request, const std::string& job_id);
    ApiResponse resume(const ApiRequest& request, const std::string& job_id);

    std::string case_dir_;
    std::string identity_path_;
    AuditLog audit_;
    SessionManager sessions_;
    JobQueue queue_;
    RateLimiter limiter_;
    OidcVerifyFn oidc_verifier_;
    std::string oidc_issuer_;
    std::string oidc_client_id_;
    mutable std::mutex oidc_mutex_;
    OidcConfiguration oidc_configuration_;
    std::string oidc_jwks_;
    std::int64_t oidc_cache_expires_ = 0;
};

} // namespace deep::cloud
