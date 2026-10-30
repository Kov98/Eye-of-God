#pragma once

#include <cstdint>
#include <string>

namespace deep {

int kernel_version(std::wstring& error);
int kernel_read_disk(int disk_number, std::uint64_t offset, std::uint32_t length,
                     const std::wstring& out_file, std::wstring& error);

} // namespace deep
