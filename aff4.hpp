#pragma once

#include <string>

namespace deep {

// AFF4-L directory container: version.txt, container.description,
// information.turtle and an uncompressed ImageStream split into 32 KiB chunks.
int create_aff4(const std::wstring& source, const std::wstring& out_dir,
                std::wstring& error);
int create_aff4_zip(const std::wstring& source, const std::wstring& out_file,
                    std::wstring& error);
int verify_aff4(const std::wstring& out_dir, std::wstring& error);
int verify_aff4_zip(const std::wstring& zip_file, std::wstring& error);

} // namespace deep
