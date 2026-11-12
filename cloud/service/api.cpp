#include "api.hpp"
#include "cloud/enterprise/secret_store.hpp"

#include "cloud/core/manifest_verify.hpp"
#include "cloud/core/rbac.hpp"
#include "cloud/enterprise/identity.hpp"
#include "cloud/core/winhttp_transport.hpp"
#include "third_party/nlohmann/json.hpp"

#include <filesystem>
#include <chrono>
#include <sstream>
#include <fstream>
#include <iterator>
#include <vector>

namespace deep::cloud {
namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
}

std::string json_error(const std::string& message) {
    return nlohmann::json{{"error", message}}.dump();
}

nlohmann::json worker_summary(const std::string& case_dir) {
    int online = 0;
    int stale = 0;
    int stopped = 0;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path root =
        std::filesystem::path(case_dir) / "cloud" / "workers";
    std::error_code error;
    if (std::filesystem::exists(root, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
            if (error || !entry.is_regular_file() ||
                entry.path().extension() != ".json") continue;
            try {
                std::ifstream input(entry.path(), std::ios::binary);
                const auto document = nlohmann::json::parse(
                    std::string((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>()));
                const auto last = document.value("last_seen_epoch", static_cast<std::int64_t>(0));
                const std::string status = document.value("status", "");
                if (status == "stopped") ++stopped;
                else if (now - last <= 30) ++online;
                else ++stale;
            } catch (const std::exception&) {
                ++stale;
            }
        }
    }
    return {{"online", online}, {"stale", stale}, {"stopped", stopped},
            {"total", online + stale + stopped}};
}
bool bearer_token(const std::string& header, std::string& token) {
    constexpr const char* prefix = "Bearer ";
    if (header.rfind(prefix, 0) != 0) return false;
    token = header.substr(std::string(prefix).size());
    return !token.empty();
}

} // namespace

CloudApiService::CloudApiService(std::string case_dir, std::string identity_path,
                                 std::string audit_path, int requests_per_minute)
    : case_dir_(std::move(case_dir)),
      identity_path_(std::move(identity_path)),
      audit_(std::move(audit_path)),
      sessions_(8 * 60 * 60),
      queue_((std::filesystem::path(case_dir_) / "cloud" / "queue").string()),
      limiter_(requests_per_minute) {}

ApiResponse CloudApiService::unauthorized() {
    return {401, "application/json", json_error("authentication required")};
}

ApiResponse CloudApiService::error(int status, const std::string& message) {
    return {status, "application/json", json_error(message)};
}

ApiResponse CloudApiService::health() {
    return {200, "application/json",
            nlohmann::json{{"status", "ok"}, {"service", "olho-de-deus-cloud"},
                           {"version", 1}}.dump()};
}

ApiResponse CloudApiService::config() {
    return {200, "application/json",
            nlohmann::json{{"service", "olho-de-deus-cloud"},
                           {"version", 1},
                           {"oidc_configured", !oidc_issuer_.empty() && !oidc_client_id_.empty()},
                           {"issuer", oidc_issuer_},
                           {"client_id", oidc_client_id_}}.dump()};
}
ApiResponse CloudApiService::login(const ApiRequest& request) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(request.body);
    } catch (const std::exception&) {
        return error(400, "invalid JSON body");
    }
    if (!body.is_object() ||
        (body.contains("username") && !body["username"].is_string()) ||
        (body.contains("password") && !body["password"].is_string()))
        return error(400, "username and password must be strings");
    const std::string username = body.value("username", "");
    const std::string password = body.value("password", "");
    if (username.empty() || password.empty())
        return error(400, "username and password are required");

    LocalIdentityStore store(identity_path_);
    std::string load_error;
    if (!store.load(load_error)) return error(500, "identity store unavailable");
    const auto principal = store.authenticate(username, password);
    if (!principal) {
        std::string audit_error;
        audit_.append(username, "api.login", "service", "denied",
                      "invalid credentials", audit_error);
        return error(401, "invalid credentials");
    }
    std::string session_error;
    const auto token = sessions_.create(*principal, session_error);
    if (!token) return error(500, "could not create session");
    std::string audit_error;
    audit_.append(principal->id, "api.login", "service", "allowed",
                  role_name(principal->role), audit_error);
    return {200, "application/json",
            nlohmann::json{{"token", *token},
                           {"principal", {{"id", principal->id},
                                          {"display_name", principal->display_name},
                                          {"role", role_name(principal->role)}}},
                           {"expires_in", 8 * 60 * 60}}.dump()};
}

