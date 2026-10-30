#include "cloud_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <windows.h>
#include <bcrypt.h>

namespace deep::cloud {
namespace {

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool starts_with_ci(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) return false;
    return lower(value.substr(0, prefix.size())) == lower(prefix);
}

bool is_token_char(char c) {
    return !std::isspace(static_cast<unsigned char>(c)) && c != '&' &&
           c != '"' && c != '\'' && c != ',' && c != '}' && c != ')';
}

void replace_secret_value(std::string& text, const std::string& key) {
    std::size_t search = 0;
    while (true) {
        const std::size_t found = text.find(key, search);
        if (found == std::string::npos) return;
        if (found > 0) {
            const unsigned char before = static_cast<unsigned char>(text[found - 1]);
            if (std::isalnum(before) || text[found - 1] == '_') {
                search = found + key.size();
                continue;
            }
        }
        std::size_t cursor = found + key.size();
        while (cursor < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[cursor])) ||
                text[cursor] == '"' || text[cursor] == '\''))
            ++cursor;
        if (cursor >= text.size() || (text[cursor] != '=' && text[cursor] != ':')) {
            search = found + key.size();
            continue;
        }
        ++cursor;
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])))
            ++cursor;
        if (cursor < text.size() && (text[cursor] == '"' || text[cursor] == '\'')) ++cursor;
        const std::size_t begin = cursor;
        while (cursor < text.size() && is_token_char(text[cursor])) ++cursor;
        if (cursor == begin) {
            search = found + key.size();
            continue;
        }
        text.replace(begin, cursor - begin, "[REDACTED]");
        search = begin + 10;
    }
}

bool valid_sha256(std::string_view value) {
    if (value.size() != 64) return false;
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::wstring widen_utf8(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

// Atomic move onto the final path. The part file only becomes visible under
// its real name once the byte stream and its digest are complete.
bool replace_file(const std::string& from, const std::string& to) {
    const std::wstring wide_from = widen_utf8(from);
    const std::wstring wide_to = widen_utf8(to);
    if (!wide_from.empty() && !wide_to.empty()) {
        return MoveFileExW(wide_from.c_str(), wide_to.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    std::error_code error;
    std::filesystem::rename(std::filesystem::u8path(from), std::filesystem::u8path(to), error);
    return !error;
}

} // namespace

struct Sha256Hasher::Impl {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    std::string digest;
    bool finished = false;
    bool usable = false;

    ~Impl() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

Sha256Hasher::Sha256Hasher() : impl_(std::make_unique<Impl>()) {
    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD result_size = 0;
    if (BCryptOpenAlgorithmProvider(&impl_->algorithm, BCRYPT_SHA256_ALGORITHM,
            nullptr, 0) != 0) {
        return;
    }
    const bool properties =
        BCryptGetProperty(impl_->algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
            &result_size, 0) == 0 &&
        BCryptGetProperty(impl_->algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size),
            &result_size, 0) == 0 &&
        hash_size == 32 && object_size > 0;
    if (!properties) return;
    impl_->object.assign(object_size, 0);
    if (BCryptCreateHash(impl_->algorithm, &impl_->hash, impl_->object.data(),
            object_size, nullptr, 0, 0) != 0) {
        return;
    }
    impl_->usable = true;
}

Sha256Hasher::~Sha256Hasher() = default;
Sha256Hasher::Sha256Hasher(Sha256Hasher&&) noexcept = default;
Sha256Hasher& Sha256Hasher::operator=(Sha256Hasher&&) noexcept = default;

bool Sha256Hasher::valid() const {
    return impl_ && impl_->usable;
}

bool Sha256Hasher::update(const void* data, std::size_t length) {
    if (!impl_ || !impl_->usable || impl_->finished) return false;
    if (length == 0) return true;
    if (!data) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t offset = 0;
    while (offset < length) {
        const std::size_t chunk = std::min<std::size_t>(length - offset, 1u << 20);
        if (BCryptHashData(impl_->hash,
                const_cast<PUCHAR>(bytes + offset),
                static_cast<ULONG>(chunk), 0) != 0) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

bool Sha256Hasher::finalize() {
    if (!impl_ || !impl_->usable) return false;
    if (impl_->finished) return !impl_->digest.empty();
    unsigned char digest[32] = {};
    if (BCryptFinishHash(impl_->hash, digest, 32, 0) != 0) return false;
    if (impl_->hash) {
        BCryptDestroyHash(impl_->hash);
        impl_->hash = nullptr;
    }
    static const char* hex = "0123456789abcdef";
    impl_->digest.clear();
    impl_->digest.reserve(64);
    for (unsigned char byte : digest) {
        impl_->digest.push_back(hex[(byte >> 4) & 0xf]);
        impl_->digest.push_back(hex[byte & 0xf]);
    }
    impl_->finished = true;
    return true;
}

std::string Sha256Hasher::hex_digest() const {
    return impl_ ? impl_->digest : std::string();
}

struct DownloadSink::Impl {
    std::string destination;
    std::string part_path;
    std::ofstream file;
    Sha256Hasher hasher;
    std::uint64_t max_bytes = 0;
    std::uint64_t bytes = 0;
    bool limit_exceeded = false;
    bool opened = false;
    bool committed = false;
    std::string sha256;
};

DownloadSink::DownloadSink() : impl_(std::make_unique<Impl>()) {}
DownloadSink::~DownloadSink() {
    if (impl_ && impl_->opened && !impl_->committed) discard();
}
DownloadSink::DownloadSink(DownloadSink&&) noexcept = default;
DownloadSink& DownloadSink::operator=(DownloadSink&& other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->opened && !impl_->committed) discard();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool DownloadSink::open(const std::string& destination, std::uint64_t max_bytes,
                        std::string& error) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->opened || impl_->committed) discard();
    error.clear();
    if (destination.empty()) {
        error = "download destination path is empty";
        return false;
    }
    const std::filesystem::path target = std::filesystem::u8path(destination);
    std::error_code directory_error;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), directory_error);
        if (directory_error) {
            error = "could not create download directory";
            return false;
        }
    }
    impl_->destination = destination;
    impl_->part_path = destination + ".part";
    impl_->max_bytes = max_bytes;
    impl_->bytes = 0;
    impl_->limit_exceeded = false;
    impl_->sha256.clear();
    impl_->committed = false;
    impl_->file.clear();
    impl_->hasher = Sha256Hasher();
    if (!impl_->hasher.valid()) {
        error = "could not initialise SHA-256";
        return false;
    }
    impl_->file.open(std::filesystem::u8path(impl_->part_path),
        std::ios::binary | std::ios::trunc);
    if (!impl_->file) {
        impl_->file.clear();
        error = "could not open download part file";
        return false;
    }
    impl_->opened = true;
    return true;
}

