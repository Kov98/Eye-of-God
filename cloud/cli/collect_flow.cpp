#include "collect_flow.hpp"

#include "cloud/connectors/m365/graph_client.hpp"
#include "cloud/connectors/m365/m365_collector.hpp"
#include "cloud/core/dpapi_token_vault.hpp"
#include "cloud/core/winhttp_transport.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace deep::cloud {

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

    if (options.case_dir.empty() || options.account.empty() || options.tenant.empty() ||
        options.vault_key.empty() || options.legal_basis.empty() || options.job.empty()) {
        result.error = "missing required argument";
        std::cerr << "collect: missing required argument\n";
        return 2;
    }
    if (cancelled && cancelled()) return mark_cancelled("collection cancelled");

    DpapiTokenVault vault;
    const auto token = vault.load(options.vault_key);
    if (!token || token->access_token.empty()) {
        result.error = "no cloud token in vault; run --cloud-m365-auth first";
        std::cerr << "collect: no cloud token in vault; run --cloud-m365-auth first\n";
        return 3;
    }
    if (token->expired()) {
        result.error = "cloud token expired; run --cloud-m365-auth again";
        std::cerr << "collect: cloud token expired; run --cloud-m365-auth again\n";
        return 3;
    }

    WinHttpTransport transport(L"OlhoDeDeus-Cloud-Collect/0.1", options.mock_http);
    RetryPolicy policy;
    policy.max_attempts = 4;
    HttpClient http(transport, policy);
    GraphClient graph(http, token->access_token, options.base_url);

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
