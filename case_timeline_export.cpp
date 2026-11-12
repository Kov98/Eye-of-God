// language: C++, file: case_timeline_export.cpp
#include "case_timeline_export.hpp"

#include <sqlite3.h>

#include <cstdio>
#include <string>
#include <vector>

namespace deep::forensic {
namespace {

struct row {
    std::string timestamp_utc;
    std::string object_type;
    std::string channel;
    std::string provider;
    std::string account;
    std::string title;
    std::string external_id;
    long long size_bytes = 0;
    std::string sha256;
    std::string storage_path;
};

// CSV: aspas quando o campo tem separador, aspas ou quebra de linha; aspas
// internas duplicadas. Sem isso um assunto de e-mail com virgula quebraria a
// planilha na coluna errada.
std::string csv_field(const std::string& value) {
    bool needs_quotes = false;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out += "\"";
    return out;
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8] = {};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

const char* kQuery =
    "SELECT COALESCE(o.modified_at_utc,''), COALESCE(o.object_type,''), COALESCE(o.path,''),"
    " COALESCE(j.provider,''), COALESCE(a.account_id,''), COALESCE(o.name,''),"
    " COALESCE(o.external_id,''), COALESCE(o.size_bytes,0), COALESCE(art.sha256,''),"
    " COALESCE(art.storage_path,'')"
    " FROM cloud_object o"
    " JOIN cloud_job j ON j.id = o.job_pk"
    " LEFT JOIN cloud_account a ON a.id = o.account_pk"
    " LEFT JOIN cloud_artifact art ON art.object_pk = o.id"
    // Evento sem carimbo (arquivo de evidencia) vai para o fim: no comeco ele
    // pareceria o primeiro fato da linha do tempo.
    " ORDER BY CASE WHEN COALESCE(o.modified_at_utc,'') = '' THEN 1 ELSE 0 END,"
    " o.modified_at_utc, o.external_id";

}  // namespace

TimelineExportReport export_case_timeline(const std::string& db_path,
                                          const std::string& out_path,
                                          const std::string& format) {
    TimelineExportReport report;
    report.path = out_path;
    if (db_path.empty() || out_path.empty()) {
        report.error = "informe o banco do caso e o arquivo de saida";
        return report;
    }
    const bool as_json = format == "json" || format == "JSON";

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        report.error = std::string("nao foi possivel abrir o banco: ") +
                       (db != nullptr ? sqlite3_errmsg(db) : "erro desconhecido");
        if (db != nullptr) sqlite3_close(db);
        return report;
    }

    std::vector<row> rows;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, kQuery, -1, &statement, nullptr) != SQLITE_OK) {
        report.error = std::string("consulta falhou: ") + sqlite3_errmsg(db);
        sqlite3_close(db);
        return report;
    }
    while (sqlite3_step(statement) == SQLITE_ROW) {
        row entry;
        const auto text = [&](int column) {
            const unsigned char* value = sqlite3_column_text(statement, column);
            return value == nullptr ? std::string()
                                    : std::string(reinterpret_cast<const char*>(value));
        };
        entry.timestamp_utc = text(0);
        entry.object_type = text(1);
        entry.channel = text(2);
        entry.provider = text(3);
        entry.account = text(4);
        entry.title = text(5);
        entry.external_id = text(6);
        entry.size_bytes = sqlite3_column_int64(statement, 7);
        entry.sha256 = text(8);
        entry.storage_path = text(9);
        rows.push_back(std::move(entry));
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);

    if (rows.empty()) {
        report.error = "nenhum evento no banco do caso";
        return report;
    }

    std::FILE* out = std::fopen(out_path.c_str(), "wb");
    if (out == nullptr) {
        report.error = "nao foi possivel escrever " + out_path;
        return report;
    }

    const auto write = [&](const std::string& text) {
        std::fwrite(text.data(), 1, text.size(), out);
    };

    if (as_json) {
        write("[\n");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const row& entry = rows[i];
            std::string line = " {\"timestamp_utc\":\"" + json_escape(entry.timestamp_utc) +
                               "\",\"object_type\":\"" + json_escape(entry.object_type) +
                               "\",\"channel\":\"" + json_escape(entry.channel) +
                               "\",\"provider\":\"" + json_escape(entry.provider) +
                               "\",\"account\":\"" + json_escape(entry.account) +
                               "\",\"title\":\"" + json_escape(entry.title) +
                               "\",\"external_id\":\"" + json_escape(entry.external_id) +
                               "\",\"size_bytes\":" + std::to_string(entry.size_bytes) +
                               ",\"sha256\":\"" + json_escape(entry.sha256) +
                               "\",\"storage_path\":\"" + json_escape(entry.storage_path) + "\"}";
            if (i + 1 < rows.size()) line += ",";
            write(line + "\n");
        }
        write("]\n");
    } else {
        write("timestamp_utc,object_type,channel,provider,account,title,external_id,size_bytes,"
              "sha256,storage_path\n");
        for (const row& entry : rows) {
            std::string line;
            line += csv_field(entry.timestamp_utc) + ",";
            line += csv_field(entry.object_type) + ",";
            line += csv_field(entry.channel) + ",";
            line += csv_field(entry.provider) + ",";
            line += csv_field(entry.account) + ",";
            line += csv_field(entry.title) + ",";
            line += csv_field(entry.external_id) + ",";
            line += std::to_string(entry.size_bytes) + ",";
            line += csv_field(entry.sha256) + ",";
            line += csv_field(entry.storage_path);
            write(line + "\n");
        }
    }
    std::fclose(out);
    report.rows = static_cast<int>(rows.size());
    report.ok = true;
    return report;
}

}  // namespace deep::forensic
