// language: C++, file: timeline_export.cpp
#include "timeline_export.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    if (filter.from.isValid()) parts << QStringLiteral("from=") + filter.from.toString(Qt::ISODate);
    if (filter.to.isValid()) parts << QStringLiteral("to=") + filter.to.toString(Qt::ISODate);
    if (filter.cloud_only) parts << QStringLiteral("cloud=1");
    return parts.join(QStringLiteral("; "));
}

}  // namespace

timeline_export_result export_timeline_csv(const QString& csv_path,
                                          const QVector<timeline_event>& events,
                                          const timeline_filter& filter,
                                          const QString& case_dir) {
    timeline_export_result result;
    result.csv_path = csv_path;
    result.meta_path = csv_path + QStringLiteral(".meta.json");
    if (csv_path.isEmpty()) {
        result.error = QStringLiteral("caminho vazio");
        return result;
    }

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
        append_field(out, QString::fromLatin1(
                              QCryptographicHash::hash(event.raw.toUtf8(),
                                                       QCryptographicHash::Sha256)
                                  .toHex()));
        out.append(',');
        append_field(out, event.raw);
        out.append("\r\n");
    }

    const QByteArray digest =
        QCryptographicHash::hash(out, QCryptographicHash::Sha256).toHex();
    result.sha256 = QString::fromLatin1(digest);
    result.bytes = out.size();
    result.rows = events.size();

    // Publica como os selos do caso: escreve no .tmp e troca depois. Um export
    // interrompido no meio nao pode deixar um CSV meio escrito com nome final.
    const QString temp_path = csv_path + QStringLiteral(".tmp");
    {
        QFile temp(temp_path);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error = QStringLiteral("nao consegui escrever ") + temp_path;
            return result;
        }
        if (temp.write(out) != out.size()) {
            temp.close();
            QFile::remove(temp_path);
            result.error = QStringLiteral("escrita incompleta em ") + temp_path;
            return result;
        }
        temp.close();
    }
    QFile::remove(csv_path);
    if (!QFile::rename(temp_path, csv_path)) {
        QFile::remove(temp_path);
        result.error = QStringLiteral("nao consegui publicar ") + csv_path;
        return result;
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("format"), QStringLiteral("olho-timeline-csv/1"));
    meta.insert(QStringLiteral("generated_at_utc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    meta.insert(QStringLiteral("case_dir"), QDir(case_dir).absolutePath());
    meta.insert(QStringLiteral("rows"), static_cast<double>(result.rows));
    meta.insert(QStringLiteral("bytes"), static_cast<double>(result.bytes));
    meta.insert(QStringLiteral("sha256_csv"), result.sha256);
    meta.insert(QStringLiteral("filter"), filter_text(filter));
    meta.insert(QStringLiteral("scope"),
                QStringLiteral("janela carregada da Timeline (nao o banco do caso)"));
    QJsonArray columns;
    for (const char* name : {"timestamp_utc", "when_local", "provider", "channel", "event_id",
                             "record_id", "from_cloud", "size_bytes", "sha256", "raw_json"}) {
        columns.append(QString::fromLatin1(name));
    }
    meta.insert(QStringLiteral("columns"), columns);
    const QByteArray meta_bytes = QJsonDocument(meta).toJson(QJsonDocument::Indented);
    QFile meta_file(result.meta_path);
    if (!meta_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("CSV escrito, mas o sidecar falhou: ") + result.meta_path;
        return result;
    }
    meta_file.write(meta_bytes);
    meta_file.close();

    result.ok = true;
    return result;
}

}  // namespace olho::ui
