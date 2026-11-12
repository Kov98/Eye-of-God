// language: C++, file: social/takeout_import.cpp
#include "social/takeout_import.hpp"

#include "android_backup.hpp"
#include "social/mbox_reader.hpp"
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

bool is_mbox_name(const std::string& name) {
    if (name.size() < 6) return false;
    return lower_copy(name).compare(name.size() - 5, 5, ".mbox") == 0;
}

bool ends_with(const std::string& value, const char* suffix) {
    const std::string lowered = lower_copy(value);
    const std::string tail = suffix;
    return lowered.size() >= tail.size() &&
           lowered.compare(lowered.size() - tail.size(), tail.size(), tail) == 0;
}

bool is_photo_name(const std::string& name) {
    static const char* extensions[] = {".jpg", ".jpeg", ".png", ".heic", ".heif",
                                       ".gif", ".webp", ".mp4", ".mov", ".3gp", ".webm"};
    for (const char* extension : extensions) {
        if (ends_with(name, extension)) return true;
    }
    return false;
}

// Drive e a pasta "Drive/" dentro do Takeout; o resto da arvore nao e Drive.
bool is_drive_name(const std::string& name) {
    if (ends_with(name, ".json")) return false;
    const std::string lowered = lower_copy(name);
    // A pasta pode aparecer na raiz do pacote ("Drive/x") ou aninhada
    // ("Takeout/Drive/x"): as duas formas contam.
    return lowered.rfind("drive/", 0) == 0 || lowered.find("/drive/") != std::string::npos;
}

bool is_contacts_name(const std::string& name) { return ends_with(name, ".vcf"); }

// O Takeout guarda o carimbo da foto como segundos desde a epoca, em texto.
std::string epoch_seconds_to_utc(const std::string& raw) {
    long long seconds = 0;
    bool any = false;
    for (char c : raw) {
        if (c < '0' || c > '9') break;
        seconds = seconds * 10 + (c - '0');
        any = true;
        if (seconds > 4102444800LL) return std::string();   // ano 2100: valor suspeito
    }
    if (!any) return std::string();
    const std::time_t stamp = static_cast<std::time_t>(seconds);
    std::tm tm{};
    if (gmtime_s(&tm, &stamp) != 0) return std::string();
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

struct contact_entry {
    std::string name;
    std::string email;
    std::string phone;
};

// vCard minimo: FN/N, EMAIL e TEL. O que nao estiver la fica vazio, em vez de
// ser inventado a partir de outra linha.
void parse_vcard(const std::string& text, std::vector<contact_entry>& out) {
    std::size_t pos = 0;
    contact_entry current;
    bool open = false;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string lowered = lower_copy(line);
        if (lowered.rfind("begin:vcard", 0) == 0) {
            current = contact_entry{};
            open = true;
        } else if (lowered.rfind("fn:", 0) == 0 || lowered.rfind("fn;", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) current.name = line.substr(colon + 1);
        } else if (lowered.rfind("email", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) current.email = line.substr(colon + 1);
        } else if (lowered.rfind("tel", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) current.phone = line.substr(colon + 1);
        } else if (lowered.rfind("end:vcard", 0) == 0 && open) {
            if (!current.name.empty() || !current.email.empty() || !current.phone.empty()) {
                out.push_back(current);
            }
            open = false;
        }
        if (end == text.size()) break;
        pos = end + 1;
    }
}

}  // namespace

bool looks_like_takeout(const std::wstring& source) {
    const std::string narrow(source.begin(), source.end());
    if (lower_copy(narrow).find("takeout") != std::string::npos) return true;
    std::error_code error;
    const fs::path path(source);
    if (fs::is_directory(path, error)) {
        for (fs::recursive_directory_iterator it(path, error), end; !error && it != end;
             it.increment(error)) {
            std::error_code type_error;
            if (!it->is_regular_file(type_error) || type_error) continue;
            if (is_mbox_name(it->path().string())) return true;
        }
        return false;
    }
    zip_archive archive;
    std::string open_error;
    if (!archive.open(source, open_error)) {
        // Nao abre como ZIP: nao ha como inspecionar. O modo diretorio continua
        // valendo, e quem chama decide o provedor pelo que conseguiu ler.
        return false;
    }
    for (const auto& entry : archive.entries()) {
        if (is_mbox_name(entry.name)) return true;
    }
    return false;
}

