#include "cloud/core/cloud_core.hpp"
#include "cloud/core/dpapi_token_vault.hpp"
#include "cloud/core/oauth_pkce.hpp"
#include "cloud/core/oauth_callback.hpp"
#include "cloud/core/winhttp_transport.hpp"
#include "cloud/connectors/m365/graph_client.hpp"
#include "cloud/connectors/m365/m365_collector.hpp"
#include "cloud/core/manifest_verify.hpp"
#include "cloud/core/rbac.hpp"
#include "cloud/core/oidc.hpp"
#include "cloud/enterprise/identity.hpp"
#include "cloud/enterprise/session.hpp"
#include "cloud/enterprise/audit.hpp"
#include "cloud/enterprise/session_vault.hpp"
#include "cloud/service/api.hpp"
#include "third_party/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <ctime>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <fstream>
#include <ctime>

using namespace deep::cloud;

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    } else {
        std::cout << "ok: " << name << "\n";
    }
}

bool request_has_bearer(const HttpRequest& request) {
    for (const auto& header : request.headers) {
        if (header.first == "Authorization") return true;
    }
    return false;
}

std::string read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool download_has_bearer(const HttpDownloadRequest& request) {
    for (const auto& header : request.headers) {
        if (header.first == "Authorization") return true;
    }
    return false;
}

// True when any ".part" staging file survived under `root`; a failed or
// successful streamed download must never leave one behind.
bool has_part_file(const std::filesystem::path& root) {
    std::error_code exists_error;
    if (!std::filesystem::exists(root, exists_error)) return false;
    for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
        std::error_code entry_error;
        if (it->is_regular_file(entry_error) && it->path().extension() == ".part")
            return true;
    }
    return false;
}

void test_redaction() {
    check(redact_header_value("Authorization", "Bearer abc.def") == "[REDACTED]",
          "authorization header is redacted");
    check(redact_header_value("Accept", "application/json") == "application/json",
          "non-sensitive header is preserved");
    const std::string url = "https://graph.microsoft.com/v1.0/me?access_token=secret&x=1";
    const std::string clean = redact_text(url);
    check(clean.find("secret") == std::string::npos, "query token removed");
    check(clean.find("[REDACTED]") != std::string::npos, "redaction marker present");
    check(redact_text("?code=oauth-code&state=s") .find("oauth-code") == std::string::npos,
          "oauth code removed");
    const std::string json = "{\"access_token\":\"abc\",\"error_code\":\"E123\"}";
    const std::string redacted = redact_text(json);
    check(redacted.find("abc") == std::string::npos, "json access token removed");
    check(redacted.find("E123") != std::string::npos, "error_code is preserved");
    HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.headers.push_back({"Authorization", "Bearer top-secret"});
    request.body = json;
    const std::string dump = redact_request(request);
    check(dump.find("top-secret") == std::string::npos, "request header redacted");
    check(dump.find("abc") == std::string::npos, "request body redacted");
}

void test_retry() {
    MockHttpTransport transport;
    HttpResponse limited;
    limited.status = 429;
    limited.headers.push_back({"Retry-After", "0"});
    transport.push(limited);
    HttpResponse success;
    success.status = 200;
    success.body = "{}";
    transport.push(success);

    RetryPolicy policy;
    policy.max_attempts = 3;
    policy.base_delay = Millis(0);
    HttpClient client(transport, policy);
    int sleeps = 0;
    const HttpResponse response = client.execute(HttpRequest{"GET", "https://example.test", {}, {}}, [&](Millis) {
        ++sleeps;
    });
    check(response.status == 200, "retry reaches success");
    check(transport.requests().size() == 2, "retry sent two requests");
    check(sleeps == 0, "zero delay does not sleep");

    MockHttpTransport failing;
    for (int i = 0; i < 3; ++i) {
        HttpResponse error;
        error.status = 503;
        failing.push(error);
    }
    policy.max_attempts = 3;
    policy.base_delay = Millis(1);
    HttpClient failing_client(failing, policy);
    int fail_sleeps = 0;
    const HttpResponse failed = failing_client.execute(HttpRequest{"GET", "https://example.test", {}, {}},
        [&](Millis) { ++fail_sleeps; });
    check(failed.status == 503, "server error returned after max attempts");
    check(failing.requests().size() == 3, "exactly max attempts sent");
    check(fail_sleeps == 2, "sleeps between attempts only");

    MockHttpTransport broken;
    HttpResponse transport_error;
    transport_error.transport_error = true;
    broken.push(transport_error);
    broken.push(success);
    policy.max_attempts = 2;
    policy.base_delay = Millis(0);
    HttpClient broken_client(broken, policy);
    const HttpResponse recovered = broken_client.execute(HttpRequest{"GET", "https://example.test", {}, {}},
        [](Millis) {});
    check(recovered.status == 200, "transport error retried");
}

void test_vault_and_oauth() {
    MemoryTokenVault vault;
    OAuthToken token;
    token.access_token = "access-secret";
    token.refresh_token = "refresh-secret";
    token.expires_in = 3600;
    token.reset_expiry();
    check(vault.save("tenant/user", token), "vault save");
    const auto loaded = vault.load("tenant/user");
    check(loaded.has_value() && loaded->access_token == "access-secret", "vault roundtrip");
    check(!loaded->expired(), "fresh token is not expired");
    OAuthToken stale = token;
    stale.obtained_at = Clock::now() - std::chrono::hours(2);
    stale.expires_in = 3600;
    check(stale.expired(), "old token expires");
    check(vault.erase("tenant/user"), "vault erase");
    check(!vault.load("tenant/user").has_value(), "vault empty after erase");
}

void test_provenance_and_authorization() {
    AuthorizationRecord auth;
    auth.case_id = "case-1";
    auth.legal_basis = "warrant-123";
    auth.approved_by = "examiner";
    check(auth.valid(), "authorization valid");
    auth.legal_basis.clear();
    check(!auth.valid(), "authorization requires legal basis");

    Provenance provenance;
    provenance.provider = "m365";
    provenance.tenant_id = "tenant";
    provenance.account_id = "user@example.com";
    provenance.external_id = "message-1";
    provenance.endpoint = "/v1.0/me/messages/message-1";
    provenance.collected_at_utc = "2026-01-01T00:00:00Z";
    provenance.sha256 = std::string(64, 'a');
    provenance.job_id = "job-1";
    provenance.legal_basis = "warrant-123";
    check(provenance.complete(), "provenance complete");
    provenance.sha256 = "short";
    check(!provenance.complete(), "sha256 must be 64 hex chars");
    provenance.sha256 = std::string(64, 'a');
    const std::string json = provenance_json(provenance);
    check(json.find("warrant-123") != std::string::npos, "provenance includes legal basis");
    check(json.find("message-1") != std::string::npos, "provenance includes external id");

    AuditEvent audit;
    audit.action = "cloud.collect";
    audit.case_id = "case-1";
    audit.result = "ok";
    const std::string audit_line = audit_json(audit);
    check(audit_line.find("access_token") == std::string::npos, "audit excludes tokens");
    check(audit_line.find("cloud.collect") != std::string::npos, "audit includes action");
}

