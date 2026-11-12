// language: C++, file: social/dyi_import.cpp
#include "social/dyi_import.hpp"

#include "android_backup.hpp"   // sha256_hex (unico SHA-256 do nucleo sem nuvem)
#include "social/takeout_import.hpp"
#include "social/telegram_import.hpp"
#include "social/zip_reader.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
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

std::string iso_from_ms(long long ms) {
    const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_s(&tm, &seconds);
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

// Verdadeiro quando a sequencia e UTF-8 bem formada. Usado para decidir se o
// mojibake pode ser desfeito sem inventar caractere.
bool is_valid_utf8(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= text.size() + 0) {
            if (i + extra > text.size() - 1) return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

// Um caminho de exportacao de conversa: qualquer .../messages/.../message_N.json
bool is_message_file(const std::string& name) {
    if (name.size() < 12) return false;
    if (name.find("/messages/") == std::string::npos &&
        name.rfind("messages/", 0) != 0) {
        return false;
    }
    if (name.find("message_") == std::string::npos) return false;
    return name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0;
}

std::string provider_label(const std::string& provider) {
    if (provider == "instagram") return "Instagram";
    return "Facebook";
}

}  // namespace

std::string repair_mojibake(const std::string& value) {
    // O provedor grava UTF-8 tratando cada byte como um code point Latin-1. Para
    // desfazer, o texto e lido como UTF-8: cada code point abaixo de 0x100 volta
    // a ser um byte e, se a sequencia resultante for UTF-8 valida, ela e o texto
    // original. Ler os bytes crus (sem decodificar) nao serve: "Ã" ja ocupa dois
    // bytes em UTF-8 e viraria dois caracteres errados.
    bool has_high = false;
    std::string bytes;
    bytes.reserve(value.size());
    std::size_t i = 0;
    while (i < value.size()) {
        const unsigned char lead = static_cast<unsigned char>(value[i]);
        std::uint32_t code = 0;
        std::size_t extra = 0;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code = lead & 0x07;
        } else {
            return value;
        }
        if (i + extra >= value.size()) return value;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(value[i + k]);
            if ((cc & 0xC0) != 0x80) return value;
            code = (code << 6) | (cc & 0x3F);
        }
        if (code >= 0x100) return value;   // ja tem code point fora de Latin-1
        if (code >= 0x80) has_high = true;
        bytes.push_back(static_cast<char>(code));
        i += extra + 1;
    }
    if (!has_high) return value;
    if (!is_valid_utf8(bytes)) return value;
    // Rejeita o caso em que nada mudaria de sentido: sequencia ja era UTF-8 de
    // um unico byte por caractere.
    return bytes;
}

