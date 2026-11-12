#pragma once

#include "cloud/connectors/m365/graph_client.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace deep::cloud {

// Per-attachment download ceiling. Zero in the config means "use the default";
// the collector clamps every configured value to the hard maximum so a bad job
// payload cannot turn into unbounded disk use. The previous 50 MB hard limit is
// gone: streaming downloads go straight to disk.
inline constexpr std::uint64_t kM365DefaultMaxAttachmentBytes =
    2ull * 1024 * 1024 * 1024; // 2 GiB
inline constexpr std::uint64_t kM365HardMaxAttachmentBytes =
    8ull * 1024 * 1024 * 1024; // 8 GiB

struct M365CollectConfig {
    std::string case_dir;
    std::string tenant_id;
    std::string account;
    std::string legal_basis;
    std::string job_id;
    std::string vault_key;
    std::string since;
    std::string until;
    int max_pages = 50;
    bool download_attachments = false;
    // Maximum size of a single attachment download. 0 selects the default and
    // values above kM365HardMaxAttachmentBytes are clamped.
    std::uint64_t max_attachment_bytes = kM365DefaultMaxAttachmentBytes;
    // Polled by the collector at least once per Graph page and once per
    // attachment. Returning true aborts the collection promptly: the report is
    // marked partial/cancelled, the checkpoint is kept and nothing is reported
    // as complete.
    std::function<bool()> cancel_check;
    // Progresso para a GUI: paginas, objetos e anexos baixados ate agora. Roda
    // na thread de trabalho; o receptor so guarda numeros, nunca toca em widget.
    std::function<void(int pages, int objects, int files)> progress;
};

struct M365CollectReport {
    bool ok = false;
    bool partial = false;
    bool cancelled = false;
    bool resumed = false;
    int objects = 0;
    int pages = 0;
    std::string output_dir;
    std::string manifest_path;
    std::string job_path;
    std::string error;
};

M365CollectReport collect_m365(GraphClient& graph, const M365CollectConfig& config);

} // namespace deep::cloud