void test_m365_collector() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_cloud_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    MockHttpTransport transport;
    HttpResponse profile;
    profile.status = 200;
    profile.body = "{\"id\":\"user-1\",\"userPrincipalName\":\"analyst@example.test\"}";
    transport.push(profile);
    HttpResponse messages;
    messages.status = 200;
    messages.body = "{\"value\":[{\"id\":\"m1\",\"subject\":\"one\",\"receivedDateTime\":\"2026-01-01T10:00:00Z\"},"
                    "{\"id\":\"m2\",\"subject\":\"two\",\"receivedDateTime\":\"2026-01-02T10:00:00Z\"}]}";
    transport.push(messages);
    HttpResponse drive;
    drive.status = 200;
    drive.body = "{\"value\":[{\"id\":\"f1\",\"name\":\"report.txt\"}]}";
    transport.push(drive);

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(transport, policy);
    GraphClient graph(http, "collector-token");
    M365CollectConfig config;
    config.case_dir = root.string();
    config.tenant_id = "tenant-1";
    config.account = "analyst@example.test";
    config.legal_basis = "test-authorization";
    config.job_id = "job-1";
    const M365CollectReport report = collect_m365(graph, config);
    check(report.ok, "M365 collector succeeds");
    check(report.objects == 3, "M365 collector counted messages and drive objects");
    check(!report.manifest_path.empty() &&
          std::filesystem::exists(report.manifest_path), "M365 manifest exists");
    check(!report.job_path.empty() && std::filesystem::exists(report.job_path),
          "M365 job journal exists");
    const auto manifest_path = std::filesystem::path(report.manifest_path);
    std::ifstream manifest(manifest_path);
    const std::string manifest_text((std::istreambuf_iterator<char>(manifest)),
                                     std::istreambuf_iterator<char>());
    check(manifest_text.find("test-authorization") != std::string::npos,
          "M365 manifest records legal basis");
    check(manifest_text.find("collector-token") == std::string::npos,
          "M365 manifest contains no token");
    std::ifstream timeline(manifest_path.parent_path() / "timeline.jsonl");
    const std::string timeline_text((std::istreambuf_iterator<char>(timeline)),
                                     std::istreambuf_iterator<char>());
    check(timeline_text.find("M365/Mail") != std::string::npos,
          "M365 timeline exists");
    check(timeline_text.find("m1") != std::string::npos, "M365 timeline has message");
    manifest.close();
    timeline.close();
    const ManifestVerifyReport verified = verify_cloud_manifests(root.string(), config.account);
    check(verified.ok, "M365 manifest verifies");
    check(verified.verified == 3, "M365 verifies three raw files");
    std::ofstream tamper(manifest_path.parent_path() / "raw" / "messages-2.json",
                         std::ios::binary | std::ios::app);
    tamper << "tamper";
    tamper.close();
    const ManifestVerifyReport tampered = verify_cloud_manifests(root.string(), config.account);
    check(!tampered.ok, "tampered evidence fails verification");
    manifest.close();
    timeline.close();
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
}
void test_m365_resume() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_resume_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    MockHttpTransport first_transport;
    HttpResponse profile;
    profile.status = 200;
    profile.body = "{\"id\":\"user-1\"}";
    first_transport.push(profile);
    HttpResponse page_one;
    page_one.status = 200;
    page_one.body = "{\"value\":[{\"id\":\"m1\",\"subject\":\"one\"}],"
                    "\"@odata.nextLink\":\"https://graph.microsoft.com/v1.0/users/a/messages?page=2\"}";
    first_transport.push(page_one);
    HttpResponse page_two_failure;
    page_two_failure.status = 500;
    first_transport.push(page_two_failure);
    HttpResponse drive;
    drive.status = 200;
    drive.body = "{\"value\":[{\"id\":\"d1\",\"name\":\"file.txt\"}]}";
    first_transport.push(drive);

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient first_http(first_transport, policy);
    GraphClient first_graph(first_http, "resume-token");
    M365CollectConfig config;
    config.case_dir = root.string();
    config.tenant_id = "tenant";
    config.account = "analyst@example.test";
    config.legal_basis = "resume-warrant";
    config.job_id = "resume-job";
    const M365CollectReport first = collect_m365(first_graph, config);
    check(!first.ok && first.partial, "first M365 pass is partial");
    check(first.resumed == false, "first pass is not a resume");

    MockHttpTransport second_transport;
    HttpResponse page_two;
    page_two.status = 200;
    page_two.body = "{\"value\":[{\"id\":\"m2\",\"subject\":\"two\"}]}";
    second_transport.push(page_two);
    HttpClient second_http(second_transport, policy);
    GraphClient second_graph(second_http, "resume-token");
    const M365CollectReport second = collect_m365(second_graph, config);
    check(second.ok, "resumed M365 pass completes");
    check(second.resumed, "second pass is marked resumed");
    check(second_transport.requests().size() == 1, "resume requests only the checkpoint page");
    check(second_transport.requests().front().url.find("page=2") != std::string::npos,
          "resume starts at checkpoint nextLink");

    std::ifstream manifest(std::filesystem::path(second.manifest_path));
    const std::string manifest_text((std::istreambuf_iterator<char>(manifest)),
                                     std::istreambuf_iterator<char>());
    check(manifest_text.find("m2") == std::string::npos, "manifest excludes timeline content");
    std::ifstream timeline(std::filesystem::path(second.manifest_path).parent_path() / "timeline.jsonl");
    const std::string timeline_text((std::istreambuf_iterator<char>(timeline)),
                                     std::istreambuf_iterator<char>());
    check(timeline_text.find("m1") != std::string::npos &&
          timeline_text.find("m2") != std::string::npos,
          "resume preserves old and new timeline events");
    manifest.close();
    timeline.close();
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
}
std::string read_test_fixture(const std::string& name) {
    std::ifstream input(std::string("cloud/tests/fixtures/") + name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
}

void test_identity_and_session() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_identity_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const std::string path = (root / "identity.json").string();
    std::string error;
    LocalIdentityStore store(path);
    check(store.load(error), "empty identity store loads");
    check(store.create_user("admin", "Admin", Role::Admin, "StrongPass123!", error),
          "admin user created");
    check(store.create_user("examiner", "Examiner", Role::Examiner, "StrongPass456!", error),
          "examiner user created");
    check(!store.create_user("examiner", "Other", Role::Auditor, "StrongPass789!", error),
          "duplicate user rejected");
    check(store.users().size() == 2, "identity store lists two users");

    const auto admin = store.authenticate("admin", "StrongPass123!");
    check(admin.has_value() && admin->role == Role::Admin, "admin authenticates");
    check(!store.authenticate("admin", "wrong-password").has_value(),
          "wrong password rejected");
    check(!store.authenticate("missing", "StrongPass123!").has_value(),
          "unknown user rejected");

    check(store.set_active("examiner", false, error), "examiner deactivated");
    check(!store.authenticate("examiner", "StrongPass456!").has_value(),
          "inactive user rejected");
    check(store.set_active("examiner", true, error), "examiner reactivated");
    check(store.set_role("examiner", Role::Reviewer, error), "examiner role changed");
    const auto examiner = store.authenticate("examiner", "StrongPass456!");
    check(examiner.has_value() && examiner->role == Role::Reviewer,
          "changed role is persisted");

    LocalIdentityStore reloaded(path);
    check(reloaded.load(error) && reloaded.authenticate("admin", "StrongPass123!").has_value(),
          "identity store reloads");

    SessionManager sessions(3600);
    const auto token = sessions.create(*admin, error);
    check(token.has_value(), "session created");
    check(sessions.validate(*token).has_value(), "session validates");
    check(sessions.revoke(*token), "session revoked");
    check(!sessions.validate(*token).has_value(), "revoked session rejected");

    SessionVault vault((root / "session.bin").string());
    PersistedSession snapshot;
    snapshot.token = *token;
    snapshot.principal = *admin;
    snapshot.expires_at_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        (std::chrono::system_clock::now() + std::chrono::hours(1)).time_since_epoch()).count();
    check(vault.save(snapshot, error), "session vault saves");
    bool found = false;
    const auto loaded_session = vault.load(found, error);
    check(found && loaded_session.has_value() &&
          loaded_session->principal.id == "admin", "session vault loads");
    SessionManager restored(3600);
    check(restored.restore(loaded_session->token, loaded_session->principal,
                          std::chrono::system_clock::time_point(std::chrono::seconds(
                              loaded_session->expires_at_epoch)), error),
          "persisted session restores");
    check(restored.validate(loaded_session->token).has_value(),
          "restored session validates");
    check(vault.erase(error), "session vault erases");
    SessionManager expired(0);
    const auto short_token = expired.create(*admin, error);
    check(short_token.has_value() && !expired.validate(*short_token).has_value(),
          "expired session rejected");

    Principal mapped;
    mapped.id = "user-1";
    mapped.display_name = "analyst@example.test";
    mapped.role = Role::Examiner;
    mapped.active = true;
    check(authorize(mapped, Permission::RunCollection).allowed,
          "mapped principal can collect");
    std::filesystem::remove_all(root);
}
void test_oidc() {
    const std::string jwks = read_test_fixture("oidc_jwks.json");
    const std::string token = read_test_fixture("oidc_token.txt");
    check(!jwks.empty() && !token.empty(), "OIDC fixtures load");
    const JwtResult valid = verify_rs256_token(token, jwks,
        "https://issuer.example.test", "client-123");
    check(valid.ok, "RS256 JWT verifies");
    check(valid.claims.subject == "user-1", "JWT subject parsed");
    check(valid.claims.email == "analyst@example.test", "JWT email parsed");
    const Principal mapped = principal_from_oidc(valid.claims, Role::Examiner);
    check(mapped.id == "user-1" && mapped.role == Role::Examiner && mapped.active,
          "OIDC claims map to principal");

    check(!verify_rs256_token(token, jwks, "https://wrong.example.test",
        "client-123").ok, "JWT wrong issuer rejected");
    check(!verify_rs256_token(token, jwks, "https://issuer.example.test",
        "wrong-client").ok, "JWT wrong audience rejected");

    std::string tampered = token;
    tampered.back() = (tampered.back() == 'A' ? 'B' : 'A');
    check(!verify_rs256_token(tampered, jwks, "https://issuer.example.test",
        "client-123").ok, "tampered JWT rejected");

    MockHttpTransport discovery_transport;
    HttpResponse discovery;
    discovery.status = 200;
    discovery.body = "{\"issuer\":\"https://issuer.example.test\","
                     "\"authorization_endpoint\":\"https://issuer.example.test/authorize\","
                     "\"token_endpoint\":\"https://issuer.example.test/token\","
                     "\"jwks_uri\":\"https://issuer.example.test/jwks\"}";
    discovery_transport.push(discovery);
    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(discovery_transport, policy);
    const OidcDiscoveryResult configuration = discover_oidc(http,
        "https://issuer.example.test/");
    check(configuration.ok, "OIDC discovery succeeds");
    check(configuration.configuration.jwks_uri == "https://issuer.example.test/jwks",
          "OIDC jwks_uri parsed");
}
void test_audit() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_audit_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const std::string path = (root / "audit.jsonl").string();
    AuditLog log(path);
    std::string error;
    check(log.append("admin", "login", "cloud", "allowed", "local", error),
          "audit login appended");
    check(log.append("admin", "user.create", "examiner", "allowed", "role=examiner", error),
          "audit user change appended");
    const AuditVerifyReport valid = log.verify();
    check(valid.ok && valid.entries == 2, "audit hash chain verifies");
    check(log.tail(10).size() == 2, "audit tail reads entries");

    std::ifstream input(path);
    std::string content((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    input.close();
    const auto first_quote = content.find("\"result\":\"allowed\"");
    if (first_quote != std::string::npos) content.replace(first_quote, 20, "\"result\":\"denied\"");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    output.close();
    check(!log.verify().ok, "tampered audit chain fails");
    std::filesystem::remove_all(root);
}
void test_rbac() {
    Principal admin;
    admin.id = "admin";
    admin.role = Role::Admin;
    admin.active = true;
    check(authorize(admin, Permission::ManageUsers).allowed, "admin can manage users");
    check(authorize(admin, Permission::RunCollection).allowed, "admin can collect");

    Principal examiner = admin;
    examiner.role = Role::Examiner;
    check(authorize(examiner, Permission::RunCollection).allowed, "examiner can collect");
    check(authorize(examiner, Permission::CancelJob).allowed, "examiner can cancel");
    check(authorize(examiner, Permission::ManageUsers).allowed == false,
          "examiner cannot manage users");

    Principal reviewer = admin;
    reviewer.role = Role::Reviewer;
    check(authorize(reviewer, Permission::VerifyEvidence).allowed, "reviewer can verify");
    check(authorize(reviewer, Permission::RunCollection).allowed == false,
          "reviewer cannot collect");

    Principal auditor = admin;
    auditor.role = Role::Auditor;
    check(authorize(auditor, Permission::ViewAudit).allowed, "auditor can view audit");
    check(authorize(auditor, Permission::RunCollection).allowed == false,
          "auditor cannot collect");

    auditor.active = false;
    check(authorize(auditor, Permission::ViewAudit).allowed == false,
          "inactive principal is denied");

    Role parsed = Role::Admin;
    check(parse_role("Examiner", parsed) && parsed == Role::Examiner,
          "role parser is case-insensitive");
    check(!parse_role("root", parsed), "unknown role is rejected");
    check(role_name(Role::Auditor) == "auditor", "role name");
    check(permission_name(Permission::RunCollection) == "run_collection",
          "permission name");
}
void test_attachment_redirect() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_redirect_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    MockHttpTransport transport;
    HttpResponse profile; profile.status = 200; profile.body = "{\"id\":\"u\"}";
    transport.push(profile);
    HttpResponse messages; messages.status = 200;
    messages.body = "{\"value\":[{\"id\":\"m1\",\"hasAttachments\":true}]}";
    transport.push(messages);
    HttpResponse list; list.status = 200;
    list.body = "{\"value\":[{\"id\":\"a1\",\"name\":\"large.bin\",\"size\":5}]}";
    transport.push(list);
    MockDownloadSpec redirect;
    redirect.status = 302;
    redirect.headers.push_back({"Location", "https://outlook.office365.com/attachment/a1"});
    transport.push_download(std::move(redirect));
    MockDownloadSpec payload;
    payload.status = 200;
    payload.chunks = {"hello"};
    transport.push_download(std::move(payload));
    HttpResponse drive; drive.status = 200; drive.body = "{\"value\":[]}";
    transport.push(drive);

    RetryPolicy policy; policy.max_attempts = 1;
    HttpClient http(transport, policy);
    GraphClient graph(http, "token");
    M365CollectConfig config;
    config.case_dir = root.string();
    config.tenant_id = "t";
    config.account = "a@b.test";
    config.legal_basis = "w";
    config.job_id = "redirect-job";
    config.download_attachments = true;
    const M365CollectReport report = collect_m365(graph, config);
    check(report.ok, "redirect attachment collector completes");
    check(report.objects == 2, "redirect attachment counted");
    check(transport.requests().size() >= 5, "redirect attachment followed value");
    std::ifstream timeline(std::filesystem::path(report.manifest_path).parent_path() /
                           "timeline.jsonl");
    const std::string timeline_text((std::istreambuf_iterator<char>(timeline)),
                                     std::istreambuf_iterator<char>());
    check(timeline_text.find("M365/Attachment") != std::string::npos,
          "redirect attachment timeline event");
    timeline.close();
    bool redirect_sent_bearer = false;
    for (const auto& request : transport.requests()) {
        if (request.url != "https://outlook.office365.com/attachment/a1") continue;
        if (request_has_bearer(request)) redirect_sent_bearer = true;
    }
    check(!redirect_sent_bearer, "attachment redirect download sends no bearer");
    bool redirect_download_sent_bearer = false;
    bool value_download_authenticated = false;
    for (const auto& request : transport.download_requests()) {
        const bool authenticated = download_has_bearer(request);
        if (request.url == "https://outlook.office365.com/attachment/a1" && authenticated)
            redirect_download_sent_bearer = true;
        if (request.url.find("/$value") != std::string::npos && authenticated)
            value_download_authenticated = true;
    }
    check(value_download_authenticated, "Graph $value download sends bearer");
    check(!redirect_download_sent_bearer, "streamed redirect download sends no bearer");
    const std::filesystem::path redirect_file =
        std::filesystem::path(report.manifest_path).parent_path() /
        "attachments" / "m1" / "a1_large.bin";
    check(std::filesystem::exists(redirect_file) &&
          read_file_bytes(redirect_file) == "hello",
          "redirected stream is written to the attachment path");
    check(!has_part_file(std::filesystem::path(report.manifest_path).parent_path()),
          "redirected stream leaves no .part file");
    std::filesystem::remove_all(root);
}
void test_attachment_redirect_hostile() {
    const auto probe = [](const std::string& location) {
        const auto root = std::filesystem::temp_directory_path() /
            ("olho_redirect_reject_test_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        MockHttpTransport transport;
        HttpResponse profile; profile.status = 200; profile.body = "{\"id\":\"u\"}";
        transport.push(profile);
        HttpResponse messages; messages.status = 200;
        messages.body = "{\"value\":[{\"id\":\"m1\",\"hasAttachments\":true}]}";
        transport.push(messages);
        HttpResponse list; list.status = 200;
        list.body = "{\"value\":[{\"id\":\"a1\",\"name\":\"large.bin\",\"size\":5}]}";
        transport.push(list);
        MockDownloadSpec redirect;
        redirect.status = 302;
        redirect.headers.push_back({"Location", location});
        transport.push_download(std::move(redirect));
        HttpResponse drive; drive.status = 200; drive.body = "{\"value\":[]}";
        transport.push(drive);

        RetryPolicy policy; policy.max_attempts = 1;
        HttpClient http(transport, policy);
        GraphClient graph(http, "token");
        M365CollectConfig config;
        config.case_dir = root.string();
        config.tenant_id = "t";
        config.account = "a@b.test";
        config.legal_basis = "w";
        config.job_id = "redirect-reject-job";
        config.download_attachments = true;
        const M365CollectReport report = collect_m365(graph, config);
        const std::vector<HttpRequest> requests = transport.requests();
        std::filesystem::remove_all(root);
        return std::make_pair(report, requests);
    };
    const auto check_rejected = [&](const std::string& location,
                                    const std::string& label) {
        const auto [report, requests] = probe(location);
        check(!report.ok && report.partial, label + ": collector is partial");
        check(report.error.find("redirect host not allowed") != std::string::npos,
              label + ": redirect host error recorded");
        bool followed = false;
        for (const auto& request : requests) {
            if (request.url == location) followed = true;
        }
        check(!followed, label + ": redirect target not requested");
        check(requests.size() == 5, label + ": no extra download request");
    };
    check_rejected("https://graph.microsoft.com:443@attacker.example.test/x",
                   "userinfo redirect");
    check_rejected("https://graph.microsoft.com@attacker.example.test/x",
                   "userinfo redirect without port");
    check_rejected("https://outlook.office365.com.evil.test/x",
                   "suffix boundary redirect");
    check_rejected("http://graph.microsoft.com/x", "http redirect");
}
void test_m365_page_limit_partial() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_page_limit_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    MockHttpTransport transport;
    HttpResponse profile;
    profile.status = 200;
    profile.body = "{\"id\":\"user-1\"}";
    transport.push(profile);
    HttpResponse messages;
    messages.status = 200;
    messages.body = "{\"value\":[{\"id\":\"m1\",\"hasAttachments\":true}],"
                    "\"@odata.nextLink\":\"https://graph.microsoft.com/v1.0/users/a/messages?page=2\"}";
    transport.push(messages);
    HttpResponse drive;
    drive.status = 200;
    drive.body = "{\"value\":[]}";
    transport.push(drive);

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(transport, policy);
    GraphClient graph(http, "limit-token");
    M365CollectConfig config;
    config.case_dir = root.string();
    config.tenant_id = "tenant";
    config.account = "analyst@example.test";
    config.legal_basis = "warrant";
    config.job_id = "limit-job";
    config.max_pages = 1;
    config.download_attachments = true;
    const M365CollectReport report = collect_m365(graph, config);
    check(!report.ok && report.partial, "page-limited collector is partial");
    check(transport.requests().size() == 3,
          "page-limited collector does not fetch the next page");
    bool attachments_requested = false;
    for (const auto& request : transport.requests()) {
        if (request.url.find("/attachments") != std::string::npos)
            attachments_requested = true;
    }
    check(!attachments_requested,
          "page-limited messages are not used for attachments");

    std::ifstream job(std::filesystem::path(report.job_path));
    const nlohmann::json job_json = nlohmann::json::parse(job, nullptr, false);
    job.close();
    check(job_json.is_object() && job_json.value("status", "") == "partial",
          "page-limited job is partial");
    const auto source_field = [&](const char* source, const char* field) {
        if (!job_json.is_object() || !job_json.contains("sources") ||
            !job_json["sources"].is_object() ||
            !job_json["sources"].contains(source) ||
            !job_json["sources"][source].is_object()) {
            return std::string();
        }
        return job_json["sources"][source].value(field, std::string());
    };
    check(source_field("messages", "status") == "partial",
          "page-limited messages stay partial");
    check(source_field("messages", "next_path").find("page=2") != std::string::npos,
          "page-limited messages checkpoint the next path");
    check(source_field("attachments", "status") == "partial",
          "attachments stay partial when messages are partial");

    std::ifstream manifest(std::filesystem::path(report.manifest_path));
    const nlohmann::json manifest_json = nlohmann::json::parse(manifest, nullptr, false);
    manifest.close();
    check(manifest_json.is_object() && manifest_json.value("status", "") == "partial",
          "page-limited manifest is partial");
    std::filesystem::remove_all(root);
}
void test_m365_unsafe_next_link() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_unsafe_next_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    MockHttpTransport transport;
    HttpResponse profile;
    profile.status = 200;
    profile.body = "{\"id\":\"user-1\"}";
    transport.push(profile);
    HttpResponse messages;
    messages.status = 200;
    messages.body = "{\"value\":[{\"id\":\"m1\"}],"
                    "\"@odata.nextLink\":\"https://attacker.example.test/next\"}";
    transport.push(messages);
    HttpResponse drive;
    drive.status = 200;
    drive.body = "{\"value\":[]}";
    transport.push(drive);

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(transport, policy);
    GraphClient graph(http, "unsafe-token");
    M365CollectConfig config;
    config.case_dir = root.string();
    config.tenant_id = "tenant";
    config.account = "analyst@example.test";
    config.legal_basis = "warrant";
    config.job_id = "unsafe-next-job";
    const M365CollectReport report = collect_m365(graph, config);
    check(!report.ok && report.partial,
          "unsafe nextLink makes the collector partial");
    check(report.error.find("nextLink") != std::string::npos,
          "unsafe nextLink error is reported");
    check(transport.requests().size() == 3,
          "unsafe nextLink is not followed");
    std::ifstream job(std::filesystem::path(report.job_path));
    const nlohmann::json job_json = nlohmann::json::parse(job, nullptr, false);
    job.close();
    std::string next_path;
    if (job_json.is_object() && job_json.contains("sources") &&
        job_json["sources"].is_object() &&
        job_json["sources"].contains("messages") &&
        job_json["sources"]["messages"].is_object()) {
        next_path = job_json["sources"]["messages"].value("next_path", std::string());
    }
    check(next_path.empty(), "unsafe nextLink is not checkpointed");
    std::filesystem::remove_all(root);
}
void test_attachment_collector() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_attach_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    MockHttpTransport transport;
    HttpResponse profile;
    profile.status = 200;
    profile.body = "{\"id\":\"user-1\"}";
    transport.push(profile);
    HttpResponse messages;
    messages.status = 200;
    messages.body = "{\"value\":[{\"id\":\"m1\",\"subject\":\"hello\",\"hasAttachments\":true}]}";
    transport.push(messages);
    HttpResponse attachments;
    attachments.status = 200;
    attachments.body = "{\"value\":[{\"id\":\"att1\",\"name\":\"note.txt\","
                        "\"contentType\":\"text/plain\",\"size\":16,"
                        "\"contentBytes\":\"aGVsbG8gYXR0YWNobWVudA==\"}]}";
    transport.push(attachments);
    HttpResponse drive;
    drive.status = 200;
    drive.body = "{\"value\":[]}";
    transport.push(drive);

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(transport, policy);
    GraphClient graph(http, "token");
    M365CollectConfig config;
    config.case_dir = root.string();
    config.tenant_id = "tenant";
    config.account = "analyst@example.test";
    config.legal_basis = "warrant";
    config.job_id = "attach-job";
    config.download_attachments = true;
    const M365CollectReport report = collect_m365(graph, config);
    check(report.ok, "attachment collector completes");
    check(report.objects == 2, "attachment collector counts message and attachment");
    std::filesystem::path attachment;
    std::filesystem::path manifest_path(report.manifest_path);
    std::ifstream manifest(manifest_path);
    const std::string manifest_text((std::istreambuf_iterator<char>(manifest)),
                                    std::istreambuf_iterator<char>());
    check(manifest_text.find("source\\\": \\\"attachment") != std::string::npos ||
          manifest_text.find("\"source\": \"attachment\"") != std::string::npos,
          "attachment manifest entry exists");
    const std::filesystem::path attachments_root = manifest_path.parent_path() / "attachments";
    check(std::filesystem::exists(attachments_root), "attachment directory exists");
    manifest.close();
    std::filesystem::remove_all(root);
}
void test_streaming_download_transport() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_stream_transport_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    RetryPolicy policy;
    policy.max_attempts = 1;
    const std::string payload = "chunked streaming payload";
    {
        MockHttpTransport transport;
        MockDownloadSpec spec;
        spec.status = 200;
        spec.chunks = {"chunked ", "streaming ", "payload"};
        transport.push_download(std::move(spec));
        HttpClient http(transport, policy);
        HttpDownloadRequest request;
        request.url = "https://graph.microsoft.com/v1.0/users/a/messages/m1/attachments/a1/$value";
        request.destination_path = (root / "stream.bin").string();
        request.headers.push_back({"Authorization", "Bearer streaming-token"});
        const HttpDownloadResult result = http.download(request);
        check(result.ok, "streaming download succeeds");
        check(result.bytes == payload.size(), "streaming download reports every byte");
        check(result.sha256 == sha256_hex(payload), "streaming download hashes the stream");
        check(read_file_bytes(root / "stream.bin") == payload,
              "streaming download writes chunked payload");
        check(!std::filesystem::exists(root / "stream.bin.part"),
              "successful stream removes the .part file");
        check(transport.download_requests().size() == 1,
              "streaming download records one request");
    }
    {
        // A declared Content-Length over the limit is rejected before any file
        // is created, so the destination never appears.
        MockHttpTransport transport;
        MockDownloadSpec spec;
        spec.headers.push_back({"Content-Length", "4096"});
        spec.chunks = {"tiny"};
        transport.push_download(std::move(spec));
        HttpClient http(transport, policy);
        HttpDownloadRequest request;
        request.url = "https://graph.microsoft.com/v1.0/users/a/messages/m1/attachments/a1/$value";
        request.destination_path = (root / "declared-large.bin").string();
        request.max_bytes = 64;
        const HttpDownloadResult result = http.download(request);
        check(!result.ok && result.limit_exceeded,
              "declared length above max_bytes is rejected");
        check(!std::filesystem::exists(root / "declared-large.bin") &&
              !std::filesystem::exists(root / "declared-large.bin.part"),
              "declared-length rejection leaves no artifact");
    }
    {
        // The limit is also enforced while writing, even when no Content-Length
        // header was advertised.
        MockHttpTransport transport;
        MockDownloadSpec spec;
        spec.chunks = {std::string(32, 'a'), std::string(32, 'b')};
        transport.push_download(std::move(spec));
        HttpClient http(transport, policy);
        HttpDownloadRequest request;
        request.url = "https://graph.microsoft.com/v1.0/users/a/messages/m1/attachments/a1/$value";
        request.destination_path = (root / "during-write-large.bin").string();
        request.max_bytes = 40;
        const HttpDownloadResult result = http.download(request);
        check(!result.ok && result.limit_exceeded,
              "stream crossing max_bytes is rejected while writing");
        check(!std::filesystem::exists(root / "during-write-large.bin") &&
              !std::filesystem::exists(root / "during-write-large.bin.part"),
              "mid-stream limit rejection removes the .part file");
    }
    {
        MockHttpTransport transport;
        MockDownloadSpec spec;
        spec.chunks = {std::string(16, 'x'), std::string(16, 'y')};
        spec.fail_after_bytes = 16;
        spec.failure_error = "stream aborted";
        transport.push_download(std::move(spec));
        HttpClient http(transport, policy);
        HttpDownloadRequest request;
        request.url = "https://graph.microsoft.com/v1.0/users/a/messages/m1/attachments/a1/$value";
        request.destination_path = (root / "partial.bin").string();
        const HttpDownloadResult result = http.download(request);
        check(!result.ok && result.transport_error && result.error == "stream aborted",
              "partial stream is reported as a transport error");
        check(result.bytes == 0 && result.sha256.empty(),
              "partial stream reports no committed bytes or hash");
        check(!std::filesystem::exists(root / "partial.bin"),
              "partial stream never publishes the destination");
        check(!std::filesystem::exists(root / "partial.bin.part"),
              "partial stream removes the .part file");
    }
    {
        // The transport never follows redirects; the caller decides whether the
        // Location host is allowlisted and re-issues without credentials.
        MockHttpTransport transport;
        MockDownloadSpec spec;
        spec.status = 302;
        spec.headers.push_back({"Location", "https://outlook.office365.com/attachment/a1"});
        transport.push_download(std::move(spec));
        HttpClient http(transport, policy);
        HttpDownloadRequest request;
        request.url = "https://graph.microsoft.com/v1.0/users/a/messages/m1/attachments/a1/$value";
        request.destination_path = (root / "redirect.bin").string();
        request.headers.push_back({"Authorization", "Bearer streaming-token"});
        const HttpDownloadResult result = http.download(request);
        check(!result.ok && result.redirected && result.status == 302,
              "streaming download reports a redirect response");
        check(result.location == "https://outlook.office365.com/attachment/a1",
              "streaming download exposes the redirect location");
        check(!std::filesystem::exists(root / "redirect.bin") &&
              !std::filesystem::exists(root / "redirect.bin.part"),
              "redirect response leaves no file");
    }
    std::filesystem::remove_all(root);
}

