// language: C++, file: social/dyi_import.hpp, target: Windows 11 (MSVC / MinGW g++)
// Importador do arquivo "Baixar suas informacoes" (Download Your Information,
// DYI) do Facebook e do Instagram.
//
// POR QUE ESTE CAMINHO
//   Nao existe API publica de mensagens pessoais para Facebook/Instagram: a Graph
//   API nao entrega caixa de entrada de terceiros e a API de mensagens do
//   Instagram so atende conta business com app aprovado. O export oficial
//   assinado pelo titular e o caminho que sobra, e ele e bom para pericia: os
//   arquivos vem do provedor, com carimbo de tempo por mensagem.
//
// O QUE ESTE MODULO FAZ
//   Le o .zip do export (ou a pasta ja extraida), encontra os message_N.json de
//   cada conversa, copia o JSON bruto como evidencia (byte a byte, com sha256) e
//   produz o mesmo par manifesto+timeline que os conectores de nuvem, para que a
//   importacao no banco do caso seja a mesma.
//
// ARMADILHA CONHECIDA DO FORMATO
//   O Facebook grava o UTF-8 como se fosse Latin-1: "Joao" com til aparece como
//   "JoÃ£o". O texto do evento e corrigido; o arquivo bruto copiado NAO e
//   tocado, para que o hash continue sendo o do original.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::social {

struct dyi_config {
    std::wstring source;        // .zip do export ou pasta ja extraida
    std::string case_dir;       // raiz do caso
    std::string account;        // titular (rotulo usado nos eventos)
    std::string legal_basis;    // base legal registrada
    std::string job_id;         // identificador do trabalho
    std::string provider;       // "facebook", "instagram" ou vazio para detectar
};

struct dyi_report {
    bool ok = false;
    bool partial = false;
    std::string provider;        // detectado ou informado
    int threads = 0;             // conversas encontradas
    int messages = 0;            // mensagens na timeline
    int media = 0;               // mensagens com midia
    int unsent = 0;              // mensagens apagadas pelo remetente
    int files = 0;               // arquivos de conversa copiados
    int media_files = 0;         // fotos/videos/audios copiados do export
    int media_missing = 0;       // midia referenciada que nao existe no pacote
    std::uint64_t bytes = 0;     // bytes de evidencia copiados
    std::string source_sha256;   // hash do .zip entregue pelo titular
    std::uint64_t source_bytes = 0;
    std::string output_dir;
    std::string manifest_path;
    std::string timeline_path;
    std::string error;
};

dyi_report import_export(const dyi_config& config);

// Corrige o mojibake Latin-1/UTF-8 do export. Exposto para teste direto.
std::string repair_mojibake(const std::string& value);

}  // namespace olho::social
