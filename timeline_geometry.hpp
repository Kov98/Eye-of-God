// language: C++, file: timeline_geometry.hpp, target: Windows 11, Qt 6
// Geometria da plotagem da Timeline: onde cada evento cai no eixo x.
//
// POR QUE ISTO NAO ESTA DENTRO DO paintEvent
//   O desenho e o clique precisam da MESMA conta. Duas copias divergem na primeira
//   correcao, e o defeito resultante e silencioso: o grafico continua bonito e o
//   clique passa a abrir o evento vizinho. Aqui a conta tem um dono so e um
//   oraculo proprio, sem widget nenhum.
#pragma once

#include <QRectF>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace olho::ui {

struct timeline_plot {
    QRectF area;          // area util do desenho (sem goteira, eixo e titulo)
    qint64 first_ms = 0;  // epoch ms do primeiro evento
    qint64 span_ms = 1;   // de first_ms ate o ultimo; nunca zero
};

// Margens iguais as do paintEvent: goteira de 150 px para o nome da fonte, 34 px
// de titulo/lane e 44 px de eixo embaixo.
inline timeline_plot make_timeline_plot(const QRectF& widget_rect, qint64 first_ms,
                                        qint64 last_ms) {
    timeline_plot plot;
    plot.area = widget_rect.adjusted(150, 34, -26, -44);
    plot.first_ms = first_ms;
    // Parenteses: no Windows min/max sao macros, e (std::max)(...) e o jeito seguro
    // (regra HDR-3 do mesmo linter).
    plot.span_ms = (std::max)(static_cast<qint64>(1), last_ms - first_ms);
    return plot;
}

inline double timeline_x_at(const timeline_plot& plot, qint64 when_ms) {
    const double ratio = static_cast<double>(when_ms - plot.first_ms) /
                         static_cast<double>(plot.span_ms);
    return plot.area.left() + plot.area.width() * ratio;
}

// Indice do evento mais proximo de x, dentro da tolerancia. Devolve -1 quando
// nada esta perto o bastante: clicar no vazio nao pode escolher um evento por
// acidente - num laudo, "abriu o evento errado" e pior que "nao abriu nada".
inline int timeline_pick(const timeline_plot& plot, const QVector<qint64>& when_ms, double x,
                         double tolerance_px) {
    int best = -1;
    double best_distance = 0.0;
    for (int i = 0; i < when_ms.size(); ++i) {
        const double distance = std::fabs(timeline_x_at(plot, when_ms.at(i)) - x);
        if (distance > tolerance_px) continue;
        if (best < 0 || distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

// Intervalo de tempo coberto por um arraste no eixo x.
struct timeline_span {
    qint64 from_ms = 0;
    qint64 to_ms = 0;
    bool ok = false;
};

// Arraste menor que min_px nao vira periodo: clique tremido nao pode encolher a
// janela para 10 ms e esconder o caso inteiro.
inline timeline_span timeline_span_for_x(const timeline_plot& plot, double x1, double x2,
                                         double min_px = 3.0) {
    timeline_span span;
    if (plot.area.width() <= 0.0) return span;
    if (std::fabs(x2 - x1) < min_px) return span;
    const auto x_to_ms = [&plot](double x) {
        double ratio = (x - plot.area.left()) / plot.area.width();
        ratio = std::clamp(ratio, 0.0, 1.0);
        return plot.first_ms + static_cast<qint64>(ratio * static_cast<double>(plot.span_ms));
    };
    const qint64 a = x_to_ms(x1);
    const qint64 b = x_to_ms(x2);
    span.from_ms = (std::min)(a, b);   // arrastar da direita para a esquerda vale igual
    span.to_ms = (std::max)(a, b);
    span.ok = true;
    return span;
}

}  // namespace olho::ui
