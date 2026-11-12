// language: C++, file: social/telegram_import.cpp
#include "social/telegram_import.hpp"

#include "android_backup.hpp"
#include "social/zip_reader.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

namespace olho::social {
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

bool write_bytes(const fs::path& path, const std::string& data, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "nao foi possivel escrever " + path.string();
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        error = "falha ao gravar " + path.string();
        return false;
    }
    return true;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string lower_copy(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool is_result_json(const std::string& name) {
    return lower_copy(fs::path(name).filename().string()) == "result.json";
}

// O texto vem como string ou como lista de pedacos: strings soltas e objetos com
// "text" (link, bold, mention...). Concatenar na ordem e o que reconstroi a
// mensagem como o aplicativo mostra.
std::string flatten_text(const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (!value.is_array()) return std::string();
    std::string out;
    for (const auto& piece : value) {
        if (piece.is_string()) {
            out += piece.get<std::string>();
        } else if (piece.is_object()) {
            if (piece.contains("text") && piece["text"].is_string()) {
                out += piece["text"].get<std::string>();
            } else if (piece.contains("href") && piece["href"].is_string()) {
                // Entidade sem texto: o proprio href e a informacao.
                out += piece["href"].get<std::string>();
            }
        }
    }
    return out;
}

}  // namespace

bool looks_like_telegram(const std::wstring& source) {
    const std::string narrow(source.begin(), source.end());
    if (lower_copy(narrow).find("telegram") != std::string::npos) return true;
    std::error_code error;
    const fs::path path(source);
    if (fs::is_directory(path, error)) {
        for (fs::recursive_directory_iterator it(path, error), end; !error && it != end;
             it.increment(error)) {
            std::error_code type_error;
            if (!it->is_regular_file(type_error) || type_error) continue;
            if (is_result_json(it->path().string())) return true;
        }
        return false;
    }
    zip_archive archive;
    std::string open_error;
    if (!archive.open(source, open_error)) return false;
    for (const auto& entry : archive.entries()) {
        if (is_result_json(entry.name)) return true;
    }
    return false;
}

dyi_report import_telegram(const dyi_config& config) {
    dyi_report report;
    report.provider = "telegram";
    if (config.case_dir.empty() || config.account.empty() || config.legal_basis.empty() ||
        config.job_id.empty()) {
        report.error = "case_dir, account, legal_basis e job_id sao obrigatorios";
        return report;
    }
    if (config.source.empty()) {
        report.error = "informe o .zip ou a pasta do export";
        return report;
    }

    const fs::path source(config.source);
    std::error_code exists_error;
    const bool is_directory = fs::is_directory(source, exists_error);
    const bool is_file = fs::is_regular_file(source, exists_error);
    if (!is_directory && !is_file) {
        report.error = "origem nao encontrada";
        return report;
    }

    zip_archive archive;
    if (!is_directory) {
        std::string open_error;
        if (!archive.open(config.source, open_error)) {
            report.error = "zip: " + open_error;
            return report;
        }
    }

    const fs::path output = fs::path(config.case_dir) / "social" / "telegram" /
                            sanitize_component(config.account);
    const fs::path raw = output / "raw";
    const fs::path media_dir = output / "media";
    std::error_code directory_error;
    fs::create_directories(raw, directory_error);
    if (directory_error) {
        report.error = "nao foi possivel criar a pasta de saida";
        return report;
    }
    report.output_dir = output.string();
    report.manifest_path = (output / "manifest.json").string();
    report.timeline_path = (output / "timeline.jsonl").string();

    {
        const std::string previous = read_file(output / "manifest.json");
        if (!previous.empty()) {
            const auto existing = nlohmann::json::parse(previous, nullptr, false);
            if (existing.is_object()) {
                const std::string existing_job = existing.value("job_id", "");
                if (!existing_job.empty() && existing_job != config.job_id) {
                    report.error = "a pasta do provedor ja pertence ao job " + existing_job;
                    return report;
                }
            }
        }
    }

    nlohmann::json manifest;
    manifest["provider"] = "telegram";
    manifest["account"] = config.account;
    manifest["legal_basis"] = config.legal_basis;
    manifest["job_id"] = config.job_id;
    manifest["source"] = fs::path(config.source).filename().string();
    manifest["collected_at_utc"] = now_utc();
    manifest["files"] = nlohmann::json::array();
    manifest["errors"] = nlohmann::json::array();
    manifest["status"] = "running";
    // O export do Telegram nao carrega fuso: registra a limitacao no manifesto.
    manifest["date_is_local"] = true;

    if (is_file) {
        std::string source_hex;
        std::string source_error;
        if (olho::android::sha256_file(config.source, source_hex, source_error)) {
            const std::uint64_t source_size =
                static_cast<std::uint64_t>(fs::file_size(source, directory_error));
            manifest["source_sha256"] = source_hex;
            manifest["source_bytes"] = source_size;
            report.source_sha256 = source_hex;
            report.source_bytes = source_size;
        } else {
            manifest["errors"].push_back({{"source", "arquivo de origem"}, {"error", source_error}});
            report.partial = true;
        }
    } else {
        manifest["source_kind"] = "directory";
    }

    std::ofstream timeline(output / "timeline.jsonl", std::ios::binary | std::ios::trunc);
    if (!timeline) {
        report.error = "nao foi possivel abrir a timeline";
        return report;
    }

    // Indice de midia: o export referencia por caminho relativo.
    std::map<std::string, std::size_t> media_entries;
    std::map<std::string, fs::path> media_paths;
    if (is_directory) {
        std::error_code walk_error;
        for (fs::recursive_directory_iterator it(source, walk_error), end; !walk_error && it != end;
             it.increment(walk_error)) {
            std::error_code type_error;
            if (!it->is_regular_file(type_error) || type_error) continue;
            media_paths.emplace(fs::relative(it->path(), source, type_error).generic_string(),
                                it->path());
        }
    } else {
        for (std::size_t i = 0; i < archive.entries().size(); ++i) {
            media_entries.emplace(archive.entries()[i].name, i);
        }
    }

    std::map<std::string, std::string> media_copied;
    int media_index = 0;
    // O Telegram referencia a midia relativa a pasta da conversa, nao a raiz do
    // export: o caminho do propri o result.json e o primeiro candidato.
    const auto copy_media = [&](const std::string& reference,
                                const std::string& base) -> std::string {
        if (reference.empty()) return std::string();
        const auto already = media_copied.find(reference);
        if (already != media_copied.end()) return already->second;
        std::string key = reference;
        while (!key.empty() && (key.front() == '/' || key.front() == '\\')) key.erase(key.begin());
        for (char& c : key) if (c == '\\') c = '/';
        std::vector<std::string> candidates;
        if (!base.empty()) candidates.push_back(base + "/" + key);
        candidates.push_back(key);
        std::string bytes;
        std::string resolved;
        for (const std::string& candidate : candidates) {
            const auto in_zip = media_entries.find(candidate);
            if (in_zip != media_entries.end()) {
                std::string extract_error;
                if (archive.extract(archive.entries()[in_zip->second], bytes, extract_error)) {
                    resolved = candidate;
                    break;
                }
                bytes.clear();
                continue;
            }
            const auto on_disk = media_paths.find(candidate);
            if (on_disk != media_paths.end()) {
                bytes = read_file(on_disk->second);
                if (!bytes.empty()) {
                    resolved = candidate;
                    break;
                }
            }
        }
        if (resolved.empty()) {
            ++report.media_missing;
            return std::string();
        }
        std::error_code create_error;
        fs::create_directories(media_dir, create_error);
        if (create_error) {
            report.partial = true;
            return std::string();
        }
        const std::string stored = std::to_string(++media_index) + "-" +
                                   sanitize_component(fs::path(resolved).filename().string());
        std::string write_error;
        if (!write_bytes(media_dir / stored, bytes, write_error)) {
            manifest["errors"].push_back({{"source", resolved}, {"error", write_error}});
            report.partial = true;
            return std::string();
        }
        manifest["files"].push_back({{"name", "media/" + stored},
                                     {"size_bytes", bytes.size()},
                                     {"sha256", olho::android::sha256_hex(bytes.data(), bytes.size())},
                                     {"source", "media"},
                                     {"origin", resolved}});
        report.bytes += bytes.size();
        ++report.media_files;
        media_copied.emplace(reference, stored);
        return stored;
    };

    int file_index = 0;
    const auto handle_chat = [&](const std::string& name, const std::string& text) {
        const auto document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded() || !document.is_object() ||
            !document.contains("messages") || !document["messages"].is_array()) {
            manifest["errors"].push_back({{"source", name}, {"error", "result.json invalido"}});
            report.partial = true;
            return;
        }
        const std::string stored = std::to_string(++file_index) + "-" +
                                   sanitize_component(fs::path(name).parent_path().filename().string()
                                                      + "-result.json");
        std::string write_error;
        if (!write_bytes(raw / stored, text, write_error)) {
            manifest["errors"].push_back({{"source", name}, {"error", write_error}});
            report.partial = true;
            return;
        }
        manifest["files"].push_back({{"name", "raw/" + stored},
                                     {"size_bytes", text.size()},
                                     {"sha256", olho::android::sha256_hex(text.data(), text.size())},
                                     {"source", "result.json"}});
        report.bytes += text.size();
        ++report.files;
        ++report.threads;

        const std::string chat_name = document.value("name", "");
        const std::string chat_type = document.value("type", "");
        for (const auto& message : document["messages"]) {
            if (!message.is_object()) continue;
            const std::string kind = message.value("type", "message");
            const std::string date = message.value("date", "");
            const std::string from = message.value("from", message.value("actor", ""));
            std::string body = flatten_text(message.contains("text") ? message["text"]
                                                                    : nlohmann::json());
            std::string media_reference;
            for (const char* key : {"photo", "file", "video_file", "audio_file", "voice_message",
                                    "sticker"}) {
                if (media_reference.empty() && message.contains(key) && message[key].is_string()) {
                    media_reference = message[key].get<std::string>();
                }
            }
            const std::string media_type = message.value("media_type", "");
            const std::string chat_dir = fs::path(name).parent_path().generic_string();
            const std::string stored_media = copy_media(media_reference, chat_dir);

            nlohmann::json event;
            // Sem fuso no arquivo: a data entra como veio, marcada como local.
            event["timestamp_utc"] = date;
            event["channel"] = "Telegram/Messages";
            event["event_id"] = name + "#" + std::to_string(message.value("id", 0));
            event["provider"] = "Telegram";
            event["computer"] = config.account;
            event["record_id"] = chat_name;
            event["event_user_sid"] = from;
            event["event_data"] = {{"chat", chat_name},
                                   {"chat_type", chat_type},
                                   {"message_type", kind},
                                   {"from", from},
                                   {"from_id", message.value("from_id", "")},
                                   {"text", body},
                                   {"media_type", media_type},
                                   {"media_uri", media_reference},
                                   {"media_files", stored_media.empty()
                                                       ? nlohmann::json::array()
                                                       : nlohmann::json::array({stored_media})},
                                   {"reply_to", message.value("reply_to_message_id", 0)},
                                   {"date_is_local", true}};
            timeline << event.dump() << "\n";
            ++report.messages;
            if (!media_reference.empty()) ++report.media;
        }
    };

