// language: C++, file: social/takeout_import.hpp, target: Windows 11 (MSVC / MinGW g++)
// Importador do Google Takeout, com foco no que interessa a pericia: o Gmail.
//
// POR QUE MBOX
//   O Takeout entrega o Gmail como .mbox (RFC 4155), nao como JSON. Cada rotulo
//   vira um arquivo, e o mesmo e-mail pode aparecer em mais de um rotulo - o
//   importador guarda o arquivo como veio e registra a origem de cada mensagem,
//   em vez de deduplicar por conta propria e apagar a diferenca.
//
// ESCOPO
//   .mbox do Gmail (texto/plano, multipart/alternative, anexo declarado). Drive,
//   Fotos e Contatos do Takeout ficam para os modulos deles: sao formatos
//   diferentes e misturar tudo aqui esconderia o que nao foi lido.
#pragma once

#include "social/dyi_import.hpp"

#include <string>

namespace olho::social {

// Verdadeiro quando a origem parece Takeout: .mbox dentro do pacote ou pasta
// "Takeout" no caminho. Usado para o modo "Detectar" da GUI.
bool looks_like_takeout(const std::wstring& source);

// Mesmo contrato de saida do importador DYI: social/google/<conta>/manifest.json
// + timeline.jsonl, com o .mbox bruto copiado e hasheado.
dyi_report import_takeout(const dyi_config& config);

}  // namespace olho::social
