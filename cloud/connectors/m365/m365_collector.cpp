#include "m365_collector.hpp"

#include "cloud/core/cloud_core.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace deep::cloud {
namespace {

std::string now_utc() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &now);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string sanitize_component(std::string_view value) {
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.')
            result.push_back(static_cast<char>(c));
        else
            result.push_back('_');
    }
    if (result.empty()) result = "unknown";
    return result;
}

std::string path_encode(std::string_view value) {
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '@') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[(c >> 4) & 0xf]);
            result.push_back(hex[c & 0xf]);
        }
    }
    return result;
}

std::string query_encode(std::string_view value) {
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[(c >> 4) & 0xf]);
            result.push_back(hex[c & 0xf]);
        }
    }
    return result;
}

bool write_text_file(const std::filesystem::path& path, std::string_view content,
                     std::string& error) {
    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error) {
        error = "could not create output directory";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "could not open " + path.filename().string();
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        error = "could not write " + path.filename().string();
        return false;
    }
    return true;
}

bool base64_decode(std::string_view input, std::string& output) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int buffer = 0;
    int bits = 0;
    output.clear();
    for (unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const auto position = alphabet.find(static_cast<char>(c));
        if (position == std::string::npos) return false;
        buffer = (buffer << 6) | static_cast<int>(position);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 0xff));
        }
    }
    return !output.empty();
}

bool attachment_url_allowed(std::string_view url) {
    // Use the strict parser: it rejects userinfo ("https://host:443@evil")
    // instead of reading everything up to ':' as the host.
    HttpUrlParts parts;
    if (!parse_http_url(url, parts)) return false;
    if (parts.scheme != "https") return false;
    if (!parts.port.empty() && parts.port != "443") return false;
    const std::string& host = parts.host;
    const auto ends_with = [&](const char* suffix) {
        const std::string value(suffix);
        return host.size() > value.size() &&
            host.compare(host.size() - value.size(), value.size(), value) == 0;
    };
    return host == "graph.microsoft.com" ||
           host == "outlook.office365.com" ||
           ends_with(".sharepoint.com") ||
           ends_with(".office.com") ||
           ends_with(".office.net") ||
           ends_with(".microsoft.com");
}
std::string safe_file_name(std::string value) {
    value = sanitize_component(value);
    if (value.size() > 96) value.resize(96);
    return value;
}
std::string read_time(const nlohmann::json& value, const char* key) {
    if (value.contains(key) && value[key].is_string()) return value[key].get<std::string>();
    return {};
}

// Graph reports attachment sizes as JSON numbers; large attachments can exceed
// the range of a 32-bit int, so parse through a 64-bit unsigned value.
std::uint64_t declared_attachment_size(const nlohmann::json& attachment) {
    if (!attachment.contains("size")) return 0;
    const nlohmann::json& node = attachment["size"];
    if (node.is_number_unsigned()) return node.get<std::uint64_t>();
    if (node.is_number_integer()) {
        const long long value = node.get<long long>();
        return value > 0 ? static_cast<std::uint64_t>(value) : 0;
    }
    return 0;
}

} // namespace