    if (is_directory) {
        std::error_code walk_error;
        for (fs::recursive_directory_iterator it(source, walk_error), end; !walk_error && it != end;
             it.increment(walk_error)) {
            std::error_code type_error;
            if (!it->is_regular_file(type_error) || type_error) continue;
            if (!is_result_json(it->path().string())) continue;
            const std::string name = fs::relative(it->path(), source, type_error).generic_string();
            handle_chat(name, read_file(it->path()));
        }
        if (walk_error) {
            report.error = "falha ao percorrer o export: " + walk_error.message();
            report.partial = true;
        }
    } else {
        for (const auto& entry : archive.entries()) {
            if (!is_result_json(entry.name)) continue;
            std::string text;
            std::string extract_error;
            if (!archive.extract(entry, text, extract_error)) {
                manifest["errors"].push_back({{"source", entry.name}, {"error", extract_error}});
                report.partial = true;
                continue;
            }
            handle_chat(entry.name, text);
        }
    }
    timeline.flush();

    if (report.threads == 0) {
        report.error = "nenhum result.json encontrado no export";
        return report;
    }
    manifest["threads"] = report.threads;
    manifest["messages"] = report.messages;
    manifest["media_files"] = report.media_files;
    manifest["media_missing"] = report.media_missing;
    manifest["status"] = report.partial ? "partial" : "complete";
    manifest["updated_at_utc"] = now_utc();
    std::string manifest_error;
    if (!write_bytes(output / "manifest.json", manifest.dump(2), manifest_error)) {
        report.error = manifest_error;
        return report;
    }
    report.ok = report.error.empty();
    return report;
}

}  // namespace olho::social