void CloudApiService::set_oidc_verifier(OidcVerifyFn verifier) {
    oidc_verifier_ = std::move(verifier);
}

void CloudApiService::set_oidc_configuration(std::string issuer, std::string client_id) {
    std::lock_guard<std::mutex> lock(oidc_mutex_);
    oidc_issuer_ = std::move(issuer);
    oidc_client_id_ = std::move(client_id);
    oidc_configuration_ = {};
    oidc_jwks_.clear();
    oidc_cache_expires_ = 0;
}

ApiResponse CloudApiService::oidc_login(const ApiRequest& request) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(request.body);
    } catch (const std::exception&) {
        return error(400, "invalid JSON body");
    }
    if (oidc_issuer_.empty() || oidc_client_id_.empty())
        return error(503, "OIDC is not configured");
    if (!body.is_object()) return error(400, "invalid JSON body");
    if ((body.contains("issuer") && !body["issuer"].is_string()) ||
        (body.contains("client_id") && !body["client_id"].is_string()) ||
        (body.contains("id_token") && !body["id_token"].is_string()))
        return error(400, "OIDC fields must be strings");
    if (body.contains("issuer") && body.value("issuer", "") != oidc_issuer_)
        return error(400, "issuer does not match service configuration");
    if (body.contains("client_id") && body.value("client_id", "") != oidc_client_id_)
        return error(400, "client_id does not match service configuration");
    const std::string issuer = oidc_issuer_;
    const std::string client_id = oidc_client_id_;
    const std::string id_token = body.value("id_token", "");
    if (id_token.empty()) return error(400, "id_token is required");

    JwtResult verified;
    if (oidc_verifier_) {
        verified = oidc_verifier_(issuer, client_id, id_token);
    } else {
        WinHttpTransport transport;
        RetryPolicy policy;
        policy.max_attempts = 2;
        HttpClient http(transport, policy);
        std::string cache_error;
        auto load_jwks = [&](bool force) {
            std::lock_guard<std::mutex> lock(oidc_mutex_);
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (!force && !oidc_jwks_.empty() && now < oidc_cache_expires_)
                return true;
            const auto discovery = discover_oidc(http, issuer);
            if (!discovery.ok) {
                cache_error = "OIDC discovery failed: " + discovery.error;
                return false;
            }
            HttpRequest jwks_request;
            jwks_request.method = "GET";
            jwks_request.url = discovery.configuration.jwks_uri;
            jwks_request.headers.push_back({"Accept", "application/json"});
            const auto response = http.execute(jwks_request);
            if (response.status < 200 || response.status >= 300) {
                cache_error = "JWKS HTTP " + std::to_string(response.status);
                return false;
            }
            oidc_configuration_ = discovery.configuration;
            oidc_jwks_ = response.body;
            oidc_cache_expires_ = now + 3600;
            return true;
        };
        if (!load_jwks(false)) return error(502, cache_error);
        std::string jwks;
        {
            std::lock_guard<std::mutex> lock(oidc_mutex_);
            jwks = oidc_jwks_;
        }
        verified = verify_rs256_token(id_token, jwks, issuer, client_id);
        const bool key_rotation = !verified.ok &&
            (verified.error.find("no matching JWKS key") != std::string::npos ||
             verified.error.find("JWKS key rejected") != std::string::npos ||
             verified.error.find("signature") != std::string::npos);
        if (key_rotation) {
            if (!load_jwks(true)) return error(502, cache_error);
            {
                std::lock_guard<std::mutex> lock(oidc_mutex_);
                jwks = oidc_jwks_;
            }
            verified = verify_rs256_token(id_token, jwks, issuer, client_id);
        }
    }
    if (!verified.ok) return error(401, "ID token rejected: " + verified.error);

    if (verified.claims.roles.empty())
        return error(403, "OIDC token has no allowed role claim");
    const Role role = oidc_role(verified.claims, Role::Auditor);
    const Principal principal = principal_from_oidc(verified.claims, role);
    std::string session_error;
    const auto token = sessions_.create(principal, session_error);
    if (!token) return error(500, "could not create session");
    std::string audit_error;
    audit_.append(principal.id, "api.login.oidc", issuer, "allowed",
                  role_name(role), audit_error);
    return {200, "application/json",
            nlohmann::json{{"token", *token},
                           {"principal", {{"id", principal.id},
                                          {"display_name", principal.display_name},
                                          {"role", role_name(role)}}},
                           {"expires_in", 8 * 60 * 60}}.dump()};
}
ApiResponse CloudApiService::logout(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    sessions_.revoke(token);
    std::string audit_error;
    audit_.append(principal->id, "api.logout", "service", "allowed", "", audit_error);
    return {200, "application/json", nlohmann::json{{"revoked", true}}.dump()};
}