bool read_text_file(const std::filesystem::path& path, std::string& content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    content.assign((std::istreambuf_iterator<char>(input)),
                   std::istreambuf_iterator<char>());
    return true;
}
M365CollectReport collect_m365(GraphClient& graph, const M365CollectConfig& config) {
    M365CollectReport report;
    if (config.case_dir.empty() || config.account.empty() || config.legal_basis.empty() ||
        config.job_id.empty()) {
        report.error = "case_dir, account, legal_basis and job_id are required";
        return report;
    }
    if (config.cancel_check && config.cancel_check()) {
        report.partial = true;
        report.cancelled = true;
        report.error = "collection cancelled";
        return report;
    }

    const std::filesystem::path output = std::filesystem::path(config.case_dir) /
        "cloud" / "m365" / sanitize_component(config.account);
    const std::filesystem::path raw = output / "raw";
    const std::filesystem::path jobs_dir =
        std::filesystem::path(config.case_dir) / "cloud" / "jobs";
    std::error_code directory_error;
    std::filesystem::create_directories(raw, directory_error);
    if (directory_error) {
        report.error = "could not create cloud output directory";
        return report;
    }
    report.output_dir = output.string();
    report.job_path = (jobs_dir / (sanitize_component(config.job_id) + ".json")).string();
    const std::filesystem::path manifest_path = output / "manifest.json";
    const std::filesystem::path timeline_path = output / "timeline.jsonl";

    const std::string case_id = std::filesystem::path(config.case_dir).filename().string();
    bool resume = false;
    nlohmann::json previous_job;
    nlohmann::json manifest;
    std::string previous_text;
    if (read_text_file(report.job_path, previous_text)) {
        try {
            previous_job = nlohmann::json::parse(previous_text);
        } catch (const std::exception&) {
            previous_job = nlohmann::json::object();
        }
        if (previous_job.is_object() &&
            previous_job.value("status", "") == "complete") {
            report.error = "job is already complete; use a new job id to collect again";
            return report;
        }
        if (previous_job.is_object() &&
            previous_job.value("status", "") != "failed") {
            if (previous_job.value("account", config.account) != config.account ||
                previous_job.value("tenant_id", config.tenant_id) != config.tenant_id ||
                previous_job.value("legal_basis", config.legal_basis) != config.legal_basis) {
                report.error = "job id already belongs to another account/tenant/legal basis";
                return report;
            }
            resume = true;
            report.resumed = true;
        }
    }
    if (resume && read_text_file(manifest_path, previous_text)) {
        try {
            const nlohmann::json previous_manifest = nlohmann::json::parse(previous_text);
            if (previous_manifest.is_object()) manifest = previous_manifest;
        } catch (const std::exception&) {
            // A damaged manifest is rebuilt; the job journal remains authoritative.
        }
    }
    if (!manifest.is_object()) manifest = nlohmann::json::object();
    manifest["provider"] = "m365";
    manifest["tenant_id"] = config.tenant_id;
    manifest["account"] = config.account;
    manifest["legal_basis"] = config.legal_basis;
    manifest["job_id"] = config.job_id;
    manifest["max_pages"] = config.max_pages;
    if (!manifest.contains("collected_at_utc"))
        manifest["collected_at_utc"] = now_utc();
    if (!manifest["files"].is_array()) manifest["files"] = nlohmann::json::array();
    if (!manifest["errors"].is_array()) manifest["errors"] = nlohmann::json::array();
    manifest["job_status_file"] = "cloud/jobs/" +
        sanitize_component(config.job_id) + ".json";
    manifest["status"] = "running";

    report.objects = manifest.value("objects", 0);
    report.pages = manifest.value("pages", 0);
    int file_index = static_cast<int>(manifest["files"].size());

    nlohmann::json sources = previous_job.is_object() ?
        previous_job.value("sources", nlohmann::json::object()) : nlohmann::json::object();
    if (!sources.is_object()) sources = nlohmann::json::object();
    for (const char* name : {"profile", "messages", "drive", "attachments"}) {
        if (!sources.contains(name)) sources[name] = {{"status", "pending"}};
    }

    std::string current_source;
    std::string current_next;
    int current_page = 0;
    const std::string started_at = previous_job.is_object() ?
        previous_job.value("started_at_utc", now_utc()) : now_utc();

    const auto write_job = [&](const std::string& status) {
        nlohmann::json job = {
            {"job_id", config.job_id},
            {"vault_key", config.vault_key},
            {"case_id", case_id},
            {"provider", "m365"},
            {"tenant_id", config.tenant_id},
            {"account", config.account},
            {"legal_basis", config.legal_basis},
            {"status", status},
            {"source", current_source},
            {"checkpoint_page", current_page},
            {"next_path", current_next},
            {"pages", report.pages},
            {"objects", report.objects},
            {"resumed", report.resumed},
            {"started_at_utc", started_at},
            {"updated_at_utc", now_utc()},
            {"sources", sources},
            {"errors", manifest["errors"]}
        };
        std::string job_error;
        write_text_file(report.job_path, job.dump(2), job_error);
    };
    write_job("running");

    std::ofstream timeline(timeline_path,
        std::ios::binary | (resume ? std::ios::app : std::ios::trunc));
    if (!timeline) {
        report.error = "could not open cloud timeline";
        return report;
    }

    const auto add_error = [&](const std::string& source, const std::string& error) {
        report.partial = true;
        sources[source]["status"] = "partial";
        sources[source]["error"] = error;
        manifest["errors"].push_back({{"source", source}, {"error", error}});
        if (report.error.empty()) report.error = source + ": " + error;
    };

    // Cancellation is polled at least once per Graph page and once per
    // attachment. Once observed it latches: the flow skips the remaining work
    // and checkpoints the job as partial so a later lease can resume it.
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

    const auto write_page = [&](const std::string& source, std::string_view body,
                                std::string& error) {
        const std::string name = source + "-" + std::to_string(++file_index) + ".json";
        const std::filesystem::path path = raw / name;
        if (!write_text_file(path, body, error)) return false;
        manifest["files"].push_back({
            {"name", "raw/" + name},
            {"size_bytes", body.size()},
            {"sha256", sha256_hex(body)},
            {"source", source}
        });
        return true;
    };

    const auto add_message_events = [&](const nlohmann::json& document) {
        if (!document.contains("value") || !document["value"].is_array()) return;
        for (const auto& message : document["value"]) {
            if (!message.is_object()) continue;
            const std::string timestamp = read_time(message, "receivedDateTime");
            nlohmann::json event = {
                {"timestamp_utc", timestamp.empty() ? now_utc() : timestamp},
                {"channel", "M365/Mail"},
                {"event_id", message.value("id", "")},
                {"provider", "Microsoft 365"},
                {"computer", config.tenant_id},
                {"record_id", message.value("id", "")},
                {"event_user_sid", config.account},
                {"event_data", {
                    {"subject", message.value("subject", "")},
                    {"hasAttachments", message.value("hasAttachments", false)},
                    {"webLink", message.value("webLink", "")}
                }}
            };
            timeline << event.dump() << "\n";
            ++report.objects;
        }
    };

    const auto add_drive_events = [&](const nlohmann::json& document) {
        if (!document.contains("value") || !document["value"].is_array()) return;
        for (const auto& item : document["value"]) {
            if (!item.is_object()) continue;
            const std::string timestamp = read_time(item, "lastModifiedDateTime");
            nlohmann::json event = {
                {"timestamp_utc", timestamp.empty() ? now_utc() : timestamp},
                {"channel", "M365/Drive"},
                {"event_id", item.value("id", "")},
                {"provider", "Microsoft 365"},
                {"computer", config.tenant_id},
                {"record_id", item.value("id", "")},
                {"event_user_sid", config.account},
                {"event_data", {
                    {"name", item.value("name", "")},
                    {"webUrl", item.value("webUrl", "")},
                    {"size", item.value("size", 0)}
                }}
            };
            timeline << event.dump() << "\n";
            ++report.objects;
        }
    };

    const auto source_complete = [&](const std::string& source) {
        return sources[source].value("status", "") == "complete";
    };
    const auto source_next = [&](const std::string& source) {
        return sources[source].value("next_path", "");
    };

    const auto collect_source = [&](const std::string& source, const std::string& path,
                                    bool messages) {
        if (source_complete(source)) return;
        if (check_cancel()) {
            add_error(source, "collection cancelled");
            write_job("running");
            return;
        }
        current_source = source;
        current_next = source_next(source);
        current_page = sources[source].value("pages", 0);
        int pages = 0;
        int source_pages = current_page;
        bool has_more = false;
        std::string error;
        const std::string start = current_next.empty() ? path : current_next;
        const bool ok = graph.collect(start, config.max_pages,
            [&](std::string_view body, std::string& page_error) {
                if (check_cancel()) {
                    page_error = "collection cancelled";
                    return false;
                }
                if (!write_page(source, body, page_error)) return false;
                try {
                    const nlohmann::json document = nlohmann::json::parse(body);
                    std::string next_link;
                    const auto link = document.find("@odata.nextLink");
                    if (link != document.end() && link->is_string())
                        next_link = link->get<std::string>();
                    // A page is only checkpointed when its nextLink stays on the
                    // configured Graph host; otherwise the source goes partial
                    // without persisting an unsafe URL.
                    if (!next_link.empty() && !graph.is_trusted_url(next_link)) {
                        page_error = "unsafe @odata.nextLink from " + source;
                        return false;
                    }
                    if (messages) add_message_events(document);
                    else add_drive_events(document);
                    current_next = next_link;
                } catch (const std::exception&) {
                    page_error = "invalid JSON page from " + source;
                    return false;
                }
                ++source_pages;
                current_page = source_pages;
                sources[source]["status"] = "running";
                sources[source]["next_path"] = current_next;
                sources[source]["pages"] = source_pages;
                write_job("running");
                return true;
            }, error, pages, has_more);
        report.pages += pages;
        current_page = source_pages;
        sources[source]["pages"] = source_pages;
        if (ok) {
            if (has_more) {
                // The page budget ran out while Graph still advertised pages.
                // Keep the checkpoint so a later run can resume.
                sources[source]["status"] = "partial";
                sources[source]["next_path"] = current_next;
                report.partial = true;
            } else {
                sources[source]["status"] = "complete";
                sources[source]["next_path"] = "";
                current_next.clear();
            }
            write_job("running");
        } else {
            sources[source]["status"] = "partial";
            if (!current_next.empty() && !graph.is_trusted_url(current_next))
                current_next.clear();
            sources[source]["next_path"] = current_next;
            add_error(source, error);
            write_job("running");
        }
    };

    const std::string user_path = "/users/" + path_encode(config.account);

    const auto download_attachments = [&]() {
        if (!config.download_attachments) return;
        current_source = "attachments";
        current_page = 0;
        current_next.clear();
        if (!source_complete("messages")) {
            // Attachments are derived from the message pages. If the message
            // listing is partial, messages on unfetched pages would silently
            // lose their attachments, so never mark this source complete.
            sources["attachments"]["status"] = "partial";
            sources["attachments"]["next_path"] = "";
            report.partial = true;
            write_job("running");
            return;
        }
        if (source_complete("attachments")) return;
        if (check_cancel()) {
            sources["attachments"]["status"] = "partial";
            sources["attachments"]["error"] = "collection cancelled";
            write_job("running");
            return;
        }
        write_job("running");
        std::vector<std::string> message_ids;
        for (const auto& file : manifest["files"]) {
            if (!file.is_object() || file.value("source", "") != "messages") continue;
            const std::string name = file.value("name", "");
            if (name.empty()) continue;
            std::string body;
            if (!read_text_file(output / name, body)) continue;
            try {
                const auto page = nlohmann::json::parse(body);
                if (!page.contains("value") || !page["value"].is_array()) continue;
                for (const auto& message : page["value"]) {
                    if (message.is_object() && message.value("hasAttachments", false)) {
                        const std::string id = message.value("id", "");
                        if (!id.empty()) message_ids.push_back(id);
                    }
                }
            } catch (const std::exception&) {
                // A malformed page is surfaced by the messages source.
            }
        }
        std::vector<std::string> existing;
        for (const auto& file : manifest["files"]) {
            if (file.is_object() && file.value("source", "") == "attachment") {
                const std::string id = file.value("attachment_id", "");
                if (!id.empty()) existing.push_back(id);
            }
        }
        // Streaming downloads are bounded by a configurable per-attachment
        // limit instead of the old fixed 50 MB ceiling. Zero selects the
        // default and anything above the hard maximum is clamped.
        const std::uint64_t requested_limit = config.max_attachment_bytes == 0
            ? kM365DefaultMaxAttachmentBytes
            : config.max_attachment_bytes;
        const std::uint64_t attachment_limit =
            std::max<std::uint64_t>(1, std::min(requested_limit,
                kM365HardMaxAttachmentBytes));

        const auto record_attachment = [&](const std::string& relative,
                                           const std::string& sha,
                                           std::uint64_t size_bytes,
                                           std::uint64_t event_size,
                                           const std::string& message_id,
                                           const std::string& attachment_id,
                                           const std::string& attachment_name,
                                           const std::string& content_type) {
            manifest["files"].push_back({
                {"name", relative},
                {"size_bytes", size_bytes},
                {"sha256", sha},
                {"source", "attachment"},
                {"message_id", message_id},
                {"attachment_id", attachment_id},
                {"content_type", content_type},
                {"original_name", attachment_name}
            });
            const nlohmann::json event = {
                {"timestamp_utc", now_utc()},
                {"channel", "M365/Attachment"},
                {"event_id", attachment_id},
                {"provider", "Microsoft 365"},
                {"computer", config.tenant_id},
                {"record_id", attachment_id},
                {"event_user_sid", config.account},
                {"event_data", {
                    {"name", attachment_name},
                    {"size", event_size},
                    {"path", relative},
                    {"message_id", message_id},
                    {"sha256", sha}
                }}
            };
            timeline << event.dump() << "\n";
            existing.push_back(attachment_id);
            ++report.objects;
            ++current_page;
            sources["attachments"]["pages"] = current_page;
            write_job("running");
        };

        bool failed = false;
        for (const auto& message_id : message_ids) {
            if (check_cancel()) break;
            const std::string path = user_path + "/messages/" + path_encode(message_id) +
                "/attachments";
            const HttpResponse response = graph.get(path);
            if (!response.ok()) {
                add_error("attachments", "Graph HTTP " +
                    std::to_string(response.status) + " for message");
                failed = true;
                continue;
            }
            nlohmann::json attachments;
            try {
                attachments = nlohmann::json::parse(response.body);
            } catch (const std::exception&) {
                add_error("attachments", "invalid attachment list");
                failed = true;
                continue;
            }
            if (!attachments.contains("value") || !attachments["value"].is_array()) continue;
            for (const auto& attachment : attachments["value"]) {
                if (check_cancel()) break;
                if (!attachment.is_object()) continue;
                const std::string attachment_id = attachment.value("id", "");
                const std::string attachment_name = attachment.value("name", attachment_id);
                if (attachment_id.empty() ||
                    std::find(existing.begin(), existing.end(), attachment_id) != existing.end())
                    continue;
                const std::string content_type = attachment.value("contentType", "");
                const std::uint64_t declared_size = declared_attachment_size(attachment);
                if (declared_size > attachment_limit) {
                    add_error("attachments", "attachment exceeds configured size limit: " +
                        safe_file_name(attachment_name));
                    failed = true;
                    continue;
                }
                const std::string relative = "attachments/" +
                    safe_file_name(message_id) + "/" +
                    safe_file_name(attachment_id) + "_" + safe_file_name(attachment_name);
                const std::filesystem::path target = output / relative;
                const std::string encoded = attachment.value("contentBytes", "");
                if (!encoded.empty()) {
                    std::string content;
                    if (!base64_decode(encoded, content)) {
                        add_error("attachments", "invalid inline attachment content: " +
                            safe_file_name(attachment_name));
                        failed = true;
                        continue;
                    }
                    if (static_cast<std::uint64_t>(content.size()) > attachment_limit) {
                        add_error("attachments", "attachment exceeds configured size limit: " +
                            safe_file_name(attachment_name));
                        failed = true;
                        continue;
                    }
                    std::string write_error;
                    if (!write_text_file(target, content, write_error)) {
                        add_error("attachments", write_error);
                        failed = true;
                        continue;
                    }
                    const std::uint64_t content_size =
                        static_cast<std::uint64_t>(content.size());
                    record_attachment(relative, sha256_hex(content), content_size,
                        declared_size > 0 ? declared_size : content_size,
                        message_id, attachment_id, attachment_name, content_type);
                    continue;
                }
                // No inline contentBytes: Graph serves the payload through the
                // streaming `$value` endpoint, which may redirect to a
                // pre-signed download URL on an allowlisted host. The transport
                // writes to a ".part" file and only publishes it on success, so
                // a failed stream leaves no artifact and no manifest entry.
                const std::string value_path = path + "/" +
                    path_encode(attachment_id) + "/$value";
                HttpDownloadResult value = graph.download(value_path,
                    target.string(), attachment_limit);
                if (value.redirected && !value.location.empty()) {
                    if (!attachment_url_allowed(value.location)) {
                        add_error("attachments", "attachment redirect host not allowed");
                        failed = true;
                        continue;
                    }
                    // Redirect targets never receive the Graph bearer token.
                    value = graph.download_absolute(value.location, false,
                        target.string(), attachment_limit);
                }
                if (!value.ok) {
                    if (value.limit_exceeded) {
                        add_error("attachments",
                            "attachment exceeds configured size limit: " +
                            safe_file_name(attachment_name));
                    } else if (value.transport_error) {
                        add_error("attachments", value.error.empty()
                            ? "attachment download transport error" : value.error);
                    } else if (!value.error.empty()) {
                        // Local sink failures keep their precise reason instead
                        // of being reported as "HTTP 0".
                        add_error("attachments", "attachment download " + value.error);
                    } else {
                        add_error("attachments", "attachment download HTTP " +
                            std::to_string(value.status));
                    }
                    failed = true;
                    continue;
                }
                if (value.bytes == 0) {
                    std::error_code remove_error;
                    std::filesystem::remove(target, remove_error);
                    add_error("attachments", "attachment payload is empty: " +
                        safe_file_name(attachment_name));
                    failed = true;
                    continue;
                }
                record_attachment(relative, value.sha256, value.bytes,
                    declared_size > 0 ? declared_size : value.bytes,
                    message_id, attachment_id, attachment_name, content_type);
            }
            if (report.cancelled) break;
        }
        sources["attachments"]["status"] =
            (failed || report.cancelled) ? "partial" : "complete";
        sources["attachments"]["pages"] = current_page;
        if (failed) report.partial = true;
        write_job("running");
    };


    if (!source_complete("profile") && !check_cancel()) {
        current_source = "profile";
        current_page = sources["profile"].value("pages", 0);
        current_next.clear();
        const HttpResponse profile = graph.get(user_path);
        if (profile.ok()) {
            std::string error;
            if (write_page("profile", profile.body, error)) {
                manifest["profile"] = nlohmann::json::parse(profile.body, nullptr, false);
                sources["profile"]["status"] = "complete";
                sources["profile"]["pages"] = 1;
                current_page = 1;
                write_job("running");
            } else {
                add_error("profile", error);
                write_job("running");
            }
        } else {
            add_error("profile", "Graph HTTP " + std::to_string(profile.status));
            write_job("running");
        }
    }

    std::string messages_path = user_path + "/messages?$top=50";
    if (!config.since.empty() || !config.until.empty()) {
        std::string filter;
        if (!config.since.empty()) filter = "receivedDateTime ge " + config.since;
        if (!config.until.empty()) {
            if (!filter.empty()) filter += " and ";
            filter += "receivedDateTime le " + config.until;
        }
        messages_path += "&$filter=" + query_encode(filter);
    }
    collect_source("messages", messages_path, true);
    download_attachments();
    collect_source("drive", user_path + "/drive/root/children?$top=200", false);

    timeline.close();
    for (const char* name : {"profile", "messages", "drive"}) {
        if (sources[name].value("status", "") != "complete") report.partial = true;
    }
    if (config.download_attachments &&
        sources["attachments"].value("status", "") != "complete") report.partial = true;
    if (report.cancelled && report.error.empty())
        report.error = "collection cancelled";
    write_job(report.partial ? "partial" : "complete");
    manifest["status"] = report.partial ? "partial" : "complete";
    manifest["objects"] = report.objects;
    manifest["pages"] = report.pages;
    manifest["completed_at_utc"] = now_utc();
    manifest["sources"] = sources;
    report.manifest_path = manifest_path.string();
    std::string manifest_error;
    if (!write_text_file(manifest_path, manifest.dump(2), manifest_error)) {
        report.partial = true;
        if (report.error.empty()) report.error = manifest_error;
    }
    report.ok = !report.partial;
    return report;
}
} // namespace deep::cloud