void test_streaming_download_large_body() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_stream_large_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    // 50 MiB + 1 byte, just past the old in-memory ceiling. The body is
    // generated in 64 KiB chunks so the test never allocates the whole payload.
    constexpr std::size_t kChunkSize = 64 * 1024;
    constexpr std::size_t kChunkCount = 800;
    const std::uint64_t expected_bytes =
        static_cast<std::uint64_t>(kChunkSize) * kChunkCount + 1;

    MockHttpTransport transport;
    MockDownloadSpec spec;
    spec.status = 200;
    spec.chunks = {std::string(1, 'A')};
    spec.repeated_chunk_size = kChunkSize;
    spec.repeated_chunk_count = kChunkCount;
    spec.repeated_byte = 'A';
    transport.push_download(std::move(spec));

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(transport, policy);
    HttpDownloadRequest request;
    request.url = "https://graph.microsoft.com/v1.0/users/a/messages/m1/attachments/a1/$value";
    request.destination_path = (root / "large.bin").string();
    request.max_bytes = 64ull * 1024 * 1024;
    const HttpDownloadResult result = http.download(request);
    check(result.ok, "large streaming download succeeds");
    check(result.bytes == expected_bytes,
          "large streaming download reports 50 MiB + 1 byte");
    check(result.sha256 ==
              "776e8b7f1ade27a31d6d261805b8324daf30d4d2de85702cb83ddfe89eb22a39",
          "large streaming download hash matches the reference digest");
    std::error_code size_error;
    const std::uintmax_t on_disk = std::filesystem::file_size(root / "large.bin", size_error);
    check(!size_error && on_disk == expected_bytes,
          "large payload is streamed to disk at full size");
    check(!std::filesystem::exists(root / "large.bin.part"),
          "large download leaves no .part file");
    std::filesystem::remove_all(root);
}