bool DownloadSink::write(const char* data, std::size_t length, std::string& error) {
    if (!impl_ || !impl_->opened || impl_->committed) {
        error = "download sink is not open";
        return false;
    }
    if (length == 0) return true;
    if (!data) {
        error = "download sink received a null buffer";
        return false;
    }
    if (impl_->max_bytes > 0 &&
        (impl_->bytes >= impl_->max_bytes ||
         static_cast<std::uint64_t>(length) > impl_->max_bytes - impl_->bytes)) {
        impl_->limit_exceeded = true;
        error = "download exceeds the configured max_bytes limit";
        return false;
    }
    impl_->file.write(data, static_cast<std::streamsize>(length));
    if (!impl_->file) {
        error = "could not write download part file";
        return false;
    }
    if (!impl_->hasher.update(data, length)) {
        error = "could not hash download bytes";
        return false;
    }
    impl_->bytes += static_cast<std::uint64_t>(length);
    return true;
}

bool DownloadSink::commit(std::string& error) {
    if (!impl_ || !impl_->opened || impl_->committed) {
        error = "download sink is not open";
        return false;
    }
    impl_->file.flush();
    if (!impl_->file) {
        error = "could not flush download part file";
        discard();
        return false;
    }
    impl_->file.close();
    if (impl_->file.fail()) {
        error = "could not close download part file";
        discard();
        return false;
    }
    if (!impl_->hasher.finalize()) {
        error = "could not finalise SHA-256";
        discard();
        return false;
    }
    impl_->sha256 = impl_->hasher.hex_digest();
    if (impl_->sha256.empty()) {
        error = "could not finalise SHA-256";
        discard();
        return false;
    }
    if (!replace_file(impl_->part_path, impl_->destination)) {
        error = "could not finalise downloaded file";
        discard();
        return false;
    }
    impl_->opened = false;
    impl_->committed = true;
    return true;
}

void DownloadSink::discard() {
    if (!impl_) return;
    impl_->file.clear();
    if (impl_->file.is_open()) impl_->file.close();
    if (!impl_->part_path.empty()) {
        std::error_code remove_error;
        std::filesystem::remove(std::filesystem::u8path(impl_->part_path), remove_error);
    }
    impl_->opened = false;
    impl_->committed = false;
}

