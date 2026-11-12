// language: C++, file: .oracle/mbox_test.cpp
#include "social/mbox_reader.hpp"
#include "third_party/nlohmann/json.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}
static std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main() {
    // 1. Pecas isoladas.
    {
        check("base64 simples", olho::social::decode_base64("aGVsbG8gYmFzZTY0") == "hello base64");
        check("qp com acento", olho::social::decode_quoted_printable("Ol=C3=A1") == "Ol\u00e1");
        check("qp quebra suave", olho::social::decode_quoted_printable("a=\nb") == "ab");
        check("rfc2047 Q", olho::social::decode_rfc2047("=?UTF-8?Q?M=C3=A3e_com_acento?=") ==
                               "M\u00e3e com acento");
        check("rfc2047 B",
              olho::social::decode_rfc2047("=?UTF-8?B?TcOjZQ==?=") == "M\u00e3e");
        check("data com fuso negativo",
              olho::social::mail_date_to_utc("Thu, 14 Nov 2023 19:13:00 -0300") ==
                  "2023-11-14T22:13:00Z");
        check("data em UTC",
              olho::social::mail_date_to_utc("Fri, 15 Nov 2023 10:00:00 +0000") ==
                  "2023-11-15T10:00:00Z");
        check("data ilegivel devolve vazio", olho::social::mail_date_to_utc("ontem").empty());
    }

    // 2. mbox completo.
    std::vector<olho::social::mail_message> messages;
    olho::social::mbox_stats stats;
    std::string error;
    const std::string data = read_all(".oracle\\mail_fixture.mbox");
    check("parse do fixture", olho::social::parse_mbox(data, messages, stats, error));
    if (!error.empty()) std::printf("    erro: %s\n", error.c_str());
    check("tres mensagens", messages.size() == 3);
    check("estatistica de datas", stats.with_date == 3);
    check("um html puro", stats.html_only == 1);
    check("duas com corpo", stats.with_body == 2);
    if (messages.size() == 3) {
        check("assunto decodificado",
              messages[0].subject == "M\u00e3e com acento");
        check("corpo quoted-printable",
              messages[0].body == "Ol\u00e1, tudo bem?\nSegunda linha.");
        check("data convertida", messages[0].date_utc == "2023-11-14T22:13:00Z");
        check("remetente", messages[0].from.find("ana@example.com") != std::string::npos);
        check("message-id", messages[0].message_id == "<m1@example.com>");
        check("multipart escolhe text/plain", messages[1].body == "hello base64");
        check("segunda data", messages[1].date_utc == "2023-11-15T10:00:00Z");
        check("html puro marcado", messages[2].html_only && messages[2].body.empty());
        check("deslocamento cresce",
              messages[0].offset < messages[1].offset && messages[1].offset < messages[2].offset);
    }

    // 3. Saida para comparar com o oraculo do Python.
    nlohmann::json output = nlohmann::json::array();
    for (const auto& message : messages) {
        output.push_back({{"date_utc", message.date_utc},
                          {"subject", message.subject},
                          {"body", message.body},
                          {"html_only", message.html_only}});
    }
    std::ofstream out(".oracle\\mbox_ours.json", std::ios::binary);
    out << output.dump(1);
    out.close();

    std::printf(failures == 0 ? "MBOX OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