void test_attachment_streaming_collector() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_stream_attach_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);

    MockHttpTransport transport;
    HttpResponse profile;
    profile.status = 200;
    profile.body = "{\"id\":\"user-1\"}";
    transport.push(profile);
    HttpResponse messages;
    messages.status = 200;
    messages.body = "{\"value\":[{\"id\":\"m1\",\"subject\":\"hello\",\"hasAttachments\":true}]}";
    transport.push(messages);
    HttpResponse attachments;
    attachments.status = 200;
    attachments.body = "{\"value\":[{\"id\":\"a1\",\"name\":\"streamed.txt\","
                       "\"contentType\":\"text/plain\",\"size\":20}]}";
    transport.push(attachments);
    MockDownloadSpec payload;
    payload.status = 200;
    payload.chunks = {"streamed ", "attachment!"};
    transport.push_download(std::move(payload));
    HttpResponse drive;
    drive.status = 200;
    drive.body = "{\"value\":[]}";
    transport.push(drive);

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(transport, policy);
    GraphClient graph(http, "stream-token");
    M365CollectConfig config;
    config.case_dir = root.string();
    config.tenant_id = "tenant";
    config.account = "analyst@example.test";
    config.legal_basis = "warrant";
    config.job_id = "stream-job";
    config.download_attachments = true;
    config.max_attachment_bytes = 4096;
    const M365CollectReport report = collect_m365(graph, config);
    const std::string body = "streamed attachment!";
    check(report.ok, "streaming attachment collector completes");
    check(report.objects == 2, "streaming attachment collector counts message and attachment");
    check(transport.download_requests().size() == 1,
          "streaming attachment performs exactly one download");
    check(!transport.download_requests().empty() &&
          download_has_bearer(transport.download_requests().front()),
          "streaming attachment sends the bearer only to Graph");

    const std::filesystem::path manifest_path(report.manifest_path);
    std::ifstream manifest(manifest_path);
    const nlohmann::json manifest_json = nlohmann::json::parse(manifest, nullptr, false);
    manifest.close();
    nlohmann::json entry = nlohmann::json::object();
    if (manifest_json.is_object() && manifest_json.contains("files") &&
        manifest_json["files"].is_array()) {
        for (const auto& file : manifest_json["files"]) {
            if (file.is_object() && file.value("source", "") == "attachment")
                entry = file;
        }
    }
    check(entry.is_object(), "streaming attachment manifest entry exists");
    check(entry.value("sha256", "") == sha256_hex(body),
          "streaming attachment manifest records the stream hash");
    check(entry.value("size_bytes", 0ull) == body.size(),
          "streaming attachment manifest records the stream size");
    check(entry.value("message_id", "") == "m1",
          "streaming attachment manifest records the message id");
    check(entry.value("attachment_id", "") == "a1",
          "streaming attachment manifest records the attachment id");
    check(entry.value("original_name", "") == "streamed.txt",
          "streaming attachment manifest records the original name");
    check(entry.value("content_type", "") == "text/plain",
          "streaming attachment manifest records the content type");

    const std::filesystem::path output = manifest_path.parent_path();
    const std::filesystem::path target = output / "attachments" / "m1" / "a1_streamed.txt";
    check(std::filesystem::exists(target) && read_file_bytes(target) == body,
          "streaming attachment bytes are written to the case directory");
    check(!has_part_file(output), "streaming attachment leaves no .part file");
    std::ifstream timeline(output / "timeline.jsonl");
    const std::string timeline_text((std::istreambuf_iterator<char>(timeline)),
                                     std::istreambuf_iterator<char>());
    timeline.close();
    check(timeline_text.find("M365/Attachment") != std::string::npos,
          "streaming attachment emits a timeline event");
    std::filesystem::remove_all(root);
}

