// language: C++, file: .oracle_timeline/timeline_oracle.cpp
// Oraculo do carregador de timeline: quatro produtores, limite de exibicao e
// contagem honesta do que ficou de fora.
#include "timeline_index.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTextStream>

namespace {

int failures = 0;
int checks = 0;

void check(const char* what, bool ok, const QString& detail = QString()) {
    ++checks;
    if (!ok) ++failures;
    QTextStream out(stdout);
    out << (ok ? "[ok]   " : "[FALHA] ") << what;
    if (!detail.isEmpty()) out << "  (" << detail << ")";
    out << "\n";
}

bool write_lines(const QString& path, const QStringList& lines) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream stream(&file);
    for (const QString& line : lines) stream << line << "\n";
    return true;
}

QString event_line(const QString& stamp, const QString& provider, const QString& event_id) {
    return QStringLiteral("{\"timestamp_utc\": \"%1\", \"provider\": \"%2\", "
                          "\"channel\": \"%2/Messages\", \"event_id\": \"%3\", "
                          "\"record_id\": \"rec-%3\"}")
        .arg(stamp, provider, event_id);
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString root = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("tl_fixtures"));
    QDir(root).removeRecursively();

    // Quatro produtores, exatamente como no caso real.
    write_lines(root + QStringLiteral("/timeline.jsonl"),
                {event_line(QStringLiteral("2026-01-01T10:00:00Z"), QStringLiteral("Disco"),
                            QStringLiteral("d1")),
                 QStringLiteral("{ linha quebrada"),                    // invalida
                 event_line(QStringLiteral("2026-01-01T10:05:00Z"), QStringLiteral("Disco"),
                            QStringLiteral("d2"))});
    write_lines(root + QStringLiteral("/recovered/timeline.jsonl"),
                {event_line(QStringLiteral("2026-01-01T09:00:00Z"), QStringLiteral("WhatsApp"),
                            QStringLiteral("w1"))});
    write_lines(root + QStringLiteral("/social/telegram/conta/timeline.jsonl"),
                {event_line(QStringLiteral("2026-01-01T11:00:00Z"), QStringLiteral("Telegram"),
                            QStringLiteral("t1")),
                 event_line(QStringLiteral("2026-01-01T11:30:00Z"), QStringLiteral("Telegram"),
                            QStringLiteral("t2"))});
    write_lines(root + QStringLiteral("/cloud/dropbox/conta/timeline.jsonl"),
                {event_line(QStringLiteral("2026-01-01T12:00:00Z"), QStringLiteral("Dropbox"),
                            QStringLiteral("c1")),
                 event_line(QStringLiteral("2026-01-01T08:00:00Z"), QStringLiteral("Dropbox"),
                            QStringLiteral("c2"))});
    write_lines(root + QStringLiteral("/vazio/timeline.jsonl"), {});   // arquivo vazio

    // 1. Carga completa: os quatro produtores entram, ordenados por tempo.
    const olho::ui::timeline_load full = olho::ui::load_case_timeline(root, 0);
    check("le os quatro produtores", full.files == 4,
          QStringLiteral("arquivos=%1").arg(full.files));
    check("um evento por linha valida", full.total == 8 && full.events.size() == 7,
          QStringLiteral("linhas=%1 carregados=%2").arg(full.total).arg(full.events.size()));
    check("linha quebrada contada como invalida", full.invalid == 1,
          QStringLiteral("invalidas=%1").arg(full.invalid));
    check("nada truncado sem limite", !full.truncated);
    check("arquivo vazio nao conta", !full.sources.contains(QStringLiteral("vazio/timeline.jsonl")));

    // 2. Ordem cronologica entre provedores diferentes.
    QStringList order;
    for (const olho::ui::timeline_event& event : full.events) order << event.event_id;
    const QStringList expected{QStringLiteral("c2"), QStringLiteral("w1"), QStringLiteral("d1"),
                               QStringLiteral("d2"), QStringLiteral("t1"), QStringLiteral("t2"),
                               QStringLiteral("c1")};
    check("ordenado por data entre provedores", order == expected, order.join(","));
    check("primeiro e ultimo no resumo",
          full.first_stamp.startsWith(QStringLiteral("2026-01-01T0")) &&
              full.last_stamp.startsWith(QStringLiteral("2026-01-01T")),
          full.first_stamp + " .. " + full.last_stamp);

    // 3. Marca de nuvem so nos caminhos de cloud/.
    int cloud = 0;
    for (const olho::ui::timeline_event& event : full.events) {
        if (event.from_cloud) ++cloud;
    }
    check("marca apenas os eventos de cloud", cloud == 2 && full.cloud_files == 1,
          QStringLiteral("cloud=%1 arquivos=%2").arg(cloud).arg(full.cloud_files));

    // 4. Limite de exibicao: para cedo mas continua contando.
    const olho::ui::timeline_load capped =
        olho::ui::load_case_timeline(root, olho::ui::kTimelineDisplayCap);
    check("limite de 5000 nao corta um caso de 7", !capped.truncated && capped.events.size() == 7);
    const olho::ui::timeline_load tiny = olho::ui::load_case_timeline(root, 3);
    check("limite pequeno marca truncado", tiny.truncated && tiny.events.size() == 3,
          QStringLiteral("carregados=%1").arg(tiny.events.size()));
    // O total nao depende do limite: 8 linhas de evento (7 validas + 1 quebrada).
    check("limite pequeno conta o total do caso", tiny.total == 8,
          QStringLiteral("total=%1").arg(tiny.total));
    // O corte e por ORDEM DE ARQUIVO (barato), nao por data (custaria parsear tudo).
    // Entao o subconjunto inicial nao e "os mais antigos do caso" - e o que fica
    // escrito aqui para ninguem ler o teto como se fosse cronologia.
    {
        QStringList kept;
        for (const olho::ui::timeline_event& event : tiny.events) kept << event.event_id;
        const QStringList from_cloud{"c1", "c2", "w1"};
        bool subset = true;
        for (const QString& id : kept) {
            if (!from_cloud.contains(id)) subset = false;
        }
        check("o corte vem da ordem dos arquivos", subset && kept.size() == 3, kept.join(","));
    }

    // 5. Paginacao: subir o teto nunca perde evento (o passo 3 -> 15 -> ... e o mesmo
    // que a GUI usa).
    {
        int cap = 3;
        QStringList previous;
        bool subset = true;
        int rounds = 0;
        for (; rounds < 4; ++rounds) {
            const olho::ui::timeline_load page = olho::ui::load_case_timeline(root, cap);
            QStringList now;
            for (const olho::ui::timeline_event& event : page.events) now << event.event_id;
            for (const QString& id : previous) {
                if (!now.contains(id)) subset = false;
            }
            previous = now;
            if (!page.truncated) break;
            cap *= 5;
        }
        check("subir o teto nao perde evento nenhum", subset && previous.size() == 7,
              QStringLiteral("eventos=%1 em %2 rodada(s)").arg(previous.size()).arg(rounds + 1));
        const olho::ui::timeline_load all = olho::ui::load_case_timeline(root, 200000);
        check("teto alto carrega o caso inteiro",
              !all.truncated && all.events.size() == 7 && all.total == 8);
    }

    // 6. Corte cronologico: com teto, a janela fica com os MAIS ANTIGOS do caso.
    {
        const olho::ui::timeline_load oldest = olho::ui::load_case_timeline(
            root, 3, olho::ui::timeline_cap_mode::oldest_by_time);
        QStringList kept;
        for (const olho::ui::timeline_event& event : oldest.events) kept << event.event_id;
        // 08:00 c2, 09:00 w1, 10:00 d1 - os tres primeiros da historia do caso.
        const QStringList expected{QStringLiteral("c2"), QStringLiteral("w1"), QStringLiteral("d1")};
        check("corte por data pega os mais antigos", kept == expected, kept.join(","));
        check("corte por data marca o modo", oldest.chronological && oldest.truncated);
        check("corte por data conta o caso inteiro", oldest.total == 8,
              QStringLiteral("total=%1").arg(oldest.total));
        check("corte por data interpreta todas as linhas", oldest.invalid == 1,
              QStringLiteral("invalidas=%1").arg(oldest.invalid));

        // Sem teto, os dois modos concordam: e a mesma lista.
        const olho::ui::timeline_load plain = olho::ui::load_case_timeline(root, 0);
        const olho::ui::timeline_load chrono = olho::ui::load_case_timeline(
            root, 0, olho::ui::timeline_cap_mode::oldest_by_time);
        QStringList a;
        QStringList b;
        for (const olho::ui::timeline_event& event : plain.events) a << event.event_id;
        for (const olho::ui::timeline_event& event : chrono.events) b << event.event_id;
        check("sem teto os dois modos dao a mesma lista", a == b, a.join(","));
    }

    // 7. Progresso da carga: avisa durante e fecha no total.
    {
        qint64 calls = 0;
        qint64 lines_seen = 0;
        qint64 files_seen = 0;
        bool monotonic = true;
        const olho::ui::timeline_progress_fn progress =
            [&](qint64 lines, qint64 files) {
                ++calls;
                if (lines < lines_seen || files < files_seen) monotonic = false;
                lines_seen = lines;
                files_seen = files;
            };
        const olho::ui::timeline_load load =
            olho::ui::load_case_timeline(root, 0,
                                         olho::ui::timeline_cap_mode::first_in_file_order,
                                         progress);
        check("progresso avisa durante a carga", calls > 0,
              QStringLiteral("chamadas=%1").arg(calls));
        check("progresso termina no total lido", lines_seen == load.total,
              QStringLiteral("visto=%1 total=%2").arg(lines_seen).arg(load.total));
        check("progresso termina no total de arquivos", files_seen == load.files,
              QStringLiteral("visto=%1 arquivos=%2").arg(files_seen).arg(load.files));
        check("progresso nunca volta", monotonic);
    }

    // 8. Caso inexistente nao explode.
    const olho::ui::timeline_load empty =
        olho::ui::load_case_timeline(root + QStringLiteral("/nao_existe"), 0);
    check("caso inexistente devolve vazio",
          empty.events.isEmpty() && empty.total == 0 && empty.files == 0);

    QTextStream(stdout) << QStringLiteral("\ntimeline_oracle: %1 verificacoes, %2 falha(s)\n")
                               .arg(checks)
                               .arg(failures);
    return failures == 0 ? 0 : 1;
}