ApiResponse CloudApiService::jobs(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::ViewCase);
    if (!decision.allowed) return error(403, decision.reason);

    nlohmann::json result = nlohmann::json::array();
    const std::filesystem::path root =
        std::filesystem::path(case_dir_) / "cloud" / "jobs";
    if (std::filesystem::exists(root)) {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            try {
                const auto document = nlohmann::json::parse(read_file(entry.path()));
                if (!document.is_object()) continue;
                result.push_back({
                    {"job_id", document.value("job_id", "")},
                    {"status", document.value("status", "")},
                    {"account", document.value("account", "")},
                    {"tenant_id", document.value("tenant_id", "")},
                    {"source", document.value("source", "")},
                    {"checkpoint_page", document.value("checkpoint_page", 0)},
                    {"pages", document.value("pages", 0)},
                    {"objects", document.value("objects", 0)},
                    {"resumed", document.value("resumed", false)},
                    {"updated_at_utc", document.value("updated_at_utc", "")},
                    {"error_count", document.value("errors", nlohmann::json::array()).size()}
                });
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    std::string audit_error;
    audit_.append(principal->id, "api.jobs.list", "service", "allowed", "", audit_error);
    return {200, "application/json",
            nlohmann::json{{"jobs", result}, {"count", result.size()}}.dump()};
}

ApiResponse CloudApiService::audit(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::ViewAudit);
    if (!decision.allowed) return error(403, decision.reason);

    const auto report = audit_.verify();
    if (!report.ok) {
        return {409, "application/json",
                nlohmann::json{{"error", "audit chain invalid"},
                               {"details", report.errors}}.dump()};
    }
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : audit_.tail(200)) {
        entries.push_back({{"timestamp_utc", entry.timestamp_utc},
                           {"actor", entry.actor},
                           {"action", entry.action},
                           {"target", entry.target},
                           {"result", entry.result},
                           {"detail", entry.detail}});
    }
    return {200, "application/json",
            nlohmann::json{{"entries", entries}, {"count", report.entries}}.dump()};
}

