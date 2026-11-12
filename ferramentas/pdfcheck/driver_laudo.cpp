// driver_laudo.cpp -- aciona o gerador do lead (forensic_report.cpp) com casos
// sinteticos e escreve o PDF. Vive em ferramentas/pdfcheck/ de proposito: o gerador
// e do lead, este arquivo apenas o chama.
//
// modos:
//   normal    caso tipico (3 paginas)
//   hostil    dados que quebram gerador amador: parenteses, barra invertida,
//             acentos, quebra de linha dentro do texto, token sem espaco, vazio
//   vazio     documento sem evidencia e sem recuperados
//   grande    120 itens de evidencia (forca muitas paginas)
#include "../../forensic_report.hpp"

#include <cstdio>
#include <string>

namespace {

const char* kSha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void base(olho::report::document& doc) {
    doc.info.case_id = "2026-0001";
    doc.info.operator_name = "Perito Joao da Silva";
    doc.info.legal_basis = "Mandado judicial 0001234-56.2026.8.26.0100";
    doc.info.device = "Moto G84 5G / Android 14";
    doc.info.generated_at = "2026-02-11T14:22:07Z";
    doc.info.tool_version = "0.9.0";
    doc.info.tool_build = "gcc-16.1.0-ucrt64";
    doc.deleted_messages = 1234;
    doc.media_files = 87;
    doc.wal_frames = 512;
    doc.pages_decrypted = 42;
}

void add_evidencia(olho::report::document& doc, int n, const std::string& prefixo) {
    for (int i = 0; i < n; ++i) {
        olho::report::evidence_item e;
        e.path = prefixo + std::to_string(i) + ".bin";
        e.sha256 = kSha;
        e.size = 1024ull * (i + 1) * 37;
        e.source = (i % 3 == 0) ? "whatsapp" : (i % 3 == 1 ? "android" : "ios");
        doc.evidence.push_back(e);
    }
}

void modo_normal(olho::report::document& doc) {
    base(doc);
    add_evidencia(doc, 14, "caso/evidencia/arquivo_");
    for (int i = 0; i < 9; ++i) {
        olho::report::recovered_item r;
        r.kind = (i % 3 == 0) ? "mensagem apagada" : (i % 3 == 1 ? "midia" : "arquivo");
        r.detail = "Registro recuperado do freelist do banco msgstore.db com texto longo "
                   "para forcar quebra de linha: conversa de 2026-02-10 21:14 entre os "
                   "participantes, contendo numero de telefone +55 11 99999-0000 e um "
                   "endereco de e-mail perito.teste@exemplo.com.br para medir a quebra.";
        if (i % 2 == 0) r.sha256 = kSha;
        r.origin = (i % 3 == 0) ? "freelist" : (i % 3 == 1 ? "freeblock" : "orphan");
        doc.recovered.push_back(r);
    }
    doc.notes.push_back("Laudo gerado sem compressao de streams; toda a estrutura xref e "
                        "escrita por este gerador.");
    doc.notes.push_back("Nenhum dado foi coletado nesta execucao: entrada sintetica de teste.");
}

void modo_hostil(olho::report::document& doc) {
    base(doc);
    // parenteses, barra invertida e acentos no cabecalho
    doc.info.case_id = "Caso (2026)/0001 \\ teste";
    doc.info.operator_name = "Perito Joao da Silva (CREFITO) \\ perito-chefe";
    doc.info.legal_basis = "Acao cautelar de busca e apreensao - Sao Paulo/SP - "
                           "artigo 240 (CPP) \\ paragrafo unico";
    doc.info.device = "Galaxy A54 (SM-A546E) - Android 14 \\ ROM VENDOR";
    // evidencia com caminho de Windows, espaco e parenteses
    add_evidencia(doc, 3, "C:\\caso (principal)\\evidencia ");
    // recovered: parenteses, barra invertida, quebra de linha interna e token sem espaco
    {
        olho::report::recovered_item r;
        r.kind = "mensagem (apagada) \\ recuperada";
        r.detail = "linha 1 com (parenteses) e \\ barra invertida\nlinha 2 depois de quebra "
                   "de linha";
        r.origin = "freelist";
        doc.recovered.push_back(r);
    }
    {
        olho::report::recovered_item r;
        r.kind = "token sem espaco";
        // 180 caracteres sem nenhum espaco: nao existe ponto de quebra
        r.detail = std::string(180, 'X');
        r.origin = "freeblock";
        doc.recovered.push_back(r);
    }
    {
        olho::report::recovered_item r;
        r.kind = "acentos";
        r.detail = "acao, coracao, nao, pericia, laudo, evidencia, Jose, Sao Paulo, "
                   "conclusao, endereco, unico";
        r.origin = "orphan";
        doc.recovered.push_back(r);
    }
    doc.notes.push_back("Observacao (com parenteses) e \\ barra invertida na nota.");
    doc.notes.push_back(std::string(240, 'Z'));
}

void modo_acentos(olho::report::document& doc) {
    base(doc);
    doc.info.operator_name = "Perito João da Silva";
    doc.info.legal_basis = "Ação cautelar de busca e apreensão — "
                           "São Paulo/SP";
    doc.info.device = "Aparelho apreendido na residência";
    add_evidencia(doc, 2, "caso/evidência/arquivo_");
    olho::report::recovered_item r;
    r.kind = "mensagem apagada";
    r.detail = "ação, coração, perícia, laudo, evidência, "
               "José, São Paulo, conclusão, endereço, único";
    r.origin = "freelist";
    doc.recovered.push_back(r);
    doc.notes.push_back("Conclusão: não houve violação do lacre.");
}

void modo_vazio(olho::report::document& doc) {
    base(doc);
}

void modo_grande(olho::report::document& doc) {
    base(doc);
    add_evidencia(doc, 120, "caso/evidencia/arquivo_");
    for (int i = 0; i < 40; ++i) {
        olho::report::recovered_item r;
        r.kind = "midia";
        r.detail = "Item " + std::to_string(i) + " recuperado do espaco liberado.";
        r.origin = "freeblock";
        doc.recovered.push_back(r);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string modo = (argc > 2) ? argv[2] : "normal";
    const std::string out = (argc > 1) ? argv[1] : "ferramentas/pdfcheck/out/laudo_lead.pdf";
    olho::report::document doc;
    if (modo == "normal") modo_normal(doc);
    else if (modo == "hostil") modo_hostil(doc);
    else if (modo == "vazio") modo_vazio(doc);
    else if (modo == "acentos") modo_acentos(doc);
    else if (modo == "grande") modo_grande(doc);
    else { std::printf("modo desconhecido: %s\n", modo.c_str()); return 2; }

    std::string error;
    if (!olho::report::write_pdf(doc, out, error)) {
        std::printf("FALHOU: %s\n", error.c_str());
        return 1;
    }
    std::printf("OK: %s (modo %s)\n", out.c_str(), modo.c_str());
    return 0;
}
