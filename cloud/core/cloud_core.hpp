#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deep::cloud {

using Clock = std::chrono::system_clock;
using Millis = std::chrono::milliseconds;

struct HttpRequest {
    std::string method;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse {
    int status = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    bool transport_error = false;
    std::string error;

    bool ok() const {
        return !transport_error && status >= 200 && status < 300;
    }
    std::string header(std::string_view name) const;
};

// --- Streaming download --------------------------------------------------

// Incremental SHA-256 (BCrypt). One instance hashes exactly one byte stream:
// update() accepts arbitrarily sized chunks and finalize() is called once,
// after which hex_digest() returns the lower-case digest.
class Sha256Hasher {
public:
    Sha256Hasher();
    ~Sha256Hasher();
    Sha256Hasher(Sha256Hasher&&) noexcept;
    Sha256Hasher& operator=(Sha256Hasher&&) noexcept;
    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;

    bool valid() const;
    bool update(const void* data, std::size_t length);
    bool finalize();
    std::string hex_digest() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Chunked, hashing file sink used by streaming downloads. Bytes are written to
// "<destination>.part"; commit() atomically renames the finished part file onto
// the destination and freezes the digest, while discard() (and the destructor
// of an uncommitted sink) removes the part file, so a failed stream never
// leaves an artifact behind. A limit of 0 means unlimited.
class DownloadSink {
public:
    DownloadSink();
    ~DownloadSink();
    DownloadSink(DownloadSink&&) noexcept;
    DownloadSink& operator=(DownloadSink&&) noexcept;
    DownloadSink(const DownloadSink&) = delete;
    DownloadSink& operator=(const DownloadSink&) = delete;

    bool open(const std::string& destination, std::uint64_t max_bytes, std::string& error);
    bool write(const char* data, std::size_t length, std::string& error);
    bool commit(std::string& error);
    void discard();

    bool active() const;
    bool limit_exceeded() const;
    std::uint64_t bytes() const;
    const std::string& sha256() const;
    const std::string& destination() const;
    const std::string& part_path() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct HttpDownloadRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    // Final path of the downloaded file. The transport writes a ".part"
    // sibling and only renames it into place after a complete response.
    std::string destination_path;
    std::uint64_t max_bytes = 0; // 0 = unlimited
};

struct HttpDownloadResult {
    int status = 0;
    bool ok = false;
    bool transport_error = false;
    bool limit_exceeded = false;
    bool redirected = false;
    std::uint64_t bytes = 0;
    std::string sha256;
    // Populated for redirect responses only; callers must validate the host
    // before issuing another request and must never forward the bearer token.
    std::string location;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string error;
};

// Case-insensitive header lookup shared by responses and downloads.
std::string find_header_value(
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string_view name);

// Parses a base-10 unsigned integer (for example Content-Length). Returns false
// for empty, signed or non-numeric input and on overflow.
bool parse_decimal_u64(std::string_view text, std::uint64_t& value);

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
    // Streams the response body into request.destination_path. Transports that
    // cannot stream report a transport error instead of buffering the body.
    virtual HttpDownloadResult download(const HttpDownloadRequest& request);
};

struct MockDownloadSpec {
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::string> chunks;
    // Deterministic repeated chunk appended after `chunks`, so tests can
    // exercise payloads larger than any practical in-memory fixture.
    std::size_t repeated_chunk_size = 0;
    std::size_t repeated_chunk_count = 0;
    unsigned char repeated_byte = 'x';
    bool transport_error = false;
    std::string error;
    // Simulates a stream abort after this many bytes were delivered (0 = off).
    std::uint64_t fail_after_bytes = 0;
    std::string failure_error = "mock download stream failed";
};

// Deterministic transport used by tests and by the mock Graph server.
class MockHttpTransport final : public HttpTransport {
public:
    void push(HttpResponse response) { responses_.push_back(std::move(response)); }
    void push_download(MockDownloadSpec spec) { downloads_.push_back(std::move(spec)); }
    HttpResponse send(const HttpRequest& request) override;
    HttpDownloadResult download(const HttpDownloadRequest& request) override;
    const std::vector<HttpRequest>& requests() const { return requests_; }
    const std::vector<HttpDownloadRequest>& download_requests() const {
        return download_requests_;
    }
    bool empty() const { return responses_.empty(); }
    bool empty_downloads() const { return downloads_.empty(); }

private:
    std::deque<HttpResponse> responses_;
    std::deque<MockDownloadSpec> downloads_;
    std::vector<HttpRequest> requests_;
    std::vector<HttpDownloadRequest> download_requests_;
};

struct RetryPolicy {
    int max_attempts = 4;
    Millis base_delay{250};
    Millis max_delay{5000};
    bool retry_server_errors = true;
};

using SleepFn = std::function<void(Millis)>;

class HttpClient {
public:
    HttpClient(HttpTransport& transport, RetryPolicy policy = {});
    HttpResponse execute(const HttpRequest& request, const SleepFn& sleep = {});
    // Streaming downloads are retried only when the transport failed before it
    // received any response (status 0, no bytes committed); a partial stream is
    // discarded by the transport and surfaced to the caller instead.
    HttpDownloadResult download(const HttpDownloadRequest& request,
                                const SleepFn& sleep = {});

private:
    HttpTransport& transport_;
    RetryPolicy policy_;
};

// --- OAuth / token vault ------------------------------------------------

struct OAuthToken {
    std::string access_token;
    std::string refresh_token;
    std::string id_token;
    std::string token_type = "Bearer";
    int expires_in = 3600;
    Clock::time_point obtained_at = Clock::now();

    bool expired(int skew_seconds = 60) const;
    void reset_expiry();
};

class TokenVault {
public:
    virtual ~TokenVault() = default;
    virtual std::optional<OAuthToken> load(const std::string& key) = 0;
    virtual bool save(const std::string& key, const OAuthToken& token) = 0;
    virtual bool erase(const std::string& key) = 0;
};

// Phase 0 implementation only. Production will use DPAPI/Windows Credential
// Manager; the interface prevents call sites from caring which one is used.
class MemoryTokenVault final : public TokenVault {
public:
    std::optional<OAuthToken> load(const std::string& key) override;
    bool save(const std::string& key, const OAuthToken& token) override;
    bool erase(const std::string& key) override;

private:
    std::vector<std::pair<std::string, OAuthToken>> entries_;
};

// --- Redaction -----------------------------------------------------------

bool is_sensitive_header(std::string_view name);
std::string redact_header_value(std::string_view name, std::string_view value);
std::string redact_text(std::string_view text);
std::string redact_request(const HttpRequest& request);

// --- Provenance / audit --------------------------------------------------

struct AuthorizationRecord {
    std::string case_id;
    std::string legal_basis;
    std::string approved_by;
    std::optional<Clock::time_point> expires_at;

    bool valid() const;
};

struct Provenance {
    std::string provider;
    std::string tenant_id;
    std::string account_id;
    std::string external_id;
    std::string endpoint;
    std::string collected_at_utc;
    std::string sha256;
    std::string job_id;
    std::string legal_basis;

    bool complete() const;
};

struct AuditEvent {
    std::string at_utc;
    std::string actor;
    std::string action;
    std::string case_id;
    std::string provider;
    std::string tenant_id;
    std::string result;
    std::string error_code;
    std::string job_id;
};

std::string sha256_hex(std::string_view value);
std::string json_escape(std::string_view value);
std::string provenance_json(const Provenance& provenance);
std::string audit_json(const AuditEvent& event);

} // namespace deep::cloud