ApiResponse CloudApiService::verify(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::VerifyEvidence);
    if (!decision.allowed) return error(403, decision.reason);

    std::string account;
    if (!request.body.empty()) {
        try {
            const auto body = nlohmann::json::parse(request.body);
            account = body.value("account", "");
        } catch (const std::exception&) {
            return error(400, "invalid JSON body");
        }
    }
    const auto report = verify_cloud_manifests(case_dir_, account);
    std::string audit_error;
    audit_.append(principal->id, "api.verify", account.empty() ? "all" : account,
                  report.ok ? "allowed" : "failed", "", audit_error);
    return {report.ok ? 200 : 409, "application/json",
            nlohmann::json{{"ok", report.ok},
                           {"manifests", report.manifests},
                           {"files", report.files},
                           {"verified", report.verified},
                           {"objects", report.objects},
                           {"errors", report.errors}}.dump()};
}

ApiResponse CloudApiService::enqueue(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::RunCollection);
    if (!decision.allowed) return error(403, decision.reason);

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(request.body);
    } catch (const std::exception&) {
        return error(400, "invalid JSON body");
    }
    if (!body.is_object()) return error(400, "invalid JSON body");
    for (const char* key : {"job_id", "provider", "account", "tenant_id", "legal_basis",
                             "vault_key", "vault_ref", "since", "until", "root_path"}) {
        if (body.contains(key) && !body[key].is_string())
            return error(400, std::string("field must be a string: ") + key);
    }
    if ((body.contains("priority") && !body["priority"].is_number_integer()) ||
        (body.contains("max_pages") && !body["max_pages"].is_number_integer()) ||
        (body.contains("download_attachments") &&
         !body["download_attachments"].is_boolean()) ||
        (body.contains("download_files") && !body["download_files"].is_boolean()))
        return error(400, "invalid numeric/boolean field");
    QueuedJob job;
    job.job_id = body.value("job_id", "");
    job.provider = body.value("provider", "m365");
    // The provider decides which identifier is mandatory: Graph needs a tenant,
    // Dropbox has none. Refusing here keeps an unrunnable job out of the queue
    // instead of failing it later in a worker.
    if (job.provider != "m365" && job.provider != "dropbox")
        return error(400, "unsupported provider: " + job.provider);
    job.root_path = body.value("root_path", "/");
    job.download_files = body.value("download_files", false);
    job.account = body.value("account", "");
    job.tenant_id = body.value("tenant_id", "");
    job.legal_basis = body.value("legal_basis", "");
    job.vault_key = body.value("vault_key", "");
    job.vault_ref = body.value("vault_ref", "");
    job.since = body.value("since", "");
    job.until = body.value("until", "");
    job.max_pages = body.value("max_pages", 50);
    job.priority = body.value("priority", 0);
    job.download_attachments = body.value("download_attachments", false);
    job.requested_by = principal->id;
    if (job.vault_ref.empty() && !job.vault_key.empty()) {
        deep::cloud::SecretStore store;
        std::string store_error;
        if (!store.create(job.vault_key, job.vault_ref, store_error))
            return error(500, store_error);
    }
    job.vault_key.clear();
    std::string queue_error;
    if (!queue_.enqueue(job, queue_error)) return error(400, queue_error);
    std::string audit_error;
    audit_.append(principal->id, "api.job.enqueue", job.job_id, "allowed",
                  job.account, audit_error);
    return {201, "application/json",
            nlohmann::json{{"job_id", job.job_id}, {"status", "queued"},
                           {"account", job.account}}.dump()};
}

ApiResponse CloudApiService::queue(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::ViewCase);
    if (!decision.allowed) return error(403, decision.reason);
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& job : queue_.list()) {
        entries.push_back({
            {"job_id", job.job_id},
            {"provider", job.provider},
            {"account", job.account},
            {"tenant_id", job.tenant_id},
            {"root_path", job.root_path},
            {"download_files", job.download_files},
            {"legal_basis", job.legal_basis},
            {"since", job.since},
            {"until", job.until},
            {"max_pages", job.max_pages},
            {"priority", job.priority},
            {"download_attachments", job.download_attachments},
            {"attempts", job.attempts},
            {"max_attempts", job.max_attempts},
            {"next_attempt_epoch", job.next_attempt_epoch},
            {"lease_until_epoch", job.lease_until_epoch},
            {"status", job.status},
            {"requested_by", job.requested_by},
            {"created_at_utc", job.created_at_utc},
            {"started_at_utc", job.started_at_utc},
            {"finished_at_utc", job.finished_at_utc},
            {"error", job.error}
        });
    }
    return {200, "application/json",
            nlohmann::json{{"jobs", entries}, {"count", entries.size()}}.dump()};
}

