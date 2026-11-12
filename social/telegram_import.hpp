// language: C++, file: social/telegram_import.hpp, target: Windows 11 (MSVC / MinGW g++)
// Importador do export do Telegram Desktop ("Export chat history" em JSON).
//
// ARMADILHA DO FORMATO, DECLARADA
//   O campo "date" do export NAO carrega fuso: e a hora local da maquina no
//   momento em que o export foi gerado. Copiar isso como UTC seria mentir. O
//   evento guarda a data como veio e marca date_is_local=true, para que quem le
//   a timeline saiba que aquele horario precisa de contexto (o fuso de quem
//   exportou), e nao de uma conversao que nao existe no arquivo.
//
// O QUE LE
//   result.json (um por conversa exportada), com texto em string ou em lista de
//   entidades, midia referenciada por caminho relativo, mensagens de servico e
//   resposta a mensagem anterior.
#pragma once

#include "social/dyi_import.hpp"

#include <string>

namespace olho::social {

bool looks_like_telegram(const std::wstring& source);

// Mesmo contrato de saida: social/telegram/<conta>/manifest.json + timeline.jsonl.
dyi_report import_telegram(const dyi_config& config);

}  // namespace olho::social
