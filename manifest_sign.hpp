#pragma once

#include <string>

namespace deep {

// ECDSA P-256 / SHA-256 manifest signatures. Keys are CNG ECC blobs with a
// small file header; signatures live in a separate .sig JSON file.
int generate_signing_key(const std::wstring& prefix, std::wstring& error);
int sign_manifest(const std::wstring& manifest, const std::wstring& private_key,
                  const std::wstring& signature_path, std::wstring& error);
int verify_manifest(const std::wstring& manifest, const std::wstring& public_key,
                    const std::wstring& signature_path, std::wstring& error);

} // namespace deep
