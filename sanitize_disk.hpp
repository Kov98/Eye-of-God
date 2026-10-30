#pragma once

#include <string>

namespace deep {

// Physical-disk sanitizer. Refuses the disk that contains the running Windows
// installation and any disk with a mounted volume. Intended for an offline
// WinPE environment or a secondary disk. This is overwrite-based; it cannot
// guarantee removal from SSD overprovisioning, firmware, backups or the cloud.
int list_disks();
int sanitize_disk(int disk_number, const std::wstring& expected_serial,
                  unsigned passes, bool verify, bool confirm,
                  const std::wstring& out_dir);
int nvme_sanitize(int disk_number, const std::wstring& expected_serial,
                   int action, bool confirm, const std::wstring& out_dir);
int ata_erase(int disk_number, const std::wstring& expected_serial,
              bool enhanced, const std::wstring& password, bool confirm,
              const std::wstring& out_dir);

} // namespace deep