bool DownloadSink::active() const {
    return impl_ && impl_->opened;
}

bool DownloadSink::limit_exceeded() const {
    return impl_ && impl_->limit_exceeded;
}

std::uint64_t DownloadSink::bytes() const {
    return impl_ ? impl_->bytes : 0;
}

const std::string& DownloadSink::sha256() const {
    static const std::string empty;
    return impl_ ? impl_->sha256 : empty;
}

const std::string& DownloadSink::destination() const {
    static const std::string empty;
    return impl_ ? impl_->destination : empty;
}

const std::string& DownloadSink::part_path() const {
    static const std::string empty;
    return impl_ ? impl_->part_path : empty;
}

std::string find_header_value(
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string_view name) {
    const std::string wanted = lower(name);
    for (const auto& [key, value] : headers) {
        if (lower(key) == wanted) return value;
    }
    return {};
}

bool parse_decimal_u64(std::string_view text, std::uint64_t& value) {
    if (text.empty()) return false;
    std::uint64_t result = 0;
    for (unsigned char c : text) {
        if (std::isdigit(c) == 0) return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return false;
        result = result * 10 + digit;
    }
    value = result;
    return true;
}

std::string HttpResponse::header(std::string_view name) const {
    return find_header_value(headers, name);
}

HttpDownloadResult HttpTransport::download(const HttpDownloadRequest&) {
    HttpDownloadResult result;
    result.transport_error = true;
    result.error = "transport does not support streaming downloads";
    return result;
}

HttpResponse MockHttpTransport::send(const HttpRequest& request) {
    requests_.push_back(request);
    if (responses_.empty()) {
        HttpResponse missing;
        missing.transport_error = true;
        missing.error = "mock transport has no queued response";
        return missing;
    }
    HttpResponse response = std::move(responses_.front());
    responses_.pop_front();
    return response;
}

HttpDownloadResult MockHttpTransport::download(const HttpDownloadRequest& request) {
    HttpRequest recorded;
    recorded.method = request.method.empty() ? "GET" : request.method;
    recorded.url = request.url;
    recorded.headers = request.headers;
    requests_.push_back(std::move(recorded));
    download_requests_.push_back(request);

    HttpDownloadResult result;
    if (downloads_.empty()) {
        result.transport_error = true;
        result.error = "mock transport has no queued download";
        return result;
    }
    MockDownloadSpec spec = std::move(downloads_.front());
    downloads_.pop_front();
    result.status = spec.status;
    result.headers = spec.headers;
    if (spec.transport_error) {
        // A transport failure happens before any response, so no status is
        // reported to the caller (mirrors the WinHTTP transport).
        result.status = 0;
        result.transport_error = true;
        result.error = spec.error.empty() ? "mock download transport error" : spec.error;
        return result;
    }
    result.location = find_header_value(spec.headers, "Location");
    if (spec.status < 200 || spec.status >= 300) {
        result.redirected = spec.status >= 300 && spec.status < 400;
        result.error = "HTTP " + std::to_string(spec.status);
        return result;
    }
    if (request.destination_path.empty()) {
        result.error = "download destination path is empty";
        return result;
    }
    std::uint64_t declared = 0;
    const std::string content_length = find_header_value(spec.headers, "Content-Length");
    if (request.max_bytes > 0 && !content_length.empty() &&
        parse_decimal_u64(content_length, declared) && declared > request.max_bytes) {
        result.limit_exceeded = true;
        result.error = "download exceeds the configured max_bytes limit";
        return result;
    }

    std::string error;
    DownloadSink sink;
    if (!sink.open(request.destination_path, request.max_bytes, error)) {
        result.error = error;
        return result;
    }
    std::string repeated;
    if (spec.repeated_chunk_size > 0 && spec.repeated_chunk_count > 0) {
        repeated.assign(spec.repeated_chunk_size,
            static_cast<char>(spec.repeated_byte));
    }
    std::uint64_t delivered = 0;
    bool failed = false;
    const auto deliver = [&](const char* data, std::size_t length) {
        if (failed) return;
        if (spec.fail_after_bytes != 0 && delivered >= spec.fail_after_bytes) {
            failed = true;
            return;
        }
        std::size_t allowed = length;
        if (spec.fail_after_bytes != 0 &&
            delivered + allowed > spec.fail_after_bytes) {
            allowed = static_cast<std::size_t>(spec.fail_after_bytes - delivered);
        }
        if (allowed > 0 && !sink.write(data, allowed, error)) {
            failed = true;
            return;
        }
        delivered += allowed;
        if (allowed < length) failed = true;
    };
    for (const auto& chunk : spec.chunks) {
        deliver(chunk.data(), chunk.size());
        if (failed) break;
    }
    if (!failed) {
        for (std::size_t index = 0; index < spec.repeated_chunk_count; ++index) {
            deliver(repeated.data(), repeated.size());
            if (failed) break;
        }
    }
    if (failed) {
        const bool limit_hit = sink.limit_exceeded();
        sink.discard();
        result.limit_exceeded = limit_hit;
        result.transport_error = !limit_hit;
        if (limit_hit) {
            result.error = error;
        } else {
            result.error = spec.failure_error.empty()
                ? std::string("mock download stream failed")
                : spec.failure_error;
        }
        return result;
    }
    if (!sink.commit(error)) {
        result.error = error;
        return result;
    }
    result.bytes = sink.bytes();
    result.sha256 = sink.sha256();
    result.ok = true;
    return result;
}

