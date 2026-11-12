// language: C++, file: dropbox_collector.cpp
#include "cloud/connectors/dropbox/dropbox_collector.hpp"

#include "cloud/connectors/dropbox/dropbox_client.hpp"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace olho::cloud::dropbox {
namespace {

namespace fs = std::filesystem;

std::string now_utc() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

// Keeps a components name usable as a path segment: no separators, no drive
// colon, no dot-only names, bounded length. A hostile account or file name must
// not decide where evidence is written.
std::string sanitize_component(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        out.push_back(plain ? static_cast<char>(c) : '_');
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    if (out.empty()) out = "unnamed";
    if (out.size() > 80) out.resize(80);
    return out;
}

bool write_text_file(const fs::path& path, std::string_view content, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "nao foi possivel escrever " + path.string();
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
        error = "falha ao gravar " + path.string();
        return false;
    }
    return true;
}

bool read_text_file(const fs::path& path, std::string& content) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

collect_report collect(client& api, const collect_config& config) {
    collect_report report;
    if (config.case_dir.empty() || config.account.empty() || config.legal_basis.empty() ||
        config.job_id.empty()) {
        report.error = "case_dir, account, legal_basis and job_id are required";
        return report;
    }
    if (config.cancel_check && config.cancel_check()) {
        report.cancelled = true;
        report.partial = true;
        report.error = "collection cancelled";
        return report;
    }

    // The root path is sanitised before it is used as a listing argument, so a
    // hostile case file cannot point the collection outside the account.
    std::string root;
    std::string path_error;
    if (!sanitize_path(config.root_path, root, path_error)) {
        report.error = "invalid root path: " + path_error;
        return report;
    }

    const fs::path output =
        fs::path(config.case_dir) / "cloud" / "dropbox" / sanitize_component(config.account);
    const fs::path raw = output / "raw";
    const fs::path files_dir = output / "files";
    const fs::path jobs_dir = fs::path(config.case_dir) / "cloud" / "jobs";
    std::error_code directory_error;
    fs::create_directories(raw, directory_error);
    if (directory_error) {
        report.error = "could not create cloud output directory";
        return report;
    }
    if (config.download_files) {
        fs::create_directories(files_dir, directory_error);
        if (directory_error) {
            report.error = "could not create download directory";
            return report;
        }
    }
    // The journal is the resume mechanism. Without this directory a fresh case
    // silently loses every checkpoint and a resumed job restarts from zero.
    fs::create_directories(jobs_dir, directory_error);
    if (directory_error) {
        report.error = "could not create cloud job directory";
        return report;
    }

    const std::string case_id = fs::path(config.case_dir).filename().string();
    report.output_dir = output.string();
    report.job_path = (jobs_dir / (sanitize_component(config.job_id) + ".json")).string();
    const fs::path manifest_path = output / "manifest.json";
    const fs::path timeline_path = output / "timeline.jsonl";

    nlohmann::json previous_job;
    nlohmann::json manifest;
    std::string previous_text;
    std::string cursor;
    if (read_text_file(report.job_path, previous_text)) {
        try {
            previous_job = nlohmann::json::parse(previous_text);
        } catch (const std::exception&) {
            previous_job = nlohmann::json::object();
        }
        if (previous_job.is_object() && previous_job.value("status", "") == "complete") {
            report.error = "job is already complete; use a new job id to collect again";
            return report;
        }
        if (previous_job.is_object() && previous_job.value("status", "") != "failed") {
            if (previous_job.value("account", config.account) != config.account ||
                previous_job.value("legal_basis", config.legal_basis) != config.legal_basis ||
                previous_job.value("root_path", config.root_path) != config.root_path) {
                report.error = "job id already belongs to another account/root/legal basis";
                return report;
            }
            report.resumed = true;
            cursor = previous_job.value("cursor", "");
            report.pages = previous_job.value("pages", 0);
            report.objects = previous_job.value("objects", 0);
            report.files_downloaded = previous_job.value("files_downloaded", 0);
            report.bytes_downloaded = previous_job.value("bytes_downloaded", 0ull);
        }
    }

    if (report.resumed && read_text_file(manifest_path, previous_text)) {
        try {
            const nlohmann::json parsed = nlohmann::json::parse(previous_text);
            if (parsed.is_object()) manifest = parsed;
        } catch (const std::exception&) {
            // A damaged manifest is rebuilt from the new pages; the job journal
            // stays authoritative for resume.
        }
    }
    if (!manifest.is_object()) manifest = nlohmann::json::object();
    manifest["provider"] = "dropbox";
    manifest["account"] = config.account;
    manifest["legal_basis"] = config.legal_basis;
    manifest["job_id"] = config.job_id;
    manifest["root_path"] = root;
    manifest["max_pages"] = config.max_pages;
    manifest["download_files"] = config.download_files;
    if (!manifest.contains("collected_at_utc")) manifest["collected_at_utc"] = now_utc();
    if (!manifest["files"].is_array()) manifest["files"] = nlohmann::json::array();
    if (!manifest["downloads"].is_array()) manifest["downloads"] = nlohmann::json::array();
    if (!manifest["errors"].is_array()) manifest["errors"] = nlohmann::json::array();
    manifest["job_status_file"] = "cloud/jobs/" + sanitize_component(config.job_id) + ".json";
    manifest["status"] = "running";
    report.manifest_path = manifest_path.string();

    int file_index = static_cast<int>(manifest["files"].size());
    const std::string started_at = previous_job.is_object()
        ? previous_job.value("started_at_utc", now_utc())
        : now_utc();

    bool journal_failed = false;
    const auto write_job = [&](const std::string& status) {
        const nlohmann::json job = {
            {"job_id", config.job_id},
            {"case_id", case_id},
            {"provider", "dropbox"},
            {"account", config.account},
            {"legal_basis", config.legal_basis},
            {"root_path", config.root_path},
            {"status", status},
            {"cursor", cursor},
            {"pages", report.pages},
            {"objects", report.objects},
            {"files_downloaded", report.files_downloaded},
            {"bytes_downloaded", report.bytes_downloaded},
            {"resumed", report.resumed},
            {"started_at_utc", started_at},
            {"updated_at_utc", now_utc()},
            {"errors", manifest["errors"]}
        };
        std::string job_error;
        if (!write_text_file(report.job_path, job.dump(2), job_error) && !journal_failed) {
            journal_failed = true;
            report.partial = true;
            if (report.error.empty()) report.error = "checkpoint journal: " + job_error;
            manifest["errors"].push_back({{"source", "journal"}, {"error", job_error}});
        }
    };
    write_job("running");

    std::ofstream timeline(timeline_path,
                           std::ios::binary | (report.resumed ? std::ios::app : std::ios::trunc));
    if (!timeline) {
        report.error = "could not open cloud timeline";
        return report;
    }

    const auto add_error = [&](const std::string& source, const std::string& error) {
        report.partial = true;
        manifest["errors"].push_back({{"source", source}, {"error", error}});
        if (report.error.empty()) report.error = source + ": " + error;
    };

    bool cancelled_flag = false;
    const auto check_cancel = [&]() -> bool {
        if (cancelled_flag) return true;
        if (!config.cancel_check || !config.cancel_check()) return false;
        cancelled_flag = true;
        report.cancelled = true;
        report.partial = true;
        if (report.error.empty()) report.error = "collection cancelled";
        manifest["errors"].push_back({{"source", "collector"},
                                      {"error", "collection cancelled"}});
        return true;
    };

    bool budget_exhausted = false;
    bool listing_failed = false;
    bool first_page = true;
    while (true) {
        if (check_cancel()) break;
        if (report.pages >= config.max_pages) {
            // The cursor is kept, so the next run continues instead of
            // restarting. Without this the collector could loop forever.
            budget_exhausted = true;
            break;
        }

        listing page;
        std::string list_error;
        const std::string path_arg = (first_page && cursor.empty()) ? root : std::string();
        if (!api.list_page(path_arg, cursor, page, list_error)) {
            add_error("list_folder", list_error);
            listing_failed = true;
            break;
        }
        first_page = false;
        ++report.pages;

        // Keep the page exactly as received: the bytes are the evidence, the
        // parsed view is only an index of them.
        const std::string raw_name = "list-" + std::to_string(++file_index) + ".json";
        std::string raw_error;
        if (!api.last_listing_body().empty() &&
            write_text_file(raw / raw_name, api.last_listing_body(), raw_error)) {
            manifest["files"].push_back({{"name", "raw/" + raw_name},
                                         {"size_bytes", api.last_listing_body().size()},
                                         {"sha256", deep::cloud::sha256_hex(api.last_listing_body())},
                                         {"source", "list_folder"}});
        } else if (!raw_error.empty()) {
            add_error("raw", raw_error);
        }

        for (const auto& entry : page.entries) {
            const std::string timestamp =
                entry.server_modified.empty() ? now_utc() : entry.server_modified;
            timeline << nlohmann::json{
                {"timestamp_utc", timestamp},
                {"channel", "Dropbox/Files"},
                {"event_id", entry.id},
                {"provider", "Dropbox"},
                {"computer", config.account},
                {"record_id", entry.id},
                {"event_user_sid", config.account},
                {"event_data", {{"name", entry.name},
                                {"path", entry.path_lower},
                                {"tag", entry.tag},
                                {"size", entry.size},
                                {"content_hash", entry.content_hash}}}
            }.dump() << "\n";
            ++report.objects;

            if (!config.download_files || entry.tag != "file" || entry.path_lower.empty()) continue;
            if (check_cancel()) break;

            const std::string stored = std::to_string(report.files_downloaded + 1) + "-" +
                                       sanitize_component(entry.name);
            const fs::path destination = files_dir / stored;
            const auto download = api.download(entry.path_lower, destination.string(),
                                               config.max_file_bytes);
            if (!download.ok) {
                add_error("download",
                          entry.name + ": " +
                              (download.error.empty() ? "status " + std::to_string(download.status)
                                                      : download.error));
                continue;
            }
            ++report.files_downloaded;
            report.bytes_downloaded += download.bytes;
            manifest["downloads"].push_back({{"name", "files/" + stored},
                                             {"source", "download"},
                                             {"id", entry.id},
                                             {"original_name", entry.name},
                                             {"path_lower", entry.path_lower},
                                             {"size_bytes", download.bytes},
                                             {"sha256", download.sha256},
                                             {"server_modified", entry.server_modified},
                                             {"content_hash", entry.content_hash}});
        }
        if (cancelled_flag) break;
        if (config.progress) {
            config.progress(report.pages, report.objects, report.files_downloaded);
        }

        cursor = page.cursor;
        if (!page.has_more) break;
        // Checkpoint after every page: a crash here costs one page, never the
        // whole listing.
        write_job("running");
        timeline.flush();
    }

    if (budget_exhausted && !cancelled_flag && !listing_failed) {
        add_error("list_folder", "page budget reached; resume with the same job id");
    }

    // A failure before anything was collected is a failure, not a partial run.
    // Calling it partial would suggest evidence was salvaged when there is none.
    if (listing_failed && report.pages == 0 && report.objects == 0 &&
        report.files_downloaded == 0) {
        report.partial = false;
    }

    // "failed" is its own state: a job that neither finished nor saved anything
    // must never be journalled as complete (it would then refuse to re-run) nor
    // as partial (it would look resumable when there is nothing to resume).
    const std::string final_status =
        cancelled_flag ? "cancelled"
                       : (report.partial ? "partial" : (report.error.empty() ? "complete" : "failed"));
    manifest["status"] = final_status;
    manifest["pages"] = report.pages;
    manifest["objects"] = report.objects;
    manifest["files_downloaded"] = report.files_downloaded;
    manifest["bytes_downloaded"] = report.bytes_downloaded;
    manifest["cursor_after"] = cursor;
    manifest["updated_at_utc"] = now_utc();
    timeline.flush();

    std::string manifest_error;
    if (!write_text_file(manifest_path, manifest.dump(2), manifest_error)) {
        report.error = manifest_error;
        report.partial = true;
    }
    write_job(final_status);

    // A budget stop or a cancellation is resumable, not a failure: the job
    // journal holds the cursor and the manifest marks the run partial.
    report.ok = report.error.empty();
    return report;
}

}  // namespace olho::cloud::dropbox