dyi_report import_export(const dyi_config& config) {
    dyi_report report;
    if (config.case_dir.empty() || config.account.empty() || config.legal_basis.empty() ||
        config.job_id.empty()) {
        report.error = "case_dir, account, legal_basis e job_id sao obrigatorios";
        return report;
    }
    if (config.source.empty()) {
        report.error = "informe o .zip ou a pasta do export";
        return report;
    }

    // O Takeout do Google tem formato proprio (.mbox): o modo "Detectar" reconhece
    // e delega; escolher o provedor na mao tambem vale.
    if (config.provider == "google" ||
        (config.provider.empty() && looks_like_takeout(config.source))) {
        return import_takeout(config);
    }
    if (config.provider == "telegram" ||
        (config.provider.empty() && looks_like_telegram(config.source))) {
        return import_telegram(config);
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

    // Detecta o provedor pelas pistas que so um export de cada rede tem.
    std::string provider = config.provider;
    if (provider.empty()) {
        for (const auto& entry : archive.entries()) {
            if (entry.name.find("your_instagram_activity") != std::string::npos) {
                provider = "instagram";
                break;
            }
            if (entry.name.find("your_activity_across_facebook") != std::string::npos) {
                provider = "facebook";
                break;
            }
        }
        if (provider.empty()) provider = config.source.find(L"instagram") != std::wstring::npos
                                             ? "instagram" : "facebook";
    }
    if (provider != "facebook" && provider != "instagram") {
        report.error = "provedor nao suportado: " + provider;
        return report;
    }
    report.provider = provider;

    const fs::path output = fs::path(config.case_dir) / "social" / provider /
                            sanitize_component(config.account);
    const fs::path raw = output / "raw";
    std::error_code directory_error;
    fs::create_directories(raw, directory_error);
    if (directory_error) {
        report.error = "nao foi possivel criar a pasta de saida";
        return report;
    }
    report.output_dir = output.string();
    report.manifest_path = (output / "manifest.json").string();
    report.timeline_path = (output / "timeline.jsonl").string();

    // Uma pasta pertence a um trabalho. Sobrescrever a evidencia de outro job
    // seria destruir prova ja coletada, entao a divergencia de id aborta antes
    // de qualquer escrita.
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
    manifest["provider"] = provider;
    manifest["account"] = config.account;
    manifest["legal_basis"] = config.legal_basis;
    manifest["job_id"] = config.job_id;
    manifest["source"] = fs::path(config.source).filename().string();
    // O arquivo que o titular baixou e a raiz da cadeia: sem o hash dele, a
    // evidencia comecaria no meio do caminho.
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
            manifest["errors"].push_back({{"source", "arquivo de origem"},
                                          {"error", source_error}});
            report.partial = true;
        }
    } else {
        manifest["source_kind"] = "directory";
    }
    manifest["collected_at_utc"] = now_utc();
    manifest["files"] = nlohmann::json::array();
    manifest["errors"] = nlohmann::json::array();
    manifest["status"] = "running";

    std::ofstream timeline(output / "timeline.jsonl", std::ios::binary | std::ios::trunc);
    if (!timeline) {
        report.error = "nao foi possivel abrir a timeline";
        return report;
    }

    int file_index = 0;
    const std::string channel = provider_label(provider) + "/Messages";
    // Uma conversa pode chegar em varios message_N.json (paginacao do export).
    // Contar arquivos como conversas inflaria o numero; a chave e a pasta.
    std::set<std::string> threads_seen;

    // --- midia referenciada pelas mensagens -----------------------------------
    // O JSON aponta para o arquivo por "uri"; a foto/video vive no mesmo pacote.
    // Um indice por nome resolve as variacoes de prefixo do export.
    std::map<std::string, std::size_t> media_entries;   // zip: nome -> indice
    std::map<std::string, fs::path> media_paths;        // pasta: nome -> caminho
    if (is_directory) {
        std::error_code walk_error;
        fs::recursive_directory_iterator it(source, walk_error);
        const fs::recursive_directory_iterator end;
        for (; !walk_error && it != end; it.increment(walk_error)) {
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

    const fs::path media_dir = output / "media";
    std::map<std::string, std::string> media_copied;    // uri -> nome gravado
    int media_index = 0;

    // Copia a midia referenciada e devolve o nome gravado, ou vazio quando o
    // pacote nao traz o arquivo (o evento continua valendo, so sem o binario).
    const auto copy_media = [&](const std::string& uri) -> std::string {
        const auto already = media_copied.find(uri);
        if (already != media_copied.end()) return already->second;
        std::string key = uri;
        while (!key.empty() && (key.front() == '/' || key.front() == '\\')) key.erase(key.begin());
        for (char& c : key) if (c == '\\') c = '/';
        const std::string candidates[4] = {
            key,
            "your_activity_across_facebook/" + key,
            "your_instagram_activity/" + key,
            "content/" + key};
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
        media_copied.emplace(uri, stored);
        return stored;
    };

    const auto handle_thread = [&](const std::string& name, const std::string& text) {
        const auto document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded() || !document.is_object()) {
            manifest["errors"].push_back({{"source", name}, {"error", "JSON invalido"}});
            report.partial = true;
            return;
        }
        if (!document.contains("messages") || !document["messages"].is_array()) {
            manifest["errors"].push_back({{"source", name}, {"error", "sem lista de mensagens"}});
            report.partial = true;
            return;
        }

        const std::string stored = std::to_string(++file_index) + "-" +
                                   sanitize_component(fs::path(name).filename().string());
        std::string write_error;
        if (!write_bytes(raw / stored, text, write_error)) {
            manifest["errors"].push_back({{"source", name}, {"error", write_error}});
            report.partial = true;
            return;
        }
        manifest["files"].push_back({{"name", "raw/" + stored},
                                     {"size_bytes", text.size()},
                                     {"sha256", olho::android::sha256_hex(text.data(), text.size())},
                                     {"source", "messages"}});
        report.bytes += text.size();
        ++report.files;
        const std::size_t slash = name.rfind('/');
        const std::string thread_key = slash == std::string::npos ? name : name.substr(0, slash);
        if (threads_seen.insert(thread_key).second) ++report.threads;

        std::size_t index = 0;
        for (const auto& message : document["messages"]) {
            if (!message.is_object()) continue;
            ++index;
            const long long stamp = message.value("timestamp_ms", 0ll);
            const std::string sender = repair_mojibake(message.value("sender_name", ""));
            const std::string content = repair_mojibake(message.value("content", ""));
            const bool unsent = message.value("is_unsent", false);
            // Quais midias esta mensagem referencia, e o binario de cada uma.
            std::vector<std::string> media_uris;
            std::vector<std::string> media_stored;
            for (const char* key : {"photos", "videos", "audio_files", "gifs", "files"}) {
                if (!message.contains(key) || !message[key].is_array()) continue;
                for (const auto& item : message[key]) {
                    if (!item.is_object()) continue;
                    const std::string uri = item.value("uri", "");
                    if (uri.empty()) continue;
                    media_uris.push_back(uri);
                    const std::string stored = copy_media(uri);
                    if (!stored.empty()) media_stored.push_back(stored);
                }
            }
            const bool has_media = !media_uris.empty();
            int reactions = 0;
            if (message.contains("reactions") && message["reactions"].is_array()) {
                reactions = static_cast<int>(message["reactions"].size());
            }
            if (unsent) ++report.unsent;
            if (has_media) ++report.media;

            nlohmann::json event;
            event["timestamp_utc"] = iso_from_ms(stamp);
            event["channel"] = channel;
            event["event_id"] = name + "#" + std::to_string(index);
            event["provider"] = provider_label(provider);
            event["computer"] = config.account;
            event["record_id"] = fs::path(name).filename().string();
            event["event_user_sid"] = sender;
            event["event_data"] = {{"sender", sender},
                                   {"text", content},
                                   {"unsent", unsent},
                                   {"has_media", has_media},
                                   {"media_count", media_uris.size()},
                                   {"media", media_uris},
                                   {"media_files", media_stored},
                                   {"reactions", reactions}};
            timeline << event.dump() << "\n";
            ++report.messages;
        }
    };

    if (is_directory) {
        std::error_code walk_error;
        fs::recursive_directory_iterator it(source, walk_error);
        const fs::recursive_directory_iterator end;
        for (; !walk_error && it != end; it.increment(walk_error)) {
            std::error_code type_error;
            if (!it->is_regular_file(type_error) || type_error) continue;
            const std::string name = fs::relative(it->path(), source, type_error).generic_string();
            if (type_error || !is_message_file(name)) continue;
            handle_thread(name, read_file(it->path()));
        }
        if (walk_error) {
            report.error = "falha ao percorrer o export: " + walk_error.message();
            report.partial = true;
        }
    } else {
        for (const auto& entry : archive.entries()) {
            if (!is_message_file(entry.name)) continue;
            std::string text;
            std::string extract_error;
            if (!archive.extract(entry, text, extract_error)) {
                manifest["errors"].push_back({{"source", entry.name}, {"error", extract_error}});
                report.partial = true;
                continue;
            }
            handle_thread(entry.name, text);
        }
    }
    timeline.flush();

    if (report.threads == 0) {
        // Nada encontrado: nao se grava manifesto, para nao apagar (nem marcar
        // como concluida) a evidencia de uma coleta anterior.
        report.error = "nenhuma conversa encontrada no export";
        return report;
    }
    manifest["threads"] = report.threads;
    manifest["messages"] = report.messages;
    manifest["media_messages"] = report.media;
    manifest["media_files"] = report.media_files;
    manifest["media_missing"] = report.media_missing;
    manifest["unsent_messages"] = report.unsent;
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
