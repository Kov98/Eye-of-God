// language: C++, file: case_timeline_export.hpp, target: Windows 11 (MSVC / MinGW g++)
// Exportacao da timeline do caso para CSV/JSON.
//
// POR QUE FORA DO APP
//   O perito precisa conferir e citar em peca propria, sem abrir a interface. O
//   CSV existe para planilha e para o proprio laudo; o JSON para quem for
//   reprocessar. Ambos saem do banco do caso, nao da memoria da UI: o que foi
//   exportado e exatamente o que esta gravado.
//
// O QUE VAI EM CADA LINHA
//   Carimbo do evento (nao o da importacao), tipo do objeto, canal, provedor,
//   conta, autor, titulo, identificador externo, tamanho, sha256 e caminho do
//   artefato. E o conjunto minimo para rastrear a evidencia ate o arquivo.
#pragma once

#include <string>

namespace deep::forensic {

struct TimelineExportReport {
    bool ok = false;
    int rows = 0;
    std::string path;
    std::string error;
};

// format: "csv" (padrao) ou "json".
TimelineExportReport export_case_timeline(const std::string& db_path,
                                          const std::string& out_path,
                                          const std::string& format = "csv");

}  // namespace deep::forensic
