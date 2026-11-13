// language: C++, file: timeline_export.cpp
#include "timeline_export.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonObject>

#include "export_publish.hpp"

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

// A publicacao (QSaveFile + sidecar) mora em export_publish.cpp, compartilhada com
// o export da Galeria: uma implementacao so, um so lugar para corrigir.
timeline_export_result write_export(const QString& path, const QByteArray& bytes,
                                   const QString& format, const QString& hash_key,
                                   qint64 rows, const timeline_filter& filter,
                                   const QString& case_dir, const QString& payload_note) {
    QJsonObject extra;
    extra.insert(QStringLiteral("case_dir"), QDir(case_dir).absolutePath());
    extra.insert(QStringLiteral("filter"), filter_text(filter));
    extra.insert(QStringLiteral("scope"),
                 QStringLiteral("janela carregada da Timeline (nao o banco do caso)"));
    extra.insert(QStringLiteral("payload"), payload_note);
    const export_result published =
        publish_export(path, bytes, format, hash_key, rows, extra);
    timeline_export_result result;
    result.ok = published.ok;
    result.error = published.error;
    result.csv_path = published.path;   // o campo serve para os dois formatos
    result.meta_path = published.meta_path;
    result.rows = published.rows;
    result.bytes = published.bytes;
    result.sha256 = published.sha256;
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
