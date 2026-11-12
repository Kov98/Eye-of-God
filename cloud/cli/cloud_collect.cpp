// Phase 1 Microsoft 365 collector CLI. Shared flow lives in collect_flow.cpp.

#include "cloud/cli/collect_flow.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {

std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback = {}) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) return argv[i + 1];
    }
    return fallback;
}

bool present(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) if (name == argv[i]) return true;
    return false;
}

void usage() {
    std::cout
        << "cloud_collect --case <dir> --account <upn> --tenant <id> \\\n"
        << "  --vault-key <key> --legal-basis <ref> --job <id> [options]\n"
        << "  --provider m365|dropbox   default: m365 (dropbox needs no --tenant)\n"
        << "  --since YYYY-MM-DD       optional mailbox lower bound\n"
        << "  --until YYYY-MM-DD       optional mailbox upper bound\n"
        << "  --root /caminho          dropbox: folder to walk (default /)\n"
        << "  --download-files         dropbox: also download payload bytes\n"
        << "  --max-file-bytes N       dropbox: per-file ceiling (0 = default)\n"
        << "  --app-key KEY            dropbox: refresh an expired token\n"
        << "  --max-pages N            default: 50\n"
        << "  --base-url URL           default: https://graph.microsoft.com/v1.0\n"
        << "  --mock-http              allow HTTP (mock only)\n";
}

} // namespace

int main(int argc, char** argv) {
    if (present(argc, argv, "--help") || present(argc, argv, "-h")) {
        usage();
        return 0;
    }
    deep::cloud::CollectFlowOptions options;
    options.case_dir = argument(argc, argv, "--case");
    options.account = argument(argc, argv, "--account");
    options.tenant = argument(argc, argv, "--tenant");
    options.vault_key = argument(argc, argv, "--vault-key");
    options.legal_basis = argument(argc, argv, "--legal-basis");
    options.job = argument(argc, argv, "--job");
    options.since = argument(argc, argv, "--since");
    options.until = argument(argc, argv, "--until");
    options.base_url = argument(argc, argv, "--base-url",
        "https://graph.microsoft.com/v1.0");
    options.mock_http = present(argc, argv, "--mock-http");
    options.download_attachments = present(argc, argv, "--download-attachments");
    options.provider = argument(argc, argv, "--provider", "m365");
    options.root_path = argument(argc, argv, "--root", "/");
    options.app_key = argument(argc, argv, "--app-key");
    options.download_files = present(argc, argv, "--download-files");
    try {
        options.max_file_bytes = std::stoull(argument(argc, argv, "--max-file-bytes", "0"));
    } catch (...) {
        options.max_file_bytes = 0;
    }
    if (present(argc, argv, "--vault-key-stdin")) {
        std::string vault_key;
        std::getline(std::cin, vault_key);
        if (!vault_key.empty()) options.vault_key = vault_key;
    }
    try {
        options.max_pages = std::max(1,
            std::stoi(argument(argc, argv, "--max-pages", "50")));
    } catch (...) {
        options.max_pages = 50;
    }
    const bool dropbox = options.provider == "dropbox" || options.provider == "Dropbox";
    if (options.case_dir.empty() || options.account.empty() ||
        (!dropbox && options.tenant.empty()) || options.vault_key.empty() ||
        options.legal_basis.empty() || options.job.empty()) {
        usage();
        std::cerr << "error: missing required argument\n";
        return 2;
    }
    return deep::cloud::run_collect_flow(options);
}
