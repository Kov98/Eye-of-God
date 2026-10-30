#pragma once

#include <functional>
#include <string>

namespace deep::cloud {

struct CollectFlowOptions {
    std::string case_dir;
    std::string account;
    std::string tenant;
    std::string vault_key;
    std::string legal_basis;
    std::string job;
    std::string since;
    std::string until;
    std::string base_url = "https://graph.microsoft.com/v1.0";
    int max_pages = 50;
    bool mock_http = false;
    bool download_attachments = false;
};

// Result of an in-process collection. Mirrors the fields the worker needs
// without making every caller include the M365 collector header.
struct CollectFlowOutcome {
    bool ok = false;
    bool partial = false;
    bool cancelled = false;
    int objects = 0;
    int pages = 0;
    std::string output_dir;
    std::string manifest_path;
    std::string error;
};

// In-process entry point: loads the DPAPI token identified by
// options.vault_key and runs the read-only M365 collector on the calling
// thread. `cancelled` is polled by the collector (at least once per Graph page
// and once per attachment); when it returns true the collection stops
// promptly, keeps its checkpoint and is reported partial/cancelled. Always
// returns the same process-style exit codes as the CLI wrapper below, and
// optionally fills `outcome`.
int run_collect_flow(const CollectFlowOptions& options,
                     const std::function<bool()>& cancelled,
                     CollectFlowOutcome* outcome = nullptr);

// CLI compatibility wrapper used by OlhoDeDeus_deep.exe --cloud-m365-collect.
// Runs the same flow without a cancellation callback.
int run_collect_flow(const CollectFlowOptions& options);

} // namespace deep::cloud
