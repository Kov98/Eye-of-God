// language: C++, file: .oracle/dropbox_test.cpp
#include "dropbox_client.hpp"

#include <cstdio>
#include <string>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}

int main() {
    // 1. URL de autorizacao
    {
        const std::string url = olho::cloud::dropbox::authorize_url(
            "app123", "http://127.0.0.1:8080/cb", "CHALLENGE", "STATE-9");
        check("host de autorizacao", url.find("https://www.dropbox.com/oauth2/authorize") == 0);
        check("pede refresh token", url.find("token_access_type=offline") != std::string::npos);
        check("PKCE S256", url.find("code_challenge_method=S256") != std::string::npos);
        check("state presente", url.find("state=STATE-9") != std::string::npos);
        check("redirect escapado", url.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A8080%2Fcb") != std::string::npos);
    }

    // 2. token
    {
        olho::cloud::dropbox::token_set t;
        std::string err;
        check("token ok", olho::cloud::dropbox::parse_token(
            "{\"access_token\":\"sl.ABC\",\"refresh_token\":\"rt.XYZ\",\"token_type\":\"bearer\","
            "\"expires_in\":14400,\"scope\":\"files.content.read\",\"account_id\":\"dbid:1\"}", t, err));
        check("access token", t.access_token == "sl.ABC");
        check("refresh token", t.refresh_token == "rt.XYZ");
        check("expira em", t.expires_in == 14400);

        olho::cloud::dropbox::token_set t2;
        check("erro recusado", !olho::cloud::dropbox::parse_token(
            "{\"error\":\"invalid_grant\",\"error_description\":\"code expired\"}", t2, err));
        check("motivo legivel", err.find("invalid_grant") != std::string::npos && err.find("code expired") != std::string::npos);
        check("json invalido recusado", !olho::cloud::dropbox::parse_token("nao e json", t2, err));
    }

    // 3. listagem no formato real do Dropbox
    {
        const std::string json = R"({
          "entries": [
            {".tag":"file","name":"contrato.pdf","path_lower":"/caso/contrato.pdf",
             "id":"id:abc","size":54321,"server_modified":"2026-10-01T12:00:00Z",
             "content_hash":"a1b2c3"},
            {".tag":"folder","name":"anexos","path_lower":"/caso/anexos","id":"id:def"},
            {".tag":"file","name":"foto.jpg","path_lower":"/caso/foto.jpg","id":"id:ghi",
             "size":1234,"server_modified":"2026-10-02T08:30:00Z"}
          ],
          "cursor":"CURSOR-42",
          "has_more":true
        })";
        olho::cloud::dropbox::listing l;
        std::string err;
        check("listagem ok", olho::cloud::dropbox::parse_listing(json, l, err));
        check("tres entradas", l.entries.size() == 3);
        check("has_more", l.has_more);
        check("cursor", l.cursor == "CURSOR-42");
        if (l.entries.size() == 3) {
            check("arquivo com tamanho", l.entries[0].tag == "file" && l.entries[0].size == 54321);
            check("pasta sem tamanho", l.entries[1].tag == "folder" && l.entries[1].size == 0);
            check("caminho canonico", l.entries[2].path_lower == "/caso/foto.jpg");
            check("content_hash", l.entries[0].content_hash == "a1b2c3");
        }
        // Pasta vazia e resultado legitimo: precisa continuar passando.
        olho::cloud::dropbox::listing empty;
        check("pasta vazia aceita",
              olho::cloud::dropbox::parse_listing("{\"entries\":[],\"cursor\":\"C1\",\"has_more\":false}", empty, err)
              && empty.entries.empty() && empty.cursor == "C1");

        olho::cloud::dropbox::listing bad;
        check("listagem com erro recusada",
              !olho::cloud::dropbox::parse_listing("{\"error_summary\":\"path/not_found\"}", bad, err));
        check("documento de erro completo recusado",
              !olho::cloud::dropbox::parse_listing(
                  "{\"error_summary\":\"path/not_found/..\",\"error\":{\".tag\":\"path\",\"path\":{\".tag\":\"not_found\"}}}",
                  bad, err));
        check("resposta sem entries recusada",
              !olho::cloud::dropbox::parse_listing("{\"has_more\":false}", bad, err));
    }

    // 4. corpo do pedido
    {
        const std::string first = olho::cloud::dropbox::list_folder_body("/caso", "");
        check("primeira pagina sem cursor", first.find("cursor") == std::string::npos &&
                                            first.find("recursive") != std::string::npos);
        const std::string next = olho::cloud::dropbox::list_folder_body("", "CURSOR-42");
        check("proxima pagina usa cursor", next.find("CURSOR-42") != std::string::npos &&
                                           next.find("recursive") == std::string::npos);
    }

    // 5. saneamento de caminho: e o que impede um caminho hostil de escapar
    {
        std::string out, err;
        check("raiz vira vazio", olho::cloud::dropbox::sanitize_path("/", out, err) && out.empty());
        check("normaliza barras", olho::cloud::dropbox::sanitize_path("\\caso\\sub", out, err) && out == "/caso/sub");
        check("adiciona barra inicial", olho::cloud::dropbox::sanitize_path("caso", out, err) && out == "/caso");
        check("remove barra final", olho::cloud::dropbox::sanitize_path("/caso/", out, err) && out == "/caso");
        check("colapsa pontos", olho::cloud::dropbox::sanitize_path("/caso/./sub", out, err) && out == "/caso/sub");

        check("recusa pai", !olho::cloud::dropbox::sanitize_path("/caso/../etc", out, err));
        check("recusa pai no meio", !olho::cloud::dropbox::sanitize_path("/a/../../b", out, err));
        check("recusa controle", !olho::cloud::dropbox::sanitize_path(std::string("/a\x01""b"), out, err));
        check("recusa vazio", !olho::cloud::dropbox::sanitize_path("", out, err));
    }

    std::printf(failures == 0 ? "DROPBOX OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}