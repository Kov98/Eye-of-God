#pragma once

// Deep acquisition, NTFS recovery, USN and sanitization engine for
// Olho de Deus.  Kept in a separate translation unit so the original
// single-file collector can link it without becoming unreadable.
//
// This module only touches a source the operator names explicitly:
// a raw image/volume for reading, or a path/volume for destruction.
// Destructive entry points require the exact confirmation token WIPE.

#include <cstdint>
#include <string>

namespace deep {

struct ScanOptions {
    std::wstring source;        // image file, or \\.\X: / X: for a live volume
    std::wstring out_dir;       // empty: .\cases\deep_<utc>
    std::uint64_t offset = 0;   // volume byte offset inside image; 0 = auto-detect
    bool list = false;          // write mft_inventory.jsonl
    bool recover = false;       // write recovered files under <out>\recovered
    bool usn = false;           // parse $Extend\$UsnJrnl:$J
    bool carve = false;         // carve signatures from unallocated clusters
    bool slack = false;         // carve signatures from file/MFT slack
    std::uint64_t min_carve = 4096;
    std::uint64_t max_carve = 256ull * 1024ull * 1024ull;
};

struct WipeOptions {
    std::wstring target;        // file/dir (wipe-file) or image/volume (wipe-free)
    std::wstring out_dir;       // audit output; empty = .\cases\wipe_<utc>
    std::uint64_t offset = 0;   // image volume offset for wipe-free
    unsigned passes = 1;
    bool confirm = false;
};

// Returns 0 on success, 1 on failure, 2 on usage error.
int run_scan(const ScanOptions& options);
int run_usn_file(const std::wstring& journal_file, const std::wstring& out_dir);
int run_wipe_file(const WipeOptions& options);
int run_wipe_free(const WipeOptions& options);
int run_purge_usn(const std::wstring& volume, bool confirm);
int run_clear_logs(bool confirm);
int run_wipe_artifacts(bool confirm);
int run_delete_vss(bool confirm);
int run_wipe_journal(const std::wstring& image, std::uint64_t offset,
                     const std::wstring& out_dir, bool confirm);
int run_vss_read(int number, const std::wstring& out_dir);

// Headless dispatch for main.cpp. Returns 0/1/2, or -1 when the command line
// is not a deep command (the interactive collector should continue).
int run_cli(int argc, char** argv);

// Menu entry points for options 13..17.
void interactive(int choice);
void set_sanitize_mode(bool enabled);
bool sanitize_mode_enabled();

} // namespace deep
