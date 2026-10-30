#pragma once

#include <string>

namespace deep::cloud {

struct CloudReportResult {
    bool ok = false;
    std::string path;
    std::string error;
};

// Generates a self-contained HTML or JSON report from the case's cloud
// manifests, job journals and verification results. No network access.
CloudReportResult generate_cloud_report(const std::string& case_dir,
                                        const std::string& output_path,
                                        const std::string& format = {});

} // namespace deep::cloud
