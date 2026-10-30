#include "cloud_report.hpp"

#include "cloud/core/manifest_verify.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace deep::cloud {
namespace {

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
    gmtime_s(&value, &now);
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &value);
    return text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string html_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

int count_lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    int count = 0;
    std::string line;
    while (std::getline(input, line))
        if (!line.empty()) ++count;
    return count;
}

bool write_file(const std::filesystem::path& path, const std::string& text,
                std::string& error) {
    try {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot open report";
            return false;
        }
        output << text;
        output.close();
        if (!output) {
            error = "cannot write report";
            return false;
        }
        return true;
    } catch (const std::exception&) {
        error = "cannot write report";
        return false;
    }
}

std::string html_report(const nlohmann::json& report) {
    std::ostringstream html;
    const bool verified = report["verification"].value("ok", false);
    html << "<!doctype html><html><head><meta charset=\"utf-8\">"
         << "<title>Olho de Deus - Cloud report</title><style>"
         << "body{background:#121212;color:#c8c8c8;font-family:Segoe UI,Arial,sans-serif;"
         << "margin:32px;line-height:1.45}h1{color:#fff;font-size:24px;margin:0}"
         << "h2{color:#fff;font-size:15px;margin:28px 0 10px;"
         << "border-bottom:1px solid #2d2d2d;padding-bottom:6px}"
         << ".sub{color:#888;margin:4px 0 24px}.ok{color:#8bc34a;font-weight:600}"
         << ".bad{color:#ff8a65;font-weight:600}.card{background:#1a1a1a;"
         << "border:1px solid #2d2d2d;border-radius:8px;padding:14px 16px;margin:10px 0}"
         << "table{border-collapse:collapse;width:100%;background:#0f0f0f}"
         << "th{text-align:left;background:#1a1a1a;color:#d0d0d0;font-size:12px;"
         << "padding:8px;border-bottom:1px solid #2d2d2d}"
         << "td{padding:7px 8px;border-bottom:1px solid #222;font-size:13px}"
         << "code{color:#e8f1f8}</style></head><body>";
    html << "<h1>Olho de Deus / Cloud</h1>"
         << "<div class=\"sub\">Relatorio gerado em " << html_escape(report.value("generated_at_utc", ""))
         << " &middot; caso " << html_escape(report.value("case_id", "")) << "</div>";
    html << "<div class=\"card\">Verificacao: <span class=\""
         << (verified ? "ok\">OK" : "bad\">FALHA")
         << "</span> &middot; " << report["verification"].value("verified", 0)
         << "/" << report["verification"].value("files", 0)
         << " arquivos &middot; " << report.value("timeline_events", 0)
         << " eventos de timeline</div>";
    html << "<h2>Contas</h2><table><tr><th>Conta</th><th>Tenant</th><th>Job</th>"
         << "<th>Status</th><th>Paginas</th><th>Objetos</th><th>Arquivos</th></tr>";
    for (const auto& account : report["accounts"]) {
        html << "<tr><td>" << html_escape(account.value("account", ""))
             << "</td><td>" << html_escape(account.value("tenant_id", ""))
             << "</td><td>" << html_escape(account.value("job_id", ""))
             << "</td><td>" << html_escape(account.value("status", ""))
             << "</td><td>" << account.value("pages", 0)
             << "</td><td>" << account.value("objects", 0)
             << "</td><td>" << account.value("files", 0) << "</td></tr>";
    }
    html << "</table><h2>Jobs</h2><table><tr><th>Job</th><th>Conta</th>"
         << "<th>Status</th><th>Fonte</th><th>Paginas</th><th>Objetos</th></tr>";
    for (const auto& job : report["jobs"]) {
        html << "<tr><td>" << html_escape(job.value("job_id", ""))
             << "</td><td>" << html_escape(job.value("account", ""))
             << "</td><td>" << html_escape(job.value("status", ""))
             << "</td><td>" << html_escape(job.value("source", ""))
             << "</td><td>" << job.value("pages", 0)
             << "</td><td>" << job.value("objects", 0) << "</td></tr>";
    }
    html << "</table>";
    const auto errors = report["verification"].value("errors", nlohmann::json::array());
    if (!errors.empty()) {
        html << "<h2>Erros de verificacao</h2><div class=\"card bad\">";
        for (const auto& error : errors)
            html << "<div>" << html_escape(error.get<std::string>()) << "</div>";
        html << "</div>";
    }
    html << "<p class=\"sub\">Manifesto e hashes originais continuam sendo a fonte de verdade.</p>"
         << "</body></html>";
    return html.str();
}

} // namespace

