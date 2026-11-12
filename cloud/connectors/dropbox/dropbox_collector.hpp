// language: C++, file: dropbox_collector.hpp, target: Windows 11 (MSVC / MinGW g++)
// Dropbox evidence collection: walks a folder tree, keeps every page the API
// returned, and checkpoints a resumable cursor.
//
// WHY A CURSOR CHECKPOINT
//   A large account does not fit in one run. Dropbox hands back an opaque cursor
//   per page, so the job journal stores it and a later run continues exactly
//   where the previous one stopped. Nothing is re-listed and nothing is skipped.
//
// WHAT LANDS ON DISK
//   <case>/cloud/dropbox/<account>/raw/       page bodies exactly as received
//   <case>/cloud/dropbox/<account>/files/     downloaded content, when enabled
//   <case>/cloud/dropbox/<account>/manifest.json
//   <case>/cloud/dropbox/<account>/timeline.jsonl
//   <case>/cloud/jobs/<job_id>.json           checkpoint journal
//
//   The manifest is the evidence of record: every entry carries its sha256 and
//   the size actually written. The timeline is the query index built from the
//   listing metadata, one JSON object per line, newest page appended last.
#pragma once

#include "cloud/connectors/dropbox/dropbox_http.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace olho::cloud::dropbox {

inline constexpr std::uint64_t kDefaultMaxFileBytes = 2ull * 1024 * 1024 * 1024;  // 2 GiB
inline constexpr std::uint64_t kHardMaxFileBytes = 8ull * 1024 * 1024 * 1024;     // 8 GiB

struct collect_config {
    std::string case_dir;
    std::string account;
    std::string legal_basis;
    std::string job_id;
    std::string root_path = "/";
    int max_pages = 50;
    bool download_files = false;
    // Zero selects the default; values above the hard maximum are clamped, so a
    // bad job payload cannot turn into unbounded disk use.
    std::uint64_t max_file_bytes = kDefaultMaxFileBytes;
    std::function<bool()> cancel_check;
    // Progresso para quem esta olhando (a GUI): paginas lidas, objetos vistos e
    // arquivos baixados ate agora. Chamado na thread de trabalho, entao o
    // receptor nao pode tocar em widget - so guardar numeros.
    std::function<void(int pages, int objects, int files)> progress;
};

struct collect_report {
    bool ok = false;
    bool partial = false;
    bool cancelled = false;
    bool resumed = false;
    int objects = 0;
    int pages = 0;
    int files_downloaded = 0;
    std::uint64_t bytes_downloaded = 0;
    std::string output_dir;
    std::string manifest_path;
    std::string job_path;
    std::string error;
};

collect_report collect(client& api, const collect_config& config);

}  // namespace olho::cloud::dropbox
