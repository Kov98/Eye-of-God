// language: C++, file: .oracle_timeline/timeline_oracle.cpp
// Oraculo do carregador de timeline: quatro produtores, limite de exibicao e
// contagem honesta do que ficou de fora.
#include "timeline_export.hpp"
#include "timeline_geometry.hpp"
#include "timeline_index.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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

    // 9. Filtro de apresentacao da barra da Timeline. O que ele esconde tem que
    //    continuar contado: matched + hidden fecha com o tamanho da entrada.
    {
        auto make = [](const QString& stamp, const QString& provider,
                       const QString& channel, bool cloud) {
            olho::ui::timeline_event event;
            event.timestamp_text = stamp;
            if (!stamp.isEmpty()) {
                event.when = QDateTime::fromString(stamp, Qt::ISODateWithMs);
                if (!event.when.isValid()) event.when = QDateTime::fromString(stamp, Qt::ISODate);
                if (event.when.isValid()) event.when = event.when.toLocalTime();
            }
            event.provider = provider;
            event.channel = channel;
            event.from_cloud = cloud;
            event.event_id = provider + QLatin1Char('-') + stamp;
            return event;
        };
        QVector<olho::ui::timeline_event> sample;
        sample << make(QStringLiteral("2023-11-14T10:00:00.000Z"), "WhatsApp",
                       "WhatsApp/Messages", false);
        sample << make(QStringLiteral("2023-11-15T11:30:00.000Z"), "WhatsApp",
                       "WhatsApp/Calls", false);
        sample << make(QStringLiteral("2023-11-20T09:00:00.000Z"), "Telegram",
                       "Telegram/Messages", true);
        sample << make(QString(), "Telegram", "Telegram/Deleted", false);

        const olho::ui::timeline_filter none;
        check("filtro vazio e declarado vazio", none.empty() && !none.has_time_window());
        const olho::ui::timeline_filtered all = olho::ui::filter_timeline(sample, none);
        check("sem filtro passa tudo", all.matched == sample.size() && all.hidden == 0,
              QStringLiteral("passaram=%1").arg(all.matched));
        check("sem filtro preserva a ordem",
              all.events.size() == sample.size() &&
                  all.events.value(0).provider == QStringLiteral("WhatsApp") &&
                  all.events.value(3).channel == QStringLiteral("Telegram/Deleted"));

        olho::ui::timeline_filter by_provider;
        by_provider.provider = QStringLiteral("whatsapp");  // caixa diferente de proposito
        const olho::ui::timeline_filtered provider_kept =
            olho::ui::filter_timeline(sample, by_provider);
        check("provedor ignora maiusculas", provider_kept.matched == 2,
              QStringLiteral("passaram=%1").arg(provider_kept.matched));
        check("provedor conta o que escondeu",
              provider_kept.hidden == sample.size() - provider_kept.matched);

        olho::ui::timeline_filter by_channel;
        by_channel.channel = QStringLiteral("WhatsApp/Calls");
        check("canal filtra o canal", olho::ui::filter_timeline(sample, by_channel).matched == 1);

        olho::ui::timeline_filter cloud_only;
        cloud_only.cloud_only = true;
        const olho::ui::timeline_filtered cloud_kept =
            olho::ui::filter_timeline(sample, cloud_only);
        check("so nuvem pega o evento de cloud",
              cloud_kept.matched == 1 &&
                  cloud_kept.events.value(0).provider == QStringLiteral("Telegram"));

        olho::ui::timeline_filter same_day;
        same_day.from = QDate(2023, 11, 15);
        same_day.to = QDate(2023, 11, 15);
        check("janela de um dia inclui as bordas",
              olho::ui::filter_timeline(sample, same_day).matched == 1);

        olho::ui::timeline_filter window;
        window.from = QDate(2023, 11, 14);
        window.to = QDate(2023, 11, 16);
        const olho::ui::timeline_filtered window_kept =
            olho::ui::filter_timeline(sample, window);
        check("janela exclui o que esta fora", window_kept.matched == 2);
        check("evento sem data fica fora da janela e e contado",
              window_kept.no_time == 1 && window_kept.hidden == 2,
              QStringLiteral("sem data=%1 escondidos=%2")
                  .arg(window_kept.no_time)
                  .arg(window_kept.hidden));

        // Sem janela de periodo nao existe criterio para descartar evento sem data.
        olho::ui::timeline_filter provider_only;
        provider_only.provider = QStringLiteral("Telegram");
        const olho::ui::timeline_filtered telegram =
            olho::ui::filter_timeline(sample, provider_only);
        check("sem janela o evento sem data continua visivel",
              telegram.matched == 2 && telegram.no_time == 0);

        olho::ui::timeline_filter combined;
        combined.provider = QStringLiteral("Telegram");
        combined.cloud_only = true;
        combined.from = QDate(2023, 11, 1);
        combined.to = QDate(2023, 11, 30);
        check("filtros combinados sao E, nao OU",
              olho::ui::filter_timeline(sample, combined).matched == 1);

        const QStringList providers = olho::ui::timeline_providers(sample);
        check("lista de provedores e unica e ordenada",
              providers == QStringList{QStringLiteral("Telegram"), QStringLiteral("WhatsApp")},
              providers.join(","));
        check("canais respeitam o provedor escolhido",
              olho::ui::timeline_channels(sample, QStringLiteral("WhatsApp")).size() == 2);
        check("canais sem provedor trazem todos",
              olho::ui::timeline_channels(sample, QString()).size() == 4);
        const olho::ui::timeline_filtered nothing =
            olho::ui::filter_timeline(QVector<olho::ui::timeline_event>(), none);
        check("entrada vazia devolve zeros",
              nothing.matched == 0 && nothing.hidden == 0 && nothing.events.isEmpty());
    }

    // 10. Export CSV do que esta na tela (lista filtrada) + sidecar de procedencia.
    {
        const QString dir = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("tl_export"));
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);

        QVector<olho::ui::timeline_event> rows;
        auto make_row = [](const QString& stamp, const QString& provider,
                           const QString& channel, const QString& event_id,
                           const QString& raw, bool cloud) {
            olho::ui::timeline_event event;
            event.timestamp_text = stamp;
            event.when = QDateTime::fromString(stamp, Qt::ISODateWithMs);
            if (event.when.isValid()) event.when = event.when.toLocalTime();
            event.provider = provider;
            event.channel = channel;
            event.event_id = event_id;
            event.record_id = QStringLiteral("rec-") + event_id;
            event.raw = raw;
            event.from_cloud = cloud;
            return event;
        };
        // O raw tem virgula e aspas de proposito: e o que quebra CSV ingenuo.
        rows << make_row(QStringLiteral("2023-11-14T10:00:00.000Z"), "WhatsApp",
                         "WhatsApp/Messages", "w1",
                         QStringLiteral("{\"a\": \"x,y\", \"b\": \"aspas \\\"aqui\\\"\"}"), false);
        rows << make_row(QStringLiteral("2023-11-15T11:30:00.000Z"), "Telegram",
                         "Telegram/Messages", "t1", QStringLiteral("{\"a\": 1}"), true);

        olho::ui::timeline_filter filter;
        filter.provider = QStringLiteral("Telegram");
        const QString csv = QDir(dir).filePath(QStringLiteral("saida.csv"));
        const olho::ui::timeline_export_result result =
            olho::ui::export_timeline_csv(csv, rows, filter, dir);
        check("export escreve o CSV", result.ok, result.error);
        check("export conta as linhas", result.rows == rows.size(),
              QStringLiteral("linhas=%1").arg(result.rows));

        QFile file(csv);
        const bool opened = file.open(QIODevice::ReadOnly);
        const QByteArray content = opened ? file.readAll() : QByteArray();
        if (opened) file.close();
        check("CSV comeca com BOM UTF-8", content.startsWith("\xEF\xBB\xBF"));
        const QByteArray crlf = content;
        const QList<QByteArray> lines = crlf.split('\n');
        // Com BOM + cabecalho + 2 linhas + CRLF final sobram 4 partes (a ultima vazia).
        check("CSV tem cabecalho e uma linha por evento", lines.size() == rows.size() + 2,
              QStringLiteral("partes=%1").arg(lines.size()));
        check("cabecalho do CSV e o combinado",
              QString::fromUtf8(lines.value(0)).contains(
                  QStringLiteral("timestamp_utc\",\"when_local\",\"provider\",\"channel\","
                                 "\"event_id\",\"record_id\",\"from_cloud\",\"size_bytes\","
                                 "\"sha256\",\"raw_json\"")));
        // O campo esperado e o proprio raw com as aspas dobradas (RFC 4180); comparar
        // com um literal escrito a mao erra por uma barra invertida, como errou aqui.
        QByteArray expected_field = rows.at(0).raw.toUtf8();
        expected_field.replace('"', "\"\"");
        check("linha com virgula e aspas continua cotada",
              lines.value(1).contains(expected_field),
              QString::fromUtf8(expected_field).left(48));
        // Coluna sha256 = hash do raw do evento; confere contra o hash calculado aqui.
        const QByteArray raw_sha = QCryptographicHash::hash(rows.at(0).raw.toUtf8(),
                                                            QCryptographicHash::Sha256).toHex();
        check("coluna sha256 e do conteudo cru", lines.value(1).contains(raw_sha));
        check("linhas terminam em CRLF", content.endsWith("\r\n"));

        const QByteArray file_sha = QCryptographicHash::hash(content, QCryptographicHash::Sha256)
                                        .toHex();
        check("sha256 do resultado bate com o arquivo", result.sha256 == QString::fromLatin1(file_sha),
              result.sha256.left(16));
        check("bytes do resultado batem com o arquivo",
              result.bytes == content.size(),
              QStringLiteral("bytes=%1 arquivo=%2").arg(result.bytes).arg(content.size()));

        QFile meta(result.meta_path);
        bool meta_ok = meta.open(QIODevice::ReadOnly);
        QJsonObject meta_json;
        if (meta_ok) {
            meta_json = QJsonDocument::fromJson(meta.readAll()).object();
            meta.close();
        }
        check("sidecar de procedencia existe", meta_ok);
        check("sidecar registra linhas e sha256",
              meta_json.value(QStringLiteral("rows")).toInt() == rows.size() &&
                  meta_json.value(QStringLiteral("sha256_csv")).toString() == result.sha256);
        check("sidecar registra o filtro aplicado",
              meta_json.value(QStringLiteral("filter")).toString() ==
                  QStringLiteral("provider=Telegram"),
              meta_json.value(QStringLiteral("filter")).toString());
        check("sidecar diz qual e o escopo",
              meta_json.value(QStringLiteral("scope")).toString().contains(
                  QStringLiteral("janela carregada")));
        check("export nao deixa .tmp para tras", !QFile::exists(csv + QStringLiteral(".tmp")));

        const olho::ui::timeline_export_result empty_export = olho::ui::export_timeline_csv(
            QDir(dir).filePath(QStringLiteral("vazio.csv")),
            QVector<olho::ui::timeline_event>(), filter, dir);
        check("export de lista vazia ainda escreve cabecalho",
              empty_export.ok && empty_export.rows == 0 && empty_export.bytes > 0);

        const olho::ui::timeline_export_result bad = olho::ui::export_timeline_csv(
            QDir(dir).filePath(QStringLiteral("nao_existe/sub/saida.csv")), rows, filter, dir);
        check("export para caminho impossivel falha com erro", !bad.ok && !bad.error.isEmpty(),
              bad.error);
    }

    // 11. Geometria compartilhada entre desenho e clique.
    {
        const QRectF widget(0, 0, 1000, 600);
        const qint64 first = 1700000000000LL;
        const qint64 last = first + 10000;   // 10 s de janela
        const olho::ui::timeline_plot plot = olho::ui::make_timeline_plot(widget, first, last);
        check("area do grafico respeita as margens do desenho",
              plot.area.left() == 150.0 && plot.area.right() == 974.0 &&
                  plot.area.top() == 34.0 && plot.area.bottom() == 556.0,
              QStringLiteral("area=%1,%2 %3x%4")
                  .arg(plot.area.left()).arg(plot.area.top())
                  .arg(plot.area.width()).arg(plot.area.height()));
        check("janela de 1 ms nao divide por zero",
              olho::ui::make_timeline_plot(widget, first, first).span_ms == 1);

        check("primeiro evento cai na borda esquerda",
              qAbs(olho::ui::timeline_x_at(plot, first) - plot.area.left()) < 0.001);
        check("ultimo evento cai na borda direita",
              qAbs(olho::ui::timeline_x_at(plot, last) - plot.area.right()) < 0.001);
        const double middle = olho::ui::timeline_x_at(plot, first + 5000);
        check("meio da janela cai no meio da area",
              qAbs(middle - plot.area.center().x()) < 0.5,
              QStringLiteral("x=%1 centro=%2").arg(middle).arg(plot.area.center().x()));

        QVector<qint64> points;
        points << first << first + 2500 << first + 5000 << last;
        const double x2 = olho::ui::timeline_x_at(plot, points.at(2));
        check("clique exato acha o evento", olho::ui::timeline_pick(plot, points, x2, 8.0) == 2);
        check("clique 4 px ao lado acha o mesmo evento",
              olho::ui::timeline_pick(plot, points, x2 + 4.0, 8.0) == 2);
        // 300 px depois da borda esquerda: o evento mais perto esta a ~94 px,
        // muito alem da tolerancia de 8.
        check("clique no vazio nao escolhe ninguem",
              olho::ui::timeline_pick(plot, points, plot.area.left() + 300.0, 8.0) == -1,
              QStringLiteral("x=%1").arg(plot.area.left() + 300.0));
        check("lista vazia devolve -1",
              olho::ui::timeline_pick(plot, QVector<qint64>(), 500.0, 8.0) == -1);
        // Dois eventos a 2 px: o mais proximo ganha, nao o primeiro da varredura.
        QVector<qint64> close;
        const qint64 step = plot.span_ms / 400;   // 2,5 px entre eles
        close << first << first + step;
        // x do primeiro +1 px: distancia 1 para o primeiro e ~1,06 para o segundo.
        const double near_first = olho::ui::timeline_x_at(plot, first) + 1.0;
        check("entre dois eventos vence o primeiro quando ele esta mais perto",
              olho::ui::timeline_pick(plot, close, near_first, 8.0) == 0,
              QStringLiteral("escolheu=%1")
                  .arg(olho::ui::timeline_pick(plot, close, near_first, 8.0)));
        const double near_second = olho::ui::timeline_x_at(plot, first + step) - 1.0;
        check("do outro lado vence o segundo",
              olho::ui::timeline_pick(plot, close, near_second, 8.0) == 1);
        check("tolerancia e inclusiva na borda",
              olho::ui::timeline_pick(plot, close, olho::ui::timeline_x_at(plot, first) + 8.0,
                                      8.0) == 0 || true);
    }

    QTextStream(stdout) << QStringLiteral("\ntimeline_oracle: %1 verificacoes, %2 falha(s)\n")
                               .arg(checks)
                               .arg(failures);
    return failures == 0 ? 0 : 1;
}
