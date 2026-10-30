#pragma once

#include <string>

namespace deep {

// Read-only exposure audit: local sync folders, backup tools, shadow copies,
// Windows.old, File History, browser sync caches, VM snapshots and the default
// gateway. It never deletes anything.
int exposure_audit(const std::wstring& out_dir);

// Local-only cleanup. Remote backups, cloud copies, router logs and provider
// logs are outside the reach of this binary and are reported as residual risk.
int clean_local_residue(const std::wstring& out_dir, bool confirm,
                        bool include_sync_cache);

} // namespace deep
