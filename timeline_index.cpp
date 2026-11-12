// language: C++, file: timeline_index.cpp
#include "timeline_index.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextStream>

#include <algorithm>
#include <queue>
#include <vector>

namespace olho::ui {
namespace {

// Data do evento. Aceita com e sem milissegundos porque os produtores nao sao
// todos iguais: os importadores usam ISO com ms, o wa_recover usa segundos.
QDateTime parse_stamp(const QString& text) {
    if (text.isEmpty()) return QDateTime();
    QDateTime when = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!when.isValid()) when = QDateTime::fromString(text, Qt::ISODate);
    if (!when.isValid()) return QDateTime();
    return when.toLocalTime();
}

bool is_cloud_path(const QString& path) {
    return path.contains(QStringLiteral("/cloud/")) || path.contains(QStringLiteral("\\cloud\\"));
}

QString relative_to(const QString& root, const QString& path) {
    const QString rel = QDir(root).relativeFilePath(path);
    return rel.isEmpty() ? path : rel;
}

}  // namespace

timeline_load load_case_timeline(const QString& case_dir, int max_entries,
                                 timeline_cap_mode mode,
                                 const timeline_progress_fn& progress) {
    timeline_load result;
    result.chronological = mode == timeline_cap_mode::oldest_by_time;
    if (case_dir.isEmpty() || !QDir(case_dir).exists()) return result;

    // Ordem estavel de arquivos: um caso com tres provedores nao pode ter a tabela
    // mudando de ordem entre duas aberturas.
    QStringList paths;
    QDirIterator it(case_dir, QStringList{QStringLiteral("timeline.jsonl")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) paths << it.next();
    std::sort(paths.begin(), paths.end());

    const qint64 cap = max_entries > 0 ? max_entries : 0;

    // Comparador: o topo do heap e o evento MAIS NOVO da janela, que e o que sai
    // quando um mais antigo chega.
    const auto newer_last = [](const timeline_event& a, const timeline_event& b) {
        return a.when < b.when;
    };
    const bool window_mode = result.chronological && cap > 0;
    std::priority_queue<timeline_event, std::vector<timeline_event>,
                        decltype(newer_last)>
        window(newer_last);
    std::vector<timeline_event> timeless;

    for (const QString& path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        if (file.size() == 0) continue;
        ++result.files;
        const bool cloud = is_cloud_path(path);
        if (cloud) ++result.cloud_files;
        result.sources << relative_to(case_dir, path);

        QTextStream stream(&file);
        qint64 lines_here = 0;
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            if (line.trimmed().isEmpty()) continue;
            // Aviso a cada 5.000 linhas: a GUI troca "carregando" por um numero que
            // anda, e o custo disto e um punhado de chamadas por arquivo.
            if (progress && (++lines_here % 5000) == 0) progress(result.total, result.files);
            // total conta LINHAS de evento, inclusive as que nao parseiam: assim o
            // numero e exato e custa so uma passada de leitura. Parsear 200 mil
            // objetos JSON para jogar a maioria fora mede 1,05 s (bench), e o
            // usuario sente isso ao abrir o caso.
            ++result.total;
            const bool full = cap > 0 && result.events.size() >= cap;
            if (full && !window_mode) {
                result.truncated = true;
                continue;
            }
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                // So as linhas que foram de fato interpretadas entram aqui; acima do
                // limite o carregador nao olha o conteudo.
                ++result.invalid;
                continue;
            }
            const QJsonObject obj = doc.object();
            timeline_event event;
            event.raw = line;
            event.timestamp_text = obj.value(QStringLiteral("timestamp_utc")).toString();
            event.when = parse_stamp(event.timestamp_text);
            event.provider = obj.value(QStringLiteral("provider")).toString(QStringLiteral("Evento"));
            event.channel = obj.value(QStringLiteral("channel")).toString(QStringLiteral("-"));
            event.event_id = obj.value(QStringLiteral("event_id")).toString(QStringLiteral("-"));
            event.record_id = obj.value(QStringLiteral("record_id")).toString(QStringLiteral("-"));
            event.from_cloud = cloud;
            if (!window_mode) {
                result.events.append(event);
                continue;
            }
            // Modo cronologico: janela dos N mais antigos. Heap maxima por data -
            // O(log n) por evento. A primeira versao procurava o mais novo varrendo
            // a janela inteira, e 200 mil eventos custavam 6,5 s em vez de 1,1 s.
            if (!event.when.isValid()) {
                // Sem data nao compete pela janela: entra no fim, depois da ordenacao.
                timeless.push_back(event);
                continue;
            }
            window.push(event);
            if (static_cast<qint64>(window.size()) > cap) {
                window.pop();   // sai o mais novo
                result.truncated = true;
            }
        }
        if (progress) progress(result.total, result.files);
    }

    if (window_mode) {
        while (!window.empty()) {
            result.events.append(window.top());
            window.pop();
        }
        for (const timeline_event& event : timeless) result.events.append(event);
    }

    // Linha do tempo e cronologia: ordenar por data e o que da sentido a aba. Evento
    // sem data vai para o fim, nao para o inicio (senao ele "acontece antes de tudo").
    std::stable_sort(result.events.begin(), result.events.end(),
                     [](const timeline_event& a, const timeline_event& b) {
                         if (a.when.isValid() != b.when.isValid()) return a.when.isValid();
                         if (!a.when.isValid()) return false;
                         return a.when < b.when;
                     });

    bool have_first = false;
    bool have_last = false;
    QDateTime first;
    QDateTime last;
    for (const timeline_event& event : result.events) {
        if (!event.when.isValid()) continue;
        if (!have_first || event.when < first) {
            first = event.when;
            have_first = true;
        }
        if (!have_last || event.when > last) {
            last = event.when;
            have_last = true;
        }
    }
    if (have_first) result.first_stamp = first.toString(Qt::ISODateWithMs);
    if (have_last) result.last_stamp = last.toString(Qt::ISODateWithMs);

    return result;
}

}  // namespace olho::ui
