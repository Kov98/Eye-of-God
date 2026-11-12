// language: C++, file: social/mbox_reader.hpp, target: Windows 11 (MSVC / MinGW g++)
// Leitor de mbox (o formato do Gmail no Takeout do Google).
//
// ESCOPO
//   Separa mensagens, decodifica cabecalhos (RFC 2047), converte a data para UTC
//   e extrai o corpo texto/plano. Corpo em HTML ou anexo binario e registrado
//   como tal, nao inventado como texto.
//
// O QUE NAO FAZ
//   Nao monta arvore MIME completa nem renderiza HTML: escolhe a primeira parte
//   text/plain, que e o que a timeline precisa. O que nao consegue decodificar
//   fica marcado (truncated) em vez de sair como texto corrompido.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace olho::social {

struct mail_message {
    std::size_t offset = 0;          // onde a mensagem comeca no arquivo
    std::string from;
    std::string to;
    std::string subject;
    std::string date_raw;            // como veio no cabecalho
    std::string date_utc;            // convertido; vazio quando ilegivel
    std::string message_id;
    std::string body;                // text/plain decodificado
    std::vector<std::string> attachments;
    bool html_only = false;          // so havia text/html
    bool truncated = false;          // corpo nao decodificado por completo
};

struct mbox_stats {
    int messages = 0;
    int with_date = 0;
    int with_body = 0;
    int html_only = 0;
    int undecodable = 0;
};

// Recebe o arquivo inteiro (o Gmail entrega um .mbox por rotulo).
bool parse_mbox(const std::string& data, std::vector<mail_message>& out, mbox_stats& stats,
                std::string& error);

// Pecas expostas para teste direto.
std::string decode_rfc2047(const std::string& value);
std::string decode_quoted_printable(const std::string& value);
std::string decode_base64(const std::string& value);
std::string mail_date_to_utc(const std::string& raw);

}  // namespace olho::social