HttpClient::HttpClient(HttpTransport& transport, RetryPolicy policy)
    : transport_(transport), policy_(policy) {}

HttpResponse HttpClient::execute(const HttpRequest& request, const SleepFn& sleep) {
    const SleepFn sleeper = sleep ? sleep : SleepFn([](Millis delay) {
        std::this_thread::sleep_for(delay);
    });
    const int attempts = std::max(1, policy_.max_attempts);
    HttpResponse response;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        response = transport_.send(request);
        const bool retryable = response.transport_error || response.status == 429 ||
            (policy_.retry_server_errors && response.status >= 500 &&
             response.status <= 599);
        if (!retryable || attempt + 1 >= attempts) return response;

        Millis delay = policy_.base_delay;
        for (int i = 0; i < attempt; ++i) {
            delay *= 2;
            if (delay >= policy_.max_delay) {
                delay = policy_.max_delay;
                break;
            }
        }
        const std::string retry_after = response.header("Retry-After");
        if (!retry_after.empty()) {
            int seconds = 0;
            try {
                seconds = std::stoi(retry_after);
            } catch (...) {
                seconds = 0;
            }
            if (seconds > 0) {
                const Millis explicit_delay(static_cast<long long>(seconds) * 1000);
                delay = std::min(explicit_delay, policy_.max_delay);
            }
        }
        if (delay > Millis::zero()) sleeper(delay);
    }
    return response;
}

HttpDownloadResult HttpClient::download(const HttpDownloadRequest& request,
                                        const SleepFn& sleep) {
    const SleepFn sleeper = sleep ? sleep : SleepFn([](Millis delay) {
        std::this_thread::sleep_for(delay);
    });
    const int attempts = std::max(1, policy_.max_attempts);
    HttpDownloadResult result;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        result = transport_.download(request);
        // Only failures that happened before any response arrived are retried;
        // a partial stream must not silently re-download and duplicate work.
        const bool retryable = result.transport_error && result.status == 0 &&
            !result.limit_exceeded;
        if (!retryable || attempt + 1 >= attempts) return result;

        Millis delay = policy_.base_delay;
        for (int i = 0; i < attempt; ++i) {
            delay *= 2;
            if (delay >= policy_.max_delay) {
                delay = policy_.max_delay;
                break;
            }
        }
        if (delay > Millis::zero()) sleeper(delay);
    }
    return result;
}

bool OAuthToken::expired(int skew_seconds) const {
    if (expires_in <= 0) return true;
    const auto deadline = obtained_at + std::chrono::seconds(expires_in - skew_seconds);
    return Clock::now() >= deadline;
}

void OAuthToken::reset_expiry() {
    obtained_at = Clock::now();
}

std::optional<OAuthToken> MemoryTokenVault::load(const std::string& key) {
    for (const auto& [stored_key, token] : entries_) {
        if (stored_key == key) return token;
    }
    return std::nullopt;
}

bool MemoryTokenVault::save(const std::string& key, const OAuthToken& token) {
    for (auto& [stored_key, stored_token] : entries_) {
        if (stored_key == key) {
            stored_token = token;
            return true;
        }
    }
    entries_.emplace_back(key, token);
    return true;
}

bool MemoryTokenVault::erase(const std::string& key) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == key) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