ApiResponse CloudApiService::metrics(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::ViewCase);
    if (!decision.allowed) return error(403, decision.reason);
    const auto queue_metrics = queue_.metrics();
    const auto audit_report = audit_.verify();
    return {200, "application/json",
            nlohmann::json{
                {"queue", {{"total", queue_metrics.total},
                           {"queued", queue_metrics.queued},
                           {"running", queue_metrics.running},
                           {"completed", queue_metrics.completed},
                           {"failed", queue_metrics.failed},
                           {"cancelled", queue_metrics.cancelled},
                           {"expired_leases", queue_metrics.expired_leases}}},
                {"workers", worker_summary(case_dir_)},
                {"audit", {{"ok", audit_report.ok},
                           {"entries", audit_report.entries}}}
            }.dump()};
}
ApiResponse CloudApiService::cancel(const ApiRequest& request, const std::string& job_id) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::CancelJob);
    if (!decision.allowed) return error(403, decision.reason);
    std::string queue_error;
    if (!queue_.cancel(job_id, queue_error)) return error(400, queue_error);
    std::string audit_error;
    audit_.append(principal->id, "api.job.cancel", job_id, "allowed", "", audit_error);
    return {200, "application/json", nlohmann::json{{"job_id", job_id},
                                                     {"status", "cancelled"}}.dump()};
}
ApiResponse CloudApiService::pause(const ApiRequest& request, const std::string& job_id) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::CancelJob);
    if (!decision.allowed) return error(403, decision.reason);
    std::string queue_error;
    if (!queue_.pause(job_id, queue_error)) return error(400, queue_error);
    std::string audit_error;
    audit_.append(principal->id, "api.job.pause", job_id, "allowed", "", audit_error);
    return {200, "application/json", nlohmann::json{{"job_id", job_id},
                                                     {"status", "paused"}}.dump()};
}

