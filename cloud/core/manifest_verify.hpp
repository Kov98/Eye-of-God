#pragma once

#include <string>
#include <vector>

namespace deep::cloud {

struct ManifestVerifyReport {
    bool ok = false;
    int manifests = 0;
    int files = 0;
    int verified = 0;
    int objects = 0;
    std::vector<std::string> errors;
};

// Verifies every cloud/m365 manifest under case_dir. When account is empty,
// all M365 accounts are checked. Paths inside the manifest are constrained to
// the manifest directory (no absolute paths or traversal).
ManifestVerifyReport verify_cloud_manifests(const std::string& case_dir,
                                            const std::string& account = {});

} // namespace deep::cloud
