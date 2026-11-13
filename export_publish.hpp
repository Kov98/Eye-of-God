// language: C++, file: export_publish.hpp, target: Windows 11, Qt 6
// Publicacao comum dos exports do produto (Timeline, Galeria).
//
// POR QUE UM MODULO SO
//   Todo export tem a mesma exigencia: arquivo publicado de forma atomica, sha256
//   do conteudo e um sidecar .meta.json dizendo de onde saiu. Duas copias disso
//   divergem na primeira correcao - e o defeito seria silencioso, porque o arquivo
//   continua parecendo valido.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace olho::ui {

struct export_result {
    bool ok = false;
    QString error;
    QString path;
    QString meta_path;
    qint64 rows = 0;
    qint64 bytes = 0;
    QString sha256;   // do arquivo publicado, hex minusculo
};

// Grava bytes em <path> via QSaveFile (temporario + commit atomico: um export
// interrompido nao deixa arquivo pela metade com o nome final) e escreve
// <path>.meta.json com format, generated_at_utc, rows, bytes, <hash_key>, e os
// campos de extra (filter, scope, payload...).
export_result publish_export(const QString& path, const QByteArray& bytes,
                             const QString& format, const QString& hash_key, qint64 rows,
                             const QJsonObject& extra);

}  // namespace olho::ui