ApiResponse CloudApiService::resume(const ApiRequest& request, const std::string& job_id) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::CancelJob);
    if (!decision.allowed) return error(403, decision.reason);
    std::string queue_error;
    if (!queue_.resume(job_id, queue_error)) return error(400, queue_error);
    std::string audit_error;
    audit_.append(principal->id, "api.job.resume", job_id, "allowed", "", audit_error);
    return {200, "application/json", nlohmann::json{{"job_id", job_id},
                                                     {"status", "queued"}}.dump()};
}
ApiResponse CloudApiService::prometheus(const ApiRequest& request) {
    std::string token;
    if (!bearer_token(request.authorization, token)) return unauthorized();
    const auto principal = sessions_.validate(token);
    if (!principal) return unauthorized();
    const auto decision = authorize(*principal, Permission::ViewCase);
    if (!decision.allowed) return error(403, decision.reason);
    const auto queue_metrics = queue_.metrics();
    const auto workers = worker_summary(case_dir_);
    const auto audit_report = audit_.verify();
    std::ostringstream out;
    out << "# HELP olho_cloud_queue_jobs Jobs by state.\n"
        << "# TYPE olho_cloud_queue_jobs gauge\n"
        << "olho_cloud_queue_jobs{state=\"queued\"} " << queue_metrics.queued << "\n"
        << "olho_cloud_queue_jobs{state=\"running\"} " << queue_metrics.running << "\n"
        << "olho_cloud_queue_jobs{state=\"completed\"} " << queue_metrics.completed << "\n"
        << "olho_cloud_queue_jobs{state=\"failed\"} " << queue_metrics.failed << "\n"
        << "olho_cloud_queue_jobs{state=\"cancelled\"} " << queue_metrics.cancelled << "\n"
        << "olho_cloud_queue_expired_leases " << queue_metrics.expired_leases << "\n"
        << "# HELP olho_cloud_workers Workers by state.\n"
        << "# TYPE olho_cloud_workers gauge\n"
        << "olho_cloud_workers{state=\"online\"} " << workers.value("online", 0) << "\n"
        << "olho_cloud_workers{state=\"stale\"} " << workers.value("stale", 0) << "\n"
        << "olho_cloud_workers{state=\"stopped\"} " << workers.value("stopped", 0) << "\n"
        << "# HELP olho_cloud_audit_ok Audit hash chain status.\n"
        << "# TYPE olho_cloud_audit_ok gauge\n"
        << "olho_cloud_audit_ok " << (audit_report.ok ? 1 : 0) << "\n"
        << "# HELP olho_cloud_audit_entries Audit entries verified.\n"
        << "# TYPE olho_cloud_audit_entries gauge\n"
        << "olho_cloud_audit_entries " << audit_report.entries << "\n";
    return {200, "text/plain; version=0.0.4", out.str()};
}
ApiResponse CloudApiService::handle(const ApiRequest& request) {
    std::string path = request.path;
    const auto query = path.find('?');
    if (query != std::string::npos) path = path.substr(0, query);
    if (request.method == "GET" && path == "/health") return health();
    int retry_after = 0;
    std::string rate_key = "anonymous";
    if (path.rfind("/v1/session", 0) == 0) {
        rate_key = "login";
    } else {
        std::string token;
        if (bearer_token(request.authorization, token)) {
            const auto rate_principal = sessions_.validate(token);
            if (rate_principal) rate_key = "user:" + rate_principal->id;
        }
    }
    if (!limiter_.allow(rate_key, retry_after)) {
        return {429, "application/json",
                nlohmann::json{{"error", "rate limit exceeded"},
                               {"retry_after", retry_after}}.dump()};
    }
    if (request.method == "POST" && path == "/v1/session") return login(request);
    if (request.method == "POST" && path == "/v1/session/oidc") return oidc_login(request);
    if (request.method == "DELETE" && path == "/v1/session") return logout(request);
    if (request.method == "GET" && path == "/v1/config") return config();
    if (request.method == "GET" && path == "/v1/jobs") return jobs(request);
    if (request.method == "GET" && path == "/v1/audit") return audit(request);
    if (request.method == "POST" && path == "/v1/verify") return verify(request);
    if (request.method == "POST" && path == "/v1/jobs") return enqueue(request);
    if (request.method == "POST" && path == "/v1/queue") return enqueue(request);
    if (request.method == "GET" && path == "/v1/queue") return queue(request);
    if (request.method == "GET" && path == "/v1/metrics") return metrics(request);
    if (request.method == "GET" && path == "/v1/metrics/prometheus") return prometheus(request);

    constexpr const char* queue_prefix = "/v1/queue/";
    if (path.rfind(queue_prefix, 0) == 0) {
        std::string suffix = path.substr(std::string(queue_prefix).size());
        if (request.method == "DELETE" && suffix.find('/') == std::string::npos)
            return cancel(request, suffix);
        if (request.method == "POST" && suffix.size() > 6 &&
            suffix.compare(suffix.size() - 6, 6, "/pause") == 0)
            return pause(request, suffix.substr(0, suffix.size() - 6));
        if (request.method == "POST" && suffix.size() > 7 &&
            suffix.compare(suffix.size() - 7, 7, "/resume") == 0)
            return resume(request, suffix.substr(0, suffix.size() - 7));
    }

    return error(404, "route not found");
}

} // namespace deep::cloud
