// language: C++, file: report_main.cpp, target: Windows 11 (MSVC / MinGW g++)
// Builds a forensic PDF from a recovered case directory.
//
//   olho_report <case-dir> --out <file.pdf> [--id X] [--operator Y]
//               [--legal Z] [--device W] [--notes "..."]
//
// The case directory is the output of wa_recover: report.json, conversations.json
// and media/. Every file in it is hashed here, at report time, so the hash in the
// document is of the bytes that exist now — not of a number copied from a log.
#include "case_db_seal.hpp"   // selo do caso: db + midia, chaveado pela chave do caso
#include "custody_chain.hpp"
#include "evidence_verify.hpp"
#include "forensic_report.hpp"
#include "ios_backup.hpp"     // sha256_file, already proven against the iOS oracle

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string now_iso8601() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buf);
}

std::string arg_value(int argc, char** argv, const char* name, const std::string& fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return fallback;
}

bool has_flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return true;
    }
    return false;
}

// Flags que podem aparecer mais de uma vez (--recorte). arg_value devolve so o
// primeiro, e um laudo pode citar varios recortes.
std::vector<std::string> arg_all(int argc, char** argv, const char* name) {
    std::vector<std::string> out;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) out.push_back(argv[i + 1]);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("uso: %s <case-dir> --out <file.pdf> [--id X] [--operator Y]"
                    " [--legal Z] [--device W] [--notes \"...\"]\n"
                    "       [--seal-key ARQUIVO] [--allow-broken-seal]\n"
                    "       [--recorte ARQUIVO]... [--allow-broken-recorte]\n"
                    "  o selo de integridade (db + midia) e verificado antes de gerar o laudo;\n"
                    "  selo violado recusa o laudo com exit 4, salvo --allow-broken-seal\n"
                    "  cada --recorte e um export da Timeline (.csv/.jsonl): o laudo cita o\n"
                    "  recorte e confere o sha256 contra o .meta.json; divergencia recusa o\n"
                    "  laudo com exit 4, salvo --allow-broken-recorte\n",
                    argv[0]);
        return 2;
    }

    const fs::path case_dir = argv[1];
    if (!fs::is_directory(case_dir)) {
        std::printf("caso invalido: %s\n", case_dir.string().c_str());
        return 2;
    }
    const std::string out_path = arg_value(argc, argv, "--out", "laudo.pdf");

    // -----------------------------------------------------------------------
    // Selo de integridade, ANTES de qualquer trabalho caro.
    // -----------------------------------------------------------------------
    // Um laudo gerado a partir de um caso adulterado e pior do que laudo nenhum:
    // da aparencia de pericia a uma prova que ja foi mexida. Por isso o selo e
    // conferido aqui e a violacao interrompe a geracao, sem escrever arquivo. O
    // --allow-broken-seal existe para o caso em que o perito precisa DOCUMENTAR a
    // violacao: ai o laudo sai com a falha escrita no corpo.
    const bool allow_broken_seal = has_flag(argc, argv, "--allow-broken-seal");

    // A chave fica em fs::path, e nao em string: as rotinas do selo recebem
    // wstring, e converter no meio do caminho so cria chance de erro.
    fs::path seal_key_file = arg_value(argc, argv, "--seal-key", "");
    if (seal_key_file.empty()) {
        // O fluxo de aquisicao deixa a chave em <caso>/case.key e a recuperacao
        // em <caso>/recovered, entao a chave pode estar na pasta ou um nivel acima.
        std::error_code key_ec;
        for (const fs::path& candidate : {case_dir / "case.key", case_dir.parent_path() / "case.key"}) {
            if (!candidate.empty() && fs::is_regular_file(candidate, key_ec)) {
                seal_key_file = candidate;
                break;
            }
        }
    }

    std::vector<fs::path> sealed_dirs;    // pastas com media.seal
    std::vector<fs::path> candidate_dirs; // pastas de conteudo que existem no caso
    std::vector<fs::path> db_dirs;        // pastas com case.db (+ .seal)
    {
        std::error_code dir_ec;
        // Pastas possiveis de selo: a do laudo, a subpasta recovered/ (quando o
        // operador aponta a raiz do caso) e as pastas de aquisicao ao lado do
        // case.db - cloud/ e social/ sao irmaos dele, nao filhos da recuperacao.
        std::vector<fs::path> seal_roots{case_dir};
        seal_roots.push_back(case_dir / "recovered");
        const fs::path seal_parent = case_dir.parent_path();
        if (!seal_parent.empty()) {
            seal_roots.push_back(seal_parent);
            for (const char* name : {"recovered", "media", "cloud", "social"}) {
                seal_roots.push_back(seal_parent / name);
            }
        }
        // "Pasta de conteudo" e a que carrega prova extraida. A raiz do caso
        // (case.db + case.key) nao entra: avisar que ela "nao tem selo" seria
        // ruido - o selo dela e o do banco, que ja e conferido a parte.
        const auto is_content_name = [](const fs::path& dir) {
            const std::string name = dir.filename().string();
            return name == "recovered" || name == "media" || name == "cloud" || name == "social";
        };
        for (const fs::path& root : seal_roots) {
            std::error_code root_ec;
            if (fs::is_directory(root, root_ec) && is_content_name(root)) {
                candidate_dirs.push_back(root);
            }
            if (!fs::is_regular_file(root / "media.seal", root_ec)) continue;
            bool duplicate = false;
            for (const fs::path& seen : sealed_dirs) {
                if (fs::equivalent(seen, root, root_ec)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) sealed_dirs.push_back(root);
        }
        if (fs::is_regular_file(case_dir / "case.db", dir_ec)) db_dirs.push_back(case_dir);
        const fs::path parent = case_dir.parent_path();
        if (!parent.empty() && fs::is_regular_file(parent / "case.db", dir_ec)) db_dirs.push_back(parent);
    }

    // O indice do caso fica ao lado do case.db. Ele e o que denuncia selo apagado:
    // sem ele, apagar a pasta e o selo junto fica igual a "nunca selado".
    fs::path index_dir;
    {
        std::error_code index_ec;
        if (fs::is_regular_file(case_dir / "case.seal", index_ec)) {
            index_dir = case_dir;
        } else {
            const fs::path up = case_dir.parent_path();
            if (!up.empty() && fs::is_regular_file(up / "case.seal", index_ec)) index_dir = up;
        }
    }

    std::vector<std::string> seal_failures;
    // O selo mora DENTRO da pasta que ele sela: apagar o selo fica igual a "nunca
    // selado". Nao da para transformar isso em falha sem quebrar todo caso
    // anterior a este recurso, mas um caso que TEM selo no banco e uma pasta de
    // conteudo sem selo e uma assimetria que o laudo precisa mostrar.
    std::vector<std::string> seal_absences;
    if (!db_dirs.empty()) {
        std::error_code absence_ec;
        for (const fs::path& dir : candidate_dirs) {
            bool sealed_already = false;
            for (const fs::path& seen : sealed_dirs) {
                if (fs::equivalent(seen, dir, absence_ec)) {
                    sealed_already = true;
                    break;
                }
            }
            if (sealed_already) continue;
            const std::string name = dir.filename().string();
            seal_absences.push_back("pasta " + name + "/ sem selo em um caso que tem o banco selado: "
                                    "o conteudo dela nao esta coberto. Se essa pasta ja existia "
                                    "quando o caso foi selado, o selo foi removido.");
        }
    }

    std::string seal_ok_summary;
    bool seal_checked = false;
    bool seal_present = !sealed_dirs.empty() || !db_dirs.empty() || !index_dir.empty();
    std::uint64_t seal_media_files = 0;
    std::uint64_t seal_media_bytes = 0;
    std::uint64_t seal_dirs_checked = 0;
    std::uint64_t seal_index_entries = 0;
    if (seal_present && !seal_key_file.empty()) {
        deep::case_cipher seal_cipher;
        std::string crypto_error;
        if (!seal_cipher.load(seal_key_file.wstring(), crypto_error)) {
            seal_failures.push_back("chave do caso ilegivel: " + crypto_error);
        } else {
            seal_checked = true;
            for (const fs::path& dir : sealed_dirs) {
                const deep::media_seal_report media = deep::verify_media_dir(dir.wstring(), seal_cipher);
                seal_media_files += media.files;
                seal_media_bytes += media.bytes;
                ++seal_dirs_checked;
                const std::string folder = dir.filename().string();
                if (!media.ok) {
                    for (const std::string& line : media.problems) seal_failures.push_back(folder + " alterado - " + line);
                    for (const std::string& line : media.missing) seal_failures.push_back(folder + " ausente - " + line);
                    for (const std::string& line : media.added) seal_failures.push_back(folder + " fora do selo - " + line);
                    if (media.problems.empty() && media.missing.empty() && media.added.empty()) {
                        seal_failures.push_back(folder + ": " + media.error);
                    }
                }
            }
            for (const fs::path& dir : db_dirs) {
                const deep::seal_set_report one = deep::verify_case_files(dir.wstring(), seal_cipher);
                for (const std::string& line : one.errors) seal_failures.push_back("banco do caso - " + line);
                if (!one.ok && one.errors.empty()) seal_failures.push_back("banco do caso: " + one.error);
            }
            if (!index_dir.empty()) {
                const deep::case_index_report index =
                    deep::verify_case_index(index_dir.wstring(), seal_cipher);
                if (index.ok) {
                    seal_index_entries = index.entries;
                } else {
                    for (const std::string& line : index.problems) {
                        seal_failures.push_back("indice: selo com digest diferente - " + line);
                    }
                    for (const std::string& line : index.missing) {
                        seal_failures.push_back("indice: selo registrado sumiu - " + line);
                    }
                    for (const std::string& line : index.added) {
                        seal_failures.push_back("indice: selo que nao estava no indice - " + line);
                    }
                    if (index.problems.empty() && index.missing.empty() && index.added.empty()) {
                        seal_failures.push_back("indice: " + index.error);
                    }
                }
            }
            if (seal_failures.empty()) {
                seal_ok_summary = "Selo de integridade: VERIFICADO antes da geracao - " +
                                  std::to_string(seal_media_files) + " arquivo(s) extraido(s) em " +
                                  std::to_string(seal_dirs_checked) + " pasta(s), " +
                                  std::to_string(seal_media_bytes) + " bytes, o banco do caso" +
                                  (seal_index_entries > 0
                                       ? " e " + std::to_string(seal_index_entries) + " selo(s) do indice do caso"
                                       : std::string()) +
                                  ", todos conferindo (HMAC-SHA256 chaveado pela chave do caso).";
            }
        }
    }

    if (!seal_failures.empty()) {
        std::printf("  chave do selo usada: %s\n", seal_key_file.string().c_str());
        for (const std::string& line : seal_failures) {
            std::printf("  [!] %s\n", line.c_str());
        }
        if (!allow_broken_seal) {
            std::printf("LAUDO RECUSADO: o selo do caso nao confere. Nenhum arquivo foi escrito.\n");
            std::printf("  Rode --case-check no caso para o diagnostico completo.\n");
            std::printf("  Se a chave do caso for outra, aponte com --seal-key ARQUIVO.\n");
            std::printf("  Para emitir o laudo DOCUMENTANDO a violacao: --allow-broken-seal\n");
            return 4;
        }
        std::printf("[!] selo violado, laudo emitido porque --allow-broken-seal foi passado\n");
    }

    olho::report::document doc;
    doc.info.case_id = arg_value(argc, argv, "--id", case_dir.filename().string());
    doc.info.operator_name = arg_value(argc, argv, "--operator", "(nao informado)");
    doc.info.legal_basis = arg_value(argc, argv, "--legal", "(nao informada)");
    doc.info.device = arg_value(argc, argv, "--device", "(nao informado)");
    doc.info.generated_at = now_iso8601();
    doc.info.tool_version = "Olho de Deus 1.0 / deep engine + wa_recover";
    doc.info.tool_build = __DATE__ " " __TIME__;

    // O estado do selo entra no CORPO do laudo: quem le o documento precisa saber
    // se a prova foi conferida e contra qual selo - e precisa ver a marca quando
    // NAO foi.
    if (!seal_ok_summary.empty()) {
        doc.notes.push_back(seal_ok_summary);
        std::printf("selo: ok - %llu arquivo(s) extraido(s) em %llu pasta(s), %llu bytes conferidos\n",
                    (unsigned long long)seal_media_files, (unsigned long long)seal_dirs_checked,
                    (unsigned long long)seal_media_bytes);
    }
    if (!seal_failures.empty()) {
        // So chega aqui com --allow-broken-seal; a primeira nota deixa claro que
        // este documento nao e atestado de integridade.
        doc.notes.push_back("SELO DE INTEGRIDADE VIOLADO: laudo emitido a pedido do perito para "
                            "DOCUMENTAR a violacao; nao e atestado de integridade.");
        for (const std::string& line : seal_failures) doc.notes.push_back("violacao: " + line);
    }
    for (const std::string& line : seal_absences) {
        doc.notes.push_back("ATENCAO: " + line);
        std::printf("[!] ATENCAO: %s\n", line.c_str());
    }
    if (!seal_present) {
        doc.notes.push_back("Selo de integridade: ausente - a pasta do caso nao foi selada, "
                            "entao este laudo nao atesta integridade por selo. Rode --case-seal "
                            "no caso e gere o laudo de novo.");
    } else if (!seal_checked && seal_failures.empty()) {
        doc.notes.push_back("Selo de integridade: presente, NAO verificado - a chave do caso nao "
                            "foi encontrada. Use --seal-key para apontar a chave.");
    }

    // -----------------------------------------------------------------------
    // Recortes anexados: exports da Timeline (.csv/.jsonl) com sidecar.
    // -----------------------------------------------------------------------
    // Um recorte e o que o perito viu na tela, exportado com procedencia. O laudo
    // cita o recorte E confere o hash do arquivo contra o sidecar: citar um anexo
    // que nao bate seria pior que nao citar. Divergencia recusa o laudo (exit 4),
    // com a mesma regra do selo.
    const std::vector<std::string> recortes = arg_all(argc, argv, "--recorte");
    const bool allow_broken_recorte = has_flag(argc, argv, "--allow-broken-recorte");
    std::vector<std::string> recorte_failures;
    for (const std::string& path : recortes) {
        const fs::path file(path);
        const fs::path meta(path + ".meta.json");
        if (!fs::is_regular_file(file)) {
            recorte_failures.push_back(path + ": arquivo do recorte nao existe");
            continue;
        }
        if (!fs::is_regular_file(meta)) {
            recorte_failures.push_back(path + ": sidecar .meta.json ausente");
            continue;
        }
        std::ifstream meta_in(meta, std::ios::binary);
        const std::string meta_text((std::istreambuf_iterator<char>(meta_in)),
                                    std::istreambuf_iterator<char>());
        const nlohmann::json meta_json = nlohmann::json::parse(meta_text, nullptr, false);
        if (meta_json.is_discarded()) {
            recorte_failures.push_back(path + ": sidecar ilegivel (JSON invalido)");
            continue;
        }
        const std::string format = meta_json.value("format", "");
        const std::string hash_key =
            format.find("jsonl") != std::string::npos ? "sha256_jsonl" : "sha256_csv";
        const std::string expected = meta_json.value(hash_key, "");
        std::string actual;
        std::uint64_t size = 0;
        std::string error;
        if (!olho::ios::sha256_file(path, actual, size, error)) {
            recorte_failures.push_back(path + ": nao consegui ler o arquivo (" + error + ")");
            continue;
        }
        if (expected.empty()) {
            recorte_failures.push_back(path + ": sidecar sem o campo " + hash_key);
            continue;
        }
        if (actual != expected) {
            recorte_failures.push_back(path + ": sha256 do arquivo nao bate com o sidecar");
            continue;
        }
        const int rows = meta_json.value("rows", 0);
        const std::string filter = meta_json.value("filter", "");
        const std::string scope = meta_json.value("scope", "");
        doc.notes.push_back("Recorte anexado: " + file.filename().string() + " - " +
                            std::to_string(rows) + " evento(s), filtro " + filter +
                            ", sha256 CONFIRMADO (" + actual.substr(0, 16) + "...)");
        if (!scope.empty()) doc.notes.push_back("  escopo do recorte: " + scope);
        std::printf("recorte: %s - %d evento(s), filtro %s, sha256 confirmado\n",
                    file.filename().string().c_str(), rows, filter.c_str());
    }
    if (!recorte_failures.empty()) {
        for (const std::string& line : recorte_failures) {
            std::printf("  [!] %s\n", line.c_str());
        }
        if (!allow_broken_recorte) {
            std::printf("LAUDO RECUSADO: o recorte anexado nao confere. Nenhum arquivo foi "
                        "escrito.\n");
            std::printf("  Gere o recorte de novo pela Timeline, ou emita com "
                        "--allow-broken-recorte para DOCUMENTAR a divergencia.\n");
            return 4;
        }
        doc.notes.push_back("RECORTE ANEXADO DIVERGENTE: laudo emitido com --allow-broken-recorte; "
                            "o anexo NAO e atestado de integridade.");
        for (const std::string& line : recorte_failures) {
            doc.notes.push_back("recorte: " + line);
        }
        std::printf("[!] recorte divergente, laudo emitido porque --allow-broken-recorte foi "
                    "passado\n");
    }

    // Hash every file under the case directory: this is the evidence list.
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(case_dir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file()) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    // ONE pass over the case. Hashing for the evidence list, detecting the type and
    // building the custody rows are all the same read: doing them in separate walks
    // tripled the I/O, which at 10k files was 18 of 61 seconds.
    std::uint64_t hashed = 0, failed = 0;
    std::vector<olho::custody::input_entry> chain_inputs;
    olho::evidence::verify_result type_check;
    type_check.ok = true;
    for (const auto& p : files) {
        olho::report::evidence_item item;
        item.path = fs::relative(p, case_dir, ec).string();
        if (item.path.rfind("media/", 0) == 0) item.source = "midia recuperada";

        std::string hex;
        std::uint64_t size = 0;
        std::string error;
        if (!olho::ios::sha256_file(p.string(), hex, size, error)) {
            std::printf("  [!] falha ao hashear %s: %s\n", item.path.c_str(), error.c_str());
            ++failed;
            continue;
        }
        item.sha256 = hex;
        item.size = size;
        doc.evidence.push_back(item);
        ++hashed;

        olho::custody::input_entry row;
        row.relative_path = item.path;
        row.size = size;
        row.sha256 = hex;
        row.kind = olho::evidence::detect_kind(p.wstring());
        chain_inputs.push_back(std::move(row));

        const auto verdict =
            olho::evidence::classify(chain_inputs.back().relative_path, chain_inputs.back().kind);
        ++type_check.checked;
        if (verdict.result == olho::evidence::verdict::ok) {
            ++type_check.matched;
        } else if (verdict.result == olho::evidence::verdict::mismatch) {
            ++type_check.mismatched;
            type_check.findings.push_back(verdict);
        } else {
            ++type_check.unknown;
        }
    }

    // Counts come from report.json, which wa_recover wrote. If it is absent the
    // numbers stay zero rather than being guessed.
    const fs::path report_json = case_dir / "report.json";
    if (fs::exists(report_json)) {
        std::ifstream f(report_json);
        try {
            const json j = json::parse(f);
            doc.deleted_messages = j.value("apagadas_confirmadas", 0ull);
            doc.media_files = j.value("midia_ok", 0ull);
            doc.wal_frames = j.value("wal_frames", 0ull);
            doc.pages_decrypted = j.value("paginas", 0ull);
        } catch (const std::exception& e) {
            std::printf("  [!] report.json ilegivel: %s\n", e.what());
        }
    }

    // A sample of recovered rows, with origin, so the report shows method and not
    // just totals.
    const fs::path conv = case_dir / "conversations.json";
    if (fs::exists(conv)) {
        std::ifstream f(conv);
        try {
            const json j = json::parse(f);
            std::size_t shown = 0;
            for (const auto& row : j.value("apagadas", json::array())) {
                if (shown++ >= 40) break;
                olho::report::recovered_item item;
                item.kind = "Mensagem apagada";
                item.origin = row.value("origem", "");
                const auto texts = row.value("textos", json::array());
                std::string detail;
                for (const auto& t : texts) {
                    if (!detail.empty()) detail += " | ";
                    detail += t.get<std::string>();
                }
                item.detail = detail;
                doc.recovered.push_back(std::move(item));
            }
        } catch (const std::exception& e) {
            std::printf("  [!] conversations.json ilegivel: %s\n", e.what());
        }
    }

    std::printf("tipos: %llu conferem, %llu divergem, %llu sem verificacao\n",
                (unsigned long long)type_check.matched,
                (unsigned long long)type_check.mismatched,
                (unsigned long long)type_check.unknown);
    for (const auto& note : olho::evidence::findings_as_notes(type_check)) {
        doc.notes.push_back(note);
        std::printf("  %s\n", note.c_str());
    }
    if (type_check.mismatched == 0) {
        doc.notes.push_back("Verificacao de tipo: nenhum arquivo teve conteudo divergente da "
                            "extensao declarada (" + std::to_string(type_check.matched) +
                            " conferidos).");
    }

    // Custody chain. The seal goes INTO the document, because the chain alone
    // cannot detect a removed tail that was re-sealed: only a published head can.
    olho::custody::chain custody;
    std::string custody_error;
    if (olho::custody::build_from(doc.info.case_id + "|" + doc.info.generated_at, chain_inputs,
                                  custody, custody_error)) {
        doc.notes.push_back("Cadeia de custodia: " + std::to_string(custody.entries.size()) +
                            " entradas encadeadas; selo SHA-256 final " + custody.head + ".");
        std::size_t bad_index = 0;
        std::string bad_path, verify_error;
        if (!olho::custody::verify(custody, bad_index, bad_path, verify_error)) {
            doc.notes.push_back("FALHA NA CADEIA: " + verify_error);
            std::printf("  [!] cadeia invalida: %s\n", verify_error.c_str());
        } else {
            std::printf("cadeia: %zu entradas, selo %s...\n", custody.entries.size(),
                        custody.head.substr(0, 16).c_str());
        }
        if (olho::custody::to_json(custody).size() > 0) {
            std::ofstream manifest(out_path + ".custody.json", std::ios::binary | std::ios::trunc);
            manifest << olho::custody::to_json(custody);
        }
    } else {
        doc.notes.push_back("Cadeia de custodia nao pode ser construida: " + custody_error);
        std::printf("  [!] cadeia: %s\n", custody_error.c_str());
    }

    const std::string extra = arg_value(argc, argv, "--notes", "");
    if (!extra.empty()) doc.notes.push_back(extra);
    doc.notes.push_back("Aquisicao realizada em modo somente-leitura; nenhum arquivo do "
                        "dispositivo foi alterado.");
    doc.notes.push_back("O ultimo bloco de 48 bytes de cada pagina SQLCipher e reservado ao "
                        "IV e ao MAC e nao existe em texto claro em nenhum banco SQLCipher.");
    doc.notes.push_back("Sobrescrita por software nao alcanca overprovisioning de SSD; apenas "
                        "ATA Secure Erase / NVMe Sanitize atingem a chave do controlador.");

    // Signing is opt-in: --sign [filtro-do-assunto]. Without a certificate the
    // report is still written and says plainly that it carries no signature.
    olho::report::signer_options sign_opts;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sign") == 0) {
            sign_opts.enabled = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') sign_opts.subject_filter = argv[i + 1];
        } else if (std::strcmp(argv[i], "--tsa") == 0 && i + 1 < argc) {
            sign_opts.enabled = true;
            sign_opts.tsa_url = argv[i + 1];
        } else if (std::strcmp(argv[i], "--tsa-selftest") == 0) {
            sign_opts.enabled = true;
            sign_opts.tsa_selftest = true;
        }
    }

    olho::report::signature_info sig;
    std::string error;
    if (!olho::report::write_pdf_signed(doc, out_path, sign_opts, &sig, error)) {
        std::printf("FALHOU: %s\n", error.c_str());
        return 1;
    }
    if (sign_opts.enabled) {
        if (sig.signed_pdf) {
            std::printf("assinado por: %s\n  digest: %s\n",
                        sig.subject.c_str(), sig.digest.c_str());
            if (sig.timestamped) {
                std::printf("  carimbo de tempo: %s  (TSA: %s)\n",
                            sig.timestamp_time.c_str(), sig.tsa_subject.c_str());
            } else {
                std::printf("  SEM carimbo de tempo%s\n",
                            sig.error.empty() ? "" : (" - " + sig.error).c_str());
            }
        } else {
            std::printf("[!] NAO assinado: %s\n", sig.error.c_str());
        }
    }
    std::printf("laudo: %s\n  evidencia: %llu arquivos hasheados (%llu falhas)\n"
                "  apagadas: %llu  midia: %llu  wal_frames: %llu  paginas: %llu\n",
                out_path.c_str(), (unsigned long long)hashed, (unsigned long long)failed,
                (unsigned long long)doc.deleted_messages, (unsigned long long)doc.media_files,
                (unsigned long long)doc.wal_frames, (unsigned long long)doc.pages_decrypted);
    return 0;
}
