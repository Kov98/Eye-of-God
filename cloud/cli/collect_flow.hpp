#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace deep::cloud {

struct CollectFlowOptions {
    // Selects the connector. "m365" keeps the Microsoft Graph path; "dropbox"
    // needs no tenant and walks a folder tree instead.
    std::string provider = "m365";
    std::string case_dir;
    std::string account;
    std::string tenant;
    std::string vault_key;
    std::string legal_basis;
    std::string job;
    std::string since;
    std::string until;
    std::string base_url = "https://graph.microsoft.com/v1.0";
    // Dropbox only: folder to walk and whether payload bytes are downloaded.
    std::string root_path = "/";
    bool download_files = false;
    // App key used to refresh an expired Dropbox token. Empty means the vault
    // token must still be valid.
    std::string app_key;
    int max_pages = 50;
    bool mock_http = false;
    bool download_attachments = false;
    std::uint64_t max_file_bytes = 0;
    // Progresso opcional (paginas, objetos, arquivos). Chamado na thread de
    // trabalho; a GUI guarda em atomics e pinta a tela pelo timer.
    std::function<void(int pages, int objects, int files)> progress;
};

// Result of an in-process collection. Mirrors the fields the worker needs
// without making every caller include the M365 collector header.
struct CollectFlowOutcome {
    bool ok = false;
    bool partial = false;
    bool cancelled = false;
    int objects = 0;
    int pages = 0;
    int files_downloaded = 0;
    std::uint64_t bytes_downloaded = 0;
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
