// language: C++, file: evidence_verify.hpp, target: Windows 11 (MSVC / MinGW g++)
// Content-vs-extension verification for acquired evidence.
//
// WHY THIS EXISTS
//   A file named .pdf that is not a PDF enters a report as a PDF, gets hashed as a
//   PDF, and only fails when someone tries to open the exhibit. In this repository
//   there is already one such file: IMG-20260120-WA0010.pdf is a header, a byte
//   ramp and an %%EOF, with zero objects. The hash proves the bytes are unchanged;
//   it says nothing about whether they are what the name claims.
//
// The verdict is decided by content. The extension is only the claim being tested.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::evidence {

enum class verdict {
    ok,        // content matches the extension family
    mismatch,  // content is a different, recognised family (or is not what it says)
    unknown    // extension or content not in the table: no claim is made either way
};

struct file_check {
    std::string relative_path;
    std::string extension;
    std::string detected;    // "pdf", "jpeg", "zip", "unknown", ...
    verdict result = verdict::unknown;
    std::string detail;
};

struct verify_result {
    bool ok = false;
    std::uint64_t checked = 0;
    std::uint64_t matched = 0;
    std::uint64_t mismatched = 0;
    std::uint64_t unknown = 0;
    std::vector<file_check> findings;   // mismatches only
    std::string detail;
};

// Detects the container family by inspecting the bytes. Returns "unknown" when
// nothing matches; never guesses from the extension.
std::string detect_kind(const std::wstring& path);

// Classifies one already-detected file against its extension. Exposed so a
// caller that is already reading every file does not walk the tree a second
// time just to learn the verdict.
file_check classify(const std::string& relative_path, const std::string& detected);

// Walks a directory and checks every regular file.
verify_result verify_tree(const std::wstring& root);

// One line per finding, for the report notes.
std::vector<std::string> findings_as_notes(const verify_result& result);

}  // namespace olho::evidence
