// language: C++, file: timeline_export.cpp
#include "timeline_export.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace olho::ui {
namespace {

// RFC 4180: sempre entre aspas, aspas dobradas. Sempre entre aspas porque campo
// vazio, virgula e quebra de linha aparecem no raw_json e adivinhar quando cotar
// e como planilha quebrada chega no laudo.
void append_field(QByteArray& out, const QString& value) {
    QByteArray bytes = value.toUtf8();
    bytes.replace('"', "\"\"");
    out.append('"');
    out.append(bytes);
    out.append('"');
}

QString bool_text(bool value) { return value ? QStringLiteral("1") : QStringLiteral("0"); }

QString filter_text(const timeline_filter& filter) {
    if (filter.empty()) return QStringLiteral("nenhum");
    QStringList parts;
    if (!filter.provider.isEmpty()) parts << QStringLiteral("provider=") + filter.provider;
    if (!filter.channel.isEmpty()) parts << QStringLiteral("channel=") + filter.channel;
    if (filter.from.isValid()) {
        parts << QStringLiteral("from=") + filter.from.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    if (filter.to.isValid()) {
        parts << QStringLiteral("to=") + filter.to.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    if (filter.cloud_only) parts << QStringLiteral("cloud=1");
    return parts.join(QStringLiteral("; "));
}

QString sha256_hex(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray build_csv(const QVector<timeline_event>& events) {
    QByteArray out;
    out.append("\xEF\xBB\xBF", 3);   // BOM: sem ele o Excel abre em ANSI e come acentos
    out.append("\"timestamp_utc\",\"when_local\",\"provider\",\"channel\",\"event_id\","
               "\"record_id\",\"from_cloud\",\"size_bytes\",\"sha256\",\"raw_json\"\r\n");
    const QString when_header = QStringLiteral("yyyy-MM-dd HH:mm:ss");
    for (const timeline_event& event : events) {
        append_field(out, event.timestamp_text);
        out.append(',');
        append_field(out, event.when.isValid() ? event.when.toString(when_header) : QString());
        out.append(',');
        append_field(out, event.provider);
        out.append(',');
        append_field(out, event.channel);
        out.append(',');
        append_field(out, event.event_id);
        out.append(',');
        append_field(out, event.record_id);
        out.append(',');
        append_field(out, bool_text(event.from_cloud));
        out.append(',');
        append_field(out, QString::number(event.raw.toUtf8().size()));
        out.append(',');
        append_field(out, sha256_hex(event.raw.toUtf8()));
        out.append(',');
        append_field(out, event.raw);
        out.append("\r\n");
    }
    return out;
}

// JSONL: a linha ORIGINAL, sem envelope e sem BOM. Quem reprocessa recebe
// exatamente o que o produtor gravou - re-serializar aqui seria reinterpretar a
// evidencia e abrir espaco para divergencia com a fonte.
QByteArray build_jsonl(const QVector<timeline_event>& events) {
    QByteArray out;
    for (const timeline_event& event : events) {
        out.append(event.raw.toUtf8());
        out.append('\n');
    }
    return out;
}

// Publicacao comum: .tmp + rename (export interrompido nao deixa arquivo meio
// escrito com nome final) e sidecar de procedencia ao lado.
timeline_export_result write_export(const QString& path, const QByteArray& bytes,
                                   const QString& format, const QString& hash_key,
                                   qint64 rows, const timeline_filter& filter,
                                   const QString& case_dir, const QString& payload_note) {
    timeline_export_result result;
    result.csv_path = path;   // o campo serve para os dois formatos
    result.meta_path = path + QStringLiteral(".meta.json");
    if (path.isEmpty()) {
        result.error = QStringLiteral("caminho vazio");
        return result;
    }
    result.sha256 = sha256_hex(bytes);
    result.bytes = bytes.size();
    result.rows = rows;

    const QString temp_path = path + QStringLiteral(".tmp");
    {
        QFile temp(temp_path);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error = QStringLiteral("nao consegui escrever ") + temp_path;
            return result;
        }
        if (temp.write(bytes) != bytes.size()) {
            temp.close();
            QFile::remove(temp_path);
            result.error = QStringLiteral("escrita incompleta em ") + temp_path;
            return result;
        }
        temp.close();
    }
    QFile::remove(path);
    if (!QFile::rename(temp_path, path)) {
        QFile::remove(temp_path);
        result.error = QStringLiteral("nao consegui publicar ") + path;
        return result;
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("format"), format);
    meta.insert(QStringLiteral("generated_at_utc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    meta.insert(QStringLiteral("case_dir"), QDir(case_dir).absolutePath());
    meta.insert(QStringLiteral("rows"), static_cast<double>(result.rows));
    meta.insert(QStringLiteral("bytes"), static_cast<double>(result.bytes));
    meta.insert(hash_key, result.sha256);
    meta.insert(QStringLiteral("filter"), filter_text(filter));
    meta.insert(QStringLiteral("scope"),
                QStringLiteral("janela carregada da Timeline (nao o banco do caso)"));
    meta.insert(QStringLiteral("payload"), payload_note);
    const QByteArray meta_bytes = QJsonDocument(meta).toJson(QJsonDocument::Indented);
    QFile meta_file(result.meta_path);
    if (!meta_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("arquivo escrito, mas o sidecar falhou: ") + result.meta_path;
        return result;
    }
    meta_file.write(meta_bytes);
    meta_file.close();

    result.ok = true;
    return result;
}

}  // namespace

timeline_export_result export_timeline_csv(const QString& csv_path,
                                          const QVector<timeline_event>& events,
                                          const timeline_filter& filter,
                                          const QString& case_dir) {
    return write_export(csv_path, build_csv(events), QStringLiteral("olho-timeline-csv/1"),
                        QStringLiteral("sha256_csv"), events.size(), filter, case_dir,
                        QStringLiteral("colunas: timestamp_utc, when_local, provider, channel, "
                                       "event_id, record_id, from_cloud, size_bytes, sha256, "
                                       "raw_json"));
}

timeline_export_result export_timeline_jsonl(const QString& jsonl_path,
                                            const QVector<timeline_event>& events,
                                            const timeline_filter& filter,
                                            const QString& case_dir) {
    return write_export(jsonl_path, build_jsonl(events), QStringLiteral("olho-timeline-jsonl/1"),
                        QStringLiteral("sha256_jsonl"), events.size(), filter, case_dir,
                        QStringLiteral("uma linha JSON ORIGINAL por evento, sem envelope e sem "
                                       "BOM; fim de linha normalizado para \\n"));
}

}  // namespace olho::ui