CloudReportResult generate_cloud_report(const std::string& case_dir,
                                        const std::string& output_path,
                                        const std::string& format) {
    CloudReportResult result;
    if (case_dir.empty() || output_path.empty()) {
        result.error = "case_dir and output_path are required";
        return result;
    }
    const std::filesystem::path root(case_dir);
    std::string case_id = root.filename().string();
    const auto case_manifest = nlohmann::json::parse(read_text(root / "manifest.json"),
                                                      nullptr, false);
    if (case_manifest.is_object()) case_id = case_manifest.value("case_id", case_id);

    nlohmann::json report = {
        {"product", "Olho de Deus"},
        {"case_id", case_id},
        {"case_dir", case_dir},
        {"generated_at_utc", utc_now()},
        {"accounts", nlohmann::json::array()},
        {"jobs", nlohmann::json::array()},
        {"timeline_events", 0}
    };
    int timeline_events = 0;
    const std::filesystem::path accounts_root = root / "cloud" / "m365";
    std::error_code error;
    if (std::filesystem::exists(accounts_root, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(accounts_root, error)) {
            if (!entry.is_directory()) continue;
            const auto manifest_path = entry.path() / "manifest.json";
            if (!std::filesystem::exists(manifest_path)) continue;
            const auto manifest = nlohmann::json::parse(read_text(manifest_path),
                                                         nullptr, false);
            if (!manifest.is_object()) continue;
            const int events = count_lines(entry.path() / "timeline.jsonl");
            timeline_events += events;
            report["accounts"].push_back({
                {"account", manifest.value("account", "")},
                {"tenant_id", manifest.value("tenant_id", "")},
                {"job_id", manifest.value("job_id", "")},
                {"status", manifest.value("status", "unknown")},
                {"pages", manifest.value("pages", 0)},
                {"objects", manifest.value("objects", 0)},
                {"files", manifest.value("files", nlohmann::json::array()).size()},
                {"timeline_events", events},
                {"legal_basis", manifest.value("legal_basis", "")}
            });
        }
    }
    const std::filesystem::path jobs_root = root / "cloud" / "jobs";
    if (std::filesystem::exists(jobs_root, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(jobs_root, error)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            const auto job = nlohmann::json::parse(read_text(entry.path()), nullptr, false);
            if (!job.is_object()) continue;
            report["jobs"].push_back({
                {"job_id", job.value("job_id", "")},
                {"account", job.value("account", "")},
                {"status", job.value("status", "unknown")},
                {"source", job.value("source", "")},
                {"pages", job.value("pages", 0)},
                {"objects", job.value("objects", 0)},
                {"resumed", job.value("resumed", false)},
                {"updated_at_utc", job.value("updated_at_utc", "")}
            });
        }
    }
    report["timeline_events"] = timeline_events;

    const auto verification = verify_cloud_manifests(case_dir);
    report["verification"] = {
        {"ok", verification.ok},
        {"manifests", verification.manifests},
        {"files", verification.files},
        {"verified", verification.verified},
        {"objects", verification.objects},
        {"errors", verification.errors}
    };

    std::string chosen = lower(format);
    if (chosen.empty()) {
        chosen = lower(std::filesystem::path(output_path).extension().string()) == ".json"
            ? "json" : "html";
    }
    std::string content;
    if (chosen == "json") content = report.dump(2);
    else if (chosen == "html") content = html_report(report);
    else {
        result.error = "format must be html or json";
        return result;
    }
    std::string error_text;
    if (!write_file(output_path, content, error_text)) {
        result.error = error_text;
        return result;
    }
    result.ok = true;
    result.path = output_path;
    return result;
}

} // namespace deep::cloud
