// language: C++, file: cloud/cli/dropbox_flow.cpp
#include "cloud/cli/dropbox_flow.hpp"

#include <algorithm>

namespace deep::cloud {

DropboxFlowResult run_dropbox_collect(olho::cloud::dropbox::client& api,
                                      const CollectFlowOptions& options,
                                      const std::function<bool()>& cancelled) {
    using olho::cloud::dropbox::collect_config;
    using olho::cloud::dropbox::collect_report;

    DropboxFlowResult result;
    CollectFlowOutcome& out = result.outcome;

    // A tenant is not required: Dropbox has no tenant concept, and inventing one
    // would put a false value in the case database.
    if (options.case_dir.empty() || options.account.empty() || options.vault_key.empty() ||
        options.legal_basis.empty() || options.job.empty()) {
        out.error = "missing required argument: case, account, vault-key, legal-basis, job";
        result.exit_code = 2;
        return result;
    }
    if (cancelled && cancelled()) {
        out.ok = false;
        out.partial = true;
        out.cancelled = true;
        out.error = "collection cancelled";
        result.exit_code = 6;
        return result;
    }

    collect_config config;
    config.case_dir = options.case_dir;
    config.account = options.account;
    config.legal_basis = options.legal_basis;
    config.job_id = options.job;
    config.root_path = options.root_path.empty() ? std::string("/") : options.root_path;
    config.max_pages = std::max(1, options.max_pages);
    config.download_files = options.download_files;
    config.max_file_bytes = options.max_file_bytes == 0
        ? olho::cloud::dropbox::kDefaultMaxFileBytes
        : std::min(options.max_file_bytes, olho::cloud::dropbox::kHardMaxFileBytes);
    config.cancel_check = cancelled;
    config.progress = options.progress;

    const collect_report report = olho::cloud::dropbox::collect(api, config);

    out.ok = report.ok;
    out.partial = report.partial;
    out.cancelled = report.cancelled;
    out.objects = report.objects;
    out.pages = report.pages;
    out.files_downloaded = report.files_downloaded;
    out.bytes_downloaded = report.bytes_downloaded;
    out.output_dir = report.output_dir;
    out.manifest_path = report.manifest_path;
    out.error = report.error;

    if (out.cancelled) {
        // A cancellation observed after the last checkpoint is still never a
        // completed collection.
        out.ok = false;
        out.partial = true;
        if (out.error.empty()) out.error = "collection cancelled";
    }
    if (out.ok && !out.partial) {
        result.exit_code = 0;
    } else if (out.partial) {
        result.exit_code = 6;
    } else {
        result.exit_code = 5;
    }
    return result;
}

}  // namespace deep::cloud
