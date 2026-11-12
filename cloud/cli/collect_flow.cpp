#include "collect_flow.hpp"

#include "cloud/cli/dropbox_flow.hpp"
#include "cloud/connectors/dropbox/dropbox_http.hpp"
#include "cloud/connectors/m365/graph_client.hpp"
#include "cloud/connectors/m365/m365_collector.hpp"
#include "cloud/core/dpapi_token_vault.hpp"
#include "cloud/core/winhttp_transport.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace deep::cloud {
namespace {

bool is_dropbox(const CollectFlowOptions& options) {
    return options.provider == "dropbox" || options.provider == "Dropbox";
}

}  // namespace

int run_collect_flow(const CollectFlowOptions& options,
                     const std::function<bool()>& cancelled,
                     CollectFlowOutcome* outcome) {
    CollectFlowOutcome local;
    CollectFlowOutcome& result = outcome != nullptr ? *outcome : local;
    result = CollectFlowOutcome{};

    const auto mark_cancelled = [&](const std::string& message) {
        result.ok = false;
        result.partial = true;
        result.cancelled = true;
        if (result.error.empty()) result.error = message;
        return 6;
    };

    const bool dropbox = is_dropbox(options);
    if (options.case_dir.empty() || options.account.empty() ||
        (!dropbox && options.tenant.empty()) || options.vault_key.empty() ||
        options.legal_basis.empty() || options.job.empty()) {
        result.error = "missing required argument";
        std::cerr << "collect: missing required argument\n";
        return 2;
    }
    if (cancelled && cancelled()) return mark_cancelled("collection cancelled");

    DpapiTokenVault vault;
    const auto stored = vault.load(options.vault_key);
    if (!stored || stored->access_token.empty()) {
        result.error = "no cloud token in vault; run the auth command first";
        std::cerr << "collect: no cloud token in vault; run the auth command first\n";
        return 3;
    }
    OAuthToken token = *stored;

    WinHttpTransport transport(L"OlhoDeDeus-Cloud-Collect/0.1", options.mock_http);
    RetryPolicy policy;
    policy.max_attempts = 4;
    HttpClient http(transport, policy);

    if (dropbox && token.expired()) {
        // A Dropbox access token lives four hours; the refresh token is what
        // makes a long case possible. Only refreshed when the caller supplied the
        // app key, so an inaccessible secret is not guessed at.
        if (options.app_key.empty() || token.refresh_token.empty()) {
            result.error = "cloud token expired; run the auth command again";
            std::cerr << "collect: cloud token expired; run the auth command again\n";
            return 3;
        }
        olho::cloud::dropbox::client refresher(http, token.access_token);
        olho::cloud::dropbox::token_set fresh;
        std::string refresh_error;
        if (!refresher.refresh_access_token(options.app_key, token.refresh_token, fresh,
                                            refresh_error)) {
            result.error = "token refresh failed: " + refresh_error;
            std::cerr << "collect: token refresh failed: " << refresh_error << "\n";
            return 3;
        }
        token.access_token = fresh.access_token;
        if (!fresh.refresh_token.empty()) token.refresh_token = fresh.refresh_token;
        token.expires_in = static_cast<int>(fresh.expires_in == 0 ? 14400 : fresh.expires_in);
        token.reset_expiry();
        // Persist the rotation: a refresh token that is used but not stored is
        // lost the next time the app runs.
        if (!vault.save(options.vault_key, token)) {
            std::cerr << "collect: warning: refreshed token could not be saved to the vault\n";
        }
    }
    if (!dropbox && token.expired()) {
        result.error = "cloud token expired; run --cloud-m365-auth again";
        std::cerr << "collect: cloud token expired; run --cloud-m365-auth again\n";
        return 3;
    }

    if (dropbox) {
        olho::cloud::dropbox::client api(http, token.access_token);
        const DropboxFlowResult flow = run_dropbox_collect(api, options, cancelled);
        result = flow.outcome;
        std::cout << "collect: "
                  << (result.cancelled ? "cancelled"
                                       : (result.ok ? "complete"
                                                    : (result.partial ? "partial" : "failed")))
                  << "\n"
                  << "provider: dropbox\n"
                  << "objects: " << result.objects << "\n"
                  << "pages: " << result.pages << "\n"
                  << "files: " << result.files_downloaded << "\n"
                  << "bytes: " << result.bytes_downloaded << "\n"
                  << "output: " << result.output_dir << "\n"
                  << "manifest: " << result.manifest_path << "\n";
        if (!result.error.empty()) std::cerr << "error: " << result.error << "\n";
        return flow.exit_code;
    }

    GraphClient graph(http, token.access_token, options.base_url);

    M365CollectConfig config;
    config.case_dir = options.case_dir;
    config.tenant_id = options.tenant;
    config.account = options.account;
    config.legal_basis = options.legal_basis;
    config.job_id = options.job;
    config.vault_key = options.vault_key;
    config.since = options.since;
    config.until = options.until;
    config.max_pages = std::max(1, options.max_pages);
    config.download_attachments = options.download_attachments;
    config.cancel_check = cancelled;
    config.progress = options.progress;

    const M365CollectReport report = collect_m365(graph, config);
    result.ok = report.ok;
    result.partial = report.partial;
    result.cancelled = report.cancelled || (cancelled && cancelled());
    result.objects = report.objects;
    result.pages = report.pages;
    result.output_dir = report.output_dir;
    result.manifest_path = report.manifest_path;
    result.error = report.error;
    if (result.cancelled) {
        // A cancellation observed after the last collector checkpoint still
        // must never be reported as a completed collection.
        result.ok = false;
        result.partial = true;
        if (result.error.empty()) result.error = "collection cancelled";
    }

    std::cout << "collect: "
              << (result.cancelled ? "cancelled"
                                   : (result.ok ? "complete"
                                                : (result.partial ? "partial" : "failed")))
              << "\n"
              << "objects: " << result.objects << "\n"
              << "pages: " << result.pages << "\n"
              << "output: " << result.output_dir << "\n"
              << "manifest: " << result.manifest_path << "\n";
    if (!result.error.empty()) std::cerr << "error: " << result.error << "\n";
    if (result.ok) return 0;
    return result.partial ? 6 : 5;
}

int run_collect_flow(const CollectFlowOptions& options) {
    return run_collect_flow(options, std::function<bool()>(), nullptr);
}

} // namespace deep::cloud
