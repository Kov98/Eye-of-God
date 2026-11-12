// language: C++, file: timeline_index.hpp, target: Windows 11, Qt 6
// Carregamento da linha do tempo do caso.
//
// POR QUE ISTO E UM MODULO, E NAO UM LACO DENTRO DO mainwindow
//   O formato timeline.jsonl e escrito por QUATRO produtores diferentes (engine de
//   disco na raiz, wa_recover em recovered/, exports sociais em social/ e
//   conectores em cloud/). Quando esse laco vivia dentro da janela, cada ajuste
//   era invisivel para os testes e a aba ficou silenciosamente vazia numa
//   aquisicao de WhatsApp. Aqui ele tem contrato e oraculo proprio.
//
// O LIMITE DE EXIBICAO E EXPLICITO
//   Um caso de 200 mil mensagens nao cabe numa tabela de itens. O carregador para
//   no limite, mas CONTINUA contando, e devolve \`total\` e \`truncated\` para a GUI
//   poder dizer "5.000 de 132.480" em vez de mentir por omissao.
#pragma once

#include <QDateTime>

#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>

namespace olho::ui {

struct timeline_event {
    QString raw;             // linha original, para o painel e o Hex View
    QDateTime when;          // invalido quando o evento nao tem data utilizavel
    QString timestamp_text;  // timestamp_utc como veio no arquivo
    QString provider;
    QString channel;
    QString event_id;
    QString record_id;
    bool from_cloud = false; // veio de cloud/<provedor>/<conta>/
};

struct timeline_load {
    QVector<timeline_event> events;  // ordenados por data (sem data vai para o fim)
    // total conta LINHAS de evento no caso (inclusive as que nao parseiam): e
    // exato e custa so leitura. invalid cobre apenas a janela carregada - acima do
    // limite o conteudo nao e interpretado, e isso fica escrito aqui para ninguem
    // confundir "nao carregado" com "invalido".
    qint64 total = 0;
    qint64 invalid = 0;
    int files = 0;                   // arquivos timeline.jsonl encontrados
    int cloud_files = 0;
    bool truncated = false;          // parou no limite de exibicao
    bool chronological = false;      // o corte foi por data (modo caro)
    QString first_stamp;             // primeiro e ultimo timestamp do caso
    QString last_stamp;
    QStringList sources;             // caminhos relativos lidos, em ordem
};

// Como o teto corta a lista.
enum class timeline_cap_mode {
    // Barato: para de interpretar ao atingir o teto, na ordem dos arquivos. A
    // primeira pagina NAO e "os mais antigos do caso" e nao e estavel entre
    // provedores - e o padrao porque custa so leitura (85 ms em 200 mil).
    first_in_file_order = 0,
    // Caro: interpreta o caso inteiro e fica com os N mais antigos. 1,14 s em 200
    // mil eventos, mas a primeira pagina e o comeco da historia.
    oldest_by_time = 1
};

// Aviso de progresso: linhas ja lidas e arquivos ja fechados. Chamado da thread da
// carga - quem consome nao pode tocar em widget aqui.
using timeline_progress_fn = std::function<void(qint64 lines, qint64 files)>;

// Varre o caso inteiro atras de timeline.jsonl (raiz, recovered/, social/, cloud/)
// e devolve no maximo \`max_entries\` eventos. Um caso inexistente devolve vazio.
timeline_load load_case_timeline(const QString& case_dir, int max_entries,
                                 timeline_cap_mode mode = timeline_cap_mode::first_in_file_order,
                                 const timeline_progress_fn& progress = {});

// Limite usado pela GUI. Exportado para o teste e o log falarem do mesmo numero.
constexpr int kTimelineDisplayCap = 5000;

}  // namespace olho::ui
