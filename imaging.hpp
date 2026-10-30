#pragma once

#include <cstdint>
#include <string>

namespace deep {

struct ImageOptions {
    std::wstring source;        // image file, or \\.\X: / X: for a live volume
    std::wstring output;        // raw image path; split adds .001, .002, ...
    std::uint64_t split_bytes = 0; // 0 = single file
};

// Creates a raw image with an incremental SHA-256 and a JSON manifest.
// Returns 0 on success, 1 on failure, 2 on usage error.
int create_image(const ImageOptions& options);

// Recomputes the hash of an image (single file or first split part with a
// manifest) and compares it with the manifest. Returns 0 when verified.
int verify_image(const std::wstring& image, const std::wstring& manifest);

} // namespace deep
