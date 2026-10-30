#pragma once

#include <string>

namespace deep::cloud {

struct CloudDbImportReport {
    bool ok = false;
    int jobs = 0;
    int objects = 0;
    int objects_existing = 0;
    int artifacts = 0;
    int artifacts_existing = 0;
    int accounts = 0;
    std::string database_path;
    std::string error;
};

// Imports cloud/m365/*/manifest.json and timeline.jsonl into the case SQLite
// database. The manifest remains the evidence of record; the DB is a query
// index, not a replacement for raw hashes.
CloudDbImportReport import_cloud_case(const std::string& case_dir,
                                      const std::string& database_path = {});

} // namespace deep::cloud
