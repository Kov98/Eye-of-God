#pragma once

#include <string>

namespace deep {

int sanitize_hive(const std::wstring& hive_path, const std::wstring& out_dir,
                  bool confirm);

} // namespace deep
