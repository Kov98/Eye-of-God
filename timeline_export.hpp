// language: C++, file: timeline_export.hpp, target: Windows 11, Qt 6
// Export do que esta NA TELA da Timeline (lista ja filtrada) para CSV, com um
// .meta.json ao lado dizendo de onde saiu.
//
// POR QUE NAO REUSA O EXPORT DO BANCO
//   deep::forensic::export_case_timeline() le o banco do caso e exporta o caso
//   inteiro. A barra da Timeline filtra uma JANELA carregada de timeline.jsonl
//   (provedor, canal, periodo, nuvem). Sao coisas diferentes, e um export que
//   dissesse "caso inteiro" teria que ignorar o filtro que o operador acabou de
//   aplicar. Este escreve exatamente o que a lista mostra - e o sidecar registra
//   o filtro e o sha256 para ninguem confundir os dois.
#pragma once

#include "timeline_index.hpp"

#include <QByteArray>
#include <QString>

namespace olho::ui {

struct timeline_export_result {
    bool ok = false;
    QString error;        // preenchido quando ok == false
    QString csv_path;
    QString meta_path;
    qint64 rows = 0;
    qint64 bytes = 0;
    QString sha256;       // do arquivo CSV inteiro, hex minusculo
};

// CSV: BOM UTF-8 (Excel), CRLF, todo campo entre aspas.
// Colunas: timestamp_utc,when_local,provider,channel,event_id,record_id,from_cloud,
//          size_bytes,sha256,raw_json
// A coluna sha256 e a do CONTEUDO da linha crua - e o mesmo hash que o painel
// Detalhes mostra para o evento, entao a planilha e a tela podem ser conferidas
// uma contra a outra.
timeline_export_result export_timeline_csv(const QString& csv_path,
                                           const QVector<timeline_event>& events,
                                           const timeline_filter& filter,
                                           const QString& case_dir);

// JSONL: a linha ORIGINAL de cada evento, uma por linha, sem envelope e sem BOM.
// Serve para reprocessar sem passar por planilha - e por ser a linha crua, da para
// conferir contra a fonte linha a linha (o CSV ja reinterpreta os campos).
timeline_export_result export_timeline_jsonl(const QString& jsonl_path,
                                             const QVector<timeline_event>& events,
                                             const timeline_filter& filter,
                                             const QString& case_dir);

}  // namespace olho::ui