void test_attachment_streaming_limits() {
    const auto make_root = [](const char* prefix) {
        const auto root = std::filesystem::temp_directory_path() /
            (std::string(prefix) + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        return root;
    };
    const auto manifest_text = [](const std::filesystem::path& manifest_path) {
        std::ifstream manifest(manifest_path);
        return std::string((std::istreambuf_iterator<char>(manifest)),
                           std::istreambuf_iterator<char>());
    };

    // (a) Declared size over the configured limit: rejected before download.
    {
        const auto root = make_root("olho_stream_limit_declared_");
        MockHttpTransport transport;
        HttpResponse profile; profile.status = 200; profile.body = "{\"id\":\"u\"}";
        transport.push(profile);
        HttpResponse messages; messages.status = 200;
        messages.body = "{\"value\":[{\"id\":\"m1\",\"hasAttachments\":true}]}";
        transport.push(messages);
        HttpResponse list; list.status = 200;
        list.body = "{\"value\":[{\"id\":\"a1\",\"name\":\"huge.bin\",\"size\":4096}]}";
        transport.push(list);
        HttpResponse drive; drive.status = 200; drive.body = "{\"value\":[]}";
        transport.push(drive);
        RetryPolicy policy; policy.max_attempts = 1;
        HttpClient http(transport, policy);
        GraphClient graph(http, "token");
        M365CollectConfig config;
        config.case_dir = root.string();
        config.tenant_id = "t";
        config.account = "a@b.test";
        config.legal_basis = "w";
        config.job_id = "declared-limit-job";
        config.download_attachments = true;
        config.max_attachment_bytes = 1024;
        const M365CollectReport report = collect_m365(graph, config);
        check(!report.ok && report.partial,
              "declared oversize attachment makes the collector partial");
        check(report.error.find("configured size limit") != std::string::npos,
              "declared oversize attachment reports the configured limit");
        check(transport.download_requests().empty(),
              "declared oversize attachment is never downloaded");
        check(manifest_text(std::filesystem::path(report.manifest_path))
                  .find("\"source\": \"attachment\"") == std::string::npos,
              "declared oversize attachment has no manifest entry");
        check(!has_part_file(std::filesystem::path(report.manifest_path).parent_path()),
              "declared oversize attachment leaves no .part file");
        std::filesystem::remove_all(root);
    }

    // (b) The stream itself exceeds the limit mid-flight.
    {
        const auto root = make_root("olho_stream_limit_mid_");
        MockHttpTransport transport;
        HttpResponse profile; profile.status = 200; profile.body = "{\"id\":\"u\"}";
        transport.push(profile);
        HttpResponse messages; messages.status = 200;
        messages.body = "{\"value\":[{\"id\":\"m1\",\"hasAttachments\":true}]}";
        transport.push(messages);
        HttpResponse list; list.status = 200;
        list.body = "{\"value\":[{\"id\":\"a1\",\"name\":\"grow.bin\",\"size\":8}]}";
        transport.push(list);
        MockDownloadSpec spec;
        spec.status = 200;
        spec.chunks = {std::string(16, 'q'), std::string(16, 'r')};
        transport.push_download(std::move(spec));
        HttpResponse drive; drive.status = 200; drive.body = "{\"value\":[]}";
        transport.push(drive);
        RetryPolicy policy; policy.max_attempts = 1;
        HttpClient http(transport, policy);
        GraphClient graph(http, "token");
        M365CollectConfig config;
        config.case_dir = root.string();
        config.tenant_id = "t";
        config.account = "a@b.test";
        config.legal_basis = "w";
        config.job_id = "mid-limit-job";
        config.download_attachments = true;
        config.max_attachment_bytes = 16;
        const M365CollectReport report = collect_m365(graph, config);
        check(!report.ok && report.partial,
              "mid-stream oversize attachment makes the collector partial");
        check(report.error.find("configured size limit") != std::string::npos,
              "mid-stream oversize attachment reports the configured limit");
        check(transport.download_requests().size() == 1,
              "mid-stream oversize attachment stops after one download");
        check(manifest_text(std::filesystem::path(report.manifest_path))
                  .find("\"source\": \"attachment\"") == std::string::npos,
              "mid-stream oversize attachment has no manifest entry");
        const std::filesystem::path output =
            std::filesystem::path(report.manifest_path).parent_path();
        check(!std::filesystem::exists(output / "attachments" / "m1" / "a1_grow.bin"),
              "mid-stream limit failure leaves no artifact");
        check(!has_part_file(output), "mid-stream limit failure removes the .part file");
        std::filesystem::remove_all(root);
    }

    // (c) Metadata above the old 50 MB cap is no longer rejected by itself.
    {
        const auto root = make_root("olho_stream_limit_legacy_");
        MockHttpTransport transport;
        HttpResponse profile; profile.status = 200; profile.body = "{\"id\":\"u\"}";
        transport.push(profile);
        HttpResponse messages; messages.status = 200;
        messages.body = "{\"value\":[{\"id\":\"m1\",\"hasAttachments\":true}]}";
        transport.push(messages);
        HttpResponse list; list.status = 200;
        list.body = "{\"value\":[{\"id\":\"a1\",\"name\":\"big-metadata.bin\","
                    "\"size\":60000000}]}";
        transport.push(list);
        MockDownloadSpec spec;
        spec.status = 200;
        spec.chunks = {"small"};
        transport.push_download(std::move(spec));
        HttpResponse drive; drive.status = 200; drive.body = "{\"value\":[]}";
        transport.push(drive);
        RetryPolicy policy; policy.max_attempts = 1;
        HttpClient http(transport, policy);
        GraphClient graph(http, "token");
        M365CollectConfig config;
        config.case_dir = root.string();
        config.tenant_id = "t";
        config.account = "a@b.test";
        config.legal_basis = "w";
        config.job_id = "legacy-cap-job";
        config.download_attachments = true;
        // Default limit (2 GiB): a 60 MB declared size is accepted.
        const M365CollectReport report = collect_m365(graph, config);
        check(report.ok, "attachment above the old 50 MB cap is accepted by default");
        check(transport.download_requests().size() == 1,
              "attachment above the old 50 MB cap is streamed");
        const std::string text = manifest_text(std::filesystem::path(report.manifest_path));
        check(text.find("\"size_bytes\": 5") != std::string::npos,
              "streamed manifest records actual bytes, not declared metadata");
        std::ifstream timeline(std::filesystem::path(report.manifest_path).parent_path() /
                               "timeline.jsonl");
        const std::string timeline_text((std::istreambuf_iterator<char>(timeline)),
                                         std::istreambuf_iterator<char>());
        timeline.close();
        check(timeline_text.find("\"size\":60000000") != std::string::npos ||
              timeline_text.find("\"size\": 60000000") != std::string::npos,
              "timeline keeps the declared attachment size");
        std::filesystem::remove_all(root);
    }
}

void test_api_service() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_api_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "cloud" / "jobs");
    const std::string identity_path = (root / "identity.json").string();
    LocalIdentityStore store(identity_path);
    std::string error;
    check(store.load(error) &&
          store.create_user("admin", "Admin", Role::Admin, "AdminPass123!", error),
          "API identity created");
    std::ofstream job(root / "cloud" / "jobs" / "job.json");
    job << "{\"job_id\":\"api-job\",\"status\":\"complete\",\"account\":\"a@b.test\","
           "\"tenant_id\":\"tenant\",\"pages\":2,\"objects\":4,"
           "\"errors\":[],\"vault_key\":\"must-not-leak\"}";
    job.close();

    CloudApiService service(root.string(), identity_path,
                            (root / "audit.jsonl").string());
    const auto health = service.handle({"GET", "/health", "", ""});
    check(health.status == 200 && health.body.find("ok") != std::string::npos,
          "API health works");
    const auto unconfigured_oidc = service.handle({"POST", "/v1/session/oidc", "",
        R"json({"id_token":"token"})json"});
    check(unconfigured_oidc.status == 503, "API OIDC requires configuration");
    service.set_oidc_configuration("https://issuer.example.test", "client");
    service.set_oidc_verifier([](std::string_view, std::string_view, std::string_view) {
        JwtResult result;
        result.ok = true;
        result.claims.subject = "oidc-user";
        result.claims.email = "oidc@example.test";
        result.claims.issuer = "https://issuer.example.test";
        result.claims.roles = {"examiner"};
        return result;
    });
    const auto oidc = service.handle({"POST", "/v1/session/oidc", "",
        R"json({"issuer":"https://issuer.example.test","client_id":"client","id_token":"token"})json"});
    check(oidc.status == 200 && oidc.body.find("oidc-user") != std::string::npos,
          "API OIDC login works");
    check(oidc.body.find("examiner") != std::string::npos,
          "API OIDC role maps");
    check(service.handle({"GET", "/v1/jobs", "", ""}).status == 401,
          "API jobs requires auth");
    CloudApiService limited(root.string(), identity_path,
                                (root / "audit.jsonl").string(), 1);
    check(limited.handle({"GET", "/v1/jobs", "Bearer same", ""}).status == 401,
          "rate limit first request passes auth");
    check(limited.handle({"GET", "/v1/jobs", "Bearer same", ""}).status == 429,
          "API rate limit rejects excess");
    check(service.handle({"POST", "/v1/session", "",
                          "{\"username\":\"admin\",\"password\":\"bad\"}"}).status == 401,
          "API rejects bad password");
    const auto login = service.handle({"POST", "/v1/session", "",
        "{\"username\":\"admin\",\"password\":\"AdminPass123!\"}"});
    check(login.status == 200, "API login works");
    const auto token_json = nlohmann::json::parse(login.body);
    const std::string token = token_json.value("token", "");
    check(!token.empty(), "API returns session token");
    const std::string auth = "Bearer " + token;
    const auto jobs = service.handle({"GET", "/v1/jobs", auth, ""});
    check(jobs.status == 200 && jobs.body.find("api-job") != std::string::npos,
          "API lists jobs");
    check(jobs.body.find("must-not-leak") == std::string::npos,
          "API does not expose vault key");
    const auto audit = service.handle({"GET", "/v1/audit", auth, ""});
    check(audit.status == 200 && audit.body.find("api.login") != std::string::npos,
          "API reads audit");
    const auto enqueue = service.handle({"POST", "/v1/jobs", auth,
        R"json({"job_id":"queued-job","account":"a@b.test","tenant_id":"t","legal_basis":"warrant","vault_key":"secret-ref","max_pages":5})json"});
    check(enqueue.status == 201, "API enqueues job");
    const auto queue = service.handle({"GET", "/v1/queue", auth, ""});
    check(queue.status == 200 && queue.body.find("queued-job") != std::string::npos,
          "API lists queue");
    check(queue.body.find(R"json("provider":"m365")json") != std::string::npos,
          "API queue reports the provider");
    check(queue.body.find("secret-ref") == std::string::npos,
          "API queue does not expose vault key");
    std::filesystem::create_directories(root / "cloud" / "workers");
    std::ofstream heartbeat(root / "cloud" / "workers" / "w1.json");
    heartbeat << "{\"worker_id\":\"w1\",\"last_seen_epoch\":" << std::time(nullptr)
              << ",\"status\":\"online\",\"parallel\":1}";
    heartbeat.close();
    const auto metrics = service.handle({"GET", "/v1/metrics", auth, ""});
    check(metrics.status == 200 && metrics.body.find(R"json("queued":1)json") != std::string::npos,
          "API exposes queue metrics");
    check(metrics.body.find(R"json("online":1)json") != std::string::npos,
          "API exposes online workers");
    const auto prometheus = service.handle({"GET", "/v1/metrics/prometheus", auth, ""});
    check(prometheus.status == 200 &&
          prometheus.body.find("olho_cloud_queue_jobs") != std::string::npos,
          "API exposes Prometheus metrics");
    // Dropbox jobs carry no tenant and must be accepted; an unknown provider
    // must be refused before it reaches a worker. This runs after the metrics
    // assertions so the queued count there stays one.
    const auto dropbox_job = service.handle({"POST", "/v1/jobs", auth,
        R"json({"job_id":"queued-dbx","provider":"dropbox","account":"k@e.test","legal_basis":"warrant","vault_key":"secret-ref","root_path":"/caso","download_files":true})json"});
    check(dropbox_job.status == 201, "API enqueues a dropbox job without tenant");
    const auto bad_provider = service.handle({"POST", "/v1/jobs", auth,
        R"json({"job_id":"queued-bad","provider":"icloud","account":"k@e.test","legal_basis":"warrant","vault_key":"secret-ref"})json"});
    check(bad_provider.status == 400, "API refuses an unknown provider");
    const auto queue_after = service.handle({"GET", "/v1/queue", auth, ""});
    check(queue_after.body.find(R"json("provider":"dropbox")json") != std::string::npos &&
              queue_after.body.find("queued-bad") == std::string::npos,
          "dropbox job queued and the bad one dropped");
    JobQueue queue_store((root / "cloud" / "queue").string());
    check(queue_store.claim("queued-job", "worker-1", 60, error),
          "queue claim succeeds");
    check(!queue_store.claim("queued-job", "worker-2", 60, error),
          "queue claim is exclusive");
    check(service.handle({"DELETE", "/v1/queue/queued-job", auth, ""}).status == 200,
          "API cancels queued job");
    QueuedJob high;
    high.job_id = "priority-high";
    high.account = "a@b.test";
    high.tenant_id = "t";
    high.legal_basis = "warrant";
    high.vault_key = "ref";
    high.priority = 50;
    QueuedJob low = high;
    low.job_id = "priority-low";
    low.priority = -10;
    check(queue_store.enqueue(high, error) && queue_store.enqueue(low, error),
          "priority jobs enqueue");
    const auto ordered = queue_store.list();
    check(!ordered.empty() && ordered.front().job_id == "priority-high",
          "queue orders higher priority first");    const auto verify = service.handle({"POST", "/v1/verify", auth, "{}"});
    check(verify.status == 409, "API verify reports missing manifest");
    check(service.handle({"GET", "/nope", auth, ""}).status == 404,
          "API unknown route");
    check(service.handle({"DELETE", "/v1/session", auth, ""}).status == 200,
          "API logout works");
    check(service.handle({"GET", "/v1/jobs", auth, ""}).status == 401,
          "API revokes session");
    std::filesystem::remove_all(root);
}
void test_sha256() {
    check(sha256_hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 known vector");
}

void test_graph_client() {
    MockHttpTransport transport;
    HttpResponse first;
    first.status = 200;
    first.body = "{\"value\":[{\"id\":\"m1\"}],\"@odata.nextLink\":\"/v1.0/users/a/messages?page=2\"}";
    transport.push(first);
    HttpResponse second;
    second.status = 200;
    second.body = "{\"value\":[{\"id\":\"m2\"}]}";
    transport.push(second);

    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpClient http(transport, policy);
    GraphClient graph(http, "graph-token");
    int pages = 0;
    bool has_more = false;
    std::string error;
    int bodies = 0;
    const bool ok = graph.collect("/users/a/messages", 5,
        [&](std::string_view body, std::string& handler_error) {
            if (body.find("m1") != std::string::npos || body.find("m2") != std::string::npos)
                ++bodies;
            else {
                handler_error = "unexpected page";
                return false;
            }
            return true;
        }, error, pages, has_more);
    check(ok, "Graph pagination succeeds");
    check(pages == 2, "Graph fetched two pages");
    check(!has_more, "Graph does not advertise more pages");
    check(bodies == 2, "Graph page callback ran twice");
    check(!transport.requests().empty() &&
          transport.requests().front().headers.size() >= 1 &&
          transport.requests().front().headers.front().second == "Bearer graph-token",
          "Graph sends bearer token");

    MockHttpTransport denied;
    HttpResponse unauthorized;
    unauthorized.status = 401;
    unauthorized.body = "{}";
    denied.push(unauthorized);
    HttpClient denied_http(denied, policy);
    GraphClient denied_graph(denied_http, "bad-token");
    int denied_pages = 0;
    bool denied_more = false;
    std::string denied_error;
    check(!denied_graph.collect("/users/a/messages", 1,
        [](std::string_view, std::string&) { return true; }, denied_error, denied_pages,
        denied_more),
        "Graph 401 fails");
    check(denied_error.find("401") != std::string::npos, "Graph 401 error preserved");
}
void test_graph_url_safety() {
    RetryPolicy policy;
    policy.max_attempts = 1;
    HttpResponse success;
    success.status = 200;
    success.body = "{\"value\":[]}";

    MockHttpTransport relative_transport;
    relative_transport.push(success);
    HttpClient relative_http(relative_transport, policy);
    GraphClient relative_graph(relative_http, "url-token");
    const HttpResponse relative = relative_graph.get("/users/a/messages");
    check(relative.ok(), "relative Graph URL succeeds");
    check(relative_transport.requests().size() == 1 &&
          relative_transport.requests().front().url ==
              "https://graph.microsoft.com/v1.0/users/a/messages",
          "relative Graph URL resolves against base URL");
    check(relative_transport.requests().size() == 1 &&
          request_has_bearer(relative_transport.requests().front()),
          "relative Graph URL sends bearer");

    MockHttpTransport absolute_transport;
    absolute_transport.push(success);
    HttpClient absolute_http(absolute_transport, policy);
    GraphClient absolute_graph(absolute_http, "url-token");
    const HttpResponse absolute =
        absolute_graph.get("https://graph.microsoft.com/v1.0/me/messages?page=2");
    check(absolute.ok(), "same-host absolute Graph URL succeeds");
    check(absolute_transport.requests().size() == 1 &&
          request_has_bearer(absolute_transport.requests().front()),
          "same-host absolute URL sends bearer");

    const auto rejected = [&](const char* url, const std::string& label) {
        MockHttpTransport transport;
        HttpClient http(transport, policy);
        GraphClient graph(http, "url-token");
        const HttpResponse response = graph.get(url);
        check(response.transport_error, label);
        check(transport.requests().empty(), label + ": no request sent");
    };
    rejected("http://graph.microsoft.com/v1.0/me", "absolute http URL is rejected");
    rejected("https://attacker.example.test/v1.0/me",
             "foreign absolute URL is rejected");
    rejected("https://graph.microsoft.com.evil.test/v1.0/me",
             "suffixed foreign absolute URL is rejected");
    rejected("https://graph.microsoft.com:443@attacker.example.test/x",
             "userinfo absolute URL is rejected");
    rejected("https://graph.microsoft.com@attacker.example.test/x",
             "userinfo absolute URL without port is rejected");
    rejected("ftp://graph.microsoft.com/v1.0/me", "non-http scheme is rejected");

    // The page budget can stop collection while Graph still advertises a page.
    MockHttpTransport truncated_transport;
    HttpResponse truncated_page;
    truncated_page.status = 200;
    truncated_page.body = "{\"value\":[],"
                          "\"@odata.nextLink\":\"/v1.0/users/a/messages?page=2\"}";
    truncated_transport.push(truncated_page);
    HttpClient truncated_http(truncated_transport, policy);
    GraphClient truncated_graph(truncated_http, "url-token");
    int truncated_pages = 0;
    bool truncated_more = false;
    std::string truncated_error;
    check(truncated_graph.collect("/users/a/messages", 1,
        [](std::string_view, std::string&) { return true; }, truncated_error,
        truncated_pages, truncated_more), "page limit is not a collect error");
    check(truncated_pages == 1 && truncated_more,
          "page limit reports a pending next page");
    check(truncated_transport.requests().size() == 1,
          "page limit stops at max_pages");

    const auto hostile_next = [&](const char* link, const std::string& label) {
        MockHttpTransport transport;
        HttpResponse page;
        page.status = 200;
        page.body = std::string("{\"value\":[],\"@odata.nextLink\":\"") + link + "\"}";
        transport.push(page);
        HttpClient http(transport, policy);
        GraphClient graph(http, "url-token");
        int fetched = 0;
        bool more = true;
        std::string error;
        const bool ok = graph.collect("/users/a/messages", 5,
            [](std::string_view, std::string&) { return true; }, error, fetched, more);
        check(!ok, label);
        check(transport.requests().size() == 1, label + ": no follow-up request");
        check(!more, label + ": no more pages claimed");
    };
    hostile_next("https://attacker.example.test/v1.0/users/a/messages?page=2",
                 "foreign nextLink is rejected");
    hostile_next("http://graph.microsoft.com/v1.0/users/a/messages?page=2",
                 "http nextLink is rejected");
    hostile_next("https://graph.microsoft.com:443@attacker.example.test/x",
                 "userinfo nextLink is rejected");
}
void test_oauth_callback() {
    OAuthCallbackServer server;
    std::string start_error;
    check(server.start(start_error), "callback server starts");
    check(!server.redirect_uri().empty(), "callback redirect URI exists");
    std::thread client([&server]() {
        WinHttpTransport transport(L"OlhoDeDeus-Cloud-Test/0.1", true);
        HttpRequest request;
        request.method = "GET";
        request.url = server.redirect_uri() + "?code=callback-code&state=callback-state";
        transport.send(request);
    });
    const CallbackResult callback = server.wait(std::chrono::seconds(5));
    client.join();
    check(callback.ok(), "callback receives code");
    check(callback.code == "callback-code", "callback code parsed");
    check(callback.state == "callback-state", "callback state parsed");
    server.stop();
}
void test_oauth_pkce() {
    const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    check(pkce_challenge(verifier) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
          "RFC 7636 PKCE vector");
    const PkcePair pair = generate_pkce();
    check(pair.verifier.size() == 43, "PKCE verifier length");
    check(pair.challenge.size() == 43, "PKCE challenge length");
    check(pair.challenge == pkce_challenge(pair.verifier), "PKCE challenge matches verifier");

    OAuthConfig config;
    config.authorization_endpoint = "https://login.example.test/authorize";
    config.token_endpoint = "https://login.example.test/token";
    config.client_id = "client-id";
    config.redirect_uri = "http://127.0.0.1:7777/callback";
    config.scope = "User.Read Mail.Read Files.Read.All";
    const std::string url = build_authorize_url(config, "state-1", pair.challenge);
    check(url.find("response_type=code") != std::string::npos, "authorize includes response_type");
    check(url.find("client_id=client-id") != std::string::npos, "authorize includes client_id");
    check(url.find("code_challenge_method=S256") != std::string::npos,
          "authorize uses S256");
    check(url.find("scope=User.Read%20Mail.Read%20Files.Read.All") != std::string::npos,
          "authorize encodes scope");
    check(url.find("client_secret") == std::string::npos, "authorize has no client secret");

    MockHttpTransport transport;
    HttpResponse token_response;
    token_response.status = 200;
    token_response.body = "{\"access_token\":\"oauth-access\",\"refresh_token\":\"oauth-refresh\","
                          "\"token_type\":\"Bearer\",\"expires_in\":1800}";
    transport.push(token_response);
    const OAuthTokenResult exchanged = exchange_authorization_code(
        transport, config, "one-time-code", pair.verifier);
    check(exchanged.token.has_value(), "authorization code exchange succeeds");
    check(exchanged.token && exchanged.token->access_token == "oauth-access",
          "exchanged access token parsed");
    check(exchanged.token && exchanged.token->refresh_token == "oauth-refresh",
          "exchanged refresh token parsed");
    check(exchanged.token && exchanged.token->expires_in == 1800, "expires_in parsed");
    check(!transport.requests().empty() &&
          transport.requests().front().body.find("code_verifier=" + pair.verifier) != std::string::npos,
          "exchange sends verifier");
    check(transport.requests().front().body.find("client_secret") == std::string::npos,
          "exchange has no client secret");

    MockHttpTransport refresh_transport;
    HttpResponse refresh_response;
    refresh_response.status = 200;
    refresh_response.body = "{\"access_token\":\"new-access\",\"expires_in\":3600}";
    refresh_transport.push(refresh_response);
    const OAuthTokenResult refreshed = refresh_access_token(
        refresh_transport, config, "oauth-refresh");
    check(refreshed.token && refreshed.token->access_token == "new-access",
          "refresh token exchange succeeds");

    MockHttpTransport failure;
    HttpResponse invalid;
    invalid.status = 400;
    invalid.body = "{\"error\":\"invalid_grant\",\"error_description\":\"expired code\"}";
    failure.push(invalid);
    const OAuthTokenResult failed = exchange_authorization_code(
        failure, config, "expired", verifier);
    check(!failed.token.has_value(), "invalid grant has no token");
    check(failed.error.find("invalid_grant") != std::string::npos,
          "invalid grant error is preserved");
}
void test_dpapi_vault() {
    DpapiTokenVault vault;
    OAuthToken token;
    token.access_token = "dpapi-access-token";
    token.refresh_token = "dpapi-refresh-token";
    token.token_type = "Bearer";
    token.expires_in = 3600;
    token.reset_expiry();
    const std::string key = "test/case/dpapi-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    check(vault.save(key, token), "dpapi save");
    const auto loaded = vault.load(key);
    check(loaded.has_value() && loaded->access_token == "dpapi-access-token",
          "dpapi roundtrip");
    check(vault.load(key).has_value(), "dpapi token is readable");
    check(vault.erase(key), "dpapi erase");
    check(!vault.load(key).has_value(), "dpapi token gone after erase");
}
} // namespace

int main() {
    test_redaction();
    test_retry();
    test_vault_and_oauth();
    test_provenance_and_authorization();
    test_dpapi_vault();
    test_oauth_pkce();
    test_oauth_callback();
    test_m365_resume();
    test_identity_and_session();
    test_audit();
    test_api_service();
    test_rbac();
    test_oidc();
    test_sha256();
    test_graph_client();
    test_graph_url_safety();
    test_m365_collector();
    test_m365_page_limit_partial();
    test_m365_unsafe_next_link();
    test_attachment_redirect();
    test_attachment_redirect_hostile();
    test_attachment_collector();
    test_streaming_download_transport();
    test_streaming_download_large_body();
    test_attachment_streaming_collector();
    test_attachment_streaming_limits();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "cloud_core: all tests passed\n";
    return 0;
}