dyi_report import_takeout(const dyi_config& config) {
    dyi_report report;
    report.provider = "google";
    if (config.case_dir.empty() || config.account.empty() || config.legal_basis.empty() ||
        config.job_id.empty()) {
        report.error = "case_dir, account, legal_basis e job_id sao obrigatorios";
        return report;
    }
    if (config.source.empty()) {
        report.error = "informe o .zip ou a pasta do Takeout";
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

    const fs::path output = fs::path(config.case_dir) / "social" / "google" /
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

    // Uma pasta pertence a um trabalho: nao se sobrescreve evidencia de outro.
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
    manifest["provider"] = "google";
    manifest["account"] = config.account;
    manifest["legal_basis"] = config.legal_basis;
    manifest["job_id"] = config.job_id;
    manifest["source"] = fs::path(config.source).filename().string();
    manifest["collected_at_utc"] = now_utc();
    manifest["files"] = nlohmann::json::array();
    manifest["errors"] = nlohmann::json::array();
    manifest["status"] = "running";

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

    std::ofstream timeline(output / "timeline.jsonl", std::ios::binary | std::ios::trunc);
    if (!timeline) {
        report.error = "nao foi possivel abrir a timeline";
        return report;
    }

    int file_index = 0;
    int extra_index = 0;
    int drive_files = 0;
    int contacts_found = 0;
    const auto handle_mbox = [&](const std::string& name, const std::string& bytes) {
        const std::string stored = std::to_string(++file_index) + "-" +
                                   sanitize_component(fs::path(name).filename().string());
        std::string write_error;
        if (!write_bytes(raw / stored, bytes, write_error)) {
            manifest["errors"].push_back({{"source", name}, {"error", write_error}});
            report.partial = true;
            return;
        }
        manifest["files"].push_back({{"name", "raw/" + stored},
                                     {"size_bytes", bytes.size()},
                                     {"sha256", olho::android::sha256_hex(bytes.data(), bytes.size())},
                                     {"source", "mbox"}});
        report.bytes += bytes.size();
        ++report.files;
        ++report.threads;

        std::vector<mail_message> messages;
        mbox_stats stats;
        std::string mbox_error;
        if (!parse_mbox(bytes, messages, stats, mbox_error)) {
            manifest["errors"].push_back({{"source", name}, {"error", mbox_error}});
            report.partial = true;
            return;
        }
        for (const auto& message : messages) {
            nlohmann::json event;
            event["timestamp_utc"] = message.date_utc.empty() ? now_utc() : message.date_utc;
            event["channel"] = "Google/Mail";
            event["event_id"] = name + "@" + std::to_string(message.offset);
            event["provider"] = "Google";
            event["computer"] = config.account;
            event["record_id"] = fs::path(name).filename().string();
            event["event_user_sid"] = message.from;
            event["event_data"] = {{"from", message.from},
                                   {"to", message.to},
                                   {"subject", message.subject},
                                   {"body", message.body},
                                   {"message_id", message.message_id},
                                   {"attachments", message.attachments},
                                   {"html_only", message.html_only},
                                   {"date_raw", message.date_raw}};
            timeline << event.dump() << "\n";
            ++report.messages;
            if (!message.attachments.empty()) {
                ++report.media;
                report.media_files += static_cast<int>(message.attachments.size());
            }
        }
    };

    // Uma lista de nomes + um leitor unico servem para os dois modos (pasta e zip),
    // em vez de duplicar cada varredura.
    std::vector<std::string> names;
    std::map<std::string, std::size_t> zip_index;
    std::error_code walk_error;
    if (is_directory) {
        for (fs::recursive_directory_iterator it(source, walk_error), end; !walk_error && it != end;
             it.increment(walk_error)) {
            std::error_code type_error;
            if (!it->is_regular_file(type_error) || type_error) continue;
            names.push_back(fs::relative(it->path(), source, type_error).generic_string());
        }
        if (walk_error) {
            report.error = "falha ao percorrer o Takeout: " + walk_error.message();
            report.partial = true;
        }
    } else {
        for (std::size_t i = 0; i < archive.entries().size(); ++i) {
            names.push_back(archive.entries()[i].name);
            zip_index.emplace(archive.entries()[i].name, i);
        }
    }
    std::sort(names.begin(), names.end());

    const auto read_named = [&](const std::string& name, std::string& bytes) -> bool {
        bytes.clear();
        if (is_directory) {
            bytes = read_file(source / fs::path(name));
            return !bytes.empty();
        }
        const auto found = zip_index.find(name);
        if (found == zip_index.end()) return false;
        std::string error;
        return archive.extract(archive.entries()[found->second], bytes, error);
    };

    for (const std::string& name : names) {
        if (!is_mbox_name(name)) continue;
        std::string bytes;
        if (!read_named(name, bytes)) {
            manifest["errors"].push_back({{"source", name}, {"error", "nao foi possivel ler"}});
            report.partial = true;
            continue;
        }
        handle_mbox(name, bytes);
    }

    const auto copy_into = [&](const std::string& folder, const std::string& name,
                               const std::string& bytes, const char* kind) {
        std::error_code create_error;
        fs::create_directories(output / folder, create_error);
        if (create_error) {
            report.partial = true;
            return std::string();
        }
        const std::string stored = std::to_string(++extra_index) + "-" +
                                   sanitize_component(fs::path(name).filename().string());
        std::string write_error;
        if (!write_bytes(output / folder / stored, bytes, write_error)) {
            manifest["errors"].push_back({{"source", name}, {"error", write_error}});
            report.partial = true;
            return std::string();
        }
        manifest["files"].push_back({{"name", folder + "/" + stored},
                                     {"size_bytes", bytes.size()},
                                     {"sha256", olho::android::sha256_hex(bytes.data(), bytes.size())},
                                     {"source", kind},
                                     {"origin", name}});
        report.bytes += bytes.size();
        ++report.files;
        return stored;
    };

    // --- Google Fotos: o carimbo vem no sidecar .json ao lado da imagem ------
    for (const std::string& name : names) {
        if (!is_photo_name(name)) continue;
        std::string bytes;
        if (!read_named(name, bytes)) continue;
        std::string sidecar;
        std::string taken;
        if (read_named(name + ".json", sidecar)) {
            const auto document = nlohmann::json::parse(sidecar, nullptr, false);
            if (document.is_object() && document.contains("photoTakenTime") &&
                document["photoTakenTime"].is_object()) {
                const auto& stamp = document["photoTakenTime"];
                if (stamp.contains("timestamp") && stamp["timestamp"].is_string()) {
                    taken = epoch_seconds_to_utc(stamp["timestamp"].get<std::string>());
                }
            }
        }
        const std::string stored = copy_into("media", name, bytes, "photo");
        if (stored.empty()) continue;
        ++report.media_files;
        nlohmann::json event;
        event["timestamp_utc"] = taken;          // vazio quando o sidecar nao existe
        event["channel"] = "Google/Photos";
        event["event_id"] = name;
        event["provider"] = "Google";
        event["computer"] = config.account;
        event["record_id"] = fs::path(name).filename().string();
        event["event_user_sid"] = config.account;
        event["event_data"] = {{"file", "media/" + stored},
                               {"origin", name},
                               {"photo_taken_utc", taken},
                               {"timestamp_source", taken.empty() ? "none" : "sidecar"}};
        timeline << event.dump() << "\n";
        ++report.messages;
    }

    // --- Drive: arquivo e evidencia, nao evento (o Takeout nao traz hora) ----
    for (const std::string& name : names) {
        if (!is_drive_name(name)) continue;
        std::string bytes;
        if (!read_named(name, bytes)) continue;
        if (copy_into("drive", name, bytes, "drive").empty()) continue;
        ++drive_files;
    }

    // --- Contatos: vCard, um evento por contato ----------------------------
    for (const std::string& name : names) {
        if (!is_contacts_name(name)) continue;
        std::string bytes;
        if (!read_named(name, bytes)) continue;
        if (copy_into("raw", name, bytes, "vcard").empty()) continue;
        std::vector<contact_entry> contacts;
        parse_vcard(bytes, contacts);
        for (const auto& contact : contacts) {
            nlohmann::json event;
            event["timestamp_utc"] = "";          // vCard nao carrega data
            event["channel"] = "Google/Contacts";
            event["event_id"] = name + "#" + contact.name;
            event["provider"] = "Google";
            event["computer"] = config.account;
            event["record_id"] = fs::path(name).filename().string();
            event["event_user_sid"] = contact.name;
            event["event_data"] = {{"name", contact.name},
                                   {"email", contact.email},
                                   {"phone", contact.phone},
                                   {"timestamp_source", "none"}};
            timeline << event.dump() << "\n";
            ++contacts_found;
        }
    }
    timeline.flush();

    if (report.threads == 0 && extra_index == 0 && contacts_found == 0) {
        report.error = "nada reconhecido no Takeout (mbox, fotos, drive ou contatos)";
        return report;
    }
    manifest["threads"] = report.threads;
    manifest["messages"] = report.messages;
    manifest["attachments"] = report.media_files;
    manifest["photo_files"] = report.media_files;
    manifest["drive_files"] = drive_files;
    manifest["contacts"] = contacts_found;
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
