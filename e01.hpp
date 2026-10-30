#pragma once

#include <string>

namespace deep {

// Wrappers around the bundled libewf ewfacquire/ewfexport tools. The binaries
// live under libewf/ next to the executable (or third_party/libewf during
// development). They produce/read real E01 files; the engine then works on the
// exported raw image.
int e01_create(const std::wstring& source, const std::wstring& out_base,
               std::wstring& error);
int e01_export(const std::wstring& e01_file, const std::wstring& out_raw,
               std::wstring& error);

} // namespace deep