bool is_sensitive_header(std::string_view name) {
    const std::string key = lower(name);
    return key == "authorization" || key == "proxy-authorization" ||
           key == "cookie" || key == "set-cookie" || key == "x-api-key" ||
           key == "client_secret" || key == "access_token" ||
           key == "refresh_token";
}

std::string redact_header_value(std::string_view name, std::string_view value) {
    if (is_sensitive_header(name)) return "[REDACTED]";
    if (starts_with_ci(value, "bearer ")) return "Bearer [REDACTED]";
    return std::string(value);
}

std::string redact_text(std::string_view text) {
    std::string result(text);
    for (const char* key : {"access_token", "refresh_token", "id_token",
                            "client_secret", "password", "authorization_code"}) {
        replace_secret_value(result, key);
    }
    // Authorization: Bearer <value> and ?code=<value> are common OAuth leaks.
    std::size_t auth = lower(result).find("authorization:");
    while (auth != std::string::npos) {
        const std::size_t begin = auth + std::string("authorization:").size();
        const std::size_t end = result.find('\n', begin);
        result.replace(begin, (end == std::string::npos ? result.size() : end) - begin,
                       " [REDACTED]");
        auth = lower(result).find("authorization:", begin + 11);
    }
    replace_secret_value(result, "code");
    return result;
}

std::string redact_request(const HttpRequest& request) {
    std::ostringstream output;
    output << request.method << ' ' << redact_text(request.url) << "\n";
    for (const auto& [name, value] : request.headers) {
        output << name << ": " << redact_header_value(name, value) << "\n";
    }
    if (!request.body.empty()) output << redact_text(request.body);
    return output.str();
}

bool AuthorizationRecord::valid() const {
    if (case_id.empty() || legal_basis.empty() || approved_by.empty()) return false;
    if (expires_at && Clock::now() >= *expires_at) return false;
    return true;
}

bool Provenance::complete() const {
    return !provider.empty() && !tenant_id.empty() && !account_id.empty() &&
           !external_id.empty() && !endpoint.empty() && !collected_at_utc.empty() &&
           valid_sha256(sha256) && !job_id.empty() && !legal_basis.empty();
}

std::string sha256_hex(std::string_view value) {
    Sha256Hasher hasher;
    if (!hasher.valid()) return {};
    if (!hasher.update(value.data(), value.size())) return {};
    if (!hasher.finalize()) return {};
    return hasher.hex_digest();
}
std::string json_escape(std::string_view value) {
    std::ostringstream output;
    for (unsigned char c : value) {
        switch (c) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (c < 0x20) {
                const char* hex = "0123456789abcdef";
                output << "\\u00" << hex[(c >> 4) & 0xf] << hex[c & 0xf];
            } else {
                output << static_cast<char>(c);
            }
        }
    }
    return output.str();
}

std::string provenance_json(const Provenance& provenance) {
    std::ostringstream output;
    output << "{"
           << "\"provider\":\"" << json_escape(provenance.provider) << "\","
           << "\"tenant_id\":\"" << json_escape(provenance.tenant_id) << "\","
           << "\"account_id\":\"" << json_escape(provenance.account_id) << "\","
           << "\"external_id\":\"" << json_escape(provenance.external_id) << "\","
           << "\"endpoint\":\"" << json_escape(provenance.endpoint) << "\","
           << "\"collected_at_utc\":\"" << json_escape(provenance.collected_at_utc) << "\","
           << "\"sha256\":\"" << json_escape(provenance.sha256) << "\","
           << "\"job_id\":\"" << json_escape(provenance.job_id) << "\","
           << "\"legal_basis\":\"" << json_escape(provenance.legal_basis) << "\""
           << "}";
    return output.str();
}

std::string audit_json(const AuditEvent& event) {
    std::ostringstream output;
    output << "{"
           << "\"at_utc\":\"" << json_escape(event.at_utc) << "\","
           << "\"actor\":\"" << json_escape(event.actor) << "\","
           << "\"action\":\"" << json_escape(event.action) << "\","
           << "\"case_id\":\"" << json_escape(event.case_id) << "\","
           << "\"provider\":\"" << json_escape(event.provider) << "\","
           << "\"tenant_id\":\"" << json_escape(event.tenant_id) << "\","
           << "\"result\":\"" << json_escape(event.result) << "\","
           << "\"error_code\":\"" << json_escape(event.error_code) << "\","
           << "\"job_id\":\"" << json_escape(event.job_id) << "\""
           << "}";
    return output.str();
}

} // namespace deep::cloud
