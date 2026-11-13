// language: C++, file: export_publish.cpp
#include "export_publish.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace olho::ui {

export_result publish_export(const QString& path, const QByteArray& bytes,
                             const QString& format, const QString& hash_key, qint64 rows,
                             const QJsonObject& extra) {
    export_result result;
    result.path = path;
    result.meta_path = path + QStringLiteral(".meta.json");
    if (path.isEmpty()) {
        result.error = QStringLiteral("caminho vazio");
        return result;
    }
    result.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    result.bytes = bytes.size();
    result.rows = rows;

    // QSaveFile grava num temporario e troca no commit. remove+rename deixaria uma
    // janela em que o arquivo anterior nao existe mais.
    QSaveFile payload(path);
    if (!payload.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("nao consegui escrever ") + path;
        return result;
    }
    if (payload.write(bytes) != bytes.size()) {
        payload.cancelWriting();
        result.error = QStringLiteral("escrita incompleta em ") + path;
        return result;
    }
    if (!payload.commit()) {
        result.error = QStringLiteral("nao consegui publicar ") + path;
        return result;
    }

    QJsonObject meta = extra;
    meta.insert(QStringLiteral("format"), format);
    meta.insert(QStringLiteral("generated_at_utc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    meta.insert(QStringLiteral("rows"), static_cast<double>(rows));
    meta.insert(QStringLiteral("bytes"), static_cast<double>(result.bytes));
    meta.insert(hash_key, result.sha256);
    const QByteArray meta_bytes = QJsonDocument(meta).toJson(QJsonDocument::Indented);
    QSaveFile meta_file(result.meta_path);
    if (!meta_file.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("arquivo escrito, mas o sidecar falhou: ") + result.meta_path;
        return result;
    }
    meta_file.write(meta_bytes);
    if (!meta_file.commit()) {
        result.error = QStringLiteral("sidecar nao pode ser publicado: ") + result.meta_path;
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace olho::ui
